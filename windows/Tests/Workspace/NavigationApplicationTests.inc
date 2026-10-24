void applicationNavigation(){
    RestoreFixture fixture;auto &app=*fixture.app;
    ScreamSeq::Tests::ownGuiWindow(app.window);resizeRestoreClient(app,1440,900);
    app.workspaceState.lowerVisible=false;app.workspaceState.panels[0].hidden=true;app.layoutControls();
    const auto song=songState(app),position=app.position(),selection=app.selection();
    app.command(mixingGraphPanelCommand);auto *owner=app.songRoutingWindow.get();
    restoreCheck(owner&&owner->docked()&&owner->visible()&&app.trackerWorkspaceVisible(),"Direct mixing graph route must show the graph beside Pattern");
    const auto handle=owner->window();ScreamSeq::Tests::ownGuiWindow(handle);
    restoreCheck(app.workspaceSnapshot().at("locations").at("mixingGraph")=="right","API does not describe the actual graph placement");
    // Expand the compact inspector and retain an intentionally incomplete raw
    // value through float/dock, same-owner restore and Pattern return.
    SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3846,BN_CLICKED),LPARAM(GetDlgItem(handle,3846)));
    const auto gain=GetDlgItem(handle,3807);restoreCheck(IsWindowVisible(gain),"Compact graph inspector did not expose routing gain");
    const auto kind=GetDlgItem(handle,3804);SendMessageW(kind,CB_SETCURSEL,1,0);SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3804,CBN_SELCHANGE),LPARAM(kind));
    SetFocus(gain);restoreCheck(GetFocus()==gain,"Compact routing gain cannot receive keyboard focus");
    const auto rawField=[&]{auto value=controlState(gain);value.erase("bounds");value.erase("visible");value.erase("enabled");return value;};
    SetWindowTextW(gain,L"-.");SendMessageW(gain,EM_SETSEL,1,2);
    const auto raw=rawField(),captured=owner->snapshot();
    app.command(mixingGraphFloatCommand);
    restoreCheck(app.songRoutingWindow.get()==owner&&!owner->docked()&&owner->visible()&&rawField()==raw&&GetFocus()==gain,"Float replaced the graph owner or lost its raw field/caret/focus");
    app.command(mixingGraphPanelCommand);
    restoreCheck(owner->docked()&&app.songRoutingWindow.get()==owner&&rawField()==raw&&GetFocus()==gain,"Panel action followed remembered float placement or lost its draft/focus");
    const auto layout=app.layoutConfiguration();app.restoreLayoutConfiguration(layout);
    restoreCheck(app.songRoutingWindow.get()==owner&&rawField()==raw&&owner->snapshot().at("generation")==captured.at("generation"),"Saved layout restore reloaded the retained routing draft");
    app.command(navigationPatternCommand);
    auto returnedLayout=layout;returnedLayout["editors"]["compactSelection"]="pattern";
    restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="pattern"&&app.layoutConfiguration()==returnedLayout&&app.position()==position&&app.selection()==selection,"Pattern return changed layout/cursor/selection or missed keyboard focus");
    restoreCheck(songState(app)==song,"Navigation changed music or document history");
    // Compact fallback must keep its graph tab accessible and restore Pattern.
    resizeRestoreClient(app,900,600);app.command(mixingGraphPanelCommand);
    restoreCheck(owner->visible()&&IsWindowVisible(app.controls.at(editorMixingGraphTab)),"Narrow workspace lost its selected graph tab");
    app.command(navigationPatternCommand);restoreCheck(app.trackerWorkspaceVisible()&&GetFocus()==app.window,"Pattern could not be reached from compact graph");
    restoreCheck(rawField()==raw,"Compact navigation discarded routing text/caret");
    fixture.close();
}

void applicationRoutingRestoreAtomicity(){
    RestoreFixture fixture;auto &app=*fixture.app;auto next=app.layoutConfiguration();
    next["editors"]["locations"]["mixingGraph"]="right";next["editors"]["selected"]["right"]="mixingGraph";
    next["editors"]["compactSelection"]="mixingGraph";
    const auto before=app.layoutConfiguration(),song=songState(app);
    app.targetRead="graph.get";app.fault=RestoreApplication::Fault::read;
    restoreRejected([&]{app.restoreLayoutConfiguration(next);});
    restoreCheck(!app.songRoutingWindow&&app.layoutConfiguration()==before&&songState(app)==song,"Failed graph preparation partially adopted a layout/owner");
    app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(next);
    restoreCheck(app.songRoutingWindow&&app.songRoutingWindow->docked()&&app.songRoutingWindow->visible(),"Prepared graph was not adopted into the selected region");
    ScreamSeq::Tests::ownGuiWindow(app.songRoutingWindow->window());fixture.close();
}

void applicationPatternHeaderLayout(){
    RestoreFixture fixture;auto &app=*fixture.app;ScreamSeq::Tests::ownGuiWindow(app.window);
    resizeRestoreClient(app,1300,720);app.command(mixingGraphPanelCommand);app.command(navigationPatternCommand);
    ScreamSeq::Tests::ownGuiWindow(app.songRoutingWindow->window());
    const auto original=songState(app),position=app.position(),selection=app.selection();
    const auto checkBounds=[&]{
        const auto grid=app.patternGridRect();const float scale=GetDpiForWindow(app.window)/96.f;
        std::vector<RECT> visible;
        for(int id:{effectColumnChooser,graphCommandsCommand,noteColumnMuteCommand,effectsCommand,scratchGesturesCommand,patternHeaderMenuCommand}){
            const auto control=app.controls.at(id);if(!IsWindowVisible(control))continue;
            RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT *>(&bounds),2);
            std::cout<<"Pattern header control "<<id<<" bounds "<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom
                <<" grid "<<grid.x<<","<<grid.y<<","<<grid.w<<" scale "<<scale<<"\n";
            restoreCheck(bounds.left>=int(grid.x*scale)-1&&bounds.right<=int((grid.x+grid.w)*scale)+1&&
                bounds.top>=int(grid.y*scale)&&bounds.bottom<=int((grid.y+27)*scale)+1,
                "Pattern header control escaped its grid into another panel or row");
            for(const auto &other:visible){RECT overlap{};restoreCheck(!IntersectRect(&overlap,&bounds,&other),"Pattern header controls overlap");}
            visible.push_back(bounds);
        }
        restoreCheck(app.patternHeaderTextWidth()>0,"Pattern header lost all identity-label space");
    };
    checkBounds();restoreCheck(IsWindowVisible(app.controls.at(patternHeaderMenuCommand)),"Compact Pattern header lacks its action menu");
    const auto items=app.patternHeaderItems();
    for(int id:{noteColumnMuteCommand,effectsCommand,graphCommandsCommand,scratchGesturesCommand})
        restoreCheck(app.workspaceMenuAllows(items,id),"Compact Pattern header lost an ordinary action");
    const auto stale=app.patternHeaderTarget();auto moved=app.position();moved["row"]=3;app.navigate(moved);
    restoreRejected([&]{app.dispatchPatternHeaderAction(stale,app.patternHeaderColumnsLast);});
    restoreCheck(songState(app)==original,"Stale compact header menu changed another target");app.navigate(position);
    const auto count=app.view->effectColumns.at(app.channel);const unsigned changed=count==8?7:count+1;
    app.dispatchPatternHeaderAction(app.patternHeaderTarget(),app.patternHeaderColumnsFirst+int(changed)-1);
    restoreCheck(app.view->effectColumns.at(app.channel)==changed,"Compact header FX count did not use the ordinary shared edit");
    app.edit("history.undo",RestoreJson::object());
    restoreCheck(app.view->effectColumns.at(app.channel)==count,"Compact header FX count was not one reversible edit");
    app.command(mixingGraphFloatCommand);resizeRestoreClient(app,1600,950);checkBounds();
    restoreCheck(IsWindowVisible(app.controls.at(noteColumnMuteCommand))&&!IsWindowVisible(app.controls.at(patternHeaderMenuCommand)),
        "Wide Pattern header did not restore its direct actions");
    SetFocus(app.controls.at(noteColumnMuteCommand));app.command(mixingGraphPanelCommand);resizeRestoreClient(app,1300,720);checkBounds();
    // Explicitly focused controls hidden by a subsequent layout must yield to
    // the same header's visible replacement, never an invisible native child.
    app.command(mixingGraphFloatCommand);resizeRestoreClient(app,1600,950);SetFocus(app.controls.at(scratchGesturesCommand));
    resizeRestoreClient(app,1000,720);checkBounds();
    restoreCheck(IsWindowVisible(GetFocus()),"Header resize left keyboard focus on a hidden action");
    restoreCheck(app.position()==position&&app.selection()==selection,"Header presentation changed Pattern cursor or selection");
    fixture.close();
}

void applicationNavigationResizeAndDestinations(){
    RestoreFixture fixture;auto &app=*fixture.app;
    resizeRestoreClient(app,1300,720);app.command(navigationPluginsCommand);
    const auto lower=app.lowerEditorName();restoreCheck(app.workspaceState.lowerVisible,"Navigation fixture needs a visible lower panel");
    app.edit("mixer.enable",RestoreJson::object());const auto song=songState(app);
    app.command(mixingGraphPanelCommand);auto *owner=app.songRoutingWindow.get();
    restoreCheck(owner&&app.trackerWorkspaceVisible()&&owner->visible()&&owner->docked()&&!app.workspaceState.lowerVisible&&app.lowerEditorName()==lower,
        "Default graph action did not preserve Pattern beside the graph and retain the collapsed lower page");
    const auto handle=owner->window();ScreamSeq::Tests::ownGuiWindow(handle);
    const auto click=[&](int id){SendMessageW(handle,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(handle,id)));};
    click(3848); // Readable view; the View button opens the native choice menu.
    const auto centre=[&]{const auto canvas=owner->snapshot().at("canvas");const auto viewport=canvas.at("viewport");
        restoreCheck(viewport[2]>0&&viewport[3]>0&&!canvas.at("nodes").empty(),"Viewport fixture lost its canvas");
        const auto &node=canvas.at("nodes")[0];const auto zoom=canvas.at("zoom").get<float>();
        return std::array<float,3>{node.at("x").get<float>()+(viewport[0].get<float>()+viewport[2].get<float>()/2-node.at("rect")[0].get<float>())/zoom,
            node.at("y").get<float>()+(viewport[1].get<float>()+viewport[3].get<float>()/2-node.at("rect")[1].get<float>())/zoom,zoom};};
    const auto expected=centre();
    const auto sameCentre=[&]{const auto actual=centre();for(size_t i=0;i<actual.size();++i)restoreCheck(std::abs(actual[i]-expected[i])<.02f,"Graph resize/dock changed the world-space center or zoom");};
    resizeRestoreClient(app,1600,950);sameCentre();
    app.command(mixingGraphFloatCommand);sameCentre();
    app.command(mixingGraphPanelCommand);sameCentre();
    restoreCheck(owner->snapshot().at("compact")==true,"Expected compact docked graph");
    click(3846);restoreCheck(owner->snapshot().at("inspector")==true,"Inspector was not opened");
    resizeRestoreClient(app,1300,720);click(3846);sameCentre();
    app.command(mixingGraphFloatCommand);app.command(mixerRouting);
    restoreCheck(app.songRoutingWindow.get()==owner&&owner->docked(),"Mixer route followed remembered floating placement");
    app.command(navigationPatternCommand);const auto position=app.position();
    // Float is explicit even after a previous docked placement, and the lower
    // Precise notes tab must dock the same retained owner again.
    app.command(floatNotesCommand);auto *notes=app.preciseNoteWindow.get();
    restoreCheck(notes&&notes->visible()&&!notes->docked(),"Precise notes window command did not float");
    app.selectDockTab(dockNotes);
    restoreCheck(app.preciseNoteWindow.get()==notes&&notes->docked()&&notes->visible()&&app.workspaceSnapshot().at("locations").at("preciseNotes")=="bottom",
        "Precise notes panel tab reopened a floating window or replaced its owner");
    app.command(navigationPatternCommand);
    restoreCheck(app.position()==position&&GetFocus()==app.window&&songState(app)==song,"Presentation navigation changed music, cursor or Pattern focus");
    fixture.close();
}

void applicationDenseRoutingFraming(){
    RestoreFixture fixture;auto &app=*fixture.app;resizeRestoreClient(app,1300,720);
    app.edit("mixer.enable",RestoreJson::object());
    const auto plugin=addRestoreGain(app),target="plugin:"+plugin;
    app.edit("graph.layout.set",{{"positions",RestoreJson::array({{{"node",target},{"x",100000},{"y",100000}}})}});
    const auto song=songState(app),graph=app.documentOperation("graph.get",{{"includeState",false}});
    app.command(mixingGraphPanelCommand);auto *owner=app.songRoutingWindow.get();
    restoreCheck(owner&&owner->docked(),"Dense graph did not open beside Pattern");
    const auto handle=owner->window();ScreamSeq::Tests::ownGuiWindow(handle);
    const auto canvas=[&]{return owner->snapshot().at("canvas");};
    const auto contained=[&](const RestoreJson &n){const auto c=canvas(),v=c.at("viewport"),r=n.at("rect");
        return r[0].get<float>()>=v[0].get<float>()-.1f&&r[1].get<float>()>=v[1].get<float>()-.1f&&
            r[0].get<float>()+r[2].get<float>()<=v[0].get<float>()+v[2].get<float>()+.1f&&
            r[1].get<float>()+r[3].get<float>()<=v[1].get<float>()+v[3].get<float>()+.1f;};
    restoreCheck(canvas().at("zoom")>=.75f&&contained(canvas().at("nodes")[0]),"Dense graph opened as unreadable or offscreen nodes");
    SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3849,BN_CLICKED),0);
    restoreCheck(canvas().at("zoom")<.15f,"Dense overview fixture did not exercise the old clipped zoom floor");
    const auto nodes=canvas().at("nodes");for(const auto &n:nodes)restoreCheck(contained(n),"Whole overview clips a saved node");
    const auto found=std::find_if(nodes.begin(),nodes.end(),[&](const auto &n){return n.at("id")==target;});
    restoreCheck(found!=nodes.end(),"Dense routing target missing");
    const auto picker=GetDlgItem(handle,3802);SendMessageW(picker,CB_SETCURSEL,found-nodes.begin(),0);
    SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3802,CBN_SELCHANGE),LPARAM(picker));
    SetFocus(handle);SendMessageW(handle,WM_KEYDOWN,VK_HOME,0);SendMessageW(handle,WM_KEYUP,VK_HOME,0);
    const auto focused=canvas();restoreCheck(focused.at("zoom")==1&&owner->snapshot().at("selected")==target&&GetFocus()==handle,"Home did not focus the retained selected stage");
    for(const auto &n:focused.at("nodes"))if(n.at("id")==target)restoreCheck(contained(n)&&n.at("rect")[2]>=184,"Selected stage is not readable inside the viewport");
    app.command(mixingGraphFloatCommand);app.command(mixingGraphPanelCommand);
    restoreCheck(canvas()==focused,"Float/dock lost dense graph framing");
    // Shrink a centered stage until both fixed socket hit regions cover its
    // body. An overview click must select it, never start a cable/layout edit.
    for(unsigned step=0;step<10;++step)SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3817,BN_CLICKED),0);
    SendMessageW(picker,CB_SETCURSEL,0,0);SendMessageW(handle,WM_COMMAND,MAKEWPARAM(3802,CBN_SELCHANGE),LPARAM(picker));
    restoreCheck(owner->snapshot().at("selected")!=target,"Overview click fixture already selected its target");
    const auto overviewSnapshot=canvas();RestoreJson rect;
    for(const auto &n:overviewSnapshot.at("nodes"))if(n.at("id")==target)rect=n.at("rect");
    restoreCheck(!rect.is_null()&&rect[2].get<float>()<20,"Overview socket-overlap fixture is not small enough");
    const auto dpi=GetDpiForWindow(handle)/96.f;
    const auto point=MAKELPARAM(int(std::lround((rect[0].get<float>()+rect[2].get<float>()/2)*dpi)),
        int(std::lround((rect[1].get<float>()+rect[3].get<float>()/2)*dpi)));
    SendMessageW(handle,WM_LBUTTONDOWN,MK_LBUTTON,point);
    SendMessageW(handle,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(GET_X_LPARAM(point)+20,GET_Y_LPARAM(point)+20));
    SendMessageW(handle,WM_LBUTTONUP,0,point);
    restoreCheck(owner->snapshot().at("selected")==target&&!owner->snapshot().at("draft").get<bool>()&&
        !owner->snapshot().at("layoutDraft").get<bool>()&&canvas()==overviewSnapshot,"Overview body drag created a cable/layout edit or missed selection");
    SendMessageW(handle,WM_LBUTTONDBLCLK,MK_LBUTTON,point);SendMessageW(handle,WM_LBUTTONUP,0,point);
    restoreCheck(canvas().at("zoom")==1&&owner->snapshot().at("selected")==target,"Overview double-click did not focus readable stage");
    restoreCheck(songState(app)==song&&app.documentOperation("graph.get",{{"includeState",false}})==graph,"View framing changed saved music, layout or history");
    fixture.close();
}
