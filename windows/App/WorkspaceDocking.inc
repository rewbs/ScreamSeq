    // Presentation preferences never own a musical target or an editor draft.
    struct WorkspaceEditorPanel {
        std::string location="hide", preferred="float", document, patternID;
        Json origin=Json::object();
        bool pinned=false, configured=false;
    };
    std::array<WorkspaceEditorPanel,3> workspaceEditors=[] {
        std::array<WorkspaceEditorPanel,3> panels;
        panels[2].preferred="secondary";
        return panels;
    }();
    static constexpr std::array<const char *,3> workspaceEditorNames{"automation","instruments","graphCurve"};
    std::string activeWorkspaceEditor="automation";
    bool dockTrackerSelected=false, changingEditorPlacement=false, followingEditors=false;
    Regions::Config regionConfiguration;
    float regionDragRight=460,regionDragBottom=334;
    bool previousPatternVisible=true,previousMainVisible=true;
    struct CompletedWorkspaceLayout {
        Regions::Mode mode=Regions::Mode::none;
        bool pattern=false,inspector=false;
        std::string main;
        std::vector<HWND> mainControls;
    } completedWorkspaceLayout;
    // Notes/Samples retain their public focus names in both the legacy inspector
    // and Main editor. Remember which canvas owns input rather than guessing
    // from whichever matching surface happens to be visible after a resize.
    bool inspectorCanvasFocus=false;
    static bool inspectorFocusAlias(const std::string &id){return id=="notes"||id=="samples";}
    void setWorkspaceCanvasFocus(const std::string &id,bool inspector=false){
        workspaceState.focus=id;inspectorCanvasFocus=inspector&&inspectorFocusAlias(id);
    }
    bool workspaceInspectorCanvasIntent()const{return inspectorCanvasFocus&&inspectorFocusAlias(workspaceState.focus);}
    bool workspaceMainCanvasFocused(const std::string &id)const{
        return workspaceState.focus==id&&!workspaceInspectorCanvasIntent()&&workspaceMainEditorVisible(id);
    }
    // Visible native ownership affects presentation only. A source-free Guide
    // may have a hidden owner and must retain its opening Main context. Keep the last Main
    // canvas/inspector intent intact for its next keyboard focus restoration.
    std::string workspacePresentationFocus()const {
        const auto focus=GetFocus();
        for(const auto *id:workspaceEditorNames)
            if(auto *tool=workspaceEditorWindow(id);tool&&tool->visible()&&tool->presentationOwns(focus))return id;
        return workspaceState.focus;
    }
    std::string workspaceMainCanvasIdentity(HWND=nullptr)const{return lowerEditorName();}
    struct EditorPlacementGuard {
        bool &flag;bool previous;
        explicit EditorPlacementGuard(bool &value):flag(value),previous(value){flag=true;}
        ~EditorPlacementGuard(){flag=previous;}
    };
    static bool isWorkspaceEditor(const std::string &id){return Regions::nativePanel(id);}
    static size_t workspaceEditorIndex(const std::string &id){
        if(id=="automation")return 0;if(id=="instruments")return 1;if(id=="graphCurve")return 2;
        throw ScreamSeq::Api::ApiError(-32602,"Choose automation, instruments or graphCurve");
    }
    ScreamSeq::NativeToolWindow *workspaceEditorWindow(const std::string &id)const {
        switch(workspaceEditorIndex(id)) {
        case 0:return parameterAutomationWindow.get();
        case 1:return instrumentEnvelopeWindow.get();
        case 2:return graphCurveWindow.get();
        }
        return nullptr;
    }
    Json workspaceEditorSnapshot(const std::string &id)const {
        switch(workspaceEditorIndex(id)) {
        case 0:return parameterAutomationWindow?parameterAutomationWindow->snapshot():Json::object();
        case 1:return instrumentEnvelopeWindow?instrumentEnvelopeWindow->snapshot():Json::object();
        case 2:return graphCurveWindow?graphCurveWindow->snapshot():Json::object();
        }
        return Json::object();
    }
    Json workspaceGraphCurveSourceGuard()const {
        const auto source=graphCurveContext();Json target=nullptr;
        if(source.selected)target={{"graph",source.selected->graph},{"node",source.selected->node},
            {"patternID",source.selected->patternID},{"pattern",source.selected->pattern}};
        return {{"document",source.document},{"revision",source.revision},
            {"selectionGeneration",source.selectionGeneration},{"target",std::move(target)}};
    }
    bool workspaceEditorRetainedDraft(const std::string &id)const {
        switch(workspaceEditorIndex(id)) {
        case 0:return parameterAutomationWindow&&parameterAutomationWindow->retainedDraft();
        case 1:return instrumentEnvelopeWindow&&instrumentEnvelopeWindow->retainedDraft();
        case 2:return graphCurveWindow&&graphCurveWindow->retainedDraft();
        }
        return false;
    }
    static bool dockLocation(const std::string &location){return location=="right"||location=="bottom"||location=="secondary";}
    static const char *defaultWorkspaceDockLocation(const std::string &id){return id=="graphCurve"?"secondary":"right";}
    Regions::Config currentWorkspaceRegions()const {
        auto value=regionConfiguration;
        for(size_t i=0;i<workspaceEditors.size();++i)value.locations[i]=Regions::placement(workspaceEditors[i].location);
        const auto previousMain=value.selected[1];
        if(Regions::mainBottom(previousMain))value.selected[1]=lowerEditorName();
        for(size_t i=0;i<3;++i)if(!Regions::belongs(value,static_cast<Regions::Region>(i),value.selected[i]))
            value.selected[i]=i==1?lowerEditorName():Regions::firstNative(value,static_cast<Regions::Region>(i));
        if(value.compactSelection==previousMain)value.compactSelection=value.selected[1];
        if(dockTrackerSelected||!Regions::compactAvailable(value,value.compactSelection))value.compactSelection="pattern";
        Regions::validate(value);return value;
    }
    bool hasWorkspaceDock()const {return workspaceState.layout!="Pattern focus"&&std::any_of(workspaceEditors.begin(),workspaceEditors.end(),[](const auto &p){return dockLocation(p.location);});}
    float workspaceClientWidth()const {RECT r{};GetClientRect(window,&r);return r.right*96.0f/GetDpiForWindow(window);}
    Regions::Geometry workspaceRegionGeometry()const {
        RECT client{};GetClientRect(window,&client);const auto scale=96.f/GetDpiForWindow(window);
        Regions::Minimums minimums;minimums.patternWidth=workspaceState.visible()?656.f:350.f;
        return Regions::solve({170,88,std::max(0.f,client.right*scale-178),std::max(0.f,client.bottom*scale-132)},
            currentWorkspaceRegions(),minimums,workspaceState.layout=="Pattern focus",workspaceState.lowerVisible);
    }
    bool sideWorkspaceDock()const {return hasWorkspaceDock()&&workspaceRegionGeometry().mode==Regions::Mode::regions;}
    bool trackerWorkspaceVisible()const {return !hasWorkspaceDock()||workspaceRegionGeometry().pattern.h>0;}
    bool workspaceMainEditorVisible(const std::string &id)const {
        if(workspaceState.layout=="Pattern focus"||!workspaceState.lowerVisible||lowerEditorName()!=id)return false;
        if(!hasWorkspaceDock())return true;
        const auto regions=workspaceRegionGeometry();const auto &host=regions.hosts[1];return host.visible&&host.selected==id;
    }
    ScreamSeq::WorkspaceRect workspaceMainEditorRect()const {
        if(hasWorkspaceDock())return workspaceRegionGeometry().hosts[1].body;
        const auto g=geometry();return {g.graph.x,g.graph.y,g.automation.x+g.automation.w-g.graph.x,g.graph.h};
    }
    void revealWorkspacePattern(){dockTrackerSelected=true;regionConfiguration.compactSelection="pattern";}
    void revealWorkspaceMainEditor(const std::string &id){
        if(!Regions::mainBottom(id))throw std::invalid_argument("Choose a main editor");
        regionConfiguration.selected[1]=id;regionConfiguration.compactSelection=id;
        dockTrackerSelected=false;workspaceState.lowerVisible=true;
    }
    bool workspaceCanvasVisible(const std::string &id,bool inspector)const{
        if(id=="pattern"||id=="graphLanes")return trackerWorkspaceVisible();
        if(inspectorFocusAlias(id))return inspector
            ?workspaceInspectorVisible()&&id==workspaceState.active:workspaceMainEditorVisible(id);
        // Native owners are never painted Main canvases, even when docked.
        if(isWorkspaceEditor(id))return false;
        if(id=="graph")return graphEditorVisible();
        if(Regions::mainBottom(id))return workspaceMainEditorVisible(id);
        return false;
    }
    bool workspaceMainControl(HWND target)const{
        if(!target||target==window||!IsChild(window,target)||!IsWindowVisible(target)||!IsWindowEnabled(target)||
            ScreamSeq::NativeToolWindow::belongsToTool(target)||!workspaceMainEditorVisible(lowerEditorName()))return false;
        RECT bounds{};GetWindowRect(target,&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&bounds),2);
        const auto scale=96.f/GetDpiForWindow(window);const auto body=workspaceMainEditorRect();
        return bounds.left*scale>=body.x&&bounds.top*scale>=body.y&&
            bounds.right*scale<=body.x+body.w&&bounds.bottom*scale<=body.y+body.h;
    }
    void rememberCompletedWorkspaceLayout(){
        if(IsIconic(window))return;
        CompletedWorkspaceLayout completed;
        completed.mode=hasWorkspaceDock()?workspaceRegionGeometry().mode:Regions::Mode::none;
        if(completed.mode==Regions::Mode::regions){
            completed.pattern=trackerWorkspaceVisible();completed.inspector=workspaceInspectorVisible();
            if(workspaceMainEditorVisible(lowerEditorName())){
                completed.main=lowerEditorName();
                // Record actual HWND membership while the old body is laid out.
                // A later WM_SIZE/DPI change must not classify these controls
                // against a newly solved (and possibly hidden) compact body.
                for(const auto &[id,control]:controls)if(workspaceMainControl(control))completed.mainControls.push_back(control);
            }
        }
        completedWorkspaceLayout=std::move(completed);
    }
    void retainWorkspaceFocusForResize(WPARAM sizeKind){
        // Ordinary layout restoration and panel placement keep their explicit
        // selected tab. Only the window's regions-to-tabs resize transition
        // carries the surface that currently owns keyboard input into tabs.
        if(sizeKind==SIZE_MINIMIZED||controls.empty()||preparingWorkspaceLayout||changingEditorPlacement||
            completedWorkspaceLayout.mode!=Regions::Mode::regions||!hasWorkspaceDock()||
            workspaceRegionGeometry().mode!=Regions::Mode::tabs)return;
        const auto focused=GetFocus();
        if(!focused||!IsWindow(focused)||!IsWindowVisible(focused)||!IsWindowEnabled(focused))return;
        const auto &completed=completedWorkspaceLayout;std::string host;
        if(focused==window){
            if(((workspaceState.focus=="pattern"||workspaceState.focus=="graphLanes")&&completed.pattern)||
                (workspaceInspectorCanvasIntent()&&completed.inspector))host="pattern";
            else if(!workspaceInspectorCanvasIntent()&&!completed.main.empty()&&completed.main==lowerEditorName()&&
                workspaceState.focus==completed.main)host=completed.main;
        }else{
            for(const auto *id:workspaceEditorNames)if(auto *tool=workspaceEditorWindow(id);
                tool&&tool->docked()&&tool->visible()&&tool->owns(focused))host=id;
            if(host.empty()&&!completed.main.empty()&&completed.main==lowerEditorName()&&IsChild(window,focused)&&
                !ScreamSeq::NativeToolWindow::belongsToTool(focused)&&
                std::find(completed.mainControls.begin(),completed.mainControls.end(),focused)!=completed.mainControls.end())host=completed.main;
        }
        if(!host.empty()&&Regions::compactAvailable(currentWorkspaceRegions(),host)){
            regionConfiguration.compactSelection=host;dockTrackerSelected=host=="pattern";
        }
    }
    std::string focusedWorkspaceRegion(HWND target)const{
        for(const auto *id:workspaceEditorNames)if(auto *tool=workspaceEditorWindow(id);tool&&tool->visible()&&tool->owns(target))return id;
        if(workspaceMainControl(target))return lowerEditorName();
        if(target!=window||!workspaceCanvasVisible(workspaceState.focus,inspectorCanvasFocus))return {};
        if(workspaceState.focus=="pattern"||workspaceState.focus=="graphLanes"||workspaceInspectorCanvasIntent())return "pattern";
        return workspaceState.focus;
    }
    bool workspaceFocusVisible(HWND target)const {
        return target&&IsWindow(target)&&IsWindowVisible(target)&&IsWindowEnabled(target)&&
            (target!=window||workspaceFocusedCanvasVisible());
    }
    void focusVisibleWorkspaceCanvas() {
        if(!trackerWorkspaceVisible()) {
            if(workspaceMainEditorVisible(lowerEditorName())){setWorkspaceCanvasFocus(workspaceMainCanvasIdentity());SetFocus(window);return;}
            for(const auto *id:workspaceEditorNames)if(auto *tool=workspaceEditorWindow(id);tool&&tool->visible()) {
                setWorkspaceCanvasFocus(id);SetFocus(tool->window());return;
            }
        }
        setWorkspaceCanvasFocus("pattern");SetFocus(window);
    }
    float workspaceContentTop()const {return 88.f;}
    ScreamSeq::WorkspaceRect workspaceEditorRect(const std::string &id="")const {
        const auto selected=id.empty()?activeWorkspaceEditor:id;const auto regions=workspaceRegionGeometry();
        for(const auto &host:regions.hosts)if(host.selected==selected&&host.visible)return host.body;
        return {};
    }
    void placeWorkspaceEditor(const std::string &id,const std::string &location){
        regionConfiguration=Regions::placed(currentWorkspaceRegions(),static_cast<Regions::Panel>(workspaceEditorIndex(id)),Regions::placement(location),lowerEditorName());
        auto &state=workspaceEditors[workspaceEditorIndex(id)];state.location=location;if(location!="hide")state.preferred=location;
    }
    void selectWorkspaceEditor(const std::string &id){
        auto &state=workspaceEditors[workspaceEditorIndex(id)];
        if(dockLocation(state.location)){
            const auto region=static_cast<Regions::Region>(Regions::placement(state.location));
            regionConfiguration=Regions::selected(currentWorkspaceRegions(),region,id);
            regionConfiguration.compactSelection=id;dockTrackerSelected=false;
        }
        activeWorkspaceEditor=id;
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
        if(ctrl&&alt&&key=='D'){if(!repeat)workspaceEditorRequest({{"panel",id},{"placement",workspaceEditorWindow(id)->docked()?"float":defaultWorkspaceDockLocation(id)},{"focus",true}});return true;}
        return false;
    }
    void configureWorkspaceEditor(const std::string &id) {
        auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);if(!tool||p.configured)return;
        p.configured=true;
        tool->workspaceKeys([this,id](WPARAM key,bool repeat){return editorWorkspaceKey(id,key,repeat);});
        tool->focusPresentationChanged([this]{
            frameRequested=true;if(window)InvalidateRect(window,nullptr,FALSE);
        });
        tool->workspaceDockAction([this,id]{workspaceEditorRequest({{"panel",id},{"placement",workspaceEditorWindow(id)->docked()?"float":defaultWorkspaceDockLocation(id)},{"focus",true}});});
        tool->placementChanged([this,id]{
            if(changingEditorPlacement)return;
            auto &state=workspaceEditors[workspaceEditorIndex(id)];auto *editor=workspaceEditorWindow(id);
            if(editor->visible()){placeWorkspaceEditor(id,editor->docked()?(dockLocation(state.location)?state.location:defaultWorkspaceDockLocation(id)):"float");captureWorkspaceEditorOrigin(id);}
            else {placeWorkspaceEditor(id,"hide");if(workspaceState.focus==id)workspaceState.focus="pattern";}
            layoutControls();
        });
    }
    void finishWorkspaceEditorOpen(const std::string &id) {
        auto &p=workspaceEditors[workspaceEditorIndex(id)];captureWorkspaceEditorOrigin(id);
        if(dockLocation(p.preferred)) {
            EditorPlacementGuard guard(changingEditorPlacement);workspaceEditorWindow(id)->dock(window);
            placeWorkspaceEditor(id,p.preferred);selectWorkspaceEditor(id);
            if(workspaceState.layout=="Pattern focus")workspaceState.layout="Compose";
        } else placeWorkspaceEditor(id,"float");
        layoutControls();
    }
    void ensureWorkspaceEditor(const std::string &id) {
        if(id=="graphCurve") {
            // Guide can create an owner without ever opening/capturing a curve.
            // Pointer existence alone must not skip the first actual openAt().
            if(!graphCurveWindow||workspaceEditors[workspaceEditorIndex(id)].origin.empty()||
                !graphCurveOperationGuard().value("initialized",false))openGraphCurve();
            return;
        }
        if(workspaceEditorWindow(id))return;
        if(id=="automation")openParameterAutomation();else openInstrumentEnvelope();
    }
    bool followWorkspaceEditor(const std::string &id) {
        switch(workspaceEditorIndex(id)) {
        case 0:return parameterAutomationWindow->followCursor();
        case 1:return instrumentEnvelopeWindow->followCursor();
        case 2:return graphCurveWindow->followSelection();
        }
        return false;
    }
    void followWorkspaceEditors() {
        if(followingEditors||changingEditorPlacement||busy)return;
        EditorPlacementGuard guard(followingEditors);
        for(const auto *id:workspaceEditorNames) {
            const auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);
            if(p.pinned||!tool||!tool->visible()||tool->owns(GetFocus()))continue;
            // A Guide-only owner restored into a region remains deliberately
            // empty until an explicit opening or Load selection action.
            if(id==std::string_view("graphCurve")&&!graphCurveOperationGuard().value("initialized",false))continue;
            try{followWorkspaceEditor(id);}catch(const std::exception &e){status=wide(e.what());}
        }
    }
    void workspaceEditorRequest(const Json &p) {
        const auto id=p.at("panel").get<std::string>();auto &state=workspaceEditors[workspaceEditorIndex(id)];
        for(auto i=p.begin();i!=p.end();++i)if(i.key()!="panel"&&i.key()!="placement"&&i.key()!="pinned"&&i.key()!="focus"&&i.key()!="follow"&&i.key()!="return")throw ScreamSeq::Api::ApiError(-32602,"Unknown workspace parameter");
        for(const auto *key:{"pinned","focus","follow","return"})if(p.contains(key)&&!p[key].is_boolean())throw ScreamSeq::Api::ApiError(-32602,"Panel flags must be boolean");
        if(p.contains("placement")){if(!p["placement"].is_string())throw ScreamSeq::Api::ApiError(-32602,"Choose right, bottom, secondary, float or hide");(void)Regions::placement(p["placement"].get<std::string>());}
        Json returnPoint;if(p.value("return",false))returnPoint=workspaceEditorReturnPoint(id);
        if(busy)throw ScreamSeq::Api::ApiError(-32002,"Wait for the current document operation before moving an editor");
        const auto previousFocus=GetFocus();const auto previousPanel=workspaceState.focus;
        const bool previousInspector=inspectorCanvasFocus;
        std::string retainedSurface;
        if(!p.value("focus",false)&&!p.value("return",false)) {
            retainedSurface=focusedWorkspaceRegion(previousFocus);
        }
        const auto placement=p.value("placement",state.location=="hide"?state.preferred:state.location);
        // Hiding an unopened editor is a pure presentation operation.
        if((p.contains("placement")&&placement!="hide")||p.value("focus",false)||p.value("follow",false))ensureWorkspaceEditor(id);
        EditorPlacementGuard guard(changingEditorPlacement);auto *tool=workspaceEditorWindow(id);
        if(p.contains("pinned"))state.pinned=p["pinned"].get<bool>();
        if(p.value("follow",false))state.pinned=false;
        if(tool&&(p.value("follow",false)||(p.contains("pinned")&&!state.pinned)))followWorkspaceEditor(id);
        if(p.contains("placement")) {
            placeWorkspaceEditor(id,placement);
            if(tool){if(dockLocation(placement))tool->dock(window);else if(placement=="float")tool->floatWindow();else tool->hide();}
        }
        if(tool&&((p.contains("placement")&&dockLocation(placement))||p.value("focus",false))) {
            if(workspaceState.layout=="Pattern focus")workspaceState.layout="Compose";
            placeWorkspaceEditor(id,placement=="hide"?state.preferred:placement);
            if(dockLocation(state.location)){
                tool->dock(window);selectWorkspaceEditor(id);
                if(!retainedSurface.empty()&&!sideWorkspaceDock()&&Regions::compactAvailable(currentWorkspaceRegions(),retainedSurface)){
                    regionConfiguration.compactSelection=retainedSurface;dockTrackerSelected=retainedSurface=="pattern";
                    if(previousFocus==window)setWorkspaceCanvasFocus(previousPanel,previousInspector);
                    else if(isWorkspaceEditor(retainedSurface))setWorkspaceCanvasFocus(retainedSurface);
                    else if(retainedSurface!="pattern")setWorkspaceCanvasFocus(workspaceMainCanvasIdentity(previousFocus));
                }
            }
            else tool->floatWindow();
        }
        // Returning must expose the tracker even if navigate() is a no-op at
        // the opening position and therefore does not perform another layout.
        if(p.value("return",false))revealWorkspacePattern();
        layoutControls();
        if(state.location=="hide"&&workspaceState.focus==id)setWorkspaceCanvasFocus("pattern");
        if(p.value("return",false)){setWorkspaceCanvasFocus("pattern");navigate(returnPoint);SetFocus(window);}
        else if(p.value("focus",false)&&tool){tool->show();setWorkspaceCanvasFocus(id);SetFocus(tool->window());}
        else if(workspaceFocusVisible(previousFocus))SetFocus(previousFocus);
        else if(!workspaceFocusVisible(GetFocus()))focusVisibleWorkspaceCanvas();
        ensureCursorVisible();
    }
    Json workspaceDockSnapshot()const {
        const auto regions=workspaceRegionGeometry();auto rect=[](const auto &r)->Json{return {{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}};};
        Json hosts=Json::object();for(size_t i=0;i<3;++i){const auto &host=regions.hosts[i];hosts[std::string(Regions::regionNames[i])]={{"selected",host.selected},{"visible",host.visible},{"header",rect(host.header)},{"body",rect(host.body)}};}
        return {{"mode",regions.mode==Regions::Mode::none?"none":regions.mode==Regions::Mode::regions?"regions":"tabs"},
            {"active",activeWorkspaceEditor},{"trackerVisible",trackerWorkspaceVisible()},{"rect",rect(workspaceEditorRect())},
            {"hosts",std::move(hosts)},{"compactSelection",regions.compactSelection},{"configuration",workspaceDockConfiguration()}};
    }
    Json workspaceDockConfiguration()const {
        return Regions::encode(currentWorkspaceRegions());
    }
    Regions::Config decodeWorkspaceDockConfiguration(const Json &v,const std::string &main,float lower)const {
        return Regions::decodeEditors(&v,currentWorkspaceRegions(),main,lower);
    }
    // Worker waits pump ordinary input. A candidate may be discarded, but must
    // never roll back a newer cursor, field edit, focus choice or arrangement.
    Json workspacePreparationGuard()const {
        Json editors=Json::array(),inspectors=Json::array();
        for(const auto *id:workspaceEditorNames) {
            const auto &state=workspaceEditors[workspaceEditorIndex(id)];
            auto *tool=workspaceEditorWindow(id);
            Json captured=Json::object();
            if(tool) {
                const auto snapshot=workspaceEditorSnapshot(id);
                for(const auto *key:{"initialized","generation","document","expectedRevision","pattern","patternID","graph","node","plugin","parameter","instrument","index","page","selectedPoint","retainedDraft","pending"})
                    if(snapshot.contains(key))captured[key]=snapshot.at(key);
            }
            editors.push_back({{"identity",reinterpret_cast<uintptr_t>(tool)},
                {"visible",tool&&tool->visible()},{"docked",tool&&tool->docked()},
                {"location",state.location},{"preferred",state.preferred},{"pinned",state.pinned},
                {"origin",state.origin},{"document",state.document},{"patternID",state.patternID},{"captured",std::move(captured)}});
        }
        for(const auto &panel:workspaceState.panels)
            inspectors.push_back({{"pinned",panel.pinned},{"opened",panel.opened},{"hidden",panel.hidden},
                {"target",panel.target},{"origin",panel.origin},{"sample",panel.sample}});
        const auto current=controller->view();
        return {{"document",documentId},{"revision",view->session.revision},
            {"workerDocument",current->session.documentId},{"workerRevision",current->session.revision},
            {"context",contextRevision},{"position",position()},
            {"focus",reinterpret_cast<uintptr_t>(GetFocus())},{"focusedPanel",workspaceState.focus},{"inspectorCanvasFocus",inspectorCanvasFocus},
            {"layout",layoutConfiguration()},{"inspectors",std::move(inspectors)},{"editors",std::move(editors)},
            {"plugin",selectedPlugin},{"parameter",selectedParameter},{"pluginDraft",pluginDraft},{"pluginPresetPending",pluginPresetPending},
            {"notes",{{"captured",noteCaptured},{"generation",noteDraftGeneration},{"pending",notePending}}},
            {"effects",effectDraftExists},
            {"curve",graphCurveOperationGuard()},{"curveSource",workspaceGraphCurveSourceGuard()},
            {"graph",{{"document",graphDocument},{"revision",graphRevision},{"generation",graphGeneration},
                {"id",graphID},{"node",graphNode},{"dirty",graphDirty},{"fieldDraft",graphFieldDirty},{"pending",graphPending}}},
            {"mixer",{{"document",mixerDocument},{"revision",mixerRevision},{"generation",mixerGeneration},
                {"target",mixerTarget},{"dirty",mixerDirty},{"pending",mixerPending}}}};
    }
    void validateWorkspacePreparation(const Json &guard)const {
        if(guard!=workspacePreparationGuard())
            throw ScreamSeq::Api::ApiError(-32003,"Workspace changed while preparing the layout; current edits were retained");
    }
    struct PreparedWorkspaceEditors {
        std::shared_ptr<bool> preparing=std::make_shared<bool>(true);
        std::unique_ptr<ScreamSeq::ParameterAutomationWindow> automation;
        std::unique_ptr<ScreamSeq::InstrumentEnvelopeWindow> instruments;
        std::unique_ptr<ScreamSeq::GraphCurveWindow> graphCurve;
    };
    PreparedWorkspaceEditors prepareWorkspaceDockConfiguration(const Regions::Config &v,const Json &guard,
        const ScreamSeq::GraphCurveWindow::Context &curveContext) {
        PreparedWorkspaceEditors prepared;
        // The caller captured the curve source before any main/native read.
        // Capture the parameter target before this helper's first wait. These
        // windows remain unregistered, hidden and disconnected from placement.
        const auto [plugin,parameter]=initialParameterAutomationTarget();
        if(v.locations[0]!=Regions::Placement::hidden&&!parameterAutomationWindow) {
            prepared.automation=makeParameterAutomationWindow(prepared.preparing);
            prepared.automation->initializeHidden(plugin,parameter);
            validateWorkspacePreparation(guard);
        }
        if(v.locations[1]!=Regions::Placement::hidden&&!instrumentEnvelopeWindow) {
            prepared.instruments=makeInstrumentEnvelopeWindow(prepared.preparing);
            prepared.instruments->initializeHidden();
            validateWorkspacePreparation(guard);
        }
        if(v.locations[2]!=Regions::Placement::hidden&&!graphCurveWindow) {
            prepared.graphCurve=makeGraphCurveWindow(prepared.preparing);
            prepared.graphCurve->initializeHidden(curveContext.selected);
            validateWorkspacePreparation(guard);
        }
        // An existing Guide-only owner and all its child HWNDs survive unchanged.
        // It can remain empty until explicit open/Follow; no hidden read is
        // performed into an already live native owner during restore.
        return prepared;
    }
    void adoptWorkspaceEditors(PreparedWorkspaceEditors &&prepared) {
        // Only these staged objects share this token. Existing editors keep
        // their ordinary callbacks throughout preparation and later waits.
        *prepared.preparing=false;
        if(prepared.automation) {
            parameterAutomationWindow=std::move(prepared.automation);
            configureWorkspaceEditor("automation");captureWorkspaceEditorOrigin("automation");
        }
        if(prepared.instruments) {
            instrumentEnvelopeWindow=std::move(prepared.instruments);
            connectInstrumentEnvelopeTyping();configureWorkspaceEditor("instruments");captureWorkspaceEditorOrigin("instruments");
        }
        if(prepared.graphCurve) {
            graphCurveWindow=std::move(prepared.graphCurve);
            configureWorkspaceEditor("graphCurve");captureWorkspaceEditorOrigin("graphCurve");
        }
    }
    void restoreWorkspaceDockConfiguration(const Regions::Config &v) {
        EditorPlacementGuard guard(changingEditorPlacement);
        regionConfiguration=v;
        for(size_t i=0;i<workspaceEditors.size();++i){workspaceEditors[i].location=Regions::name(v.locations[i]);if(workspaceEditors[i].location!="hide")workspaceEditors[i].preferred=workspaceEditors[i].location;}
        dockTrackerSelected=v.compactSelection=="pattern";
        if(isWorkspaceEditor(v.compactSelection))activeWorkspaceEditor=v.compactSelection;
    }
    template<class Place>void layoutWorkspaceEditors(Place apply) {
        EditorPlacementGuard guard(changingEditorPlacement);
        const auto regions=workspaceRegionGeometry();const bool dock=hasWorkspaceDock(),tracker=trackerWorkspaceVisible();
        const auto focused=GetFocus();
        // This layout may hide workspace surfaces, but it does not own another
        // modeless tool's pending-focus protocol. A disabled native field may
        // intentionally leave focus null until that tool completes its work.
        bool ownedWorkspaceFocus=focused&&(focused==window||
            (IsChild(window,focused)&&!ScreamSeq::NativeToolWindow::belongsToTool(focused)));
        if(focused)for(const auto *id:workspaceEditorNames)if(auto *tool=workspaceEditorWindow(id);tool&&tool->owns(focused))ownedWorkspaceFocus=true;
        std::map<int,ScreamSeq::WorkspaceRect> shown;
        auto place=[&](int id,float x,float y,float w,float h){shown[id]={x,y,w,h};};
        if(dock&&regions.mode==Regions::Mode::regions)for(size_t i=0;i<3;++i){
            const auto &host=regions.hosts[i];if(!host.visible)continue;
            layoutWorkspaceRegionHeader(place,i,host.header);
        }
        if(dock&&regions.mode==Regions::Mode::tabs){
            float x=regions.tabs.x+4;const auto y=regions.tabs.y+2;
            for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{editorTrackerTab,80.f},{editorAutomationTab,96.f},{editorInstrumentTab,96.f},{editorGraphCurveTab,100.f}}){place(id,x,y,width,24);x+=width+4;}
            const auto mainButton=regionControlBase+regionControlStride;
            place(mainButton,x,y,116,24);ScreamSeq::NativeControls::text(controls.at(mainButton),workspaceRegionTitle(lowerEditorName())+L" ▾");x+=120;
            for(size_t i=0;i<3;++i)if(regions.hosts[i].visible&&isWorkspaceEditor(regions.hosts[i].selected)){
                if(x+222<=regions.tabs.x+regions.tabs.w-4)layoutWorkspaceRegionActions(place,i,x,y);
                else {const auto more=regionControlBase+int(i)*regionControlStride+1;place(more,x,y,28,24);ScreamSeq::NativeControls::text(controls.at(more),L"…");}
            }
        }
        const auto placeOnce=[&](int id){const auto found=shown.find(id);if(found==shown.end())apply(id,0,0,1,1,false);else{const auto &r=found->second;apply(id,r.x,r.y,r.w,r.h,true);}};
        for(int id=editorTrackerTab;id<=editorReturnCommand;++id)placeOnce(id);
        placeOnce(editorGraphCurveTab);
        for(int id=regionControlBase;id<=regionControlEnd;++id)if(controls.contains(id))placeOnce(id);
        for(const auto *id:workspaceEditorNames) {
            const auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);if(!tool)continue;
            if(dockLocation(p.location)) {
                if(!tool->docked())tool->dock(window);
                const auto r=workspaceEditorRect(id);const bool show=dock&&r.w>0&&r.h>0;
                if(show)tool->dockBounds(r.x,r.y,r.w,r.h);
                if(show&&!tool->visible())tool->show();else if(!show&&tool->visible())tool->hide();
            } else if(p.location=="float") {if(tool->docked())tool->floatWindow();if(!tool->visible())tool->show();}
            else if(tool->visible())tool->hide();
            // A pre-existing Guide-only curve receives only presentation state
            // on restore. Capture Return when its body is first actually shown,
            // without loading a target or changing the retained child windows.
            if(id==std::string_view("graphCurve")&&tool->visible())captureWorkspaceEditorOrigin(id);
        }
        ScreamSeq::NativeControls::active(controls.at(editorTrackerTab),tracker);
        for(const auto &[button,panel]:std::array<std::pair<int,const char *>,3>{{
            {editorAutomationTab,"automation"},{editorInstrumentTab,"instruments"},{editorGraphCurveTab,"graphCurve"}}}) {
            const auto *tool=workspaceEditorWindow(panel);
            ScreamSeq::NativeControls::active(controls.at(button),tool&&tool->visible());
        }
        const bool main=workspaceMainEditorVisible(lowerEditorName());
        if((previousPatternVisible&&!tracker)||(previousMainVisible&&!main))releaseTypedNotes();
        previousPatternVisible=tracker;previousMainVisible=main;
        // Preserve a newly valid focus even if the original surface was hidden.
        if(ownedWorkspaceFocus&&!workspaceFocusVisible(GetFocus()))focusVisibleWorkspaceCanvas();
    }
    static std::wstring workspaceRegionTitle(const std::string &id){
        if(id=="automation")return L"Automation";if(id=="instruments")return L"Instrument";if(id=="graphCurve")return L"Graph curve";
        if(id=="notes")return L"Notes";if(id=="samples")return L"Samples";if(id=="effects")return L"Pattern FX";
        if(id=="plugins")return L"Plugins";if(id=="mixer")return L"Mixer";if(id=="graph")return L"Graph";return L"Tracker";
    }
    bool workspaceEditorFollowHeld(const std::string &id)const {
        const auto &state=workspaceEditors[workspaceEditorIndex(id)];if(state.pinned)return false;
        const auto *tool=workspaceEditorWindow(id);if(!tool)return false;
        return tool->owns(GetFocus())||workspaceEditorRetainedDraft(id);
    }
    void installWorkspaceRegionControls(){
        for(int region=0;region<3;++region)for(int action=0;action<5;++action){
            const auto id=regionControlBase+region*regionControlStride+action;
            const auto control=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            if(!control)throw std::runtime_error("Cannot create workspace region controls");controls.emplace(id,control);
        }
    }
    template<class Place>void layoutWorkspaceRegionActions(Place place,size_t region,float x,float y){
        const auto config=currentWorkspaceRegions();const auto &id=config.selected[region];if(!isWorkspaceEditor(id))return;
        const auto base=regionControlBase+int(region)*regionControlStride;
        const auto &panel=workspaceEditors[workspaceEditorIndex(id)];
        const std::array<std::wstring,4> labels={L"…",panel.pinned?L"Pinned":workspaceEditorFollowHeld(id)?L"Follow held":L"Following",L"Cursor",L"Return"};
        for(size_t action=0;action<4;++action){const float width=std::array<float,4>{28,70,54,54}[action];
            place(base+int(action)+1,x,y,width,24);ScreamSeq::NativeControls::text(controls.at(base+int(action)+1),labels[action]);x+=width+4;
        }
    }
    template<class Place>void layoutWorkspaceRegionHeader(Place place,size_t region,const ScreamSeq::WorkspaceRect &r){
        const auto config=currentWorkspaceRegions();const auto &id=config.selected[region];const auto base=regionControlBase+int(region)*regionControlStride;
        const bool native=isWorkspaceEditor(id);const float width=native?100.f:144.f;
        place(base,r.x+4,r.y+2,width,24);ScreamSeq::NativeControls::text(controls.at(base),workspaceRegionTitle(id)+L" ▾");
        if(native)layoutWorkspaceRegionActions(place,region,r.x+width+8,r.y+2);
        else {place(base+1,r.x+r.w-88,r.y+2,84,24);ScreamSeq::NativeControls::text(controls.at(base+1),L"Collapse");}
    }
    void drawWorkspaceRegionChrome(){
        if(!hasWorkspaceDock())return;const auto regions=workspaceRegionGeometry();auto &s=*surface;
        if(regions.mode==Regions::Mode::tabs)s.fill(regions.tabs.x,regions.tabs.y,regions.tabs.w,regions.tabs.h,0x202834);
        else for(const auto &host:regions.hosts)if(host.visible){
            const auto &r=host.header;s.fill(r.x,r.y,r.w,r.h,0x202834);s.line(r.x,r.y+r.h-1,r.x+r.w,r.y+r.h-1,0x344151);
        }
        for(const auto &r:{regions.verticalDivider,regions.horizontalDivider})if(r.w>0&&r.h>0)s.fill(r.x,r.y,r.w,r.h,0x344151);
    }
    void workspaceRegionMenu(size_t region,bool placement){
        const auto config=currentWorkspaceRegions();const auto id=config.selected[region];
        if(placement&&!isWorkspaceEditor(id)){workspaceState.lowerVisible=false;revealWorkspacePattern();layoutControls();focusVisibleWorkspaceCanvas();return;}
        const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot open panel menu");
        struct MenuGuard{HMENU menu;~MenuGuard(){DestroyMenu(menu);}}guard{menu};
        std::vector<std::string> choices;
        if(placement)choices={"right","bottom","secondary","float","hide","pin","cursor","return"};
        else {if(region==1)for(const auto name:Regions::mainBottomNames)choices.emplace_back(name);
            for(const auto name:Regions::panelNames)if(Regions::belongs(config,static_cast<Regions::Region>(region),name))choices.emplace_back(name);}
        for(size_t i=0;i<choices.size();++i){
            const std::wstring label=placement?std::array<const wchar_t *,8>{L"Dock right",L"Dock bottom",L"Dock secondary",L"Float",L"Hide",L"Pin target",L"Follow cursor",L"Return to opening position"}[i]:workspaceRegionTitle(choices[i]);
            const bool checked=placement?(i==5?workspaceEditors[workspaceEditorIndex(id)].pinned:workspaceEditors[workspaceEditorIndex(id)].location==choices[i]):config.selected[region]==choices[i];
            AppendMenuW(menu,MF_STRING|(checked?MF_CHECKED:0),UINT_PTR(i+1),label.c_str());
        }
        RECT anchor{};GetWindowRect(controls.at(regionControlBase+int(region)*regionControlStride+(placement?1:0)),&anchor);
        releaseTypedNotes();
        const auto selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,anchor.left,anchor.bottom,window,nullptr);
        if(!selected)return;
        if(config!=currentWorkspaceRegions())throw std::runtime_error("Workspace changed while the panel menu was open");
        const auto &choice=choices.at(size_t(selected-1));
        if(placement){
            if(choice=="pin")workspaceEditorRequest({{"panel",id},{"pinned",!workspaceEditors[workspaceEditorIndex(id)].pinned}});
            else if(choice=="cursor"||choice=="return")workspaceEditorRequest({{"panel",id},{choice=="cursor"?"follow":"return",true}});
            else workspaceEditorRequest({{"panel",id},{"placement",choice},{"focus",choice!="hide"}});
        }
        else if(isWorkspaceEditor(choice))workspaceEditorRequest({{"panel",choice},{"focus",true}});
        else {revealWorkspaceMainEditor(choice);command(choice=="notes"?notesCommand:choice=="samples"?samplesCommand:choice=="effects"?effectsCommand:choice=="plugins"?pluginsCommand:choice=="mixer"?mixerCommand:graphCommand);}
    }
    bool workspaceRegionMouseDown(float x,float y){
        if(!sideWorkspaceDock())return false;const auto geometry=workspaceRegionGeometry();
        if(geometry.verticalDivider.contains(x,y))dragging=10;else if(geometry.horizontalDivider.contains(x,y))dragging=11;else return false;
        const auto config=currentWorkspaceRegions();regionDragRight=config.rightWidth;regionDragBottom=config.bottomHeight;SetCapture(window);return true;
    }
    bool workspaceRegionMouseMove(float x,float y){
        if(dragging!=10&&dragging!=11)return false;RECT client{};GetClientRect(window,&client);const auto scale=96.f/GetDpiForWindow(window);
        if(dragging==10)regionConfiguration.rightWidth=std::clamp(client.right*scale-8-x,440.f,1600.f);
        else regionConfiguration.bottomHeight=std::clamp(client.bottom*scale-44-y,128.f,1600.f);
        layoutControls();ensureCursorVisible();return true;
    }
    bool cancelWorkspaceRegionDrag(){
        if(dragging!=10&&dragging!=11)return false;
        regionConfiguration.rightWidth=regionDragRight;regionConfiguration.bottomHeight=regionDragBottom;dragging=0;ReleaseCapture();layoutControls();return true;
    }
    bool focusNextWorkspaceRegion(){
        if(!hasWorkspaceDock())return false;std::vector<std::pair<std::string,HWND>> regions;
        if(trackerWorkspaceVisible())regions.emplace_back("pattern",window);
        if(workspaceMainEditorVisible(lowerEditorName()))regions.emplace_back(lowerEditorName(),window);
        for(const auto *id:{"instruments","automation","graphCurve"})if(auto *tool=workspaceEditorWindow(id);tool&&tool->visible())regions.emplace_back(id,tool->window());
        if(regions.empty())return true;const auto focused=focusedWorkspaceRegion(GetFocus());
        auto current=std::find_if(regions.begin(),regions.end(),[&](const auto &item){return item.first==focused;});
        auto next=current==regions.end()||++current==regions.end()?regions.begin():current;
        setWorkspaceCanvasFocus(Regions::mainBottom(next->first)?workspaceMainCanvasIdentity():next->first);SetFocus(next->second);return true;
    }
    void openConnectedWorkspace(){
        auto configuration=layoutConfiguration();configuration["layout"]="Compose";configuration["active"]="notes";
        configuration["hidden"]=Json::array({true,true});configuration["lowerVisible"]=true;configuration["lowerEditor"]="graph";
        Regions::Config regions;regions.locations={Regions::Placement::secondary,Regions::Placement::right,Regions::Placement::hidden};
        regions.selected={"instruments","graph","automation"};regions.compactSelection="pattern";configuration["editors"]=Regions::encode(regions);
        restoreLayoutConfiguration(configuration);
    }
    void openGraphEditingWorkspace(){
        auto configuration=layoutConfiguration();configuration["layout"]="Compose";configuration["active"]="notes";
        configuration["hidden"]=Json::array({true,true});configuration["lowerVisible"]=true;configuration["lowerEditor"]="graph";
        Regions::Config regions;regions.locations={Regions::Placement::secondary,Regions::Placement::hidden,Regions::Placement::right};
        regions.selected={"graphCurve","graph","automation"};regions.compactSelection="pattern";configuration["editors"]=Regions::encode(regions);
        restoreLayoutConfiguration(configuration);
    }
    bool workspaceEditorCommand(int id) {
        if(id==connectedWorkspaceCommand){openConnectedWorkspace();return true;}
        if(id==graphEditingWorkspaceCommand){openGraphEditingWorkspace();return true;}
        if(id==openGraphCurveCommand){openGraphCurve();return true;}
        if(id==dockGraphCurveCommand){workspaceEditorRequest({{"panel","graphCurve"},{"placement","secondary"},{"focus",true}});return true;}
        if(id==editorGraphCurveTab){const auto &state=workspaceEditors[workspaceEditorIndex("graphCurve")];workspaceEditorRequest({{"panel","graphCurve"},{"placement",state.location=="hide"?state.preferred:state.location},{"focus",true}});return true;}
        if(id>=regionControlBase&&id<=regionControlEnd){
            const auto region=size_t((id-regionControlBase)/regionControlStride);const auto action=(id-regionControlBase)%regionControlStride;
            if(action<2){workspaceRegionMenu(region,action==1);return true;}
            if(action>4)return true;
            const auto panel=currentWorkspaceRegions().selected[region];if(!isWorkspaceEditor(panel))return true;
            Json request={{"panel",panel}};if(action==2)request["pinned"]=!workspaceEditors[workspaceEditorIndex(panel)].pinned;
            else if(action==3)request["follow"]=true;else if(action==4)request["return"]=true;workspaceEditorRequest(request);return true;
        }
        if(id==dockAutomationCommand||id==dockInstrumentCommand){workspaceEditorRequest({{"panel",id==dockAutomationCommand?"automation":"instruments"},{"placement","right"},{"focus",true}});return true;}
        if(id<editorTrackerTab||id>editorReturnCommand)return false;
        if(id==editorTrackerTab){revealWorkspacePattern();workspaceState.focus="pattern";layoutControls();SetFocus(window);return true;}
        if(id==editorAutomationTab||id==editorInstrumentTab){const std::string panel=id==editorAutomationTab?"automation":"instruments";const auto &state=workspaceEditors[workspaceEditorIndex(panel)];workspaceEditorRequest({{"panel",panel},{"placement",state.location=="hide"?state.preferred:state.location},{"focus",true}});return true;}
        Json p={{"panel",activeWorkspaceEditor}};
        if(id==editorFloatCommand){p["placement"]="float";p["focus"]=true;}else if(id==editorHideCommand)p["placement"]="hide";
        else if(id==editorPinCommand)p["pinned"]=!workspaceEditors[workspaceEditorIndex(activeWorkspaceEditor)].pinned;
        else if(id==editorCursorCommand)p["follow"]=true;else p["return"]=true;
        workspaceEditorRequest(p);return true;
    }
