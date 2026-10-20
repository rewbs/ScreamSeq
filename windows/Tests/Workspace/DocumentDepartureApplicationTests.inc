RestoreJson departureOpenRequest(RestoreApplication &app,const std::filesystem::path &path) {
    return {{"expectedRevision",app.view->session.revision},{"path",utf8Path(path)},{"discard",true}};
}
template<class Action>void requireDepartureRefusal(Action action) {
    try{action();}catch(const ScreamSeq::Api::ApiError &error){
        restoreCheck(error.code==-32002&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted&&
            std::string(error.what()).find("Document replacement is finishing")!=std::string::npos,"Expected the typed departure-input refusal, not validation or ordinary worker busy");return;
    }
    throw std::runtime_error("Departure input was accepted");
}
std::filesystem::path saveDepartureFixture(RestoreFixture &fixture) {
    const auto path=fixture.folder/L"departure.screamseq";auto &app=*fixture.app;
    app.documentOperation("document.save",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(path)},{"overwrite",true}});
    return path;
}
void beginDepartureDraft(RestoreApplication &app) {
    auto cursor=app.position();cursor["row"]=4;cursor["column"]=3;cursor["following"]=false;app.navigate(cursor);
    app.revealWorkspacePattern();app.workspaceState.focus="pattern";app.layoutControls();
    restoreCheck(app.beginPatternNudgeEdit("nudge-forward"),"Create departure nudge draft");
    rawDraftText(app.controls.at(app.nudgeStrengthField),L"-");
}
void applicationNewAndDemo() {
    for(bool demo:{false,true}) {
        RestoreFixture fixture;auto &app=*fixture.app;beginDepartureDraft(app);
        const auto draft=census(app);const auto song=songState(app);const auto identity=app.documentId;const auto stopped=app.stopGeneration;
        const auto field=app.controls.at(app.nudgeStrengthField);SetFocus(field);
        const auto params=RestoreJson{{"expectedRevision",app.view->session.revision},{"demo",demo},{"discard",true}};
        const auto refusal=restoreRejected([&]{app.documentOperation("document.new",params);});
        restoreCheck(refusal.find("native editor work")!=std::string::npos&&census(app).sameWork(draft)&&songState(app)==song,
            "API New/Demo bypassed raw native draft protection");
        app.departureChoice=IDCANCEL;app.command(demo?openDemoCommand:newDocumentCommand);
        restoreCheck(app.documentId==identity&&census(app).sameWork(draft)&&GetFocus()==field&&app.stopGeneration==stopped,
            "Cancel New/Demo lost the document, raw fields, focus or playback");
        app.departureChoice=1002;app.failDepartureStop=true;
        restoreRejected([&]{app.newFile(demo);});app.failDepartureStop=false;
        restoreCheck(app.documentId==identity&&census(app).sameWork(draft)&&!app.nativeDepartureConsent&&!app.departureInput,
            "Failed New/Demo Stop consumed drafts or retained discard authority");
        app.beforeDepartureAdmission=[&]{rawDraftText(field,L"-.");};
        restoreRejected([&]{app.newFile(demo);});
        restoreCheck(app.documentId==identity&&restoreText(field)==L"-."&&!app.nativeDepartureConsent,
            "New/Demo reused discard consent after newer raw input");
        app.command(demo?openDemoCommand:newDocumentCommand);
        restoreCheck(app.documentId!=identity&&census(app).drafts.empty()&&!app.patternNudge.active&&!app.departureInput&&
            !app.nativeDepartureConsent&&app.view->path.empty()&&app.view->session.document.at("canUndo")==false,
            "Successful native New/Demo failed to retire owners and start independent history");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;
        const auto take=app.documentOperation("recording.start",{{"expectedRevision",app.view->session.revision},{"channels",{0}},{"instrument",1}}).at("take");
        const auto identity=app.documentId;app.newFile();
        restoreCheck(app.documentId==identity&&app.view->recording.at("take")==take,"Native New discarded a retained MIDI take");
        app.documentOperation("recording.discard",{{"expectedRevision",app.view->session.revision},{"take",take}});
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;const auto path=fixture.folder/L"pre-new-shortcuts.json";
        const RestoreJson saved={{"version",1},{"overrides",{{app.shortcutCommandID(inputOctaveUpCommand),{"ctrl+n"}}}}};
        {std::ofstream file(path);file<<saved.dump();restoreCheck(bool(file),"Write previous user shortcut fixture");}
        app.loadWorkspaceShortcuts(path);
        restoreCheck(app.workspaceShortcuts->keys(app.shortcutCommandID(newDocumentCommand)).empty()&&
            app.workspaceShortcuts->keys(app.shortcutCommandID(inputOctaveUpCommand))==std::vector<std::string>{"ctrl+n"},
            "New Ctrl+N default displaced an existing saved user binding");
    }
}

void applicationDepartureRefusalAndAdoption() {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);
    beginDepartureDraft(app);app.openSongTiming();const auto timing=app.songTimingWindow->window();ScreamSeq::Tests::ownGuiWindow(timing);
    rawDraftText(GetDlgItem(timing,7602),L"-");
    const auto original=census(app);const auto song=songState(app);const auto doc=app.documentId;const auto stops=app.stopGeneration;
    const auto refused=restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    restoreCheck(refused.find("native editor work")!=std::string::npos&&app.documentId==doc&&songState(app)==song&&census(app).sameWork(original)&&app.stopGeneration==stops,
        "API discard bypassed retained drafts or stopped playback before admission");
    app.departureChoice=IDCANCEL;restoreCheck(!app.reviewNativeDeparture()&&census(app).sameWork(original),"Cancel changed draft identity");
    app.departureChoice=1001;restoreCheck(!app.reviewNativeDeparture()&&census(app).sameWork(original),"Review consumed or changed a draft");
    app.departureChoice=1002;restoreCheck(app.reviewNativeDeparture(),"Native discard review did not authorize the exact snapshot");
    app.beforeDepartureAdmission=[&]{rawDraftText(app.controls.at(app.nudgeStrengthField),L"-.");};
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    restoreCheck(app.documentId==doc&&!app.departureInput&&IsWindow(timing)&&restoreText(app.controls.at(app.nudgeStrengthField))==L"-.","Late raw input did not revoke native discard consent");

    restoreCheck(app.reviewNativeDeparture(),"Review newer raw draft");app.failDepartureStop=true;
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});app.failDepartureStop=false;
    restoreCheck(app.documentId==doc&&!app.departureInput&&!app.documentDrafts.departing()&&IsWindow(timing)&&restoreText(GetDlgItem(timing,7602))==L"-",
        "Failed Stop retired native owners or leaked the admission lease");

    restoreCheck(app.reviewNativeDeparture(),"Review after failed Stop");bool guarded=false;
    app.beforeDepartureAdmission=[&]{app.deferredViews.push_back({mixerRouting,app.documentId,app.viewCommandTarget(mixerRouting)});};
    app.duringDepartureAdmission=[&]{
        const auto field=app.controls.at(app.nudgeStrengthField);const auto raw=restoreText(field);
        SetWindowTextW(field,L"late text");SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));
        SendMessageW(GetDlgItem(timing,7602),WM_CHAR,'1',0);
        restoreCheck(restoreText(field)==raw&&restoreText(GetDlgItem(timing,7602))==L"-","Final admission did not guard Main and floating raw controls");
        requireDepartureRefusal([&]{app.documentOperation("transport.note",RestoreJson::object());});
        requireDepartureRefusal([&]{app.workspace("workspace.input",RestoreJson::object());});
        requireDepartureRefusal([&]{app.navigate(app.position());});
        requireDepartureRefusal([&]{app.independentOperation("midi.settings.set",RestoreJson::object());});
        guarded=app.documentDrafts.departing()&&bool(app.departureInput);
    };
    app.documentOperation("document.open",departureOpenRequest(app,path));
    restoreCheck(guarded&&app.documentId!=doc&&!app.departureInput&&!app.documentDrafts.departing()&&!IsWindow(timing)&&!app.songTimingWindow,
        "Successful Open did not finish admission, native retirement and C++ owner cleanup");
    restoreCheck(census(app).drafts.empty()&&!app.patternNudge.active&&app.view==app.controller->view(),"Old captured drafts survived adopted refresh");
    restoreCheck(app.deferredViews.empty()&&app.status.find(L"View target changed")!=std::wstring::npos,
        "Document replacement silently discarded a queued view without explaining its changed target");
    app.openSongTiming();ScreamSeq::Tests::ownGuiWindow(app.songTimingWindow->window());
    restoreCheck(app.songTimingWindow->documentDraft()->document==app.documentId,"Reopened native editor captured the previous song");
    fixture.close();
}
void applicationDepartureRefreshFailure() {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);const auto old=app.documentId;
    app.failDepartureRefresh=true;
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    restoreCheck(app.controller->view()->session.documentId!=old&&app.departureAdopted&&app.departureInput&&app.documentDrafts.departing(),
        "Post-adoption refresh failure released input or rolled the model back");
    const auto current=app.snapshot();restoreCheck(current.documentId==app.controller->view()->session.documentId&&current.context.at("nativeRefreshPending")==true,
        "Readback reported the old song after adoption");
    requireDepartureRefusal([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    requireDepartureRefusal([&]{app.play(RestoreJson::object());});
    const auto recoveryStatus=app.status;app.stop();
    restoreCheck(app.status==recoveryStatus,"Stop hid the retained native-refresh recovery action");
    SendMessageW(app.window,WM_ENDSESSION,FALSE,0);
    restoreCheck(bool(app.departureInput),"Unapproved shutdown cancellation released adopted-refresh protection");
    const auto id=app.controller->view()->session.documentId;app.failDepartureRefresh=false;
    MSG retry{};retry.hwnd=app.window;retry.message=WM_KEYDOWN;retry.wParam=VK_F5;
    restoreCheck(app.filterDepartureMessage(retry)&&!app.departureInput&&!app.documentDrafts.departing()&&app.documentId==id&&app.view==app.controller->view(),
        "F5 did not finish the adopted refresh without repeating Open");
    retry.lParam=LPARAM(1)<<30;
    restoreCheck(app.filterDepartureMessage(retry),"Held retry key escaped into normal transport shortcuts");
    retry.message=WM_KEYUP;
    restoreCheck(app.filterDepartureMessage(retry),"Retry release escaped into musical key handling");
    retry.message=WM_KEYDOWN;retry.lParam=0;
    restoreCheck(!app.filterDepartureMessage(retry),"A new F5 press stayed blocked after recovery");
    fixture.close();
}
void applicationDepartureSessionCancel() {
    RestoreFixture fixture;auto &app=*fixture.app;(void)saveDepartureFixture(fixture);beginDepartureDraft(app);
    const auto before=census(app);const auto song=songState(app);
    app.departureChoice=IDCANCEL;restoreCheck(!app.canClose()&&!app.departureInput&&census(app).sameWork(before),"Close Cancel lost native work");
    app.departureChoice=1002;
    restoreCheck(SendMessageW(app.window,WM_QUERYENDSESSION,0,0)==TRUE&&app.departureClosing&&app.departureInput,"Shutdown query did not acquire final admission");
    SendMessageW(app.controls.at(app.nudgeStrengthField),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"late"));
    SendMessageW(app.window,WM_ENDSESSION,FALSE,0);
    restoreCheck(!app.departureInput&&!app.documentDrafts.departing()&&census(app).sameWork(before)&&songState(app)==song&&restoreText(app.controls.at(app.nudgeStrengthField))==L"-",
        "Canceled OS shutdown lost raw drafts, song state or input availability");
    fixture.close();
}

void applicationDepartureRecoveryBrowser() {
    RestoreFixture fixture;auto &app=*fixture.app;
    app.configureRecovery(fixture.folder/L"recovery");
    const auto saved=app.recoveryOperation("recovery.save",{{"expectedRevision",app.view->session.revision}});
    const auto copy=saved.at("data").at("lastCopy");
    app.openRecoveryBrowser();const auto browser=app.recoveryWindow->window();ScreamSeq::Tests::ownGuiWindow(browser);
    const auto list=GetDlgItem(browser,7001);
    restoreCheck(ListView_GetItemCount(list)==1,"Recovery fixture did not publish its saved copy");
    ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    beginDepartureDraft(app);const auto before=census(app);const auto old=app.documentId;
    const auto refused=restoreRejected([&]{app.recoveryOperation("recovery.restore",{{"expectedRevision",app.view->session.revision},{"id",copy}});});
    restoreCheck(refused.find("native editor work")!=std::string::npos&&app.documentId==old&&census(app).sameWork(before),
        "API recovery discarded native work without review");
    app.departureChoice=IDCANCEL;
    SendMessageW(browser,WM_COMMAND,7004,reinterpret_cast<LPARAM>(GetDlgItem(browser,7004)));
    restoreCheck(app.documentId==old&&census(app).sameWork(before),"Native recovery Cancel lost work");
    app.departureChoice=1002;
    SendMessageW(browser,WM_COMMAND,7004,reinterpret_cast<LPARAM>(GetDlgItem(browser,7004)));
    restoreCheck(app.documentId!=old&&!app.departureInput&&!app.documentDrafts.departing()&&census(app).drafts.empty(),
        "Native recovery did not retire reviewed work and release admission");
    restoreCheck(app.recoveryWindow&&app.recoveryWindow->window()==browser&&IsWindow(browser)&&ListView_GetItemCount(list)==1,
        "Recovery destroyed its own active browser or lost the selected copy report");
    fixture.close();
}
