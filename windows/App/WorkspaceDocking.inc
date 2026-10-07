    // Retained native editors share a right-hand dock. At narrow widths its
    // tabs use the workspace body, leaving every editor usable at normal scale.
    struct WorkspaceEditorPanel {
        std::string location="hide", preferred="float", document, patternID;
        Json origin=Json::object();
        bool pinned=true, configured=false;
    };
    std::array<WorkspaceEditorPanel,2> workspaceEditors;
    std::string activeWorkspaceEditor="automation";
    bool dockTrackerSelected=false, changingEditorPlacement=false, followingEditors=false;
    struct EditorPlacementGuard {
        bool &flag;bool previous;
        explicit EditorPlacementGuard(bool &value):flag(value),previous(value){flag=true;}
        ~EditorPlacementGuard(){flag=previous;}
    };
    static bool isWorkspaceEditor(const std::string &id){return id=="automation"||id=="instruments";}
    static size_t workspaceEditorIndex(const std::string &id){
        if(id=="automation")return 0;if(id=="instruments")return 1;
        throw ScreamSeq::Api::ApiError(-32602,"Choose automation or instruments");
    }
    ScreamSeq::NativeToolWindow *workspaceEditorWindow(const std::string &id)const {
        return id=="automation"?static_cast<ScreamSeq::NativeToolWindow *>(parameterAutomationWindow.get()):static_cast<ScreamSeq::NativeToolWindow *>(instrumentEnvelopeWindow.get());
    }
    bool hasWorkspaceDock()const {return workspaceState.layout!="Pattern focus"&&std::any_of(workspaceEditors.begin(),workspaceEditors.end(),[](const auto &p){return p.location=="right";});}
    float workspaceClientWidth()const {RECT r{};GetClientRect(window,&r);return r.right*96.0f/GetDpiForWindow(window);}
    bool sideWorkspaceDock()const {return hasWorkspaceDock()&&workspaceClientWidth()>=1424;}
    bool trackerWorkspaceVisible()const {return !hasWorkspaceDock()||sideWorkspaceDock()||dockTrackerSelected;}
    bool workspaceFocusVisible(HWND target)const {
        return target&&IsWindow(target)&&IsWindowVisible(target)&&IsWindowEnabled(target)&&(target!=window||trackerWorkspaceVisible());
    }
    void focusVisibleWorkspaceCanvas() {
        if(!trackerWorkspaceVisible())if(auto *tool=workspaceEditorWindow(activeWorkspaceEditor);tool&&tool->visible()) {
            workspaceState.focus=activeWorkspaceEditor;SetFocus(tool->window());return;
        }
        workspaceState.focus="pattern";SetFocus(window);
    }
    float workspaceContentTop()const {return hasWorkspaceDock()?122.0f:88.0f;}
    ScreamSeq::WorkspaceRect workspaceEditorRect()const {
        RECT r{};GetClientRect(window,&r);const auto scale=96.0f/GetDpiForWindow(window),w=r.right*scale,h=r.bottom*scale;
        return {sideWorkspaceDock()?w-468:170,122,sideWorkspaceDock()?460:w-178,std::max(1.0f,h-166)};
    }
    void captureWorkspaceEditorOrigin(const std::string &id) {
        auto &p=workspaceEditors[workspaceEditorIndex(id)];if(!p.origin.empty())return;
        p.origin=position();p.document=documentId;
        for(const auto &pattern:view->session.document.at("patterns"))if(pattern.at("index")==patternIndex){p.patternID=pattern.at("id");break;}
    }
    Json workspaceEditorReturnPoint(const std::string &id)const {
        const auto &p=workspaceEditors[workspaceEditorIndex(id)];
        if(p.document!=documentId||p.origin.empty())throw ScreamSeq::Api::ApiError(-32602,"This editor's opening document is no longer active");
        auto target=p.origin;bool found=false;
        for(const auto &pattern:view->session.document.at("patterns"))if(pattern.at("id")==p.patternID){target["pattern"]=pattern.at("index");found=true;break;}
        if(!found)throw ScreamSeq::Api::ApiError(-32602,"This editor's opening pattern no longer exists");
        validatePosition(target);target["following"]=false;return target;
    }
    bool editorWorkspaceKey(const std::string &id,WPARAM key,bool repeat) {
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        if(ctrl&&!alt&&key=='K'){if(!repeat)command(paletteCommand);return true;}
        if(ctrl&&alt&&key=='W'){if(!repeat)command(layoutsCommand);return true;}
        if(ctrl&&alt&&key=='D'){if(!repeat)workspaceEditorRequest({{"panel",id},{"placement",workspaceEditorWindow(id)->docked()?"float":"right"},{"focus",true}});return true;}
        if(ctrl&&alt&&key=='L'){if(!repeat){const auto focus=GetFocus();command(liveKeysCommand);if(workspaceEditorWindow(id)->owns(focus)&&workspaceFocusVisible(focus))SetFocus(focus);}return true;}
        if(!ctrl&&!alt&&key==VK_SPACE&&GetFocus()==workspaceEditorWindow(id)->window()){if(!repeat){if(device.running()&&!auditionOnly)stop();else play();}return true;}
        return false;
    }
    void configureWorkspaceEditor(const std::string &id) {
        auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);if(!tool||p.configured)return;
        p.configured=true;
        tool->workspaceKeys([this,id](WPARAM key,bool repeat){return editorWorkspaceKey(id,key,repeat);});
        tool->placementChanged([this,id]{
            if(changingEditorPlacement)return;
            auto &state=workspaceEditors[workspaceEditorIndex(id)];auto *editor=workspaceEditorWindow(id);
            if(editor->visible()){state.location=editor->docked()?"right":"float";state.preferred=state.location;captureWorkspaceEditorOrigin(id);}
            else {state.location="hide";if(workspaceState.focus==id)workspaceState.focus="pattern";}
            layoutControls();
        });
    }
    void finishWorkspaceEditorOpen(const std::string &id) {
        auto &p=workspaceEditors[workspaceEditorIndex(id)];captureWorkspaceEditorOrigin(id);
        if(p.preferred=="right") {
            EditorPlacementGuard guard(changingEditorPlacement);workspaceEditorWindow(id)->dock(window);
            p.location="right";activeWorkspaceEditor=id;dockTrackerSelected=false;
            if(workspaceState.layout=="Pattern focus")workspaceState.layout="Compose";
        } else p.location="float";
        layoutControls();
    }
    void ensureWorkspaceEditor(const std::string &id) {
        if(workspaceEditorWindow(id))return;
        if(id=="automation")openParameterAutomation();else openInstrumentEnvelope();
    }
    bool followWorkspaceEditor(const std::string &id) {
        return id=="automation"?parameterAutomationWindow->followCursor():instrumentEnvelopeWindow->followCursor();
    }
    void followWorkspaceEditors() {
        if(followingEditors||changingEditorPlacement||busy)return;
        EditorPlacementGuard guard(followingEditors);
        for(const auto *id:{"automation","instruments"}) {
            const auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);
            if(p.pinned||!tool||!tool->visible()||tool->owns(GetFocus()))continue;
            try{followWorkspaceEditor(id);}catch(const std::exception &e){status=wide(e.what());}
        }
    }
    void workspaceEditorRequest(const Json &p) {
        const auto id=p.at("panel").get<std::string>();auto &state=workspaceEditors[workspaceEditorIndex(id)];
        for(auto i=p.begin();i!=p.end();++i)if(i.key()!="panel"&&i.key()!="placement"&&i.key()!="pinned"&&i.key()!="focus"&&i.key()!="follow"&&i.key()!="return")throw ScreamSeq::Api::ApiError(-32602,"Unknown workspace parameter");
        for(const auto *key:{"pinned","focus","follow","return"})if(p.contains(key)&&!p[key].is_boolean())throw ScreamSeq::Api::ApiError(-32602,"Panel flags must be boolean");
        if(p.contains("placement")&&(!p["placement"].is_string()||(p["placement"]!="right"&&p["placement"]!="float"&&p["placement"]!="hide")))throw ScreamSeq::Api::ApiError(-32602,"Choose right, float or hide for this editor");
        Json returnPoint;if(p.value("return",false))returnPoint=workspaceEditorReturnPoint(id);
        if(busy)throw ScreamSeq::Api::ApiError(-32002,"Wait for the current document operation before moving an editor");
        const auto previousFocus=GetFocus();
        const bool retainTrackerFocus=!p.value("focus",false)&&previousFocus==window&&trackerWorkspaceVisible();
        const auto placement=p.value("placement",state.location=="hide"?state.preferred:state.location);
        // Hiding an unopened editor is a pure presentation operation.
        if((p.contains("placement")&&placement!="hide")||p.value("focus",false)||p.value("follow",false))ensureWorkspaceEditor(id);
        EditorPlacementGuard guard(changingEditorPlacement);auto *tool=workspaceEditorWindow(id);
        if(p.contains("pinned"))state.pinned=p["pinned"].get<bool>();
        if(p.value("follow",false))state.pinned=false;
        if(tool&&(p.value("follow",false)||(p.contains("pinned")&&!state.pinned)))followWorkspaceEditor(id);
        if(p.contains("placement")) {
            state.location=placement;
            if(placement!="hide")state.preferred=placement;
            if(tool){if(placement=="right")tool->dock(window);else if(placement=="float")tool->floatWindow();else tool->hide();}
        }
        if(tool&&((p.contains("placement")&&placement=="right")||p.value("focus",false))) {
            if(workspaceState.layout=="Pattern focus")workspaceState.layout="Compose";
            state.location=placement=="hide"?state.preferred:placement;
            if(state.location=="right"){tool->dock(window);activeWorkspaceEditor=id;dockTrackerSelected=retainTrackerFocus&&!sideWorkspaceDock();}
            else tool->floatWindow();
        }
        // Returning must expose the tracker even if navigate() is a no-op at
        // the opening position and therefore does not perform another layout.
        if(p.value("return",false))dockTrackerSelected=true;
        layoutControls();
        if(state.location=="hide"&&workspaceState.focus==id)workspaceState.focus="pattern";
        if(p.value("return",false)){workspaceState.focus="pattern";navigate(returnPoint);SetFocus(window);}
        else if(p.value("focus",false)&&tool){tool->show();workspaceState.focus=id;SetFocus(tool->window());}
        else if(workspaceFocusVisible(previousFocus))SetFocus(previousFocus);
        else if(!workspaceFocusVisible(GetFocus()))focusVisibleWorkspaceCanvas();
        ensureCursorVisible();
    }
    Json workspaceDockSnapshot()const {
        auto r=workspaceEditorRect();return {{"mode",!hasWorkspaceDock()?"none":sideWorkspaceDock()?"side":"tabs"},{"active",activeWorkspaceEditor},{"trackerVisible",trackerWorkspaceVisible()},{"rect",{{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}}}};
    }
    Json workspaceDockConfiguration()const {
        return {{"locations",Json::array({workspaceEditors[0].location,workspaceEditors[1].location})},{"active",activeWorkspaceEditor},{"tracker",dockTrackerSelected}};
    }
    void validateWorkspaceDockConfiguration(const Json &v)const {
        auto need=[](bool condition){if(!condition)throw ScreamSeq::Api::ApiError(-32602,"Invalid saved editor placement");};
        need(v.is_object()&&v.size()==3&&v.contains("locations")&&v["locations"].is_array()&&v["locations"].size()==2);
        for(const auto &p:v["locations"])need(p.is_string()&&(p=="right"||p=="float"||p=="hide"));
        need(v.contains("active")&&v["active"].is_string()&&isWorkspaceEditor(v["active"].get<std::string>()));
        need(v.contains("tracker")&&v["tracker"].is_boolean());
    }
    void prepareWorkspaceDockConfiguration(const Json &v) {
        // Create unopened editors before changing existing presentation state.
        for(const auto *id:{"automation","instruments"})if(v["locations"][workspaceEditorIndex(id)]!="hide")ensureWorkspaceEditor(id);
    }
    void restoreWorkspaceDockConfiguration(const Json &v) {
        EditorPlacementGuard guard(changingEditorPlacement);
        for(size_t i=0;i<2;++i){workspaceEditors[i].location=v["locations"][i].get<std::string>();if(workspaceEditors[i].location!="hide")workspaceEditors[i].preferred=workspaceEditors[i].location;}
        activeWorkspaceEditor=v["active"].get<std::string>();dockTrackerSelected=v["tracker"].get<bool>();
    }
    template<class Place>void layoutWorkspaceEditors(Place place) {
        EditorPlacementGuard guard(changingEditorPlacement);
        if(hasWorkspaceDock()) {
            RECT client{},outer{};GetClientRect(window,&client);GetWindowRect(window,&outer);const auto minimum=LONG(std::ceil(666*GetDpiForWindow(window)/96.0f));
            if(client.bottom<minimum)SetWindowPos(window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top+minimum-client.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        if(workspaceEditors[workspaceEditorIndex(activeWorkspaceEditor)].location!="right")for(const auto *id:{"automation","instruments"})if(workspaceEditors[workspaceEditorIndex(id)].location=="right"){activeWorkspaceEditor=id;break;}
        const bool dock=hasWorkspaceDock(),side=sideWorkspaceDock(),tracker=trackerWorkspaceVisible();const auto r=workspaceEditorRect();
        // Hide primary native controls beneath the retained editor in tab mode.
        if(!tracker)for(auto [id,h]:controls){if(id>=editorTrackerTab&&id<=editorReturnCommand)continue;RECT bounds{};GetWindowRect(h,&bounds);POINT p{bounds.left,bounds.top};ScreenToClient(window,&p);if(p.x>=int(164*GetDpiForWindow(window)/96.0f)&&p.y>=int(88*GetDpiForWindow(window)/96.0f))ScreamSeq::NativeControls::show(h,false);}
        float x=176;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{editorTrackerTab,92.f},{editorAutomationTab,108.f},{editorInstrumentTab,108.f},{editorFloatCommand,60.f},{editorHideCommand,54.f},{editorPinCommand,72.f},{editorCursorCommand,66.f},{editorReturnCommand,66.f}}){place(id,x,91,width,25,dock);x+=width+4;}
        if(dock){ScreamSeq::NativeControls::text(controls.at(editorTrackerTab),side?L"Focus tracker":L"Tracker");ScreamSeq::NativeControls::text(controls.at(editorPinCommand),workspaceEditors[workspaceEditorIndex(activeWorkspaceEditor)].pinned?L"Pinned":L"Following");}
        for(const auto *id:{"automation","instruments"}) {
            const auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);if(!tool)continue;
            if(p.location=="right") {
                if(!tool->docked())tool->dock(window);
                tool->dockBounds(r.x,r.y,r.w,r.h);
                const bool show=dock&&id==activeWorkspaceEditor&&(side||!dockTrackerSelected);
                if(show&&!tool->visible())tool->show();else if(!show&&tool->visible())tool->hide();
            } else if(p.location=="float") {if(tool->docked())tool->floatWindow();if(!tool->visible())tool->show();}
            else if(tool->visible())tool->hide();
        }
        for(int id:{editorTrackerTab,editorAutomationTab,editorInstrumentTab})ScreamSeq::NativeControls::active(controls.at(id),id==editorTrackerTab?tracker:id==editorAutomationTab?activeWorkspaceEditor=="automation"&&(!tracker||side):activeWorkspaceEditor=="instruments"&&(!tracker||side));
        if(!tracker&&GetFocus()==window)focusVisibleWorkspaceCanvas();
    }
    bool workspaceEditorCommand(int id) {
        if(id==dockAutomationCommand||id==dockInstrumentCommand){workspaceEditorRequest({{"panel",id==dockAutomationCommand?"automation":"instruments"},{"placement","right"},{"focus",true}});return true;}
        if(id<editorTrackerTab||id>editorReturnCommand)return false;
        if(id==editorTrackerTab){dockTrackerSelected=true;workspaceState.focus="pattern";layoutControls();SetFocus(window);return true;}
        if(id==editorAutomationTab||id==editorInstrumentTab){workspaceEditorRequest({{"panel",id==editorAutomationTab?"automation":"instruments"},{"placement","right"},{"focus",true}});return true;}
        Json p={{"panel",activeWorkspaceEditor}};
        if(id==editorFloatCommand){p["placement"]="float";p["focus"]=true;}else if(id==editorHideCommand)p["placement"]="hide";
        else if(id==editorPinCommand)p["pinned"]=!workspaceEditors[workspaceEditorIndex(activeWorkspaceEditor)].pinned;
        else if(id==editorCursorCommand)p["follow"]=true;else p["return"]=true;
        workspaceEditorRequest(p);return true;
    }
