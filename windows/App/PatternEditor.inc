// A retained, nonmodal FX draft. Cursor navigation and worker refreshes cannot
// redirect its captured cell or silently rebase its revision.
    bool effectEditorOpen=false,effectDraftExists=false,settingEffectFields=false;
    uint64_t effectDraftGeneration=0;
    unsigned effectDraftPattern=0,effectDraftRow=0,effectDraftChannel=0,effectDraftColumn=0;
    std::string effectDraftRevision,effectDraftDocument;
    Json effectChoices=Json::array(),effectBindingChoices=Json::array();
    std::vector<size_t> visibleEffectChoices;
    int effectSelected=-1;
    std::wstring effectEditorStatus=L"Enter on an FX cell to edit";
    struct InitialEffects {
        std::shared_ptr<const ScreamSeq::DocumentView> source;
        std::string document,revision,plugin;
        uint64_t context=0,generation=0;
        uint32_t parameter=0;
        unsigned pattern=0,row=0,channel=0,column=0;
        Json cursor,parameterCatalog,choices=Json::array(),bindings=Json::array();
        std::vector<size_t> visible;
        std::vector<std::wstring> choiceLabels,bindingLabels;
        std::array<std::wstring,5> previousFields,fields;
        std::array<LRESULT,2> previousSelections{};
        int selected=0,selectedBinding=-1;
        std::wstring status=L"Draft / changes apply to the displayed cell / Escape returns to the grid";
    };
    std::array<std::wstring,5> initialEffectFields() const {
        return {effectField(effectValue),effectField(effectOffset),effectField(effectDuration),effectField(effectRange),effectField(effectSearch)};
    }
    void validateInitialEffects(const InitialEffects &initial) const {
        const auto published=controller->view();
        if(busy||recoveryRestoring||effectDraftExists||effectEditorOpen||effectDraftGeneration!=initial.generation||
            documentId!=initial.document||view->session.documentId!=initial.document||view->session.revision!=initial.revision||
            published->session.documentId!=initial.document||published->session.revision!=initial.revision||
            contextRevision!=initial.context||position()!=initial.cursor||selectedPlugin!=initial.plugin||selectedParameter!=initial.parameter||
            pluginParameters!=initial.parameterCatalog||initialEffectFields()!=initial.previousFields||
            SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0)!=initial.previousSelections[0]||
            SendMessageW(controls.at(effectBinding),CB_GETCURSEL,0,0)!=initial.previousSelections[1])
            throw std::runtime_error("Song, target or FX draft changed while preparing the layout");
    }
    InitialEffects prepareInitialEffects(const std::string &code="") const {
        if(busy||recoveryRestoring||effectDraftExists||effectEditorOpen)
            throw std::runtime_error("Prepare only an unopened FX editor while the document is ready");
        InitialEffects initial;initial.source=view;initial.document=documentId;initial.revision=view->session.revision;
        initial.context=contextRevision;initial.generation=effectDraftGeneration;initial.cursor=position();
        initial.pattern=patternIndex;initial.row=row;initial.channel=channel;initial.column=column>=3?(column-3)/2:0;
        initial.plugin=selectedPlugin;initial.parameter=selectedParameter;initial.parameterCatalog=pluginParameters;
        initial.previousFields=initialEffectFields();initial.previousSelections={SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0),SendMessageW(controls.at(effectBinding),CB_GETCURSEL,0,0)};
        if(!initial.source->patterns.contains(initial.pattern)||initial.row>=initial.source->pattern(initial.pattern).rows||
            initial.channel>=initial.source->channels||initial.column>=initial.source->effectColumns.at(initial.channel))
            throw std::runtime_error("The inspected FX cell no longer exists / choose another target");
        initial.choices=initial.source->commands.at("effect");
        for(auto [kind,display,name]:std::initializer_list<std::tuple<const char *,const char *,const char *>>{
            {"parameter-set","PS","Parameter set"},{"parameter-slide","PL","Parameter slide"},{"pitch-set","BS","Pitch set"},{"pitch-slide","BL","Pitch slide"},{"note-cut","NC","Precise note cut"}})
            initial.choices.push_back({{"kind",kind},{"displayCode",display},{"name",name}});
        const auto current=initial.source->effect(initial.pattern,initial.row,initial.channel,initial.column);
        std::wstring currentCode=L"..";
        if(current){
            wchar_t display[4]{};using Kind=Tracker::PatternCommandKind;
            switch(current->kind){
            case Kind::TrackerEffect:
                if(!current->effect)wcscpy_s(display,L"..");
                else if(initial.source->effectMasks[current->effect])swprintf_s(display,L"%c%X",initial.source->effectLetters[current->effect],unsigned(current->parameter>>4));
                else swprintf_s(display,L"0%c",initial.source->effectLetters[current->effect]);break;
            case Kind::ParameterSet:wcscpy_s(display,L"PS");break;case Kind::ParameterSlide:wcscpy_s(display,L"PL");break;
            case Kind::PitchSet:wcscpy_s(display,L"BS");break;case Kind::PitchSlide:wcscpy_s(display,L"BL");break;case Kind::NoteCut:wcscpy_s(display,L"NC");break;
            }
            currentCode=display;
        }
        const auto wanted=code.empty()?currentCode:wide(code);bool selected=false;
        for(size_t i=0;i<initial.choices.size();++i){
            const auto &choice=initial.choices[i];if(!selected&&wide(choice.at("displayCode").get<std::string>())==wanted){initial.selected=int(i);selected=true;}
            initial.visible.push_back(i);initial.choiceLabels.push_back(wide(choice.at("displayCode").get<std::string>()+"  "+choice.at("name").get<std::string>()));
        }
        const bool same=current&&(code.empty()||wide(code)==currentCode);
        const auto kind=initial.choices.at(initial.selected).value("kind",std::string("tracker"));
        const auto number=[](double value){std::wostringstream text;text<<std::setprecision(12)<<value;return text.str();};
        initial.fields[0]=number(same?(kind=="tracker"?current->parameter:current->value):kind=="tracker"?initial.choices.at(initial.selected).at("suggestedParameter").get<double>():0);
        initial.fields[1]=number(same?current->position%Tracker::performanceUnitsPerRow:0);
        initial.fields[2]=number(same?current->duration:(kind=="parameter-slide"||kind=="pitch-slide"?Tracker::performanceUnitsPerRow:0));
        initial.fields[3]=number(same?current->pitchRange:2);initial.fields[4]=L"";
        for(const auto &[id,binding]:initial.source->nativePattern->performance.bindings){
            if(same&&current->binding==id)initial.selectedBinding=int(initial.bindings.size());
            initial.bindings.push_back({{"id",id}});initial.bindingLabels.push_back(std::to_wstring(id)+L"  "+wide(binding.name.empty()?binding.plugin:binding.name));
        }
        for(const auto &parameter:initial.parameterCatalog)if(parameter.at("id")==initial.parameter&&parameter.value("writable",false)&&!initial.plugin.empty()){
            unsigned free=1;while(free<=255&&initial.source->nativePattern->performance.bindings.contains(uint16_t(free)))++free;
            if(free<=255){
                initial.bindings.push_back({{"id",free},{"plugin",initial.plugin},{"parameter",initial.parameter},{"name",parameter.at("name")}});
                initial.bindingLabels.push_back(L"New: "+wide(parameter.at("name").get<std::string>()));
                if(initial.selectedBinding<0)initial.selectedBinding=int(initial.bindings.size())-1;
            }
            break;
        }
        if(initial.selectedBinding<0&&!initial.bindings.empty())initial.selectedBinding=0;
        validateInitialEffects(initial);return initial;
    }
    void adoptInitialEffects(InitialEffects &&initial) {
        validateInitialEffects(initial);
        effectDraftPattern=initial.pattern;effectDraftRow=initial.row;effectDraftChannel=initial.channel;effectDraftColumn=initial.column;
        effectDraftRevision=std::move(initial.revision);effectDraftDocument=std::move(initial.document);effectChoices=std::move(initial.choices);effectBindingChoices=std::move(initial.bindings);
        visibleEffectChoices=std::move(initial.visible);effectSelected=initial.selected;effectEditorStatus=std::move(initial.status);
        const bool previous=settingEffectFields;settingEffectFields=true;struct Guard{bool &setting;bool previous;~Guard(){setting=previous;}}guard{settingEffectFields,previous};
        SendMessageW(controls.at(effectKind),CB_RESETCONTENT,0,0);for(const auto &label:initial.choiceLabels)SendMessageW(controls.at(effectKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(controls.at(effectKind),CB_SETCURSEL,effectSelected,0);
        SendMessageW(controls.at(effectBinding),CB_RESETCONTENT,0,0);for(const auto &label:initial.bindingLabels)SendMessageW(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(controls.at(effectBinding),CB_SETCURSEL,initial.selectedBinding,0);
        for(size_t i=0;i<initial.fields.size();++i)SetWindowTextW(controls.at(std::array<int,5>{effectValue,effectOffset,effectDuration,effectRange,effectSearch}[i]),initial.fields[i].c_str());
        effectDraftExists=true;++effectDraftGeneration;
    }
    bool effectEditorVisible() const {return workspaceMainEditorVisible("effects");}
    ScreamSeq::WorkspaceRect effectEditorRect() const {return workspaceMainEditorRect();}
    std::wstring effectField(int id) const {
        const auto h=controls.at(id);const int count=GetWindowTextLengthW(h);std::wstring value(size_t(count)+1,0);
        GetWindowTextW(h,value.data(),count+1);value.resize(count);return value;
    }
    double effectNumber(int id,double low,double high,bool whole=false) const {
        const auto text=effectField(id);size_t used=0;double number=0;
        try {number=std::stod(text,&used);}catch(...){throw std::runtime_error("Enter a numeric FX value");}
        if(used!=text.size()||!std::isfinite(number)||number<low||number>high||(whole&&std::floor(number)!=number))throw std::runtime_error("FX value is outside its allowed range");
        return number;
    }
    void installEffectControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{effectsCommand,L"Pattern FX"},{effectApply,L"Apply"},{effectReload,L"Use cursor"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{effectKind,effectBinding})controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{effectValue,effectOffset,effectDuration,effectRange,effectSearch}) {
            controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(controls.at(id),EM_SETLIMITTEXT,id==effectSearch?128:32,0);
        }
        for(int id=effectsCommand;id<=effectSearch;++id)if(!controls.at(id))throw std::runtime_error("Cannot create pattern FX controls");
    }
    void filterEffects() {
        if(settingEffectFields)return;
        auto query=effectField(effectSearch);std::transform(query.begin(),query.end(),query.begin(),towlower);
        visibleEffectChoices.clear();SendMessageW(controls.at(effectKind),CB_RESETCONTENT,0,0);
        int selected=-1;
        for(size_t i=0;i<effectChoices.size();++i) {
            auto label=wide(effectChoices[i].at("displayCode").get<std::string>()+"  "+effectChoices[i].at("name").get<std::string>());
            auto match=label;std::transform(match.begin(),match.end(),match.begin(),towlower);
            if(!query.empty()&&match.find(query)==std::wstring::npos)continue;
            if(int(i)==effectSelected)selected=int(visibleEffectChoices.size());
            visibleEffectChoices.push_back(i);SendMessageW(controls.at(effectKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(controls.at(effectKind),CB_SETCURSEL,selected,0);
    }
    void openEffectEditor(const std::string &code="",bool reload=false) {
        if(busy)throw std::runtime_error("Wait for the document worker before opening an FX draft");
        revealWorkspaceMainEditor("effects");
        graphEditorOpen=false;
        mixerEditorOpen=false;
        noteEditorOpen=false;
        if(effectDraftExists&&!reload) {
            effectEditorOpen=true;
            workspaceState.show("notes",position(),cursorSample(),false);layoutControls();SetFocus(controls.at(effectValue));
            effectEditorStatus=L"Existing draft retained / Use cursor to edit another cell";return;
        }
        effectEditorOpen=effectDraftExists=true;++effectDraftGeneration;workspaceState.show("notes",position(),cursorSample(),false);
        effectDraftPattern=patternIndex;effectDraftRow=row;effectDraftChannel=channel;effectDraftColumn=column>=3?(column-3)/2:0;
        effectDraftRevision=view->session.revision;effectDraftDocument=documentId;
        effectChoices=view->commands.at("effect");
        for(auto [kind,code,name]:std::initializer_list<std::tuple<const char *,const char *,const char *>>{
            {"parameter-set","PS","Parameter set"},{"parameter-slide","PL","Parameter slide"},
            {"pitch-set","BS","Pitch set"},{"pitch-slide","BL","Pitch slide"},{"note-cut","NC","Precise note cut"}})
            effectChoices.push_back({{"kind",kind},{"displayCode",code},{"name",name}});
        const auto current=view->effect(patternIndex,row,channel,effectDraftColumn);
        const auto currentCode=current?effectText(current).first:L"..";
        effectSelected=0;
        for(size_t i=0;i<effectChoices.size();++i)if(wide(effectChoices[i].at("displayCode").get<std::string>())==(code.empty()?currentCode:wide(code))){effectSelected=int(i);break;}
        settingEffectFields=true;SetWindowTextW(controls.at(effectSearch),L"");settingEffectFields=false;filterEffects();
        const bool same=current&&(code.empty()||wide(code)==currentCode);
        const auto kind=effectChoices.at(effectSelected).value("kind",std::string("tracker"));
        const double value=same?(kind=="tracker"?current->parameter:current->value):kind=="tracker"?effectChoices.at(effectSelected).at("suggestedParameter").get<double>():0;
        auto set=[&](int id,double n){std::wostringstream out;out<<std::setprecision(12)<<n;SetWindowTextW(controls.at(id),out.str().c_str());};
        set(effectValue,value);set(effectOffset,same?current->position%Tracker::performanceUnitsPerRow:0);
        set(effectDuration,same?current->duration:(kind=="parameter-slide"||kind=="pitch-slide"?Tracker::performanceUnitsPerRow:0));
        set(effectRange,same?current->pitchRange:2);
        effectBindingChoices=Json::array();SendMessageW(controls.at(effectBinding),CB_RESETCONTENT,0,0);int selectedBinding=-1;
        for(const auto &[id,binding]:view->nativePattern->performance.bindings) {
            if(same&&current->binding==id)selectedBinding=int(effectBindingChoices.size());
            effectBindingChoices.push_back({{"id",id}});
            const auto label=std::to_wstring(id)+L"  "+wide(binding.name.empty()?binding.plugin:binding.name);
            SendMessageW(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        // The rack's selected parameter can become a new stable binding in the
        // same transaction as this command; no intermediate document mutation.
        for(const auto &parameter:pluginParameters)if(parameter.at("id")==selectedParameter&&parameter.value("writable",false)&&!selectedPlugin.empty()) {
            unsigned free=1;while(free<=255&&view->nativePattern->performance.bindings.contains(uint16_t(free)))++free;
            if(free<=255) {
                effectBindingChoices.push_back({{"id",free},{"plugin",selectedPlugin},{"parameter",selectedParameter},{"name",parameter.at("name")}});
                const auto label=L"New: "+wide(parameter.at("name").get<std::string>());
                SendMessageW(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                if(selectedBinding<0)selectedBinding=int(effectBindingChoices.size())-1;
            }
            break;
        }
        if(selectedBinding<0&&!effectBindingChoices.empty())selectedBinding=0;
        SendMessageW(controls.at(effectBinding),CB_SETCURSEL,selectedBinding,0);
        effectEditorStatus=L"Draft / changes apply to the displayed cell / Escape returns to the grid";
        layoutControls();SetFocus(controls.at(effectValue));
    }
    bool effectCommand(int id) {
        if(id<effectsCommand||id>effectSearch)return false;
        if(id==effectsCommand||id==effectReload){openEffectEditor("",id==effectReload);return true;}
        if(id==effectKind) {
            const auto selected=SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0);
            if(selected>=0&&size_t(selected)<visibleEffectChoices.size()) {
                effectSelected=int(visibleEffectChoices[size_t(selected)]);const auto &choice=effectChoices.at(effectSelected);
                const auto kind=choice.value("kind",std::string("tracker"));
                SetWindowTextW(controls.at(effectValue),std::to_wstring(choice.value("suggestedParameter",0)).c_str());
                SetWindowTextW(controls.at(effectDuration),(kind=="parameter-slide"||kind=="pitch-slide")?L"65536":L"0");
                SetWindowTextW(controls.at(effectOffset),L"0");SetWindowTextW(controls.at(effectRange),L"2");
            }
        }
        if(id==effectApply) {
            try {
                if(effectDraftDocument!=documentId||effectDraftRevision!=view->session.revision)throw std::runtime_error("Document changed / draft retained. Use cursor to reload before applying");
                if(effectSelected<0||SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0)<0)throw std::runtime_error("Choose an FX command");
                const auto &choice=effectChoices.at(effectSelected);const auto kind=choice.value("kind",std::string("tracker"));
                Json command={{"kind",kind}};Json binding=nullptr;
                if(kind=="tracker") {
                    command["effect"]=choice.at("command");const auto mask=choice.at("parameterMask").get<unsigned>();
                    command["parameter"]=(unsigned(effectNumber(effectValue,0,255,true))&~mask)|choice.at("parameterValue").get<unsigned>();
                }
                else {
                    command["offset"]=unsigned(effectNumber(effectOffset,0,65535,true));
                    if(kind=="parameter-slide"||kind=="pitch-slide")command["duration"]=unsigned(effectNumber(effectDuration,1,UINT32_MAX,true));
                    if(kind=="parameter-set"||kind=="parameter-slide") {
                        const auto selected=SendMessageW(controls.at(effectBinding),CB_GETCURSEL,0,0);
                        if(selected<0||size_t(selected)>=effectBindingChoices.size())throw std::runtime_error("Choose a parameter binding, or select a parameter in Plugins first");
                        binding=effectBindingChoices.at(size_t(selected));command["binding"]=binding.at("id");command["value"]=effectNumber(effectValue,0,1);
                    } else if(kind=="pitch-set"||kind=="pitch-slide") {command["value"]=effectNumber(effectValue,-96,96);command["pitchRange"]=unsigned(effectNumber(effectRange,1,96,true));}
                }
                Json params={{"expectedRevision",effectDraftRevision},{"pattern",effectDraftPattern},{"row",effectDraftRow},{"channel",effectDraftChannel},{"column",effectDraftColumn},{"command",command}};
                if(binding.is_object()&&binding.contains("plugin")) {
                    auto snapshot=documentOperation("pattern.effects.get",{{"pattern",effectDraftPattern}});
                    auto commands=snapshot.at("commands");
                    std::erase_if(commands.get_ref<Json::array_t &>(),[&](const auto &c){return c.at("channel")==effectDraftChannel&&c.at("column")==effectDraftColumn&&c.at("position").template get<unsigned>()/Tracker::performanceUnitsPerRow==effectDraftRow;});
                    command["position"]=effectDraftRow*Tracker::performanceUnitsPerRow+command.value("offset",0u);command.erase("offset");command["channel"]=effectDraftChannel;command["column"]=effectDraftColumn;commands.push_back(command);
                    documentOperation("pattern.effects.set",{{"expectedRevision",effectDraftRevision},{"pattern",effectDraftPattern},{"commands",commands},{"bindings",Json::array({binding})}});
                } else documentOperation("pattern.effect.set",params);
                effectDraftRevision=view->session.revision;effectEditorStatus=L"Applied / document Undo available";
            } catch(const std::exception &e){effectEditorStatus=wide(e.what());status=effectEditorStatus;}
        }
        return true;
    }
    template<class Place> void layoutEffectControls(Place place) {
        const auto r=workspaceMainEditorRect();const bool show=effectEditorVisible();
        const auto pattern=geometry().pattern;place(effectsCommand,pattern.x+pattern.w-254,pattern.y+3,78,24,trackerWorkspaceVisible());
        place(effectReload,r.x+r.w-152,r.y+4,82,24,show);place(effectApply,r.x+r.w-64,r.y+4,58,24,show);
        const float x=r.x+12,w=r.w-24;
        place(effectSearch,x,r.y+47,std::min(170.0f,w*.26f),25,show);
        place(effectKind,x+std::min(180.0f,w*.28f),r.y+47,w-std::min(180.0f,w*.28f),260,show);
        place(effectValue,x,r.y+100,94,25,show);place(effectOffset,x+104,r.y+100,84,25,show);
        place(effectDuration,x+198,r.y+100,94,25,show);place(effectRange,x+302,r.y+100,64,25,show);
        place(effectBinding,x+376,r.y+100,std::max(80.0f,w-376),220,show);
    }
    void drawEffectEditor() {
        auto &s=*surface;const auto r=workspaceMainEditorRect();const float x=r.x+12;
        s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x1a222e);
        s.uiText(L"PATTERN FX / P"+std::to_wstring(effectDraftPattern)+L" R"+std::to_wstring(effectDraftRow)+L" CH"+std::to_wstring(effectDraftChannel+1)+L" FX"+std::to_wstring(effectDraftColumn+1),x,r.y+8,r.w-170,0x6edac5);
        s.uiText(L"Search effects",x,r.y+28,160,0x91a0b2);
        s.uiText(L"Command",x+180,r.y+28,300,0x91a0b2);
        for(auto [label,offset]:std::initializer_list<std::pair<const wchar_t *,float>>{{L"Value",0.0f},{L"Offset units",104.0f},{L"Slide units",198.0f},{L"Range st",302.0f},{L"Parameter binding",376.0f}})
            s.uiText(label,x+offset,r.y+80,offset==376?r.w-400:100,0x91a0b2);
        s.uiText(effectDraftRevision==view->session.revision?effectEditorStatus:L"Document changed / draft retained. Use cursor to reload before applying",x,r.y+132,r.w-24,0xe0bc75);
        s.uiText(L"Value: tracker 0–255 / parameter 0–1 / pitch in semitones. Timing: 65536 units per row.",x,r.y+153,r.w-24,0x91a0b2);
        s.unclip();s.outline(r.x,r.y,r.w,r.h,0x56858b);
    }
