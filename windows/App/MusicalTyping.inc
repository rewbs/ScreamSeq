// UI-thread musical input. Physical keys retain their original sound and voice
// generation across cursor movement, document publication and nested waits.
    struct TypedNote {
        std::string document,identity;
        unsigned slot=0,note=0;
        bool sample=true,started=false,pending=false;
        uint64_t epoch=0,voice=0,token=0;
        HWND owner{};
    };
    std::map<WPARAM,TypedNote> typedNotes;
    uint64_t typingToken=0;
    bool liveKeyboard=false,typingSample=true;
    std::string typingSoundId,typingDocument;
    unsigned typingSound=0;
    static int musicalOffset(WPARAM key) {
        if(key>127)return -1;
        const std::string_view lower="ZSXDCVGBHNJM",upper="Q2W3ER5T6Y7UI";
        const auto low=lower.find(char(key)),high=upper.find(char(key));
        return low!=lower.npos?int(low):high!=upper.npos?12+int(high):-1;
    }
    static bool typingTextControl(HWND focus) {
        wchar_t name[64]{};GetClassNameW(focus,name,64);
        return _wcsicmp(name,L"Edit")==0||_wcsicmp(name,L"ComboBox")==0||_wcsicmp(name,L"ListBox")==0;
    }
    const Json &typingCatalog(bool sample)const{return view->session.document.at(sample?"samples":"instruments");}
    bool typingTargetExists(const TypedNote &held)const {
        const auto &catalog=typingCatalog(held.sample);
        return held.document==documentId&&std::any_of(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("index")==held.slot&&v.at("id")==held.identity;});
    }
    TypedNote typingTarget(bool sample,unsigned slot,unsigned pitch,HWND owner)const {
        const auto &catalog=typingCatalog(sample);
        const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("index")==slot;});
        if(found==catalog.end())throw std::runtime_error("Choose a sound for musical typing");
        return {documentId,found->at("id"),slot,pitch,sample,false,false,0,0,0,owner};
    }
    Json typedParameters(const TypedNote &held,bool on)const {
        return {{"expectedRevision",view->session.revision},{"note",held.note},{"on",on},{"velocity",100},{held.sample?"sample":"instrument",held.slot}};
    }
    bool releaseTypedKey(WPARAM key) {
        auto found=typedNotes.find(key);if(found==typedNotes.end())return false;
        const auto held=found->second;typedNotes.erase(found);frameRequested=true;
        if(!held.started||!typingTargetExists(held)||held.voice!=auditionVoices[held.note]||(!held.pending&&held.epoch!=stopGeneration))return true;
        const bool shared=std::any_of(typedNotes.begin(),typedNotes.end(),[&](const auto &v){return v.second.started&&v.second.note==held.note&&v.second.voice==held.voice;});
        if(!shared)auditionOperation("transport.note",typedParameters(held,false));
        return true;
    }
    void releaseTypedNotes(HWND owner=nullptr)noexcept {
        for(;;){const auto found=std::find_if(typedNotes.begin(),typedNotes.end(),[&](const auto &v){return !owner||v.second.owner==owner;});if(found==typedNotes.end())break;
            try{releaseTypedKey(found->first);}catch(...){/* Retired before delivery; overflow already panics. */}}
    }
    void soundTypedNote(WPARAM key,uint64_t token) {
        auto found=typedNotes.find(key);if(found==typedNotes.end()||found->second.token!=token)return;
        const auto held=found->second;
        if(!typingTargetExists(held)){typedNotes.erase(found);return;}
        // Inspection still exercises normal editing, without opening audio.
        if(inspection)return;
        const auto shared=std::find_if(typedNotes.begin(),typedNotes.end(),[&](const auto &v){const auto &h=v.second;return h.started&&h.note==held.note&&h.sample==held.sample&&h.identity==held.identity&&h.voice==auditionVoices[held.note]&&(h.pending||h.epoch==stopGeneration);});
        if(shared!=typedNotes.end()){found->second.started=true;found->second.pending=shared->second.pending;found->second.epoch=shared->second.epoch;found->second.voice=shared->second.voice;return;}
        found->second.started=true;found->second.pending=true;found->second.voice=auditionVoices[held.note]+1;
        const auto voice=found->second.voice;
        try {
            auditionOperation("transport.note",typedParameters(held,true));
            // A key-up or focus change may have retired this input during prepare.
            for(auto &[k,h]:typedNotes)if(h.note==held.note&&h.voice==voice){h.pending=false;h.epoch=stopGeneration;}
        }catch(...){std::erase_if(typedNotes,[&](const auto &v){return v.second.note==held.note&&v.second.voice==voice;});throw;}
    }
    void beginTypedNote(WPARAM key,TypedNote held) {
        if(typedNotes.contains(key))return;
        held.token=++typingToken;const auto token=held.token;typedNotes.emplace(key,std::move(held));soundTypedNote(key,token);
    }
    bool enterPatternNote(WPARAM key,bool repeat) {
        const auto offset=musicalOffset(key);if(offset<0)return false;
        if(repeat||typedNotes.contains(key))return true;
        if(!view->notesAt(patternIndex,row,channel).empty()){openNoteEditor();return true;}
        if(typingSound>255)throw std::runtime_error("Map this sample to an instrument first / pattern slots range from 0 to 255");
        const auto pitch=std::clamp(1+octave*12+unsigned(offset),view->noteMin,view->noteMax);
        auto held=typingTarget(typingSample,typingSound,pitch,window);held.token=++typingToken;
        const auto token=held.token;const auto origin=position();const auto source=documentId;
        typedNotes.emplace(key,held);
        try {
            edit("pattern.apply",{{"cells",Json::array({{{"pattern",patternIndex},{"row",row},{"channel",channel},{"note",pitch},{"instrument",typingSound}}})}});
        }catch(...){auto it=typedNotes.find(key);if(it!=typedNotes.end()&&it->second.token==token)typedNotes.erase(it);throw;}
        if(documentId==source&&position()==origin)advance();
        try{soundTypedNote(key,token);}catch(const std::exception &e){status=L"Note entered / audition unavailable: "+wide(e.what());}
        return true;
    }
    bool workspaceMusicalKey(WPARAM key,bool repeat) {
        if(GetKeyState(VK_CONTROL)&0x8000||GetKeyState(VK_MENU)&0x8000||typingTextControl(GetFocus()))return false;
        const auto offset=musicalOffset(key);if(offset<0)return false;
        if(workspaceState.focus!="samples"&&!liveKeyboard)return false;
        if(repeat||typedNotes.contains(key))return true;
        const bool sample=workspaceState.focus=="samples"||typingSample;
        beginTypedNote(key,typingTarget(sample,workspaceState.focus=="samples"?selectedSample():typingSound,std::min(120u,1+octave*12+unsigned(offset)),window));
        return true;
    }
    void connectTyping(ScreamSeq::NativeToolWindow &tool,std::function<Json()> target) {
        const auto owner=tool.window();
        tool.musicalTyping([this,owner,target=std::move(target)](HWND focus,WPARAM key,bool repeat){
            if(GetKeyState(VK_CONTROL)&0x8000||GetKeyState(VK_MENU)&0x8000||typingTextControl(focus))return false;
            if(key==VK_SPACE){if(!repeat){if(device.running()&&!auditionOnly)stop();else play();}return true;}
            const auto offset=musicalOffset(key);if(offset<0)return false;
            if(repeat||typedNotes.contains(key))return true;
            const auto t=target();
            if(t.at("document")!=documentId||t.at("revision")!=view->session.revision)throw std::runtime_error("Song changed / Reload the captured editor before audition");
            auto held=typingTarget(t.at("sample"),t.at("slot"),std::min(120u,1+octave*12+unsigned(offset)),owner);
            if(held.identity!=t.at("id").get<std::string>())throw std::runtime_error("Captured audition sound is unavailable");
            beginTypedNote(key,std::move(held));return true;
        },[this](WPARAM key){return releaseTypedKey(key);},[this,owner]{releaseTypedNotes(owner);});
    }
    void refreshTypingSounds() {
        const bool sample=view->instruments==0;const auto &catalog=typingCatalog(sample);
        if(typingDocument!=documentId||typingSample!=sample){
            typingDocument=documentId;typingSample=sample;typingSoundId.clear();
            const auto slot=view->cell(patternIndex,row,channel).instrument;
            auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("index")==slot;});
            if(found==catalog.end())found=catalog.begin();if(found!=catalog.end())typingSoundId=found->at("id");
        }
        typingSound=0;SendMessageW(controls.at(soundChooser),CB_RESETCONTENT,0,0);
        for(const auto &v:catalog){const auto slot=v.at("index").get<unsigned>();const auto text=(sample?L"S":L"I")+std::to_wstring(slot)+L" · "+wide(v.at("name").get<std::string>());
            const auto i=SendMessageW(controls.at(soundChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(controls.at(soundChooser),CB_SETITEMDATA,i,slot);
            if(v.at("id")==typingSoundId){typingSound=slot;SendMessageW(controls.at(soundChooser),CB_SETCURSEL,i,0);}}
    }
    void selectTypingSound() {
        const auto selected=SendMessageW(controls.at(soundChooser),CB_GETCURSEL,0,0);if(selected==CB_ERR)return;
        const auto slot=unsigned(SendMessageW(controls.at(soundChooser),CB_GETITEMDATA,selected,0));
        const auto held=typingTarget(typingSample,slot,49,window);typingSound=slot;typingSoundId=held.identity;
    }
    Json typingSnapshot()const {
        Json held=Json::array();for(const auto &[key,h]:typedNotes)held.push_back({{"key",key},{"note",h.note},{"sample",h.sample},{"slot",h.slot},{"id",h.identity},{"pending",h.pending},{"started",h.started}});
        return {{"sample",typingSample},{"slot",typingSound},{"id",typingSoundId},{"held",held},{"noteMin",view->noteMin},{"noteMax",view->noteMax}};
    }
