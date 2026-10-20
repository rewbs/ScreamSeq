// Retained Windows rack. Identities and draft revisions survive list refreshes;
// all state mutation goes through the same worker transactions as the API.
    Json listedPlugins=Json::array(),pluginCatalog=Json::array(),pluginParameters=Json::array();
    std::string selectedPlugin,pluginDocument,pluginDetailsRevision,pluginDetailsTarget;
    std::string pluginDraftRevision,pluginDraftTarget;
    uint64_t pluginDraftGeneration=0;
    Json pluginPrograms=Json::array(),pluginPorts=Json::array();
    std::string selectedProgram,programCatalogRevision;
    int pluginDetailPage=0,selectedPort=-1;
    bool pluginProgramDraft=false,pluginExtrasError=false;
    #include "PluginPresetIntegration.inc"
    std::map<std::string,std::string> pendingPluginTriggers; // Plugin ID -> created instrument ID awaiting assignment.
    uint32_t selectedParameter=0,pluginDraftParameter=0;
    ScreamSeq::PluginParameterField pluginParameterField;
    bool settingPluginFields=false,pluginDraft=false,refreshingPlugins=false,pluginLibraryLoaded=false;
    std::unique_ptr<ScreamSeq::PluginInstrumentsWindow> pluginInstruments;
    std::unique_ptr<ScreamSeq::PluginLibraryWindow> pluginLibraryWindow;
    std::unique_ptr<ScreamSeq::PluginPathWindow> pluginPathWindow;
    void openPluginPath(const Json &target){
        if(!pluginPathWindow||(!pluginPathWindow->visible()&&!pluginPathWindow->retainedDraft()&&!pluginPathWindow->matchesTarget(target)))pluginPathWindow=std::make_unique<ScreamSeq::PluginPathWindow>(window,target,[this](const auto &method,const auto &params){return documentOperation(method,params);},[this]{return std::pair(documentId,view->session.revision);},[this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});
        pluginPathWindow->show();
    }
    void openPluginLibrary(std::string destinationDocument={},std::string destinationBus={},std::string destinationName={}){
        if(!pluginLibraryWindow)pluginLibraryWindow=std::make_unique<ScreamSeq::PluginLibraryWindow>(window,[this](const auto &method,const auto &params){
            if(method=="document.get")return view->session.document;
            auto result=documentOperation(method,params);if(method=="plugin.library.get"){pluginLibraryLoaded=false;armPluginTimer();}return result;
        },[this]{return std::pair(documentId,view->session.revision);},[this](const auto &method,const auto &params,const auto &receipt){
            auto result=documentOperationWithOutcome(method,params,receipt);if(method=="plugin.library.set"){pluginLibraryLoaded=false;armPluginTimer();}return result;
        });
        if(!destinationBus.empty())pluginLibraryWindow->setDestination(std::move(destinationDocument),std::move(destinationBus),std::move(destinationName));
        pluginLibraryWindow->show();
    }
    void openPluginInstruments(){
        if(pluginInstruments&&(pluginInstruments->visible()||pluginInstruments->retainedDraft())){pluginInstruments->show();return;}
        const auto slot=selectedPluginSlot();if(slot<0||!view->session.document.at("nativePlugins")[slot].value("isInstrument",false))throw std::runtime_error("Select an instrument plugin");
        pluginInstruments=std::make_unique<ScreamSeq::PluginInstrumentsWindow>(window,selectedPlugin,[this](const auto &method,const auto &params){return documentOperation(method,params);},[this]{return std::pair(documentId,view->session.revision);});pluginInstruments->show();
    }
    std::wstring pluginDetailStatus=L"Select a plugin to edit its parameters";
    bool pluginEditorVisible() const {return workspaceMainEditorVisible("plugins");}
    int selectedPluginSlot() const {
        const auto &rack=view->session.document.at("nativePlugins");
        for(size_t i=0;i<rack.size();++i) if(rack[i].at("instanceID")==selectedPlugin) return int(i);
        return -1;
    }
    void installPluginControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{
            {pluginAdd,L"Add"},{pluginRescan,L"Rescan"},{pluginEditor,L"Editor"},{pluginBypass,L"Bypass"},
            {pluginRemove,L"Remove"},{pluginUp,L"Up"},{pluginDown,L"Down"},{pluginUndo,L"Undo"},{pluginRedo,L"Redo"},
            {pluginAutomate,L"Automation…"},{pluginActivity,L"Activity…"},{pluginApply,L"Retry"},{pluginAssign,L"Assign"},{pluginsCommand,L"Plugins"},{pluginNewInstrument,L"New trigger"},
            {pluginLoadProgram,L"Load program"},{pluginTogglePort,L"Enable port"},{pluginAliases,L"Assigned instruments…"},{pluginSavePreset,L"Save preset…"},{pluginLoadPreset,L"Load preset…"},{pluginBrowse,L"Browse…"},{pluginReconnect,L"Reconnect plugin…"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{pluginLibrary,pluginParameter,pluginInstrument,pluginPage,pluginProgram,pluginPort,pluginValueChoice})
            controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        controls[pluginList]=CreateWindowExW(0,L"LISTBOX",L"Plugin rack",WS_CHILD|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(pluginList)),GetModuleHandleW(nullptr),nullptr);
        controls[pluginValue]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(pluginValue)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(controls.at(pluginValue),EM_SETLIMITTEXT,64,0);
        controls[pluginValueToggle]=CreateWindowExW(0,L"BUTTON",L"Parameter switch",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(pluginValueToggle)),GetModuleHandleW(nullptr),nullptr);
        if(!controls.at(pluginValueToggle))throw std::runtime_error("Cannot create native parameter switch");
        for(const int id:{pluginValueChoice,pluginValueToggle})
            ScreamSeq::check(SetWindowPos(controls.at(id),controls.at(pluginValue),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order native parameter fields");
        for(const auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{pluginParameterSearch,L"Search parameters by name, unit or ID"},{pluginProgramSearch,L"Search factory programs by name or group"}}) {
            controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            if(!controls[id])throw std::runtime_error("Cannot create plugin search field");
            SendMessageW(controls.at(id),EM_SETLIMITTEXT,128,0);SendMessageW(controls.at(id),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(label));
            ScreamSeq::check(ScreamSeq::NativeAccessibility::name(controls.at(id),label),"Name plugin search");
            const auto selector=controls.at(id==pluginProgramSearch?pluginProgram:pluginParameter);
            ScreamSeq::check(SetWindowPos(controls.at(id),GetWindow(selector,GW_HWNDPREV),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order plugin search before results");
        }
        for(const auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{pluginParameter,L"Selected plugin parameter"},{pluginValue,L"Plugin parameter value"},{pluginProgram,L"Selected factory program"}})
            ScreamSeq::check(ScreamSeq::NativeAccessibility::name(controls.at(id),label),"Name plugin field");
        for(auto label:{L"Parameters",L"Factory programs",L"Audio ports",L"Instrument assignments",L"Presets",L"Plugin location"})ScreamSeq::NativeInputGate::present(controls.at(pluginPage),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        ScreamSeq::NativeInputGate::present(controls.at(pluginPage),CB_SETCURSEL,0,0);
        for(int id=pluginList;id<=pluginActivity;++id) if(!controls.at(id)) throw std::runtime_error("Cannot create native plugin controls");
    }
    static bool pluginSearchMatches(std::wstring_view text,std::wstring_view query) {
        if(query.empty())return true;
        for(size_t i=0;i+query.size()<=text.size();++i)
            if(CompareStringOrdinal(text.data()+i,int(query.size()),query.data(),int(query.size()),TRUE)==CSTR_EQUAL)return true;
        return false;
    }
    void filterPluginChoices(bool programs) {
        const int search=programs?pluginProgramSearch:pluginParameterSearch,selector=programs?pluginProgram:pluginParameter;
        if(!controls.contains(search)||!controls.contains(selector))return;
        std::wstring query(size_t(GetWindowTextLengthW(controls.at(search)))+1,0);
        GetWindowTextW(controls.at(search),query.data(),int(query.size()));query.resize(wcslen(query.c_str()));
        const auto &catalog=programs?pluginPrograms:pluginParameters;
        ScreamSeq::NativeInputGate::present(controls.at(selector),CB_RESETCONTENT,0,0);int selected=-1;
        for(size_t i=0;i<catalog.size();++i) {
            const auto &item=catalog[i];const bool current=programs?item.at("id")==selectedProgram:item.at("id")==selectedParameter;
            auto label=wide(programs?item.at("group").get<std::string>()+" / "+item.at("name").get<std::string>():
                item.at("name").get<std::string>()+(item.at("unitLabel").get<std::string>().empty()?"":" / "+item.at("unitLabel").get<std::string>()));
            const bool match=pluginSearchMatches(label+L" "+wide(item.at("id").dump()),query);
            if(!match&&!current)continue;
            if(programs&&!item.at("loadable").get<bool>())label+=L" [unavailable]";
            if(!match)label+=L" [selected; outside search]";
            const auto row=ScreamSeq::NativeInputGate::present(controls.at(selector),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(row==CB_ERR||row==CB_ERRSPACE)throw std::runtime_error("Cannot populate plugin search results");
            if(ScreamSeq::NativeInputGate::present(controls.at(selector),CB_SETITEMDATA,row,LPARAM(i))==CB_ERR) {
                ScreamSeq::NativeInputGate::present(controls.at(selector),CB_DELETESTRING,row,0);
                throw std::runtime_error("Cannot associate a plugin search result with its target");
            }
            if(current)selected=int(row);
        }
        if(SendMessageW(controls.at(selector),CB_GETCOUNT,0,0)==0) {
            const auto label=catalog.empty()?(programs?L"No standard factory programs":L"No exposed parameters"):(programs?L"No matching programs":L"No matching parameters");
            const auto row=ScreamSeq::NativeInputGate::present(controls.at(selector),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            if(row==CB_ERR||row==CB_ERRSPACE)throw std::runtime_error("Cannot display empty plugin search");
            if(ScreamSeq::NativeInputGate::present(controls.at(selector),CB_SETITEMDATA,row,LPARAM(-1))==CB_ERR) {
                ScreamSeq::NativeInputGate::present(controls.at(selector),CB_DELETESTRING,row,0);
                throw std::runtime_error("Cannot mark empty plugin search results");
            }
            selected=int(row);
        }
        ScreamSeq::NativeInputGate::present(controls.at(selector),CB_SETCURSEL,selected,0);frameRequested=true;
    }
    int pluginChoiceIndex(int selector)const {
        const auto row=SendMessageW(controls.at(selector),CB_GETCURSEL,0,0);
        if(row==CB_ERR)return -1;
        const auto index=SendMessageW(controls.at(selector),CB_GETITEMDATA,row,0);
        const auto &catalog=selector==pluginProgram?pluginPrograms:pluginParameters;
        return index<0||size_t(index)>=catalog.size()?-1:int(index);
    }
    void syncPluginRack() {
        if(!controls.contains(pluginList)) return;
        if(pluginDocument!=documentId) {pluginDocument=documentId;selectedPlugin.clear();pluginDraft=false;pluginDetailsRevision.clear();pluginDetailsTarget.clear();listedPlugins=nullptr;pendingPluginTriggers.clear();}
        const auto &rack=view->session.document.at("nativePlugins");
        if(listedPlugins!=rack) {
            const auto top=SendMessageW(controls.at(pluginList),LB_GETTOPINDEX,0,0);
            listedPlugins=rack;SendMessageW(controls.at(pluginList),WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(controls.at(pluginList),LB_RESETCONTENT,0,0);
            if(selectedPluginSlot()<0 && !pluginDraft) selectedPlugin=rack.empty()?"":rack[0].at("instanceID").get<std::string>();
            for(size_t i=0;i<rack.size();++i) {
                auto label=std::to_wstring(i+1)+L"  "+wide(rack[i].at("name").get<std::string>())+(rack[i].value("bypass",false)?L"  [off]":L"");
                ScreamSeq::NativeInputGate::present(controls.at(pluginList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
            if(top!=LB_ERR)ScreamSeq::NativeInputGate::present(controls.at(pluginList),LB_SETTOPINDEX,top,0);
            SendMessageW(controls.at(pluginList),WM_SETREDRAW,TRUE,0);
        }
        ScreamSeq::NativeInputGate::present(controls.at(pluginList),LB_SETCURSEL,selectedPluginSlot(),0);
        armPluginTimer();
    }
    void armPluginTimer() {
        const bool editors=view->session.document.value("openPluginEditors",0u)!=0;
        if(!refreshingPlugins && (editors || (!pluginDraft && pluginEditorVisible() && (pluginDetailsRevision!=view->session.revision || pluginDetailsTarget!=selectedPlugin || !pluginLibraryLoaded))))
            SetTimer(window,1,editors?16:100,nullptr);
    }
    int pluginValueControl()const {
        if(!pluginParameterField.valid)return pluginValue;
        return pluginParameterField.kind==ScreamSeq::PluginParameterField::Kind::Toggle?pluginValueToggle:
            pluginParameterField.kind==ScreamSeq::PluginParameterField::Kind::Choice?pluginValueChoice:pluginValue;
    }
    void setPluginParameterField() {
        if(pluginDraft)return;
        pluginParameterField={};std::wstring value;
        for(const auto &p:pluginParameters)if(p.at("id")==selectedParameter) {
            pluginParameterField=ScreamSeq::PluginParameterField::read(p);
            const auto current=p.contains("manualValue")?p.at("manualValue"):p.at("value");
            if(current.is_number()){std::wostringstream stream;stream<<std::setprecision(9)<<current.get<double>();value=stream.str();}
            break;
        }
        settingPluginFields=true;
        try {
            if(!automaticPluginEdit)ScreamSeq::NativeInputGate::text(controls.at(pluginValue),value.c_str());
            ScreamSeq::NativeInputGate::present(controls.at(pluginValueChoice),CB_RESETCONTENT);
            const auto &field=pluginParameterField;
            if(field.valid&&field.kind==ScreamSeq::PluginParameterField::Kind::Choice) {
                for(const auto &choice:field.choices) {
                    const auto label=wide(choice);const auto row=ScreamSeq::NativeInputGate::present(controls.at(pluginValueChoice),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                    if(row==CB_ERR||row==CB_ERRSPACE)throw std::runtime_error("Cannot display parameter choices");
                }
                ScreamSeq::NativeInputGate::present(controls.at(pluginValueChoice),CB_SETCURSEL,WPARAM(field.value));
            }
            if(field.valid&&field.kind==ScreamSeq::PluginParameterField::Kind::Toggle) {
                const bool on=field.value==1;const auto label=wide(field.choices[on?1:0]);
                ScreamSeq::NativeInputGate::text(controls.at(pluginValueToggle),label.c_str());
                ScreamSeq::NativeInputGate::present(controls.at(pluginValueToggle),BM_SETCHECK,on?BST_CHECKED:BST_UNCHECKED);
            }
            if(!field.name.empty()) {
                if(!field.valid)pluginDetailStatus=L"Parameter metadata unavailable / no edit was made";
                else {std::wostringstream range;range<<std::setprecision(6)<<field.minimum<<L" to "<<field.maximum;
                    pluginDetailStatus=(field.writable?L"Manual value / ":L"Read-only value / ")+range.str()+L" "+wide(field.unit);}
            }
            const auto name=wide(field.name)+(field.valid?L" manual value":L" unavailable value");
            for(const int id:{pluginValue,pluginValueChoice,pluginValueToggle})
                ScreamSeq::check(ScreamSeq::NativeAccessibility::name(controls.at(id),name.c_str()),"Name native parameter value");
        }catch(...){settingPluginFields=false;throw;}
        settingPluginFields=false;
    }
    void syncPluginDiscreteCandidate() {
        const auto &field=pluginParameterField;if(!field.valid)return;
        wchar_t raw[66]{};GetWindowTextW(controls.at(pluginValue),raw,66);wchar_t *end=nullptr;const auto number=wcstod(raw,&end);
        const auto value=end!=raw&&!*end&&std::isfinite(number)&&number>=field.minimum&&number<=field.maximum&&std::floor(number)==number?number:field.value;
        if(field.kind==ScreamSeq::PluginParameterField::Kind::Choice)ScreamSeq::NativeInputGate::present(controls.at(pluginValueChoice),CB_SETCURSEL,WPARAM(value));
        if(field.kind==ScreamSeq::PluginParameterField::Kind::Toggle) {
            ScreamSeq::NativeInputGate::present(controls.at(pluginValueToggle),BM_SETCHECK,value==1?BST_CHECKED:BST_UNCHECKED);
            const auto label=wide(field.choices[value==1?1:0]);ScreamSeq::NativeInputGate::text(controls.at(pluginValueToggle),label.c_str());
        }
    }
    void pluginDiscreteValue(int id) {
        if(settingPluginFields)return;
        if(pluginDraft){syncPluginDiscreteCandidate();throw std::runtime_error("Apply or discard the retained parameter/program draft before changing a control");}
        if(!pluginParameterField.editable()||pluginValueControl()!=id){syncPluginDiscreteCandidate();throw std::runtime_error("This parameter control is unavailable");}
        const auto selected=id==pluginValueToggle?(SendMessageW(controls.at(id),BM_GETCHECK,0,0)==BST_CHECKED?1:0):SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
        if(selected<0||size_t(selected)>=pluginParameterField.choices.size()){syncPluginDiscreteCandidate();throw std::runtime_error("Choose an available parameter value");}
        const auto value=std::to_wstring(selected);
        ScreamSeq::NativeInputGate::text(controls.at(pluginValue),value.c_str());
        if(!pluginDraft)pluginFieldChanged();
        // Capture the catalogue revision, not a newer revision which may have
        // arrived while these controls were still showing the previous state.
        pluginDraftRevision=pluginDetailsRevision;pluginDraftTarget=pluginDetailsTarget;syncPluginDiscreteCandidate();
        pluginCommand(pluginApply);
        refreshPluginDetails();
    }
    void refreshPluginLibrary(bool rescan=false) {
        const auto library=documentOperation("plugin.library.get",{{"rescan",rescan}});Json catalog=Json::array();
        for(const auto &entry:library.at("plugins"))catalog.push_back(entry.at("descriptor"));
        auto old=SendMessageW(controls.at(pluginLibrary),CB_GETCURSEL,0,0);
        Json selected=old>=0 && size_t(old)<pluginCatalog.size()?pluginCatalog[size_t(old)]:Json();
        pluginCatalog=std::move(catalog);ScreamSeq::NativeInputGate::present(controls.at(pluginLibrary),CB_RESETCONTENT,0,0);int selection=0;
        for(size_t i=0;i<pluginCatalog.size();++i) {const auto &d=pluginCatalog[i];auto label=wide(d.at("name").get<std::string>()+" / "+d.at("format").get<std::string>());ScreamSeq::NativeInputGate::present(controls.at(pluginLibrary),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(d==selected)selection=int(i);}
        ScreamSeq::NativeInputGate::present(controls.at(pluginLibrary),CB_SETCURSEL,pluginCatalog.empty()?-1:selection,0);pluginLibraryLoaded=true;
    }
    void refreshPluginDetails() {
        if(pluginDraft) return;
        const auto slot=selectedPluginSlot();pluginParameters=Json::array();pluginDetailStatus=L"Select a plugin to edit its parameters";
        if(slot>=0) try {
            pluginParameters=documentOperation("plugin.parameters.get",{{"slot",slot}});
            pluginDetailStatus=wide(view->session.document.at("nativePlugins")[slot].at("name").get<std::string>());
        } catch(const std::exception &e) {pluginDetailStatus=wide(e.what());}
        if(!pluginParameters.empty()&&std::none_of(pluginParameters.begin(),pluginParameters.end(),[&](const auto &p){return p.at("id")==selectedParameter;}))selectedParameter=pluginParameters[0].at("id").get<uint32_t>();
        filterPluginChoices(false);setPluginParameterField();
        ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_RESETCONTENT,0,0);ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Unassigned"));ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_SETITEMDATA,0,0);
        unsigned assigned=slot<0?0:view->session.document.at("nativePlugins")[slot].value("instrument",0u);int instrumentSelection=0;
        for(const auto &i:view->session.document.at("instruments")) {auto n=i.at("index").get<unsigned>();auto label=std::to_wstring(n)+L"  "+wide(i.at("name").get<std::string>());auto item=ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_SETITEMDATA,item,n);if(n==assigned)instrumentSelection=int(item);}
        ScreamSeq::NativeInputGate::present(controls.at(pluginInstrument),CB_SETCURSEL,instrumentSelection,0);
        refreshPluginExtras(slot);
        pluginDetailsTarget=selectedPlugin;pluginDetailsRevision=view->session.revision;
    }
    void refreshPluginExtras(int slot) {
        pluginPrograms=Json::array();pluginPorts=Json::array();programCatalogRevision.clear();pluginExtrasError=false;
        const auto oldPort=selectedPort>=0 && size_t(selectedPort)<displayedPluginPorts.size()?displayedPluginPorts[size_t(selectedPort)]:Json();
        if(slot>=0)try {
            if(pluginDetailPage==1){auto result=documentOperation("plugin.programs.get",{{"plugin",selectedPlugin}});pluginPrograms=result.at("programs");programCatalogRevision=result.at("catalogRevision").get<std::string>();}
            if(pluginDetailPage==2)pluginPorts=documentOperation("plugin.buses.get",{{"slot",slot}}).at("buses");
        }catch(const std::exception &e){pluginDetailStatus=wide(e.what());pluginExtrasError=true;}
        if(!pluginPrograms.empty()&&std::none_of(pluginPrograms.begin(),pluginPrograms.end(),[&](const auto &p){return p.at("id")==selectedProgram;}))selectedProgram=pluginPrograms[0].at("id").get<std::string>();
        filterPluginChoices(true);
        ScreamSeq::NativeInputGate::present(controls.at(pluginPort),CB_RESETCONTENT,0,0);selectedPort=-1;displayedPluginPorts=pluginPorts;
        for(size_t i=0;i<pluginPorts.size();++i){const auto &p=pluginPorts[i];const auto index=p.at("index").get<unsigned>();
            auto label=wide(p.at("direction").get<std::string>()=="input"?"Input ":"Output ")+std::to_wstring(index)+L"  "+wide(p.at("name").get<std::string>())+L" / "+std::to_wstring(p.at("channels").get<unsigned>())+L" ch / "+(index==0?L"Main":!p.at("supported").get<bool>()?L"Unsupported":p.at("active").get<bool>()?L"On":L"Off");
            ScreamSeq::NativeInputGate::present(controls.at(pluginPort),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(!oldPort.is_null()&&oldPort.at("index")==p.at("index")&&oldPort.at("direction")==p.at("direction"))selectedPort=int(i);}
        if(selectedPort<0&&!pluginPorts.empty())selectedPort=0;
        if(pluginPorts.empty())ScreamSeq::NativeInputGate::present(controls.at(pluginPort),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No audio ports"));
        ScreamSeq::NativeInputGate::present(controls.at(pluginPort),CB_SETCURSEL,selectedPort<0?0:selectedPort,0);
    }
    Json displayedPluginPorts=Json::array();
    void pluginTimer() {
        if(busy || refreshingPlugins || pluginPresetPending) return;
        KillTimer(window,1);
        if(pluginAutoQueued&&!pluginEditorVisible()){pluginAutoQueued=false;pluginAutoFailed=true;}
        if(pluginAutoQueued&&pluginEditorVisible()){
            if(GetTickCount64()<pluginEditDue){SetTimer(window,1,100,nullptr);return;}
            pluginAutoQueued=false;
            if(pluginDraft&&!pluginProgramDraft){
                automaticPluginEdit=GetFocus()==controls.at(pluginValue);
                try{pluginCommand(pluginApply);refreshPluginDetails();}catch(const std::exception &e){pluginAutoFailed=true;status=wide(e.what());}
                automaticPluginEdit=false;layoutControls();
            }
        }
        refreshingPlugins=true;
        bool detailsChanged=false;
        try {
            if(view->session.document.value("openPluginEditors",0u)) documentOperation("flushPluginEditors",{{"force",false}});
            if(pluginEditorVisible()) {
                if(!pluginLibraryLoaded) {refreshPluginLibrary();detailsChanged=true;}
                if(!pluginDraft && (pluginDetailsRevision!=view->session.revision || pluginDetailsTarget!=selectedPlugin)) {refreshPluginDetails();detailsChanged=true;}
            }
        } catch(const std::exception &e) {status=wide(e.what());pluginLibraryLoaded=true;pluginDetailsRevision=view->session.revision;pluginDetailsTarget=selectedPlugin;}
        refreshingPlugins=false;if(detailsChanged)layoutControls();else armPluginTimer();
        // Background inspector reads yield to pending API work after finishing,
        // instead of causing random busy failures in otherwise idle sessions.
        if(api)PostMessageW(window,ScreamSeq::ApiDispatch::message,0,0);
    }
    bool pluginAutoQueued=false,pluginAutoFailed=false;ULONGLONG pluginEditDue=0;bool automaticPluginEdit=false;
    void pluginFieldChanged() {
        if(settingPluginFields) return;
        ++pluginDraftGeneration;pluginAutoQueued=true;pluginAutoFailed=false;pluginEditDue=GetTickCount64()+600;SetTimer(window,1,100,nullptr);
        if(!pluginDraft) {pluginDraft=true;pluginProgramDraft=false;pluginDraftRevision=view->session.revision;pluginDraftTarget=selectedPlugin;pluginDraftParameter=selectedParameter;}
        frameRequested=true;
    }
    template<class Place> void layoutPluginControls(const Place &place) {
        syncPluginRack();const auto r=workspaceMainEditorRect();const bool show=pluginEditorVisible();const auto x=r.x+230,y=r.y;
        place(pluginsCommand,0,0,1,1,false);place(pluginUndo,r.x+r.w-160,y+4,74,24,show);place(pluginRedo,r.x+r.w-82,y+4,74,24,show);
        place(pluginPage,x,y+4,std::min(180.0f,r.w-406),230,show);
        place(pluginLibrary,r.x+8,y+35,r.w-254,250,show);place(pluginBrowse,r.x+r.w-240,y+35,78,24,show);place(pluginAdd,r.x+r.w-154,y+35,64,24,show);place(pluginRescan,r.x+r.w-86,y+35,78,24,show);
        place(pluginList,r.x+8,y+67,210,std::max(24.0f,r.h-75),show);
        float bx=x;for(auto [id,w]:std::initializer_list<std::pair<int,float>>{{pluginEditor,66.0f},{pluginBypass,72.0f},{pluginRemove,68.0f},{pluginUp,44.0f},{pluginDown,52.0f}}) {place(id,bx,y+67,w,24,show);bx+=w+4;}
        const auto valueControl=pluginValueControl();
        const float valueWidth=valueControl==pluginValue?86.0f:std::clamp(r.w*.22f,100.0f,260.0f);
        const float parameterWidth=std::max(80.0f,r.w-320-valueWidth);
        place(pluginParameterSearch,x,y+99,r.w-242,24,show&&pluginDetailPage==0&&r.h>=164);
        place(pluginProgramSearch,x,y+99,r.w-242,24,show&&pluginDetailPage==1&&r.h>=164);
        place(pluginParameter,x,y+131,parameterWidth,240,show && pluginDetailPage==0 && r.h>=164);for(const int id:{pluginValue,pluginValueChoice,pluginValueToggle})place(id,x+parameterWidth+6,y+131,valueWidth,id==pluginValueChoice?240:24,show&&pluginDetailPage==0&&r.h>=164&&valueControl==id);place(pluginApply,x+parameterWidth+valueWidth+12,y+131,68,24,show && pluginDetailPage==0 && r.h>=164 && pluginAutoFailed && pluginDraft);
        place(pluginAutomate,x,y+163,132,24,show&&pluginDetailPage==0&&r.h>=196);
        place(pluginActivity,x+140,y+163,112,24,show&&pluginDetailPage==0&&r.h>=196);
        for(const int id:{pluginAutomate,pluginActivity})EnableWindow(controls.at(id),hasPluginParameterTarget()&&!busy&&!pluginPresetPending);
        place(pluginProgram,x,y+131,r.w-360,300,show && pluginDetailPage==1 && r.h>=164);place(pluginLoadProgram,r.x+r.w-120,y+131,112,24,show && pluginDetailPage==1 && r.h>=164);
        place(pluginPort,x,y+99,r.w-360,300,show && pluginDetailPage==2 && r.h>=132);place(pluginTogglePort,r.x+r.w-120,y+99,112,24,show && pluginDetailPage==2 && r.h>=132);
        place(pluginAliases,x,y+99,r.w-242,26,show&&pluginDetailPage==3&&r.h>=132);
        place(pluginSavePreset,x,y+99,132,26,show&&pluginDetailPage==4&&r.h>=132);
        place(pluginLoadPreset,x+140,y+99,132,26,show&&pluginDetailPage==4&&r.h>=132);
        place(pluginReconnect,x,y+99,r.w-242,26,show&&pluginDetailPage==5&&r.h>=132);
        const auto slot=selectedPluginSlot();const bool selected=slot>=0;const auto plugin=selected?view->session.document.at("nativePlugins")[slot]:Json::object();
        const bool instrument=selected && plugin.value("isInstrument",false);
        EnableWindow(controls.at(pluginAliases),instrument);
        const bool retainedPreset=pluginPresetCompletion.retained();
        ScreamSeq::NativeControls::text(controls.at(pluginSavePreset),retainedPreset?L"Review result":L"Save preset…");
        ScreamSeq::NativeControls::text(controls.at(pluginLoadPreset),retainedPreset?L"Use current state":L"Load preset…");
        EnableWindow(controls.at(pluginSavePreset),!pluginPresetPending&&(selected||retainedPreset));
        EnableWindow(controls.at(pluginLoadPreset),!pluginPresetPending&&(retainedPreset?pluginPresetNeedsReload:selected));
        place(pluginInstrument,x,y+195,r.w-424,220,show && instrument && pluginDetailPage==0 && r.h>=228);place(pluginNewInstrument,r.x+r.w-188,y+195,108,24,show && instrument && pluginDetailPage==0 && r.h>=228);place(pluginAssign,r.x+r.w-76,y+195,68,24,show && instrument && pluginDetailPage==0 && r.h>=228);
        EnableWindow(controls.at(pluginNewInstrument),instrument);ScreamSeq::NativeControls::text(controls.at(pluginNewInstrument),pendingPluginTriggers.contains(selectedPlugin)?L"Retry assign":L"New trigger");
        for(int id:{pluginBypass,pluginRemove,pluginAssign}) EnableWindow(controls.at(id),selected);
        EnableWindow(controls.at(pluginEditor),selected && plugin.value("format","")=="VST3");
        EnableWindow(controls.at(pluginReconnect),selected&&plugin.value("format","")=="VST3");
        EnableWindow(controls.at(pluginUp),selected && slot>0);EnableWindow(controls.at(pluginDown),selected && size_t(slot+1)<listedPlugins.size());
        ScreamSeq::NativeControls::text(controls.at(pluginBypass),selected && plugin.value("bypass",false)?L"Enable":L"Bypass");
        const bool writable=pluginParameterField.editable();
        EnableWindow(controls.at(pluginApply),selected&&writable);EnableWindow(controls.at(pluginValue),selected&&writable);
        for(const int id:{pluginValueChoice,pluginValueToggle})EnableWindow(controls.at(id),selected&&writable&&!pluginDraft);
        EnableWindow(controls.at(pluginUndo),view->session.document.value("canUndoPlugins",false));EnableWindow(controls.at(pluginRedo),view->session.document.value("canRedoPlugins",false));
        EnableWindow(controls.at(pluginAdd),!pluginCatalog.empty());
        bool loadable=false;for(const auto &p:pluginPrograms)if(p.at("id")==selectedProgram)loadable=p.at("loadable").get<bool>();
        EnableWindow(controls.at(pluginProgram),!pluginPrograms.empty());EnableWindow(controls.at(pluginLoadProgram),selected&&loadable);
        const auto port=selectedPort>=0 && size_t(selectedPort)<pluginPorts.size()?pluginPorts[size_t(selectedPort)]:Json();
        EnableWindow(controls.at(pluginPort),!pluginPorts.empty());EnableWindow(controls.at(pluginTogglePort),!port.is_null()&&port.at("index")!=0&&port.at("supported").get<bool>());
        ScreamSeq::NativeControls::text(controls.at(pluginTogglePort),!port.is_null()&&port.at("active").get<bool>()?L"Disable port":L"Enable port");
    }
    bool hasPluginParameterTarget()const {
        return selectedPluginSlot()>=0&&std::any_of(pluginParameters.begin(),pluginParameters.end(),[&](const auto &p){return p.at("id")==selectedParameter;});
    }
    bool pluginCommand(int id) {
        if(id<pluginList || id>pluginActivity)return false;
        if(busy){if(id==pluginValueChoice||id==pluginValueToggle)syncPluginDiscreteCandidate();throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");}
        workspaceState.focus="plugins";
        if(id==pluginValueChoice||id==pluginValueToggle){pluginDiscreteValue(id);return true;}
        if(id==pluginAutomate||id==pluginActivity) {
            if(!hasPluginParameterTarget())throw std::runtime_error("Select an available plugin parameter first");
            if(id==pluginAutomate)openParameterAutomation(selectedPlugin,selectedParameter,true);
            else openParameterActivity(selectedPlugin,selectedParameter,true);
            return true;
        }
        if(id==pluginBrowse){openPluginLibrary();return true;}
        if(id==pluginReconnect){if(selectedPluginSlot()<0)throw std::runtime_error("Select a VST3 plugin");openPluginPath({{"plugin",selectedPlugin}});return true;}
        if(id==pluginSavePreset||id==pluginLoadPreset){pluginPresetFile(id==pluginSavePreset);return true;}
        if(id==pluginAliases){openPluginInstruments();return true;}
        if(id==pluginsCommand) {effectEditorOpen=false;workspaceState.layout="Compose";workspaceState.active="notes";SetFocus(controls.at(pluginList));return true;}
        if(id==pluginPage){if(pluginDraft){ScreamSeq::NativeInputGate::present(controls.at(pluginPage),CB_SETCURSEL,pluginDetailPage,0);throw std::runtime_error("Apply the draft or press Escape before changing the detail page");}
            const auto page=SendMessageW(controls.at(pluginPage),CB_GETCURSEL,0,0);if(page>=0&&page<6){pluginDetailPage=int(page);pluginDetailsTarget.clear();}SetFocus(controls.at(pluginPage));return true;}
        if(id==pluginList) {
            if(pluginDraft) {ScreamSeq::NativeInputGate::present(controls.at(pluginList),LB_SETCURSEL,selectedPluginSlot(),0);throw std::runtime_error("Apply the parameter draft or press Escape before selecting another plugin");}
            const auto slot=SendMessageW(controls.at(pluginList),LB_GETCURSEL,0,0);if(slot!=LB_ERR)selectedPlugin=listedPlugins.at(size_t(slot)).at("instanceID").get<std::string>();pluginDetailsTarget.clear();return true;
        }
        if(id==pluginParameter) {
            if(pluginDraft) {filterPluginChoices(false);throw std::runtime_error("Apply the parameter draft or press Escape before choosing another parameter");}
            const auto selected=pluginChoiceIndex(pluginParameter);if(selected>=0)selectedParameter=pluginParameters.at(size_t(selected)).at("id").get<uint32_t>();filterPluginChoices(false);setPluginParameterField();return true;
        }
        if(id==pluginLibrary || id==pluginInstrument)return true;
        if(id==pluginProgram){const auto chosen=pluginChoiceIndex(pluginProgram);if(chosen>=0){
            ++pluginDraftGeneration;
            selectedProgram=pluginPrograms[size_t(chosen)].at("id").get<std::string>();if(!pluginDraft){pluginDraft=true;pluginProgramDraft=true;pluginDraftRevision=pluginDetailsRevision;pluginDraftTarget=pluginDetailsTarget;}filterPluginChoices(true);}return true;}
        if(id==pluginPort){const auto chosen=SendMessageW(controls.at(pluginPort),CB_GETCURSEL,0,0);if(chosen>=0&&size_t(chosen)<pluginPorts.size())selectedPort=int(chosen);return true;}
        if(id==pluginRescan) {refreshPluginLibrary(true);status=L"Plugin library scan finished";return true;}
        if(pluginDraft && id!=pluginApply && id!=pluginLoadProgram)throw std::runtime_error("Apply the draft or press Escape before changing the rack");
        if(id==pluginUndo || id==pluginRedo) {edit(id==pluginUndo?"history.undo":"history.redo",{{"domain","plugins"}});return true;}
        if(id==pluginAdd) {auto chosen=SendMessageW(controls.at(pluginLibrary),CB_GETCURSEL,0,0);if(chosen==CB_ERR)throw std::runtime_error("Select a plugin from the library");auto result=edit("plugin.add",{{"descriptor",pluginCatalog.at(size_t(chosen))}});selectedPlugin=view->session.document.at("nativePlugins").at(result.at("slot").get<size_t>()).at("instanceID").get<std::string>();return true;}
        const auto slot=selectedPluginSlot();if(slot<0)throw std::runtime_error("Select a plugin in the rack");
        if(id==pluginLoadProgram || id==pluginTogglePort){
            if((pluginDraft?pluginDraftRevision:pluginDetailsRevision)!=view->session.revision || (pluginDraft?pluginDraftTarget:pluginDetailsTarget)!=selectedPlugin)
                throw ScreamSeq::Api::ApiError(-32001,"The song or plugin changed. Press Escape to refresh the selection before applying it.");
            if(id==pluginLoadProgram){if(pluginDraft&&!pluginProgramDraft)throw std::runtime_error("Apply or discard the parameter draft first");
                edit("plugin.programs.load",{{"plugin",selectedPlugin},{"program",selectedProgram},{"expectedCatalogRevision",programCatalogRevision}});pluginDraft=false;pluginProgramDraft=false;pluginDetailsRevision.clear();
            }else{if(selectedPort<0||size_t(selectedPort)>=pluginPorts.size())throw std::runtime_error("Choose an audio port");const auto &port=pluginPorts[size_t(selectedPort)];const auto index=port.at("index").get<unsigned>();
                if(index==0||!port.at("supported").get<bool>())throw std::runtime_error("The main or unsupported audio port cannot be toggled");
                const auto key=port.at("direction")=="input"?"auxiliaryInputs":"auxiliaryOutputs";
                auto ports=view->session.document.at("nativePlugins")[slot].value(key,Json::array());
                if(port.at("active").get<bool>())std::erase_if(ports.get_ref<Json::array_t &>(),[&](const auto &v){return v==index;});else ports.push_back(index);
                edit("plugin.buses.set",{{"slot",slot},{port.at("direction")=="input"?"inputs":"outputs",ports}});pluginDetailsRevision.clear();}
            return true;
        }
        if(id==pluginNewInstrument) {
            const auto plugin=view->session.document.at("nativePlugins")[slot];
            if(!plugin.value("isInstrument",false))throw std::runtime_error("Select an instrument plugin");
            unsigned instrument=0;
            if(auto pending=pendingPluginTriggers.find(selectedPlugin);pending!=pendingPluginTriggers.end())
                for(const auto &i:view->session.document.at("instruments"))if(i.at("id")==pending->second)instrument=i.at("index").get<unsigned>();
            if(!instrument){
                const auto pluginName=plugin.at("name").get<std::string>();
                const auto created=edit("instrument.create",{{"empty",true},{"name",pluginName.size()<=192?pluginName+" trigger":"Plugin trigger"}});
                instrument=created.at("instrument").get<unsigned>();
                for(const auto &i:view->session.document.at("instruments"))if(i.at("index")==instrument)pendingPluginTriggers[selectedPlugin]=i.at("id").get<std::string>();
            }
            // Retain the created stable identity if assignment fails. Retrying
            // this action then assigns that trigger instead of appending another.
            edit("instrument.plugin.set",{{"instrument",instrument},{"plugin",selectedPlugin},{"channel",1}});
            pendingPluginTriggers.erase(selectedPlugin);pluginDetailsRevision.clear();
        } else if(id==pluginApply) {
            pluginAutoQueued=false;
            if(pluginDraft&&pluginProgramDraft)throw std::runtime_error("Load or discard the program selection first");
            if(!pluginParameterField.editable())throw std::runtime_error("This parameter is read-only or its metadata is unavailable");
            if(pluginDraft && (pluginDraftRevision!=view->session.revision || pluginDraftTarget!=selectedPlugin || pluginDraftParameter!=selectedParameter))throw ScreamSeq::Api::ApiError(-32001,"The song or target changed. Escape discards the parameter draft; re-enter it against the current state.");
            wchar_t text[66]{};GetWindowTextW(controls.at(pluginValue),text,66);wchar_t *end=nullptr;auto value=wcstod(text,&end);
            if(end==text || *end || !std::isfinite(value))throw std::runtime_error("Enter a finite parameter value");
            edit("plugin.parameters.set",{{"plugin",selectedPlugin},{"values",Json::array({{{"id",selectedParameter},{"value",value}}})}});pluginDraft=false;pluginDetailsRevision.clear();
        } else if(id==pluginEditor)edit("plugin.editor.open",{{"slot",slot}});
        else if(id==pluginBypass)edit("plugin.bypass",{{"slot",slot},{"bypass",!view->session.document.at("nativePlugins")[slot].value("bypass",false)}});
        else if(id==pluginRemove)edit("plugin.remove",{{"slot",slot}});
        else if(id==pluginUp || id==pluginDown)edit("plugin.move",{{"slot",slot},{"direction",id==pluginUp?-1:1}});
        else if(id==pluginAssign) {auto selected=SendMessageW(controls.at(pluginInstrument),CB_GETCURSEL,0,0);if(selected==CB_ERR)throw std::runtime_error("Choose a tracker instrument");auto instrument=SendMessageW(controls.at(pluginInstrument),CB_GETITEMDATA,selected,0);edit("plugin.assign",{{"slot",slot},{"instrument",instrument}});}
        return true;
    }
    void drawPluginEditor() {
        auto r=workspaceMainEditorRect();auto &s=*surface;s.fill(r.x,r.y,r.w,r.h,0x171f29);s.fill(r.x,r.y,r.w,32,0x232e3b);
        s.uiText(L"PLUGINS  /  "+std::to_wstring(listedPlugins.size())+L" in rack",r.x+12,r.y+8,210,0x72dcc6);
        if(listedPlugins.empty())s.uiText(L"Add an effect or instrument",r.x+18,r.y+76,194,0x8fa8ba);
        if(r.h>=(pluginDetailPage==1?193:161) && pluginDetailPage!=0)s.uiText(pluginDetailPage==4&&!pluginPresetStatus.empty()?pluginPresetStatus:pluginExtrasError?pluginDetailStatus:pluginDraft?L"Program selection / Load applies / Escape discards":pluginDetailPage==1?L"Factory settings replace the saved state in one Undo step":pluginDetailPage==2?L"Enable auxiliary ports here; routing remains a separate assignment":pluginDetailPage==3?L"Share one plugin across tracker instruments and MIDI channels":pluginDetailPage==4?L"Save and load sounds / routing and instrument assignments stay in the song":L"Reconnect a moved or Mac-origin VST3 to its matching Windows module",r.x+230,r.y+(pluginDetailPage==1?166:134),r.w-242,pluginDraft||pluginExtrasError?0xe2b86c:0x8fa8ba);
        else if(pluginDetailPage==0) {
            const bool instrument=selectedPluginSlot()>=0&&view->session.document.at("nativePlugins")[selectedPluginSlot()].value("isInstrument",false);
            const float statusY=instrument?230.0f:198.0f;
            if(r.h>=statusY+27)s.uiText(pluginDraft?L"Unsaved parameter / correct the value or press Escape to discard":pluginDetailStatus,r.x+230,r.y+statusY,r.w-242,pluginDraft?0xe2b86c:0x8fa8ba);
        }
    }
