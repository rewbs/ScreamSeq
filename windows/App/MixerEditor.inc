// Retained bus draft: UI values never mutate the song until a guarded Apply.
    bool mixerEditorOpen=false,mixerDirty=false,mixerPending=false,settingMixerFields=false,refreshingMixer=false;
    uint64_t mixerGeneration=0;
    std::string mixerDocument,mixerRevision,mixerTarget;
    Json mixerData=Json::object(),mixerDraft=Json::object();
    std::vector<Tracker::MixerMeter> mixerMeters;
    std::wstring mixerStatus=L"Enable routing to create a mixer from the song tracks";
    std::unique_ptr<ScreamSeq::SongRoutingWindow> songRoutingWindow;
    void openSongRouting(){
        if(!songRoutingWindow)songRoutingWindow=std::make_unique<ScreamSeq::SongRoutingWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{return std::pair(documentId,view->session.revision);},[this](const auto &node){
            if(busy)throw std::runtime_error("Document worker is busy");
            if(!node.graph.empty()){if(graphDirty||graphFieldDirty||curveDraft())throw std::runtime_error("Apply or Reload the existing graph draft first");graphID=node.graph;command(graphCommand);}
            else if(!node.plugin.empty()){if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or discard the captured plugin draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==node.plugin;}))throw std::runtime_error("This insert's plugin is unavailable; its route is retained");selectedPlugin=node.plugin;pluginDetailsTarget.clear();graphEditorOpen=mixerEditorOpen=false;command(pluginsCommand);armPluginTimer();}
            else if(!node.bus.empty()){if(mixerDirty||mixerPending)throw std::runtime_error("Apply or Reload the captured mixer draft first");mixerTarget=node.bus;command(mixerCommand);}
        });
        songRoutingWindow->show();
    }
    bool mixerEditorVisible() const {return !graphEditorVisible()&&mixerEditorOpen&&workspaceState.layout!="Pattern focus";}
    void installMixerControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{mixerCommand,L"Mixer"},{mixerEnable,L"Enable mixer"},{mixerAdd,L"Add group"},
            {mixerRemove,L"Remove"},{mixerReload,L"Reload"},{mixerApply,L"Apply"},{mixerMute,L"Mute off"},{mixerSolo,L"Solo off"},{mixerRouting,L"Routing…"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        controls[mixerList]=CreateWindowExW(0,L"LISTBOX",L"Mixer buses",WS_CHILD|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(mixerList)),GetModuleHandleW(nullptr),nullptr);
        controls[mixerOutput]=CreateWindowExW(0,L"COMBOBOX",L"Main output",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(mixerOutput)),GetModuleHandleW(nullptr),nullptr);
        for(int id=mixerName;id<=mixerTiming;++id){controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(controls.at(id),EM_SETLIMITTEXT,id==mixerName?256:32,0);}
        for(int id=mixerCommand;id<=mixerRouting;++id)if(!controls.at(id))throw std::runtime_error("Cannot create mixer controls");
    }
    int mixerSelected() const {if(!mixerData.contains("buses"))return -1;const auto &b=mixerData.at("buses");for(size_t i=0;i<b.size();++i)if(b[i].at("id")==mixerTarget)return int(i);return -1;}
    void loadMixerFields() {
        settingMixerFields=true;
        const auto selected=mixerSelected();mixerDraft=selected>=0?mixerData.at("buses")[size_t(selected)]:Json::object();
        const char *keys[]={"name","preGainDB","prePan","gainDB","pan","width","timingMS"};
        for(int id=mixerName;id<=mixerTiming;++id){std::wstring text;if(selected>=0){const auto &v=mixerDraft.at(keys[id-mixerName]);text=wide(v.is_string()?v.get<std::string>():v.dump());}SetWindowTextW(controls.at(id),text.c_str());}
        SendMessageW(controls.at(mixerOutput),CB_RESETCONTENT,0,0);SendMessageW(controls.at(mixerOutput),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Disconnected"));
        int output=0;if(mixerData.contains("buses"))for(size_t i=0;i<mixerData.at("buses").size();++i){const auto &b=mixerData.at("buses")[i];auto name=wide(b.at("name").get<std::string>());SendMessageW(controls.at(mixerOutput),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(selected>=0&&mixerDraft.at("output")==b.at("id"))output=int(i+1);}
        SendMessageW(controls.at(mixerOutput),CB_SETCURSEL,output,0);SendMessageW(controls.at(mixerList),LB_SETCURSEL,selected,0);
        settingMixerFields=false;mixerDirty=false;++mixerGeneration;
    }
    void refreshMixer() {
        if(refreshingMixer||busy||mixerDirty||mixerPending)return;
        refreshingMixer=true;
        try {
            const auto generation=mixerGeneration;auto next=documentOperation("mixer.get",Json::object());
            if(generation==mixerGeneration&&!mixerDirty){
                mixerData=std::move(next);mixerRevision=view->session.revision;mixerDocument=documentId;
                if(mixerSelected()<0)mixerTarget=mixerData.at("buses").empty()?"":mixerData.at("buses")[0].at("id").get<std::string>();
                auto top=SendMessageW(controls.at(mixerList),LB_GETTOPINDEX,0,0);SendMessageW(controls.at(mixerList),WM_SETREDRAW,FALSE,0);SendMessageW(controls.at(mixerList),LB_RESETCONTENT,0,0);
                for(const auto &b:mixerData.at("buses")){auto label=wide(b.at("name").get<std::string>()+(b.at("output")==""&&b.at("kind")!="master"?" / detached":""));SendMessageW(controls.at(mixerList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
                if(top!=LB_ERR)SendMessageW(controls.at(mixerList),LB_SETTOPINDEX,top,0);SendMessageW(controls.at(mixerList),WM_SETREDRAW,TRUE,0);
                loadMixerFields();mixerStatus=L"Gain, balance, width, mute and solo apply during playback";
            }
        }catch(const std::exception &e){mixerStatus=wide(e.what());}
        refreshingMixer=false;
    }
    void mixerFieldChanged(){if(!settingMixerFields&&mixerSelected()>=0){mixerDirty=true;++mixerGeneration;frameRequested=true;}}
    void mixerTimer() {
        if(!mixerEditorVisible()){KillTimer(window,3);return;}
        if(busy||refreshingMixer||mixerPending)return;
        if(!mixerDirty&&(mixerRevision!=view->session.revision||mixerDocument!=documentId)){refreshMixer();layoutControls();}
        if(device.running()&&preparedPlayback&&mixerRevision==view->session.revision){mixerMeters=preparedPlayback->chain().mixerMeters();frameRequested=true;}
        else if(!mixerMeters.empty()){mixerMeters.clear();frameRequested=true;}
    }
    template<class Place> void layoutMixerControls(Place place) {
        const bool show=mixerEditorVisible();const auto r=sampleEditorRect();const float x=r.x+202,w=std::max(90.0f,(r.w-220)/3);
        place(mixerList,r.x+8,r.y+38,180,r.h-70,show);
        place(mixerEnable,r.x+8,r.y+5,118,25,show);place(mixerAdd,x,r.y+5,94,25,show);place(mixerRemove,x+98,r.y+5,80,25,show);
        place(mixerRouting,x+184,r.y+5,96,25,show);
        place(mixerName,x,r.y+56,w*1.5f-5,25,show);place(mixerOutput,x+w*1.5f,r.y+56,w*1.5f-5,230,show);
        for(int i=0;i<6;++i)place(mixerPreGain+i,x+float(i%3)*w,r.y+105+float(i/3)*49,w-8,25,show);
        place(mixerMute,x,r.y+192,88,25,show);place(mixerSolo,x+92,r.y+192,88,25,show);
        place(mixerReload,r.x+r.w-164,r.y+192,76,25,show);place(mixerApply,r.x+r.w-84,r.y+192,76,25,show);
        const bool active=mixerData.value("active",false),selected=mixerSelected()>=0;
        SetWindowTextW(controls.at(mixerEnable),active?L"Mixer enabled":L"Enable mixer");EnableWindow(controls.at(mixerEnable),!active&&!mixerPending);
        EnableWindow(controls.at(mixerAdd),active&&!mixerDirty&&!mixerPending);EnableWindow(controls.at(mixerRemove),selected&&!mixerDirty&&!mixerPending&&(mixerDraft.value("kind","")=="group"||mixerDraft.value("kind","")=="return"));
        for(int id=mixerName;id<=mixerTiming;++id)EnableWindow(controls.at(id),selected);
        EnableWindow(controls.at(mixerOutput),selected&&mixerDraft.value("kind","")!="master");EnableWindow(controls.at(mixerApply),selected&&!mixerPending);
        for(int id:{mixerMute,mixerSolo}){EnableWindow(controls.at(id),selected);SetWindowTextW(controls.at(id),id==mixerMute?(mixerDraft.value("mute",false)?L"Mute on":L"Mute off"):(mixerDraft.value("solo",false)?L"Solo on":L"Solo off"));}
        if(show)SetTimer(window,3,50,nullptr);
    }
    bool mixerControlCommand(int id) {
        if(id<mixerCommand||id>mixerRouting)return false;
        if(busy||mixerPending)throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        workspaceState.focus="mixer";
        if(id==mixerRouting){openSongRouting();return true;}
        if(id==mixerCommand){graphEditorOpen=false;mixerEditorOpen=true;workspaceState.layout="Compose";refreshMixer();layoutControls();SetFocus(controls.at(mixerList));return true;}
        if(id==mixerReload){mixerDirty=false;++mixerGeneration;refreshMixer();return true;}
        if(id==mixerList){if(mixerDirty){SendMessageW(controls.at(mixerList),LB_SETCURSEL,mixerSelected(),0);throw std::runtime_error("Apply or reload the captured mixer draft before changing buses");}const auto index=SendMessageW(controls.at(mixerList),LB_GETCURSEL,0,0);if(index>=0&&size_t(index)<mixerData.at("buses").size()){mixerTarget=mixerData.at("buses")[size_t(index)].at("id").get<std::string>();loadMixerFields();}return true;}
        if(id==mixerOutput){mixerFieldChanged();return true;}
        if(id==mixerMute||id==mixerSolo){const char *key=id==mixerMute?"mute":"solo";mixerDraft[key]=!mixerDraft.value(key,false);mixerFieldChanged();return true;}
        if(mixerDocument!=documentId||mixerRevision!=view->session.revision)throw std::runtime_error("Song changed; mixer draft retained. Reload to use the current song");
        Json p={{"expectedRevision",mixerRevision}};std::string method;
        if(id==mixerEnable)method="mixer.enable";
        else if(id==mixerAdd){method="mixer.bus.add";p["kind"]="group";}
        else if(id==mixerRemove){method="mixer.bus.remove";p["bus"]=mixerTarget;}
        else if(id==mixerApply){method="mixer.bus.set";p["bus"]=mixerTarget;p["name"]=utf8Path(effectField(mixerName));
            const char *keys[]={"preGainDB","prePan","gainDB","pan","width","timingMS"};for(int i=0;i<6;++i){const auto raw=effectField(mixerPreGain+i);size_t end=0;const auto value=std::stod(raw,&end);if(end!=raw.size()||!std::isfinite(value))throw std::runtime_error("Enter a finite mixer value");p[keys[i]]=value;}
            p["mute"]=mixerDraft.value("mute",false);p["solo"]=mixerDraft.value("solo",false);
            const auto output=SendMessageW(controls.at(mixerOutput),CB_GETCURSEL,0,0);p["output"]=output<=0?Json():mixerData.at("buses").at(size_t(output-1)).at("id");
        }else return true;
        const auto generation=mixerGeneration;mixerPending=true;
        try{const auto result=documentOperation(method,p);mixerPending=false;if(generation==mixerGeneration){mixerDirty=false;if(id==mixerAdd)mixerTarget=result.at("bus").get<std::string>();refreshMixer();}mixerStatus=L"Mixer edit applied / document Undo available";}
        catch(...){mixerPending=false;mixerStatus=L"Edit rejected / captured values retained";throw;}
        return true;
    }
    bool mixerKey(WPARAM key){const auto id=GetFocus()==window?0:GetDlgCtrlID(GetFocus());if(id<mixerList||id>mixerTiming)return false;if(key==VK_ESCAPE){command(mixerReload);return true;}if(key==VK_RETURN&&id>=mixerName){command(mixerApply);return true;}return false;}
    void drawMixerEditor() {
        auto &s=*surface;const auto r=sampleEditorRect();const float x=r.x+202,w=std::max(90.0f,(r.w-220)/3);s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x17202a);
        s.uiText(L"Bus name",x,r.y+38,w*1.5f,0x8390a2);s.uiText(L"Main output",x+w*1.5f,r.y+38,w*1.5f,0x8390a2);
        const wchar_t *labels[]={L"Pre gain / dB",L"Pre balance / −1…1",L"Gain / dB",L"Balance / −1…1",L"Width / 0…2",L"Timing / ms"};
        for(int i=0;i<6;++i)s.uiText(labels[i],x+float(i%3)*w,r.y+87+float(i/3)*49,w-8,0x8390a2);
        const auto selected=mixerSelected();if(selected>=0&&size_t(selected)<mixerMeters.size()){const auto &meter=mixerMeters[size_t(selected)];for(int i=0;i<2;++i){const float value=i?meter.right:meter.left;s.fill(r.x+8,r.y+r.h-27+i*5,180,3,0x283543);s.fill(r.x+8,r.y+r.h-27+i*5,180*std::clamp(value,0.0f,1.0f),3,value>1?0xe87c77:0x6edac5);}}
        const bool stale=mixerDocument!=documentId||mixerRevision!=view->session.revision;
        s.uiText(stale&&mixerDirty?L"Song changed / draft retained — Reload before applying":mixerDirty?L"Captured bus draft / Apply to commit":mixerStatus,x,r.y+r.h-27,r.w-210,stale&&mixerDirty?0xe7bb79:0x8390a2);s.unclip();s.outline(r.x,r.y,r.w,r.h,workspaceState.focus=="mixer"?0x6edac5:0x303b49);
    }
