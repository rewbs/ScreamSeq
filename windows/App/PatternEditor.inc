// A retained, nonmodal FX draft. Cursor navigation and worker refreshes cannot
// redirect its captured cell or silently rebase its revision.
    bool effectEditorOpen=false,effectDraftExists=false,settingEffectFields=false;
    unsigned effectDraftPattern=0,effectDraftRow=0,effectDraftChannel=0,effectDraftColumn=0;
    std::string effectDraftRevision,effectDraftDocument;
    Json effectChoices=Json::array(),effectBindingChoices=Json::array();
    std::vector<size_t> visibleEffectChoices;
    int effectSelected=-1;
    std::wstring effectEditorStatus=L"Enter on an FX cell to edit";
    bool effectEditorVisible() const {return !mixerEditorVisible()&&effectEditorOpen&&workspaceState.layout!="Pattern focus"&&!sampleEditorVisible();}
    ScreamSeq::WorkspaceRect effectEditorRect() const {const auto g=geometry();return {g.graph.x,g.graph.y,g.automation.x+g.automation.w-g.graph.x,g.graph.h};}
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
        mixerEditorOpen=false;
        if(busy)throw std::runtime_error("Wait for the document worker before opening an FX draft");
        noteEditorOpen=false;
        if(effectDraftExists&&!reload) {
            effectEditorOpen=true;
            workspaceState.show("notes",position(),cursorSample(),false);layoutControls();SetFocus(controls.at(effectValue));
            effectEditorStatus=L"Existing draft retained / Use cursor to edit another cell";return;
        }
        effectEditorOpen=effectDraftExists=true;workspaceState.show("notes",position(),cursorSample(),false);
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
        const auto r=effectEditorRect();const bool show=effectEditorVisible();
        const auto pattern=geometry().pattern;place(effectsCommand,pattern.x+pattern.w-172,pattern.y+3,84,24);
        place(effectReload,r.x+r.w-152,r.y+4,82,24,show);place(effectApply,r.x+r.w-64,r.y+4,58,24,show);
        const float x=r.x+12,w=r.w-24;
        place(effectSearch,x,r.y+47,std::min(170.0f,w*.26f),25,show);
        place(effectKind,x+std::min(180.0f,w*.28f),r.y+47,w-std::min(180.0f,w*.28f),260,show);
        place(effectValue,x,r.y+100,94,25,show);place(effectOffset,x+104,r.y+100,84,25,show);
        place(effectDuration,x+198,r.y+100,94,25,show);place(effectRange,x+302,r.y+100,64,25,show);
        place(effectBinding,x+376,r.y+100,std::max(80.0f,w-376),220,show);
    }
    void drawEffectEditor() {
        auto &s=*surface;const auto r=effectEditorRect();const float x=r.x+12;
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
