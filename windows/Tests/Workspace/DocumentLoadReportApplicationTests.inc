#include "windows/Project/BinaryPlist.hpp"

void applicationDocumentLoadReport() {
    RestoreFixture fixture;auto &app=*fixture.app;
    const auto source=fixture.folder/L"future-source.screamseq",copy=fixture.folder/L"recovered-copy.screamseq";
    auto document=Tracker::Document::demo();auto state=ScreamSeq::Project::newProjectState(*document);
    auto tree=ScreamSeq::Project::nativeProjectTree(*document,state);tree["version"]=7;
    const auto bytes=ScreamSeq::Project::encodePlist(tree);ScreamSeq::Project::writeProjectFile(source,bytes,false);
    app.documentOperation("document.open",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(source)}});
    const auto warnings=app.view->session.document.at("loadWarnings");
    restoreCheck(!warnings.empty()&&app.view->session.document.at("requiresSaveAs")==true,"Load report fixture did not exercise real file recovery");
    const auto button=app.controls.at(documentLoadReportCommand);SetFocus(button);
    app.command(documentLoadReportCommand);auto *report=app.documentLoadReportWindow.get();
    const auto owner=report->window(),text=GetDlgItem(owner,8301);ScreamSeq::Tests::ownGuiWindow(owner);
    const auto before=songState(app),snapshot=report->snapshot();
    restoreCheck(snapshot.at("context").at("warnings")==warnings&&snapshot.at("context").at("sourcePath")==utf8Path(source)&&
        snapshot.at("text").get<std::string>().find(warnings[0].get<std::string>())!=std::string::npos&&
        (GetWindowLongPtrW(text,GWL_STYLE)&ES_READONLY)&&IsWindowVisible(GetDlgItem(owner,8303)),
        "Native report lost source/warnings, became editable or omitted Save a copy");
    SendMessageW(text,EM_SETSEL,3,17);SetFocus(text);app.updateSongTools();app.openDocumentLoadReport();
    DWORD first=0,last=0;SendMessageW(text,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    restoreCheck(GetFocus()==text&&first==3&&last==17&&songState(app)==before,
        "Refreshing or raising the report lost text selection/focus or changed the song");
    SendMessageW(owner,WM_COMMAND,8303,reinterpret_cast<LPARAM>(GetDlgItem(owner,8303)));MSG queued{};
    restoreCheck(PeekMessageW(&queued,app.window,loadReportSaveCopyMessage,loadReportSaveCopyMessage,PM_REMOVE)!=FALSE&&
        app.pendingLoadReportSave==app.documentId,"Save a copy did not post the captured document outside its native callback");
    app.recoveryRestoring=true;DispatchMessageW(&queued);app.recoveryRestoring=false;
    restoreCheck(!app.pendingLoadReportSave&&songState(app)==before&&app.status.find(L"Wait for the current operation")!=std::wstring::npos,
        "Queued report save bypassed a recovery operation begun after its click");
    SendMessageW(owner,WM_COMMAND,8303,reinterpret_cast<LPARAM>(GetDlgItem(owner,8303)));
    restoreCheck(PeekMessageW(&queued,app.window,loadReportSaveCopyMessage,loadReportSaveCopyMessage,PM_REMOVE)!=FALSE&&
        app.pendingLoadReportSave==app.documentId,"Report could not queue a save after the pending operation ended");
    const auto rejected=restoreRejected([&]{app.documentOperation("document.save",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(source)},{"overwrite",true}});});
    restoreCheck(!rejected.empty()&&ScreamSeq::Project::readProjectBytes(source)==bytes&&report->snapshot().at("context").at("requiresSaveAs")==true,
        "Reviewing a load report weakened protected-source saving");
    app.documentOperation("document.save",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(copy)}});
    const auto saved=report->snapshot();
    restoreCheck(saved.at("context").at("warnings")==warnings&&saved.at("context").at("sourcePath")==utf8Path(source)&&
        saved.at("context").at("path")==utf8Path(copy)&&saved.at("context").at("requiresSaveAs")==false&&!IsWindowVisible(GetDlgItem(owner,8303)),
        "Save a copy erased load provenance or left the completed save requirement visible");
    report->hide();restoreCheck(GetFocus()==button,"Closing load report did not restore its entry-point focus");
    app.openDocumentLoadReport();restoreCheck(report->snapshot().at("context").at("warnings")==warnings,"Reopening the report discarded load warnings");
    MINMAXINFO limits{};SendMessageW(owner,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
    SetWindowPos(owner,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{};GetClientRect(owner,&client);
    for(int id:{8300,8301,8302,8304}){RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&bounds),2);
        restoreCheck(bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom,"Load report clips at its minimum size");}
    // Hold its queued main-window save message while replacing the document.
    // The old report can retire safely and the queued action cannot Save As the new song.
    app.documentOperation("document.new",{{"expectedRevision",app.view->session.revision}});
    restoreCheck(!app.documentLoadReportWindow&&!IsWindow(owner)&&!app.pendingLoadReportSave,"Read-only report or queued Save survived document retirement");
    const auto fresh=songState(app);DispatchMessageW(&queued);
    restoreCheck(songState(app)==fresh&&ScreamSeq::Project::readProjectBytes(source)==bytes,"Old queued report action targeted the new song or source file");
    app.openDocumentLoadReport();const auto clean=app.documentLoadReportWindow->snapshot();
    restoreCheck(clean.at("context").at("warnings").empty()&&clean.at("context").at("sourcePath")==""&&
        clean.at("text").get<std::string>().find("No load warnings")!=std::string::npos,"New document inherited an earlier report");
}
