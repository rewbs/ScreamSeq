// Retained Windows rack. Identities and draft revisions survive list refreshes;
// all state mutation goes through the same worker transactions as the API.
    Json listedPlugins=Json::array(),pluginCatalog=Json::array(),pluginParameters=Json::array();
    std::string selectedPlugin,pluginDocument,pluginDetailsRevision,pluginDetailsTarget;
    std::string pluginDraftRevision,pluginDraftTarget;
    Json pluginPrograms=Json::array(),pluginPorts=Json::array();
    std::string selectedProgram,programCatalogRevision;
    int pluginDetailPage=0,selectedPort=-1;
    bool pluginProgramDraft=false,pluginExtrasError=false;
    std::map<std::string,std::string> pendingPluginTriggers; // Plugin ID -> created instrument ID awaiting assignment.
    uint32_t selectedParameter=0,pluginDraftParameter=0;
    bool settingPluginFields=false,pluginDraft=false,refreshingPlugins=false,pluginLibraryLoaded=false;
    std::wstring pluginDetailStatus=L"Select a plugin to edit its parameters";
    bool pluginEditorVisible() const {return workspaceState.layout!="Pattern focus" && !sampleEditorVisible();}
    int selectedPluginSlot() const {
        const auto &rack=view->session.document.at("nativePlugins");
        for(size_t i=0;i<rack.size();++i) if(rack[i].at("instanceID")==selectedPlugin) return int(i);
        return -1;
    }
    void installPluginControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{
            {pluginAdd,L"Add"},{pluginRescan,L"Rescan"},{pluginEditor,L"Editor"},{pluginBypass,L"Bypass"},
            {pluginRemove,L"Remove"},{pluginUp,L"Up"},{pluginDown,L"Down"},{pluginUndo,L"Undo FX"},{pluginRedo,L"Redo FX"},
            {pluginApply,L"Apply"},{pluginAssign,L"Assign"},{pluginsCommand,L"Plugins"},{pluginNewInstrument,L"New trigger"},
            {pluginLoadProgram,L"Load program"},{pluginTogglePort,L"Enable port"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{pluginLibrary,pluginParameter,pluginInstrument,pluginPage,pluginProgram,pluginPort})
            controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        controls[pluginList]=CreateWindowExW(0,L"LISTBOX",L"Plugin rack",WS_CHILD|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(pluginList)),GetModuleHandleW(nullptr),nullptr);
        controls[pluginValue]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(pluginValue)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(controls.at(pluginValue),EM_SETLIMITTEXT,64,0);
        for(auto label:{L"Parameters",L"Factory programs",L"Audio ports"})SendMessageW(controls.at(pluginPage),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(controls.at(pluginPage),CB_SETCURSEL,0,0);
        for(int id=pluginList;id<=pluginTogglePort;++id) if(!controls.at(id)) throw std::runtime_error("Cannot create native plugin controls");
    }
    void syncPluginRack() {
        if(!controls.contains(pluginList)) return;
        if(pluginDocument!=documentId) {pluginDocument=documentId;selectedPlugin.clear();pluginDraft=false;pluginDetailsRevision.clear();pluginDetailsTarget.clear();listedPlugins=nullptr;pendingPluginTriggers.clear();}
        const auto &rack=view->session.document.at("nativePlugins");
        if(listedPlugins!=rack) {
            const auto top=SendMessageW(controls.at(pluginList),LB_GETTOPINDEX,0,0);
            listedPlugins=rack;SendMessageW(controls.at(pluginList),WM_SETREDRAW,FALSE,0);SendMessageW(controls.at(pluginList),LB_RESETCONTENT,0,0);
            if(selectedPluginSlot()<0 && !pluginDraft) selectedPlugin=rack.empty()?"":rack[0].at("instanceID").get<std::string>();
            for(size_t i=0;i<rack.size();++i) {
                auto label=std::to_wstring(i+1)+L"  "+wide(rack[i].at("name").get<std::string>())+(rack[i].value("bypass",false)?L"  [off]":L"");
                SendMessageW(controls.at(pluginList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            }
            if(top!=LB_ERR)SendMessageW(controls.at(pluginList),LB_SETTOPINDEX,top,0);
            SendMessageW(controls.at(pluginList),WM_SETREDRAW,TRUE,0);
        }
        SendMessageW(controls.at(pluginList),LB_SETCURSEL,selectedPluginSlot(),0);
        armPluginTimer();
    }
    void armPluginTimer() {
        const bool editors=view->session.document.value("openPluginEditors",0u)!=0;
        if(!refreshingPlugins && (editors || (!pluginDraft && pluginEditorVisible() && (pluginDetailsRevision!=view->session.revision || pluginDetailsTarget!=selectedPlugin || !pluginLibraryLoaded))))
            SetTimer(window,1,editors?16:100,nullptr);
    }
    void setPluginParameterField() {
        if(pluginDraft) return;
        settingPluginFields=true;std::wstring value;
        for(const auto &p:pluginParameters) if(p.at("id")==selectedParameter) {std::wostringstream stream;stream<<std::setprecision(9)<<p.at("value").get<double>();value=stream.str();break;}
        SetWindowTextW(controls.at(pluginValue),value.c_str());settingPluginFields=false;
    }
    void refreshPluginLibrary(bool rescan=false) {
        auto catalog=documentOperation("plugin.discover",{{"rescan",rescan}});
        auto old=SendMessageW(controls.at(pluginLibrary),CB_GETCURSEL,0,0);
        Json selected=old>=0 && size_t(old)<pluginCatalog.size()?pluginCatalog[size_t(old)]:Json();
        pluginCatalog=std::move(catalog);SendMessageW(controls.at(pluginLibrary),CB_RESETCONTENT,0,0);int selection=0;
        for(size_t i=0;i<pluginCatalog.size();++i) {const auto &d=pluginCatalog[i];auto label=wide(d.at("name").get<std::string>()+" / "+d.at("format").get<std::string>());SendMessageW(controls.at(pluginLibrary),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(d==selected)selection=int(i);}
        SendMessageW(controls.at(pluginLibrary),CB_SETCURSEL,pluginCatalog.empty()?-1:selection,0);pluginLibraryLoaded=true;
    }
    void refreshPluginDetails() {
        if(pluginDraft) return;
        const auto slot=selectedPluginSlot();pluginParameters=Json::array();pluginDetailStatus=L"Select a plugin to edit its parameters";
        if(slot>=0) try {
            pluginParameters=documentOperation("plugin.parameters.get",{{"slot",slot}});
            pluginDetailStatus=wide(view->session.document.at("nativePlugins")[slot].at("name").get<std::string>());
        } catch(const std::exception &e) {pluginDetailStatus=wide(e.what());}
        SendMessageW(controls.at(pluginParameter),CB_RESETCONTENT,0,0);int selected=-1;
        for(size_t i=0;i<pluginParameters.size();++i) {const auto &p=pluginParameters[i];auto label=wide(p.at("name").get<std::string>()+(p.at("unitLabel").get<std::string>().empty()?"":" / "+p.at("unitLabel").get<std::string>()));SendMessageW(controls.at(pluginParameter),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(p.at("id")==selectedParameter)selected=int(i);}
        if(selected<0 && !pluginParameters.empty()) {selected=0;selectedParameter=pluginParameters[0].at("id").get<uint32_t>();}
        SendMessageW(controls.at(pluginParameter),CB_SETCURSEL,selected,0);setPluginParameterField();
        SendMessageW(controls.at(pluginInstrument),CB_RESETCONTENT,0,0);SendMessageW(controls.at(pluginInstrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Unassigned"));SendMessageW(controls.at(pluginInstrument),CB_SETITEMDATA,0,0);
        unsigned assigned=slot<0?0:view->session.document.at("nativePlugins")[slot].value("instrument",0u);int instrumentSelection=0;
        for(const auto &i:view->session.document.at("instruments")) {auto n=i.at("index").get<unsigned>();auto label=std::to_wstring(n)+L"  "+wide(i.at("name").get<std::string>());auto item=SendMessageW(controls.at(pluginInstrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(controls.at(pluginInstrument),CB_SETITEMDATA,item,n);if(n==assigned)instrumentSelection=int(item);}
        SendMessageW(controls.at(pluginInstrument),CB_SETCURSEL,instrumentSelection,0);
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
        SendMessageW(controls.at(pluginProgram),CB_RESETCONTENT,0,0);int programSelection=-1;
        for(size_t i=0;i<pluginPrograms.size();++i){const auto &p=pluginPrograms[i];auto label=wide(p.at("group").get<std::string>()+" / "+p.at("name").get<std::string>()+(p.at("loadable").get<bool>()?"":" [unavailable]"));
            SendMessageW(controls.at(pluginProgram),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(p.at("id")==selectedProgram)programSelection=int(i);}
        if(programSelection<0&&!pluginPrograms.empty()){programSelection=0;selectedProgram=pluginPrograms[0].at("id").get<std::string>();}
        if(pluginPrograms.empty())SendMessageW(controls.at(pluginProgram),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No standard factory programs"));
        SendMessageW(controls.at(pluginProgram),CB_SETCURSEL,programSelection<0?0:programSelection,0);
        SendMessageW(controls.at(pluginPort),CB_RESETCONTENT,0,0);selectedPort=-1;displayedPluginPorts=pluginPorts;
        for(size_t i=0;i<pluginPorts.size();++i){const auto &p=pluginPorts[i];const auto index=p.at("index").get<unsigned>();
            auto label=wide(p.at("direction").get<std::string>()=="input"?"Input ":"Output ")+std::to_wstring(index)+L"  "+wide(p.at("name").get<std::string>())+L" / "+std::to_wstring(p.at("channels").get<unsigned>())+L" ch / "+(index==0?L"Main":!p.at("supported").get<bool>()?L"Unsupported":p.at("active").get<bool>()?L"On":L"Off");
            SendMessageW(controls.at(pluginPort),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(!oldPort.is_null()&&oldPort.at("index")==p.at("index")&&oldPort.at("direction")==p.at("direction"))selectedPort=int(i);}
        if(selectedPort<0&&!pluginPorts.empty())selectedPort=0;
        if(pluginPorts.empty())SendMessageW(controls.at(pluginPort),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No audio ports"));
        SendMessageW(controls.at(pluginPort),CB_SETCURSEL,selectedPort<0?0:selectedPort,0);
    }
    Json displayedPluginPorts=Json::array();
    void pluginTimer() {
        if(busy || refreshingPlugins) return;
        KillTimer(window,1);refreshingPlugins=true;
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
    void pluginFieldChanged() {
        if(settingPluginFields) return;
        if(!pluginDraft) {pluginDraft=true;pluginProgramDraft=false;pluginDraftRevision=view->session.revision;pluginDraftTarget=selectedPlugin;pluginDraftParameter=selectedParameter;}
        frameRequested=true;
    }
    template<class Place> void layoutPluginControls(const Place &place) {
        syncPluginRack();const auto r=sampleEditorRect();const bool show=pluginEditorVisible();const auto x=r.x+230,y=r.y;
        place(pluginsCommand,480,52,80,26);place(pluginUndo,r.x+r.w-160,y+4,74,24,show);place(pluginRedo,r.x+r.w-82,y+4,74,24,show);
        place(pluginPage,x,y+4,std::min(180.0f,r.w-406),230,show);
        place(pluginLibrary,r.x+8,y+35,r.w-168,250,show);place(pluginAdd,r.x+r.w-154,y+35,64,24,show);place(pluginRescan,r.x+r.w-86,y+35,78,24,show);
        place(pluginList,r.x+8,y+67,210,std::max(24.0f,r.h-75),show);
        float bx=x;for(auto [id,w]:std::initializer_list<std::pair<int,float>>{{pluginEditor,66.0f},{pluginBypass,72.0f},{pluginRemove,68.0f},{pluginUp,44.0f},{pluginDown,52.0f}}) {place(id,bx,y+67,w,24,show);bx+=w+4;}
        const float parameterWidth=std::max(80.0f,r.w-406);
        place(pluginParameter,x,y+99,parameterWidth,240,show && pluginDetailPage==0 && r.h>=132);place(pluginValue,x+parameterWidth+6,y+99,86,24,show && pluginDetailPage==0 && r.h>=132);place(pluginApply,x+parameterWidth+98,y+99,68,24,show && pluginDetailPage==0 && r.h>=132);
        place(pluginProgram,x,y+99,r.w-360,300,show && pluginDetailPage==1 && r.h>=132);place(pluginLoadProgram,r.x+r.w-120,y+99,112,24,show && pluginDetailPage==1 && r.h>=132);
        place(pluginPort,x,y+99,r.w-360,300,show && pluginDetailPage==2 && r.h>=132);place(pluginTogglePort,r.x+r.w-120,y+99,112,24,show && pluginDetailPage==2 && r.h>=132);
        const auto slot=selectedPluginSlot();const bool selected=slot>=0;const auto plugin=selected?view->session.document.at("nativePlugins")[slot]:Json::object();
        const bool instrument=selected && plugin.value("isInstrument",false);
        place(pluginInstrument,x,y+131,r.w-424,220,show && instrument && pluginDetailPage==0 && r.h>=164);place(pluginNewInstrument,r.x+r.w-188,y+131,108,24,show && instrument && pluginDetailPage==0 && r.h>=164);place(pluginAssign,r.x+r.w-76,y+131,68,24,show && instrument && pluginDetailPage==0 && r.h>=164);
        EnableWindow(controls.at(pluginNewInstrument),instrument);SetWindowTextW(controls.at(pluginNewInstrument),pendingPluginTriggers.contains(selectedPlugin)?L"Retry assign":L"New trigger");
        for(int id:{pluginEditor,pluginBypass,pluginRemove,pluginUp,pluginDown,pluginAssign}) EnableWindow(controls.at(id),selected);
        EnableWindow(controls.at(pluginEditor),selected && plugin.value("format","")=="VST3");
        EnableWindow(controls.at(pluginUp),selected && slot>0);EnableWindow(controls.at(pluginDown),selected && size_t(slot+1)<listedPlugins.size());
        SetWindowTextW(controls.at(pluginBypass),selected && plugin.value("bypass",false)?L"Enable":L"Bypass");
        bool writable=false;for(const auto &p:pluginParameters)if(p.at("id")==selectedParameter)writable=p.value("writable",false);
        EnableWindow(controls.at(pluginApply),selected && writable);EnableWindow(controls.at(pluginValue),selected && writable);
        EnableWindow(controls.at(pluginUndo),view->session.document.value("canUndoPlugins",false));EnableWindow(controls.at(pluginRedo),view->session.document.value("canRedoPlugins",false));
        EnableWindow(controls.at(pluginAdd),!pluginCatalog.empty());
        bool loadable=false;for(const auto &p:pluginPrograms)if(p.at("id")==selectedProgram)loadable=p.at("loadable").get<bool>();
        EnableWindow(controls.at(pluginProgram),!pluginPrograms.empty());EnableWindow(controls.at(pluginLoadProgram),selected&&loadable);
        const auto port=selectedPort>=0 && size_t(selectedPort)<pluginPorts.size()?pluginPorts[size_t(selectedPort)]:Json();
        EnableWindow(controls.at(pluginPort),!pluginPorts.empty());EnableWindow(controls.at(pluginTogglePort),!port.is_null()&&port.at("index")!=0&&port.at("supported").get<bool>());
        SetWindowTextW(controls.at(pluginTogglePort),!port.is_null()&&port.at("active").get<bool>()?L"Disable port":L"Enable port");
    }
    bool pluginCommand(int id) {
        if(id<pluginList || id>pluginTogglePort)return false;
        if(busy)throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        workspaceState.focus="plugins";
        if(id==pluginsCommand) {workspaceState.layout="Compose";workspaceState.active="notes";SetFocus(controls.at(pluginList));return true;}
        if(id==pluginPage){if(pluginDraft){SendMessageW(controls.at(pluginPage),CB_SETCURSEL,pluginDetailPage,0);throw std::runtime_error("Apply the draft or press Escape before changing the detail page");}
            const auto page=SendMessageW(controls.at(pluginPage),CB_GETCURSEL,0,0);if(page>=0&&page<3){pluginDetailPage=int(page);pluginDetailsTarget.clear();}SetFocus(controls.at(pluginPage));return true;}
        if(id==pluginList) {
            if(pluginDraft) {SendMessageW(controls.at(pluginList),LB_SETCURSEL,selectedPluginSlot(),0);throw std::runtime_error("Apply the parameter draft or press Escape before selecting another plugin");}
            const auto slot=SendMessageW(controls.at(pluginList),LB_GETCURSEL,0,0);if(slot!=LB_ERR)selectedPlugin=listedPlugins.at(size_t(slot)).at("instanceID").get<std::string>();pluginDetailsTarget.clear();return true;
        }
        if(id==pluginParameter) {
            if(pluginDraft) {for(size_t i=0;i<pluginParameters.size();++i)if(pluginParameters[i].at("id")==selectedParameter)SendMessageW(controls.at(pluginParameter),CB_SETCURSEL,i,0);throw std::runtime_error("Apply the parameter draft or press Escape before choosing another parameter");}
            auto selected=SendMessageW(controls.at(pluginParameter),CB_GETCURSEL,0,0);if(selected!=CB_ERR)selectedParameter=pluginParameters.at(size_t(selected)).at("id").get<uint32_t>();setPluginParameterField();return true;
        }
        if(id==pluginLibrary || id==pluginInstrument)return true;
        if(id==pluginProgram){const auto chosen=SendMessageW(controls.at(pluginProgram),CB_GETCURSEL,0,0);if(chosen>=0 && size_t(chosen)<pluginPrograms.size()){
            selectedProgram=pluginPrograms[size_t(chosen)].at("id").get<std::string>();if(!pluginDraft){pluginDraft=true;pluginProgramDraft=true;pluginDraftRevision=pluginDetailsRevision;pluginDraftTarget=pluginDetailsTarget;}}return true;}
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
            if(pluginDraft&&pluginProgramDraft)throw std::runtime_error("Load or discard the program selection first");
            if(pluginDraft && (pluginDraftRevision!=view->session.revision || pluginDraftTarget!=selectedPlugin || pluginDraftParameter!=selectedParameter))throw ScreamSeq::Api::ApiError(-32001,"The song or target changed. Escape discards the parameter draft; re-enter it against the current state.");
            wchar_t text[66]{};GetWindowTextW(controls.at(pluginValue),text,66);wchar_t *end=nullptr;auto value=wcstod(text,&end);
            if(end==text || *end || !std::isfinite(value))throw std::runtime_error("Enter a finite parameter value");
            edit("plugin.parameters.set",{{"slot",slot},{"values",Json::array({{{"id",selectedParameter},{"value",value}}})}});pluginDraft=false;pluginDetailsRevision.clear();
        } else if(id==pluginEditor)edit("plugin.editor.open",{{"slot",slot}});
        else if(id==pluginBypass)edit("plugin.bypass",{{"slot",slot},{"bypass",!view->session.document.at("nativePlugins")[slot].value("bypass",false)}});
        else if(id==pluginRemove)edit("plugin.remove",{{"slot",slot}});
        else if(id==pluginUp || id==pluginDown)edit("plugin.move",{{"slot",slot},{"direction",id==pluginUp?-1:1}});
        else if(id==pluginAssign) {auto selected=SendMessageW(controls.at(pluginInstrument),CB_GETCURSEL,0,0);if(selected==CB_ERR)throw std::runtime_error("Choose a tracker instrument");auto instrument=SendMessageW(controls.at(pluginInstrument),CB_GETITEMDATA,selected,0);edit("plugin.assign",{{"slot",slot},{"instrument",instrument}});}
        return true;
    }
    void drawPluginEditor() {
        auto r=sampleEditorRect();auto &s=*surface;s.fill(r.x,r.y,r.w,r.h,0x171f29);s.fill(r.x,r.y,r.w,32,0x232e3b);
        s.uiText(L"PLUGINS  /  "+std::to_wstring(listedPlugins.size())+L" in rack",r.x+12,r.y+8,210,0x72dcc6);
        if(listedPlugins.empty())s.uiText(L"Add an effect or instrument",r.x+18,r.y+76,194,0x8fa8ba);
        if(r.h>=161 && pluginDetailPage!=0)s.uiText(pluginExtrasError?pluginDetailStatus:pluginDraft?L"Program selection / Load applies / Escape discards":pluginDetailPage==1?L"Factory settings replace the saved state in one plugin Undo step":L"Enable auxiliary ports here; routing remains a separate assignment",r.x+230,r.y+134,r.w-242,pluginDraft||pluginExtrasError?0xe2b86c:0x8fa8ba);
        else if(r.h>=161 && (selectedPluginSlot()<0 || !view->session.document.at("nativePlugins")[selectedPluginSlot()].value("isInstrument",false)))
            s.uiText(pluginDraft?L"Parameter draft / Enter applies / Escape discards":pluginDetailStatus,r.x+230,r.y+134,r.w-242,pluginDraft?0xe2b86c:0x8fa8ba);
    }
