void moduleExportControllerTests(const std::filesystem::path &directory) {
  using namespace OpenMPT;
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    // Normalize an imported legacy source once; the demo itself intentionally
    // contains sample/volume settings that some source formats cannot encode.
    Tracker::Document imported(Tracker::Document::demo(type)->serialize());
    const std::string format=imported.song().GetModSpecifications().fileExtension;
    const auto input=directory/("export-input."+format),output=directory/("export-output."+format);
    Project::writeProjectFile(input,imported.moduleExportData(),false);
    unsigned stops=0;
    DocumentController controller(input,"module-export-"+format,[&]{++stops;},[](const auto &){});
    invoke(controller,"document.patch",{{"title","Export draft"}});
    invoke(controller,"document.patch",{{"title","Later edit"}});
    invoke(controller,"history.undo");
    const auto before=controller.view();const auto captures=recoveryCapture(controller);
    const auto beforeStops=stops;
    need(before->dirty&&before->session.document.at("canRedo")==true,"Export fixture must have dirty work and Redo");
    const Json params={{"path",output.generic_string()}};
    auto preview=params;preview["dryRun"]=true;
    const auto dry=invoke(controller,"document.exportModule",preview);
    need(dry.at("written")==false&&dry.at("format")==format&&!std::filesystem::exists(output),"Export dry run published a file");
    need(controller.view()==before&&stops==beforeStops,"Export dry run changed the native save state or stopped audio");
    const auto result=invoke(controller,"document.exportModule",params);
    need(result.at("written")==true&&result.at("projectVersion").is_null(),"Module export returned a native-save result");
    Tracker::Document exported(Project::readProjectBytes(output));
    need(exported.song().GetTitle()=="Export draft"&&exported.song().GetType()==type,"Exported file did not reopen as the captured module");
    need(controller.view()==before&&stops==beforeStops&&recoveryCapture(controller)->fingerprint==captures->fingerprint,
      "Export changed the document, native metadata, saved identity, dirty state or history");
    const auto exportedBytes=Project::readProjectBytes(output);
    auto reject=[&](Json invalid,int code=-32602) {
      bool refused=false;
      try{invoke(controller,"document.exportModule",std::move(invalid));}
      catch(const Api::ApiError &error){refused=error.code==code&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted;}
      need(refused&&controller.view()==before&&stops==beforeStops&&Project::readProjectBytes(output)==exportedBytes,
        "Export admission did not retain the existing file and musical state with a proven refusal");
    };
    reject(params);
    reject({{"path",(directory/"wrong.screamseq").generic_string()}});
    reject({{"path","relative."+format}});
    reject({{"path",(directory/"missing-parent"/("out."+format)).generic_string()}});
    reject({{"path",output.generic_string()},{"overwrite",1}});
    reject({{"path",output.generic_string()},{"overwrite",true},{"extra",true}});
    bool stale=false;
    try{call(controller,"document.exportModule",{{"path",output.generic_string()},{"overwrite",true},{"expectedRevision","stale"}});}
    catch(const Api::ApiError &error){stale=error.code==-32001;}
    need(stale&&controller.view()==before&&Project::readProjectBytes(output)==exportedBytes,"Stale export wrote a destination");
    invoke(controller,"document.exportModule",{{"path",output.generic_string()},{"overwrite",true}});
    need(controller.view()==before&&Project::readProjectBytes(output)==exportedBytes,"Explicit replacement changed the document or exported data");
    {
      // Admit and serialize normally, then let the real filesystem refuse
      // replacement. The worker must not invent a success/noncommit receipt.
      Project::FileDetail::Handle locked(CreateFileW(output.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
      need(locked.value!=INVALID_HANDLE_VALUE,"Cannot establish owned export publication lock");
      const auto receipt=std::make_shared<NativeCallReceipt>();bool failed=false;
      auto pending=controller.invokeCompleted("document.exportModule",
        {{"path",output.generic_string()},{"overwrite",true},{"expectedRevision",before->session.revision}},receipt);
      while(pending.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)controller.service();
      try{(void)pending.get();}catch(const std::system_error &){failed=true;}
      need(failed&&!receipt->read()&&controller.view()==before&&stops==beforeStops&&
        Project::readProjectBytes(output)==exportedBytes,"Failed export publication changed the destination or supplied a false receipt");
      for(const auto &entry:std::filesystem::directory_iterator(directory))
        need(entry.path().filename().wstring().find(output.filename().wstring()+L".staged.")!=0,
          "Failed export publication leaked a staging file");
    }
    invoke(controller,"history.redo");
    need(controller.view()->session.document.at("title")=="Later edit","Export consumed or replaced Redo");
    invoke(controller,"history.undo");
    need(controller.view()->session.document.at("title")=="Export draft","Export created an extra musical history entry");

    // Session-independent loss policy covers opaque and inactive layers too.
    auto state=Project::newProjectState(imported);
    Project::validateModuleExport(imported,state);
    for(const char *field:{"futureExtension","screamseqLoadRecovery","plugins","automation","recoveryTake"}) {
      auto extended=state;extended.preserved[field]=field==std::string("plugins")||field==std::string("automation")?
        Json::array({Json::object()}):Json{{"opaque",Json::binary({0,255,42})}};
      const auto original=extended.preserved;bool rejected=false;
      try{Project::validateModuleExport(imported,extended);}catch(const std::invalid_argument &){rejected=true;}
      need(rejected&&extended.preserved==original,"Module admission silently discarded an opaque or session-owned layer");
    }
    auto extension=state;extension.preserved["native"]=state.metadataBaseline;
    extension.preserved["native"]["futureNative"]=Json::binary({42,255,0});
    bool refused=false;try{Project::validateModuleExport(imported,extension);}catch(const std::invalid_argument &){refused=true;}
    need(refused,"Module admission ignored an unknown native metadata extension");
  }
  Tracker::Document fresh(OpenMPT::MOD_TYPE_IT,4),imported(fresh.serialize());
  auto state=Project::newProjectState(imported);
  auto future=Project::nativeProjectTree(imported,state);future["version"]=999;
  const auto source=directory/"export-protected.screamseq",destination=directory/"export-protected-copy.it";
  const auto original=Project::encodePlist(future);Project::writeProjectFile(source,original,false);
  DocumentController recovered(source,"export-protected",[]{},[](const auto &){});
  const auto before=recovered.view();
  need(before->session.document.at("requiresSaveAs")==true,"Module recovery fixture did not protect its source");
  invoke(recovered,"document.exportModule",{{"path",destination.generic_string()}});
  need(recovered.view()==before&&Project::readProjectBytes(source)==original,
    "Export changed recovered source protection, dirty state or source bytes");
  bool protectedSource=false;
  try{invoke(recovered,"document.save",{{"path",source.generic_string()},{"overwrite",true}});}
  catch(const Api::ApiError &){protectedSource=true;}
  need(protectedSource&&Project::readProjectBytes(source)==original,"Export unlocked the original recovered project");
  const auto alias=directory/"export-protected-alias.it";
  std::filesystem::create_hard_link(source,alias);
  for(bool dry:{true,false}) {
    bool refused=false;
    try{invoke(recovered,"document.exportModule",{{"path",alias.generic_string()},{"overwrite",true},{"dryRun",dry}});}
    catch(const Api::ApiError &error){refused=error.code==-32003&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted;}
    need(refused&&recovered.view()==before&&Project::readProjectBytes(alias)==original&&Project::readProjectBytes(source)==original,
      "Export bypassed recovered-source protection through a format-matching hard-link alias");
  }
  future["futureExtension"]=Json::binary({0,255,42});
  const auto opaque=directory/"export-opaque.screamseq";const auto opaqueBytes=Project::encodePlist(future);
  Project::writeProjectFile(opaque,opaqueBytes,false);
  invoke(recovered,"document.open",{{"path",opaque.generic_string()},{"discard",true}});
  const auto opaqueView=recovered.view();const auto exportedBytes=Project::readProjectBytes(destination);
  for(bool dry:{true,false}) {
    bool refused=false;
    try{invoke(recovered,"document.exportModule",{{"path",destination.generic_string()},{"overwrite",true},{"dryRun",dry}});}
    catch(const Api::ApiError &error){refused=error.code==-32602&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted;}
    need(refused&&recovered.view()==opaqueView&&Project::readProjectBytes(opaque)==opaqueBytes&&
      Project::readProjectBytes(destination)==exportedBytes,"Opaque export refusal lost state or overwrote a file");
  }
  std::cout<<"PASS module export: five formats, exact native-state retention, dry run, destinations, stale guards, Redo and opaque-loss refusal\n";
}
