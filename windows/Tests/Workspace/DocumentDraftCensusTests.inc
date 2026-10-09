// Actual Application/NativeToolWindow registrations, real EDIT notifications,
// retained children and the real worker. Token-only tests cannot prove these
// owners describe their raw fields or stay registered when hidden/reparented.
Tracker::DocumentDepartureSnapshot census(RestoreApplication &app) {
    return app.documentDrafts.capture(app.documentId,app.view->session.revision);
}
Tracker::DocumentDraft namedDraft(const Tracker::DocumentDepartureSnapshot &snapshot,const std::string &name) {
    const auto found=std::find_if(snapshot.drafts.begin(),snapshot.drafts.end(),[&](const auto &draft){return draft.name==name;});
    restoreCheck(found!=snapshot.drafts.end(),("Missing native draft owner: "+name).c_str());return *found;
}
void rawDraftText(HWND field,const wchar_t *text) {
    restoreCheck(field!=nullptr,"Native draft control missing");SetFocus(field);
    SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text));
    restoreCheck(restoreText(field)==text,"Native draft control did not retain raw input");
}
void nativeDraftCensusRetainsRawOwners() {
    using Status=Tracker::DocumentDeparture::Status;
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);const auto source=createRestoreCurveSource(app);
        const auto saved=app.documentOperation("graph.automation.get",curveRequest(source));
        app.restoreLayoutConfiguration(curveLayout(app,true));
        restoreCheck(census(app).drafts.empty(),"Opening clean native editors must not create song drafts");
        std::vector<std::pair<ScreamSeq::NativeToolWindow *,int>> editors={
            {app.preciseNoteWindow.get(),ScreamSeq::PreciseNoteWindow::velocity},
            {app.graphCurveWindow.get(),ScreamSeq::GraphCurveWindow::pointValue},
            {app.instrumentEnvelopeWindow.get(),4414},{app.parameterAutomationWindow.get(),4208}};
        for(const auto &[owner,id]:editors) {
            ScreamSeq::Tests::ownGuiWindow(owner->window());const auto before=census(app);
            rawDraftText(GetDlgItem(owner->window(),id),L"-");
            const auto after=census(app);const auto draft=namedDraft(after,restoreUtf8(restoreText(owner->window())));
            restoreCheck(after.drafts.size()==before.drafts.size()+1&&draft.dirty&&draft.document==app.documentId&&!draft.target.empty()&&!draft.revision.empty(),
                "Invalid raw native fields must enter the census with captured identity");
        }
        const auto song=songState(app),all=allControlState(app);const auto focus=GetFocus();
        const auto captured=census(app);
        restoreCheck(census(app).sameWork(captured)&&GetFocus()==focus&&allControlState(app)==all&&songState(app)==song,
            "Reading native draft summaries changed focus, controls, history or work identity");
        restoreCheck(app.documentDrafts.authorize(captured).status==Status::NeedsReview,"Raw native drafts must require explicit review");
        const auto curveName=restoreUtf8(restoreText(app.graphCurveWindow->window()));
        const auto curveDraft=namedDraft(captured,curveName);
        app.workspaceEditorRequest({{"panel","graphCurve"},{"placement","hide"}});
        restoreCheck(!app.graphCurveWindow->visible()&&census(app).sameWork(captured),"Hiding a native draft removed or changed its census entry");
        app.workspaceEditorRequest({{"panel","graphCurve"},{"placement","float"},{"focus",true}});
        restoreCheck(census(app).sameWork(captured),"Reparenting a retained owner changed its work identity");
        const auto token=app.documentDrafts.authorize(captured,true);
        rawDraftText(GetDlgItem(app.graphCurveWindow->window(),ScreamSeq::GraphCurveWindow::pointValue),L"-.");
        restoreCheck(app.documentDrafts.admit(*token.token,app.documentId,app.view->session.revision).status==Status::Changed,
            "New raw input after review must invalidate departure consent");
        const auto parent=app.graphCurveWindow->documentDraft();const auto parentState=app.graphCurveWindow->operationGuard();
        const auto original=census(app);
        uint64_t childID=0;
        {
            ScreamSeq::FormulaWorkbenchWindow formula(app.graphCurveWindow->window(),L"Census formula","mix(start,end,t)",
                RestoreJson{{"points",saved.at("points")},{"rows",saved.at("rows")},{"rowsPerBeat",saved.at("rowsPerBeat")}},0,
                [&](const auto &method,const auto &params){return app.documentOperation(method,params);},[]{return true;},[](const auto &){return false;});
            ScreamSeq::Tests::ownGuiWindow(formula.window());formula.show();
            restoreCheck(census(app).sameWork(original),"Clean formula child must not create a song draft");
            rawDraftText(GetDlgItem(formula.window(),2001),L"sin(");
            const auto child=namedDraft(census(app),"Census formula");childID=child.owner;
            restoreCheck(child.document==parent->document&&child.revision==parent->revision&&
                RestoreJson::parse(child.target)==RestoreJson::array({parent->target,0})&&child.dirty,
                "Nested formula must retain its parent's captured target and point");
            formula.hide();const auto hidden=census(app);
            restoreCheck(!formula.visible()&&namedDraft(hidden,"Census formula").owner==childID,"Hidden formula draft lost its registration");
            restoreCheck(app.documentDrafts.review(childID)&&formula.visible()&&formula.snapshot().at("source")=="sin(",
                "Review must reveal the same child and preserve invalid formula text");
            restoreCheck(app.graphCurveWindow->operationGuard()==parentState&&songState(app)==song,"Child review changed parent draft or song");
        }
        restoreCheck(census(app).sameWork(original)&&!app.documentDrafts.review(childID),"Destroyed child registration survived its HWND owner");
        {
            ScreamSeq::FormulaWorkbenchWindow reference(app.graphCurveWindow->window(),L"Census reference","",
                RestoreJson::object(),-1,[&](const auto &method,const auto &params){return app.documentOperation(method,params);});
            ScreamSeq::Tests::ownGuiWindow(reference.window());reference.show();
            SetWindowTextW(GetDlgItem(reference.window(),2002),L"sin");
            restoreCheck(census(app).sameWork(original),"Reference search must not be classified as a musical draft");
        }
        const auto live=namedDraft(census(app),curveName);
        restoreCheck(live.owner==curveDraft.owner,"Hide/show must preserve owner incarnation");
        app.graphCurveWindow.reset();
        restoreCheck(!app.documentDrafts.review(curveDraft.owner)&&census(app).drafts.size()==3,"Native owner destruction must unregister its raw draft");
    });
}

void mainAndTimingDraftCensus() {
    using Status=Tracker::DocumentDeparture::Status;
    withRestoreFixture([](RestoreApplication &app) {
        resizeRestoreClient(app,1000,720);
        auto cursor=app.position();cursor["row"]=4;cursor["column"]=3;cursor["following"]=false;app.navigate(cursor);
        app.revealWorkspacePattern();app.workspaceState.focus="pattern";app.layoutControls();
        restoreCheck(app.beginPatternNudgeEdit("nudge-forward"),"Open captured nudge editor");
        restoreCheck(census(app).drafts.empty(),"Opening a clean nudge must not create a raw draft");
        rawDraftText(app.controls.at(app.nudgeStrengthField),L"-");
        const auto captured=census(app);const auto nudge=namedDraft(captured,"In-grid nudge");
        const auto raw=restoreText(app.controls.at(app.nudgeStrengthField));const auto song=songState(app);
        app.revealWorkspaceMainEditor("plugins");app.layoutControls();
        cursor=app.position();cursor["row"]=12;app.navigate(cursor);
        restoreCheck(app.documentDrafts.review(nudge.owner)&&IsWindowVisible(app.controls.at(app.nudgeStrengthField))&&
            GetFocus()==app.controls.at(app.nudgeStrengthField)&&app.row==4,
            "Nudge review must reveal its captured grid cell and focus its retained field");
        restoreCheck(restoreText(app.controls.at(app.nudgeStrengthField))==raw&&census(app).sameWork(captured)&&songState(app)==song,
            "Nudge review must not reload, rebase or apply the captured raw draft");
        app.openSongTiming();ScreamSeq::Tests::ownGuiWindow(app.songTimingWindow->window());
        rawDraftText(GetDlgItem(app.songTimingWindow->window(),7602),L"-");
        const auto timing=app.songTimingWindow->documentDraft();
        const auto context=app.songTimingWindow->snapshot().at("captured");
        restoreCheck(timing&&timing->dirty&&RestoreJson::parse(timing->target)==RestoreJson::array({"song-timing",context.at("sequence")}),
            "Timing draft must capture its sequence identity");
        const auto both=census(app);restoreCheck(both.drafts.size()==2,"Main and native timing drafts must coexist");
        const auto token=app.documentDrafts.authorize(both,true);
        rawDraftText(GetDlgItem(app.songTimingWindow->window(),7602),L"-.");
        restoreCheck(app.documentDrafts.admit(*token.token,app.documentId,app.view->session.revision).status==Status::Changed&&songState(app)==song,
            "New timing raw text must revoke consent without changing song history");
        app.songTimingWindow.reset();
    });
}
