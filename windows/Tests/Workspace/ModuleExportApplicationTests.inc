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

    // Publication succeeds, but the native presentation callback fails. Review
    // must use the receipt, even if another writer subsequently changes the file.
    unsigned writes=0;
    app.sampleMutationCompletion=[&](const std::string &method) {
        if(method=="document.exportModule"){++writes;throw std::runtime_error("Owned export presentation failure");}
    };
    restoreRejected([&]{app.exportModuleFile();});
    restoreCheck(writes==1&&app.nativeCommandCompletion.retained()&&app.nativeCommandCompletion.returned()&&
        ScreamSeq::Project::readProjectBytes(output)!=ScreamSeq::Project::readProjectBytes(input),
        "Export completion fixture lost its published result");
    const auto published=ScreamSeq::Project::readProjectBytes(output);
    // Use a different valid module so the change is independently readable.
    const auto external=ScreamSeq::Project::readProjectBytes(input);
    ScreamSeq::Project::writeProjectFile(output,external,true);
    app.exportModuleFile(); // Re-entering Export reviews the retained result.
    app.reviewNativeCommand(); // A second Review is also harmless.
    restoreCheck(writes==1&&!app.nativeCommandCompletion.retained()&&app.nativeCommandReport.at("outcome")=="verified"&&
        ScreamSeq::Project::readProjectBytes(output)==external&&songState(app)==before&&app.view->path==path&&app.view->dirty==dirty,
        "Export receipt Review replayed publication or altered native song identity");
    app.sampleMutationCompletion={};

    // Drop all receipt paths after the real worker has published. A changed
    // destination during acknowledgement must force another read, not a retry.
    const std::string method="document.exportModule";
    const RestoreJson params={{"expectedRevision",app.view->session.revision},{"path",utf8Path(output)},{"overwrite",true}};
    app.nativeCommandTarget={{"domain","export"},{"method",method},{"documentId",app.documentId},
        {"revision",app.view->session.revision},{"params",params},{"target",RestoreJson::object()}};
    restoreRejected([&] {
        app.nativeCommandCompletion.submit([&](const auto &name,const auto &values,const auto &)->ScreamSeq::Api::CompletedCall {
            ++writes;(void)app.documentOperation(name,values);throw std::runtime_error("Owned unknown export result");
        },method,params,app.documentId,++app.nativeCommandGeneration,app.nativeCommandTarget);
    });
    restoreCheck(writes==2&&app.nativeCommandCompletion.retained()&&!app.nativeCommandCompletion.returned()&&
        ScreamSeq::Project::readProjectBytes(output)==published,"Unknown export fixture did not publish exactly once");
    app.nativeCommandChoice=IDYES;
    app.duringCommandAcknowledgement=[&] {ScreamSeq::Project::writeProjectFile(output,external,true);};
    restoreRejected([&]{app.reviewNativeCommand();});
    restoreCheck(app.nativeCommandCompletion.retained()&&writes==2,"Changed export bypassed the acknowledgement guard");
    app.reviewNativeCommand();
    restoreCheck(writes==2&&!app.nativeCommandCompletion.retained()&&app.nativeCommandReport.at("outcome")=="unverified"&&
        app.nativeCommandReport.at("observed").at("file")==app.inspectSavedCommandFile(output,true)&&
        ScreamSeq::Project::readProjectBytes(output)==external&&songState(app)==before&&app.view->path==path&&app.view->dirty==dirty,
        "Unknown export Review fabricated success, repeated publication or changed native state");
}
