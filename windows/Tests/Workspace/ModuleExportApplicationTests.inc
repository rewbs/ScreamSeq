void applicationModuleExport() {
    RestoreFixture fixture;auto &app=*fixture.app;
    Tracker::Document imported(Tracker::Document::demo(OpenMPT::MOD_TYPE_IT)->serialize());
    const auto input=fixture.folder/L"export-source.it",output=fixture.folder/L"export-result.it";
    ScreamSeq::Project::writeProjectFile(input,imported.moduleExportData(),false);
    app.documentOperation("document.open",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(input)},{"discard",true}});
    app.edit("document.patch",{{"title","Native export"}});
    app.edit("document.patch",{{"title","Retained redo"}});
    app.edit("history.undo",{{"domain","all"}});
    auto before=songState(app);const auto path=app.view->path;const auto dirty=app.view->dirty;
    restoreCheck(app.menuCommandState(exportModuleCommand).enabled,"Export is missing or disabled in the native File menu");
    app.moduleExportChooser=[](const auto &format) {
        restoreCheck(format==L"it","Export picker received the wrong source format");return std::filesystem::path{};
    };
    app.command(exportModuleCommand);
    restoreCheck(songState(app)==before&&!std::filesystem::exists(output),"Cancelled module picker changed the song or wrote a file");
    app.moduleExportChooser=[&](const auto &) {app.edit("document.patch",{{"title","Changed in picker"}});return output;};
    bool stale=false;try{app.exportModuleFile();}catch(const ScreamSeq::Api::ApiError &error){stale=error.code==-32001;}
    restoreCheck(stale&&!std::filesystem::exists(output),"Export picker silently rebased to another revision");
    app.edit("history.undo",{{"domain","all"}});before=songState(app);
    app.moduleExportChooser=[&](const auto &) {return output;};
    app.command(exportModuleCommand);
    restoreCheck(std::filesystem::exists(output)&&songState(app)==before&&app.view->path==path&&app.view->dirty==dirty,
        "Native export changed project identity, dirty state or history");
    Tracker::Document result(ScreamSeq::Project::readProjectBytes(output));
    restoreCheck(result.song().GetTitle()=="Native export","Native export command wrote another song");
    const auto observed=app.inspectSavedCommandFile(output,true);
    restoreCheck(observed.at("validated")==true&&observed.at("bytes").get<size_t>()>0,"Retained export review cannot inspect a module file");
}
