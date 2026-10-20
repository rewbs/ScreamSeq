void pluginPathReadback(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  for(bool graph:{false,true})for(int operation=0;operation<3;++operation){
    DraftRegistryHost host(main);std::pair<std::string,std::string> context{"observed-song","r1"};
    const Json target=graph?Json{{"graph","n8"},{"node","n9"}}:Json{{"plugin","stable-plugin"}};
    const std::string prefix=graph?"graph.plugin.path.":"plugin.path.";
    std::string saved="C:/old/plugin.vst3";unsigned writes=0,scans=0,reads=0;
    bool failedRead=false,malformed=false,wrongTarget=false,changeDuringRead=false;
    PluginPathWindow *owner=nullptr;
    const auto observation=[&]{auto data=target;data.update({{"descriptor",{{"name","Fixture"},{"path",saved}}},{"moduleVerified",true},{"reason",""},
      {"candidates",Json::array({{{"descriptor",{{"name","Fixture"},{"path","C:/new/plugin.vst3"}}},{"moduleSHA256","hash"}}})}});return data;};
    const auto request=[&](const std::string &method,const Json &params)->Json{
      if(method=="synchronizeView")return Json::object();
      if(method==prefix+"get"){
        ++reads;require(params==target,"Readback retargeted the captured plugin");
        if(failedRead)throw std::runtime_error("Read unavailable");
        auto data=observation();if(malformed)data["candidates"][0]["moduleSHA256"]=123;
        if(wrongTarget)data[graph?"node":"plugin"]="another-target";
        if(changeDuringRead)context.second="r3";
        return data;
      }
      require(method==(operation==1?prefix+"scan":"plugin.discover"),"Review attempted another side effect");
      ++scans;require(draftOf(*owner).pending,"Scan had no pending census protection");
      SetWindowTextW(GetDlgItem(owner->window(),3611),L"C:/later/plugin.vst3");
      throw std::runtime_error("Scan cache changed before reply failure");
    };
    PluginPathWindow tool(main,target,request,[&]{return context;},[&](const auto &method,const auto &)->Api::CompletedCall{
      require(method==prefix+"set"&&draftOf(*owner).pending,"Wrong reconnect operation");++writes;
      saved="C:/new/plugin.vst3";context.second="r2";SetWindowTextW(GetDlgItem(owner->window(),3611),L"C:/later/plugin.vst3");
      throw std::runtime_error("Reconnect reply missing");
    });owner=&tool;Tests::ownGuiWindow(tool.window());
    SetWindowTextW(GetDlgItem(tool.window(),3611),L"C:/new/plugin.vst3");
    clickDraftButton(tool.window(),operation==0?3603:operation==1?3605:3607);
    require(writes+scans==1&&draftOf(tool).uncertain&&!draftOf(tool).pending,"Unclassified plugin operation lost its outcome");
    const auto retained=draftOf(tool);
    require(host.registry.authorize(host.registry.capture(context.first,context.second),true).status==Tracker::DocumentDeparture::Status::Uncertain,"Unknown scan/reconnect allowed departure");
    for(int id:{3602,3604,3605,3606,3607,3612})clickDraftButton(tool.window(),id);
    require(draftOf(tool).sameWork(retained)&&writes+scans==1,"Unknown operation allowed Verify/Reload/scan/discard");
    failedRead=true;clickDraftButton(tool.window(),3603);failedRead=false;
    malformed=true;clickDraftButton(tool.window(),3603);malformed=false;
    wrongTarget=true;clickDraftButton(tool.window(),3603);wrongTarget=false;
    const auto revision=context.second;changeDuringRead=true;clickDraftButton(tool.window(),3603);changeDuringRead=false;context.second=revision;
    context.first="another-song";clickDraftButton(tool.window(),3603);context.first="observed-song";
    require(draftOf(tool).sameWork(retained)&&tool.snapshot().at("savedPath")=="C:/old/plugin.vst3",
      "Failed, malformed, wrong-target or raced readback altered fields or released intent");
    tool.hide();tool.show();clickDraftButton(tool.window(),3603);
    auto state=tool.snapshot();
    require(writes+scans==1&&!draftOf(tool).uncertain&&draftOf(tool).dirty&&state.at("manualPath")=="C:/later/plugin.vst3",
      "Observation replayed a write or consumed newer path input");
    require(state.at("savedPath")==saved&&state.at("report").at("outcome")=="unverified"&&state.at("readbackNeedsReload")==true,
      "Observation claimed commit/Undo success or lost the saved location");
    clickDraftButton(tool.window(),3603);clickDraftButton(tool.window(),3605);clickDraftButton(tool.window(),3607);
    require(writes+scans==1,"Readback silently rebased and enabled another operation");
    clickDraftButton(tool.window(),3604);
    require(tool.snapshot().at("readbackNeedsReload")==false&&tool.snapshot().at("manualPath")=="C:/later/plugin.vst3"&&writes+scans==1&&reads>=6,
      "Explicit Reload lost draft or repeated a scan");
  }
}
