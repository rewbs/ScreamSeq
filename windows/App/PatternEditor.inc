// A retained FX draft captures its cell and revision. The grid and inspector use
// the same typed field descriptors and musical-unit conversion.
    bool effectEditorOpen=false,effectDraftExists=false,settingEffectFields=false,effectDraftDirty=false;
    bool effectRows=false,effectInline=false;
    uint64_t effectDraftGeneration=0;
    unsigned effectDraftPattern=0,effectDraftRow=0,effectDraftChannel=0,effectDraftColumn=0,effectDraftRowsPerBeat=4;
    int effectInlineControl=0;size_t effectInlineField=0;
    std::string effectDraftRevision,effectDraftDocument;
    Json effectChoices=Json::array(),effectBindingChoices=Json::array(),effectFieldsDraft=Json::array(),effectDraftCommand=Json::object();
    std::vector<size_t> visibleEffectChoices;
    std::map<int,std::wstring> effectRenderedText;
    int effectSelected=-1;
    std::wstring effectEditorStatus=L"Enter on an FX cell to edit";
    struct InitialEffects {
        std::shared_ptr<const ScreamSeq::DocumentView> source;
        std::string document,revision,plugin;
        uint64_t context=0,generation=0;
        uint32_t parameter=0;
        unsigned pattern=0,row=0,channel=0,column=0,rowsPerBeat=4;
        bool rows=false;
        Json cursor,parameterCatalog,choices=Json::array(),bindings=Json::array(),command,descriptors;
        std::vector<size_t> visible;
        std::vector<std::wstring> choiceLabels,bindingLabels;
        std::array<std::wstring,5> previousFields;
        std::array<LRESULT,2> previousSelections{};
        int selected=0,selectedBinding=-1;
        std::wstring status=L"Draft / changes apply to the displayed cell / Escape returns to the grid";
    };
    std::array<std::wstring,5> initialEffectFields() const {
        return {effectField(effectValue),effectField(effectOffset),effectField(effectDuration),effectField(effectRange),effectField(effectSearch)};
    }
    void validateInitialEffects(const InitialEffects &initial) const {
        const auto published=controller->view();
        if(busy||recoveryRestoring||effectDraftExists||effectEditorOpen||effectDraftGeneration!=initial.generation||effectRows!=initial.rows||
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
        initial.rows=effectRows;initial.rowsPerBeat=view->patternRowsPerBeat.at(patternIndex);
        initial.plugin=selectedPlugin;initial.parameter=selectedParameter;initial.parameterCatalog=pluginParameters;
        initial.previousFields=initialEffectFields();initial.previousSelections={SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0),SendMessageW(controls.at(effectBinding),CB_GETCURSEL,0,0)};
        if(!initial.source->patterns.contains(initial.pattern)||initial.row>=initial.source->pattern(initial.pattern).rows||
            initial.channel>=initial.source->channels||initial.column>=initial.source->effectColumns.at(initial.channel))
            throw std::runtime_error("The inspected FX cell no longer exists / choose another target");
        initial.choices=initial.source->commands.at("effect");
        for(const auto &entry:initial.source->commands.at("native"))initial.choices.push_back(entry);
        const auto current=initial.source->effect(initial.pattern,initial.row,initial.channel,initial.column);
        const auto currentCode=current?effectText(current).first:L"..";
        const auto wanted=code.empty()?currentCode:wide(code);bool selected=false;
        for(size_t i=0;i<initial.choices.size();++i){
            const auto &choice=initial.choices[i];if(!selected&&wide(choice.at("displayCode").get<std::string>())==wanted){initial.selected=int(i);selected=true;}
            initial.visible.push_back(i);initial.choiceLabels.push_back(wide(choice.at("displayCode").get<std::string>()+"  "+choice.at("name").get<std::string>()));
        }
        const bool same=current&&(code.empty()||wide(code)==currentCode);
        const auto &descriptor=initial.choices.at(initial.selected);
        initial.command=same?ScreamSeq::PatternFields::command(*current):ScreamSeq::PatternFields::defaults(descriptor);
        initial.descriptors=descriptor.value("kind",std::string("tracker"))=="tracker"?Json::array({ScreamSeq::PatternFields::trackerField()}):descriptor.at("parameters");
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
        ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_RESETCONTENT,0,0);for(const auto &label:initial.choiceLabels)ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_SETCURSEL,effectSelected,0);
        ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_RESETCONTENT,0,0);for(const auto &label:initial.bindingLabels)ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_SETCURSEL,initial.selectedBinding,0);
        ScreamSeq::NativeInputGate::text(controls.at(effectSearch),L"");
        effectDraftRowsPerBeat=initial.rowsPerBeat;effectRows=initial.rows;
        effectDraftCommand=std::move(initial.command);effectFieldsDraft=std::move(initial.descriptors);
        fillEffectFields();effectDraftDirty=false;effectDraftExists=true;++effectDraftGeneration;
    }
    bool effectEditorVisible() const {return !effectInline&&workspaceMainEditorVisible("effects");}
    ScreamSeq::WorkspaceRect effectEditorRect() const {return workspaceMainEditorRect();}
    std::wstring effectField(int id) const {
        const auto h=controls.at(id);const int count=GetWindowTextLengthW(h);std::wstring value(size_t(count)+1,0);
        GetWindowTextW(h,value.data(),count+1);value.resize(count);return value;
    }
    bool isEffectControl(int id)const{return (id>=effectKind&&id<=effectUnits)||(id>=effectFieldBase&&id<effectFieldBase+10)||(id>=effectChoiceBase&&id<effectChoiceBase+10);}
    double effectNumber(int id,double low,double high,bool whole=false) const {
        const auto text=effectField(id);size_t used=0;double number=0;
        try {number=std::stod(text,&used);}catch(...){throw std::runtime_error("Enter a numeric FX value");}
        if(used!=text.size()||!std::isfinite(number)||number<low||number>high||(whole&&std::floor(number)!=number))throw std::runtime_error("FX value is outside its allowed range");return number;
    }
    int effectFieldControl(size_t index)const {
        const auto &f=effectFieldsDraft.at(index);const auto storage=f.at("storage").get<std::string>();
        if(storage=="binding")return effectBinding;
        if(storage=="value"||storage=="parameter")return effectValue;
        if(storage=="offset")return effectOffset;if(storage=="duration"||storage=="durationBeats")return effectDuration;if(storage=="pitchRange")return effectRange;
        return (f.at("type")=="choice"||f.at("type")=="boolean"?effectChoiceBase:effectFieldBase)+int(index);
    }
    void installEffectControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{effectsCommand,L"Pattern FX"},{effectApply,L"Apply"},{effectReload,L"Use cursor"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        auto combo=[&](int id){controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);};
        auto edit=[&](int id){controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(controls.at(id),EM_SETLIMITTEXT,id==effectSearch?128:48,0);};
        for(int id:{effectKind,effectBinding,effectUnits})combo(id);
        for(int id:{effectValue,effectOffset,effectDuration,effectRange,effectSearch})edit(id);
        for(int i=0;i<10;++i){edit(effectFieldBase+i);combo(effectChoiceBase+i);}
        for(const auto *unit:{L"Beats",L"Rows"})ScreamSeq::NativeInputGate::present(controls.at(effectUnits),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(unit));
        ScreamSeq::NativeInputGate::present(controls.at(effectUnits),CB_SETCURSEL,0,0);
        for(auto [id,h]:controls)if((id==effectsCommand||isEffectControl(id))&&!h)throw std::runtime_error("Cannot create pattern FX controls");
    }
    void filterEffects() {
        if(settingEffectFields)return;
        auto query=effectField(effectSearch);std::transform(query.begin(),query.end(),query.begin(),towlower);
        visibleEffectChoices.clear();ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_RESETCONTENT,0,0);int selected=-1;
        for(size_t i=0;i<effectChoices.size();++i) {
            auto label=wide(effectChoices[i].at("displayCode").get<std::string>()+"  "+effectChoices[i].at("name").get<std::string>());
            auto match=label+L" "+wide(effectChoices[i].value("description",std::string())+" "+effectChoices[i].value("equivalents",std::string()));std::transform(match.begin(),match.end(),match.begin(),towlower);
            if(!query.empty()&&match.find(query)==std::wstring::npos)continue;
            if(int(i)==effectSelected)selected=int(visibleEffectChoices.size());
            visibleEffectChoices.push_back(i);ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        ScreamSeq::NativeInputGate::present(controls.at(effectKind),CB_SETCURSEL,selected,0);
    }
    void fillEffectFields() {
        const bool previous=settingEffectFields;settingEffectFields=true;
        for(size_t i=0;i<effectFieldsDraft.size();++i){const auto &f=effectFieldsDraft[i];const int id=effectFieldControl(i);if(id==effectBinding)continue;
            const auto value=ScreamSeq::PatternFields::get(effectDraftCommand,f);
            if(f.at("type")=="choice"||f.at("type")=="boolean"){
                const Json options=f.at("type")=="boolean"?Json::array({false,true}):f.at("choices");int selected=0;ScreamSeq::NativeInputGate::present(controls.at(id),CB_RESETCONTENT,0,0);
                for(size_t j=0;j<options.size();++j){const auto label=options[j].is_boolean()?(options[j].get<bool>()?L"On":L"Off"):wide(options[j].get<std::string>());ScreamSeq::NativeInputGate::present(controls.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(options[j]==value)selected=int(j);}
                ScreamSeq::NativeInputGate::present(controls.at(id),CB_SETCURSEL,selected,0);
            }else {const auto text=wide(ScreamSeq::PatternFields::text(effectDraftCommand,f,effectDraftRowsPerBeat,effectRows));effectRenderedText[id]=text;ScreamSeq::NativeInputGate::text(controls.at(id),text.c_str());}
        }
        settingEffectFields=previous;
    }
    Json readEffectFields()const {
        Json result=effectDraftCommand;
        for(size_t i=0;i<effectFieldsDraft.size();++i){const auto &f=effectFieldsDraft[i];const int id=effectFieldControl(i);Json value;
            if(id==effectBinding){const auto selected=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);if(selected<0||size_t(selected)>=effectBindingChoices.size())throw std::runtime_error("Choose a parameter binding, or select a parameter in Plugins first");value=effectBindingChoices[size_t(selected)].at("id");}
            else if(f.at("type")=="choice"||f.at("type")=="boolean"){const auto selected=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);if(selected<0)throw std::runtime_error("Choose an FX option");value=f.at("type")=="boolean"?Json(selected!=0):f.at("choices").at(size_t(selected));}
            else if(effectRenderedText.contains(id)&&effectField(id)==effectRenderedText.at(id))value=ScreamSeq::PatternFields::get(effectDraftCommand,f);
            else {const auto raw=ScreamSeq::PatternFields::rawNumber(effectNumber(id,-1e100,1e100),f,effectDraftRowsPerBeat,effectRows,effectDraftCommand);value=f.at("type")=="integer"?Json(int64_t(raw)):Json(raw);}
            ScreamSeq::PatternFields::set(result,f,value);
        }
        if(result.at("kind")=="tracker"){const auto &choice=effectChoices.at(effectSelected);const auto mask=choice.at("parameterMask").get<unsigned>();result["parameter"]=(unsigned(result.at("parameter").get<double>())&~mask)|choice.at("parameterValue").get<unsigned>();}
        return result;
    }
    void selectEffectFields(const Json &command) {
        effectDraftCommand=command;
        effectFieldsDraft=effectChoices.at(effectSelected).value("kind",std::string("tracker"))=="tracker"?Json::array({ScreamSeq::PatternFields::trackerField()}):effectChoices.at(effectSelected).at("parameters");
        fillEffectFields();layoutControls();
    }
    void focusEffectField() {
        int target=effectFieldsDraft.empty()?effectKind:effectFieldControl(0);
        const auto fields=currentEffectFields();if(effectFieldIndex<fields.size())for(size_t i=0;i<effectFieldsDraft.size();++i)if(effectFieldsDraft[i].at("storage")==fields[effectFieldIndex].at("storage"))target=effectFieldControl(i);
        SetFocus(controls.at(target));ScreamSeq::NativeInputGate::present(controls.at(target),EM_SETSEL,0,-1);
    }
    void openEffectEditor(const std::string &code="",bool reload=false) {
        if(code=="SK"&&view->nativePattern->scratchGestures.empty()){openScratchGestures();return;}
        graphEditorOpen=false;mixerEditorOpen=false;
        if(busy)throw std::runtime_error("Wait for the document worker before opening an FX draft");effectInline=false;revealWorkspaceMainEditor("effects");
        if(effectDraftExists&&effectDraftDirty&&!reload){effectEditorOpen=true;workspaceState.show("notes",position(),cursorSample(),false);layoutControls();focusEffectField();effectEditorStatus=L"Existing draft retained / Use cursor to edit another cell";return;}
        effectEditorOpen=effectDraftExists=true;workspaceState.show("notes",position(),cursorSample(),false);
        effectDraftPattern=patternIndex;effectDraftRow=row;effectDraftChannel=channel;effectDraftColumn=column>=3?(column-3)/2:0;
        ++effectDraftGeneration;effectDraftRevision=view->session.revision;effectDraftDocument=documentId;effectDraftRowsPerBeat=view->patternRowsPerBeat.at(patternIndex);
        effectChoices=view->commands.at("effect");for(const auto &entry:view->commands.at("native"))effectChoices.push_back(entry);
        const auto current=view->effect(patternIndex,row,channel,effectDraftColumn);const auto currentCode=current?effectText(current).first:L"..";
        effectSelected=0;for(size_t i=0;i<effectChoices.size();++i)if(wide(effectChoices[i].at("displayCode").get<std::string>())==(code.empty()?currentCode:wide(code))){effectSelected=int(i);break;}
        settingEffectFields=true;ScreamSeq::NativeInputGate::text(controls.at(effectSearch),L"");settingEffectFields=false;filterEffects();
        const bool same=current&&(code.empty()||wide(code)==currentCode);
        effectBindingChoices=Json::array();ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_RESETCONTENT,0,0);int selectedBinding=-1;
        for(const auto &[id,binding]:view->nativePattern->performance.bindings){if(same&&current->binding==id)selectedBinding=int(effectBindingChoices.size());effectBindingChoices.push_back({{"id",id}});const auto label=std::to_wstring(id)+L"  "+wide(binding.name.empty()?binding.plugin:binding.name);ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(const auto &parameter:pluginParameters)if(parameter.at("id")==selectedParameter&&parameter.value("writable",false)&&!selectedPlugin.empty()){
            unsigned free=1;while(free<=255&&view->nativePattern->performance.bindings.contains(uint16_t(free)))++free;
            if(free<=255){effectBindingChoices.push_back({{"id",free},{"plugin",selectedPlugin},{"parameter",selectedParameter},{"name",parameter.at("name")}});const auto label=L"New: "+wide(parameter.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(selectedBinding<0)selectedBinding=int(effectBindingChoices.size())-1;}break;
        }
        if(selectedBinding<0&&!effectBindingChoices.empty())selectedBinding=0;ScreamSeq::NativeInputGate::present(controls.at(effectBinding),CB_SETCURSEL,selectedBinding,0);
        selectEffectFields(same?ScreamSeq::PatternFields::command(*current):ScreamSeq::PatternFields::defaults(effectChoices.at(effectSelected)));
        effectDraftDirty=false;effectEditorStatus=L"Draft / Apply edits the captured cell / Escape returns to the grid";focusEffectField();
    }
    void openInlineEffect() {
        if(effectDraftExists&&effectDraftDirty){openEffectEditor();return;}
        const auto c=column>=4?view->effect(patternIndex,row,channel,(column-3)/2):std::optional<Tracker::PatternCommand>{};
        if(!c||c->kind==Tracker::PatternCommandKind::TrackerEffect||currentEffectFields().empty()){openEffectEditor();return;}
        openEffectEditor("",true);effectInline=true;effectEditorOpen=false;effectInlineControl=0;revealWorkspacePattern();workspaceState.focus="pattern";
        const auto fields=currentEffectFields();if(!fields.empty())for(size_t i=0;i<effectFieldsDraft.size();++i)if(effectFieldsDraft[i].at("storage")==fields[std::min(effectFieldIndex,fields.size()-1)].at("storage")){effectInlineControl=effectFieldControl(i);effectInlineField=std::min(effectFieldIndex,fields.size()-1);}
        layoutControls();if(effectInlineControl){SetFocus(controls.at(effectInlineControl));ScreamSeq::NativeInputGate::present(controls.at(effectInlineControl),EM_SETSEL,0,-1);}
    }
    void effectFieldChanged(){if(!settingEffectFields&&effectDraftExists){effectDraftDirty=true;++effectDraftGeneration;}}
    bool effectCommand(int id) {
        if(id!=effectsCommand&&!isEffectControl(id))return false;
        if(id==effectsCommand||id==effectReload){openEffectEditor("",id==effectReload);return true;}
        if(id==effectUnits){try{auto command=effectDraftExists?readEffectFields():effectDraftCommand;const bool next=SendMessageW(controls.at(effectUnits),CB_GETCURSEL,0,0)==1;effectDraftCommand=std::move(command);effectRows=next;fillEffectFields();}catch(const std::exception&e){ScreamSeq::NativeInputGate::present(controls.at(effectUnits),CB_SETCURSEL,effectRows?1:0,0);effectEditorStatus=wide(e.what());}return true;}
        for(size_t i=0;i<effectFieldsDraft.size();++i)if(effectFieldsDraft[i].at("key")=="rateMode"&&id==effectFieldControl(i)){
            try{effectDraftCommand=readEffectFields();fillEffectFields();effectFieldChanged();}catch(const std::exception&e){effectEditorStatus=wide(e.what());}return true;
        }
        if(id==effectKind){const auto selected=SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0);if(selected>=0&&size_t(selected)<visibleEffectChoices.size()){effectSelected=int(visibleEffectChoices[size_t(selected)]);selectEffectFields(ScreamSeq::PatternFields::defaults(effectChoices.at(effectSelected)));effectFieldChanged();}return true;}
        if(id==effectApply) {
            try {
                if(effectDraftDocument!=documentId||effectDraftRevision!=view->session.revision)throw std::runtime_error("Document changed / draft retained. Use cursor to reload before applying");
                if(effectSelected<0||SendMessageW(controls.at(effectKind),CB_GETCURSEL,0,0)<0)throw std::runtime_error("Choose an FX command");
                const auto submittedGeneration=effectDraftGeneration;
                Json command=readEffectFields(),binding=nullptr;int bindingIndex=-1;
                if(command.contains("binding")){const auto selected=SendMessageW(controls.at(effectBinding),CB_GETCURSEL,0,0);binding=effectBindingChoices.at(size_t(selected));bindingIndex=int(selected);}
                Json params={{"expectedRevision",effectDraftRevision},{"pattern",effectDraftPattern},{"row",effectDraftRow},{"channel",effectDraftChannel},{"column",effectDraftColumn},{"command",command}};
                if(binding.is_object()&&binding.contains("plugin")) {
                    auto snapshot=documentOperation("pattern.effects.get",{{"pattern",effectDraftPattern}});auto commands=snapshot.at("commands");
                    std::erase_if(commands.get_ref<Json::array_t &>(),[&](const auto &c){return c.at("channel")==effectDraftChannel&&c.at("column")==effectDraftColumn&&c.at("position").template get<unsigned>()/Tracker::performanceUnitsPerRow==effectDraftRow;});
                    command["position"]=effectDraftRow*Tracker::performanceUnitsPerRow+command.value("offset",0u);command.erase("offset");command["channel"]=effectDraftChannel;command["column"]=effectDraftColumn;commands.push_back(command);
                    documentOperation("pattern.effects.set",{{"expectedRevision",effectDraftRevision},{"pattern",effectDraftPattern},{"commands",commands},{"bindings",Json::array({binding})}});
                    // A successfully created binding is now an existing identity.
                    effectBindingChoices[size_t(bindingIndex)]={{"id",binding.at("id")}};
                }else documentOperation("pattern.effect.set",params);
                // This revision belongs to our own successful captured-cell
                // write. Rebase any newer draft text without replacing it.
                effectDraftRevision=view->session.revision;
                if(effectDraftGeneration==submittedGeneration){
                    effectDraftCommand=params.at("command");fillEffectFields();effectDraftDirty=false;effectEditorStatus=L"Applied / document Undo available";
                    if(effectInline){effectInline=false;SetFocus(window);layoutControls();}
                }else {effectEditorStatus=L"Earlier draft applied / newer edits retained and ready to Apply";status=effectEditorStatus;}
            }catch(const std::exception&e){effectEditorStatus=wide(e.what());status=effectEditorStatus;}
        }else effectFieldChanged();return true;
    }
    template<class Place> void layoutEffectControls(Place finalPlace) {
        std::map<int,std::array<float,5>> placements;
        auto place=[&](int id,float x,float y,float w,float h,bool visible=true){placements[id]={x,y,w,h,visible?1.0f:0.0f};};
        const auto r=effectEditorRect();const bool show=effectEditorVisible();const auto pattern=geometry().pattern;
        place(effectsCommand,pattern.x+pattern.w-254,pattern.y+3,78,24,trackerWorkspaceVisible());
        for(int id:{effectValue,effectOffset,effectDuration,effectRange,effectBinding})place(id,0,0,1,1,false);
        for(int i=0;i<10;++i){place(effectFieldBase+i,0,0,1,1,false);place(effectChoiceBase+i,0,0,1,1,false);}
        place(effectReload,r.x+r.w-152,r.y+4,82,24,show);place(effectApply,r.x+r.w-64,r.y+4,58,24,show);
        const float x=r.x+12,w=std::max(0.0f,r.w-24);place(effectSearch,x,r.y+47,std::min(170.0f,w*.26f),25,show);
        place(effectKind,x+std::min(180.0f,w*.28f),r.y+47,w-std::min(180.0f,w*.28f)-92,260,show);place(effectUnits,r.x+r.w-94,r.y+47,82,160,show);
        const unsigned columns=std::max(1u,unsigned(w/156));const float cell=w/columns;
        for(size_t i=0;i<effectFieldsDraft.size();++i){const auto &f=effectFieldsDraft[i];const bool combo=f.at("type")=="choice"||f.at("type")=="boolean"||f.at("key")=="binding";place(effectFieldControl(i),x+float(i%columns)*cell,r.y+100+float(i/columns)*49,cell-10,combo?220:25,show);}
        if(effectInline&&effectInlineControl&&effectDraftDocument==documentId&&effectDraftRevision==view->session.revision&&patternIndex==effectDraftPattern&&effectDraftChannel<view->channels&&effectDraftColumn<view->effectColumns.at(effectDraftChannel)&&effectInlineField<effectColumnLayout(effectDraftChannel,effectDraftColumn).widths.size()){const auto grid=patternGridRect();const float left=grid.x+gutter+channelStart(effectDraftChannel)+effectColumnStart(effectDraftChannel,effectDraftColumn)+effectColumnLayout(effectDraftChannel,effectDraftColumn).start(effectInlineField)-horizontalScroll;const float top=grid.y+gridHeader+float(int(effectDraftRow)-int(firstRow))*rowHeight;
            const bool visible=trackerWorkspaceVisible()&&patternIndex==effectDraftPattern&&top>=grid.y+gridHeader&&top+rowHeight<=grid.y+grid.h&&left>=grid.x+gutter&&left<grid.x+grid.w;
            wchar_t name[32]{};GetClassNameW(controls.at(effectInlineControl),name,32);place(effectInlineControl,left,top,std::max(60.0f,effectColumnLayout(effectDraftChannel,effectDraftColumn).widths.at(effectInlineField)),_wcsicmp(name,L"ComboBox")==0?220:rowHeight+4,visible);
        }
        for(const auto &[id,p]:placements)finalPlace(id,p[0],p[1],p[2],p[3],p[4]!=0);
    }
    void drawEffectEditor() {
        auto &s=*surface;const auto r=effectEditorRect();const float x=r.x+12,w=std::max(0.0f,r.w-24);s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x1a222e);
        s.uiText(L"PATTERN FX / P"+std::to_wstring(effectDraftPattern)+L" R"+std::to_wstring(effectDraftRow)+L" CH"+std::to_wstring(effectDraftChannel+1)+L" FX"+std::to_wstring(effectDraftColumn+1),x,r.y+8,r.w-170,0x6edac5);
        s.uiText(L"Search effects",x,r.y+28,160,0x91a0b2);s.uiText(L"Command",x+180,r.y+28,300,0x91a0b2);
        const unsigned columns=std::max(1u,unsigned(w/156));const float cell=w/columns;
        for(size_t i=0;i<effectFieldsDraft.size();++i){const auto &f=effectFieldsDraft[i];const auto unit=ScreamSeq::PatternFields::unit(f,effectRows,effectDraftCommand);s.uiText(wide(f.at("name").get<std::string>()+(unit.empty()?"":" / "+unit)),x+float(i%columns)*cell,r.y+80+float(i/columns)*49,cell-10,0x91a0b2);}
        const float bottom=r.y+132+float(std::max(size_t(1),(effectFieldsDraft.size()+columns-1)/columns)-1)*49;
        s.uiText(effectDraftRevision==view->session.revision?effectEditorStatus:L"Document changed / draft retained. Use cursor to reload before applying",x,bottom,w,0xe0bc75);
        if(effectSelected>=0)s.uiText(wide(effectChoices.at(effectSelected).value("description",std::string("Values follow the command catalog. Timing uses this pattern's rows per beat."))),x,bottom+21,w,0x91a0b2);
        s.unclip();s.outline(r.x,r.y,r.w,r.h,0x56858b);
    }
