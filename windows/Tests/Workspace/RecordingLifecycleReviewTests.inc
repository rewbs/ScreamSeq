void recordingLifecycleReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  DraftRegistryHost host(main);std::pair<std::string,std::string> context{"lifecycle-song","r1"};
  Json take={{"take",nullptr}};unsigned starts=0,stops=0,discards=0,reads=0;
  bool readFails=false,malformed=false,refused=false,injectNewerName=true;
  std::string failMethod="sample.recording.start";SampleRecordingWindow *owner=nullptr;
  const auto makeTake=[&](const char *id,bool capturing){return Json{{"take",id},{"documentId",context.first},{"baseRevision",context.second},
    {"device","input"},{"firstChannel",0},{"channels",1},{"capturing",capturing},{"frames",24000},{"sampleRate",24000}};};
  const auto request=[&](const std::string &method,const Json &params)->Json{
    if(method=="sample.recording.devices")return {{"devices",Json::array({{{"id","input"},{"name","Owned input"},{"channels",1},{"default",true}}})}};
    if(method=="sample.recording.get"){
      ++reads;if(readFails)throw std::runtime_error("Read unavailable");
      if(malformed)return {{"take",123}};return take;
    }
    require(draftOf(*owner).pending,"Lifecycle request omitted pending census protection");
    if(refused)throw Api::ApiError(-32002,"Typed preflight refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    if(method=="sample.recording.start"){
      ++starts;require(take.at("take").is_null(),"Repeated start would replace a take");take=makeTake("original",true);
      if(injectNewerName)SetWindowTextW(GetDlgItem(owner->window(),6508),L"Newer sample name");
    }else if(method=="sample.recording.stop"){
      ++stops;require(params.at("take")==take.at("take"),"Stop targeted another take");take["capturing"]=false;
    }else if(method=="sample.recording.discard"){
      ++discards;require(params.at("take")==take.at("take"),"Discard targeted another take");take={{"take",nullptr}};
    }else throw std::runtime_error("Review issued an unexpected write");
    if(method==failMethod)throw std::runtime_error("Lifecycle reply unavailable after side effect");
    return take;
  };
  SampleRecordingWindow tool(main,request,[&]{return context;},[](const std::string &,const Json &){},
    [&](const auto &,const auto &)->Api::CompletedCall{throw std::runtime_error("Lifecycle review attempted Keep");});
  owner=&tool;Tests::ownGuiWindow(tool.window());tool.openAt();
  clickDraftButton(tool.window(),6504);
  require(starts==1&&draftOf(tool).uncertain&&tool.snapshot().at("lifecycleReview").at("method")==failMethod,
    "Unknown Record lost its captured operation");
  require(tool.snapshot().at("name")=="Newer sample name","Record failure lost newer setup");
  const auto captured=host.registry.capture(context.first,context.second);
  require(host.registry.authorize(captured,true).status==Tracker::DocumentDeparture::Status::Uncertain,"Unknown Record allowed document departure");
  for(int id:{6504,6505,6506,6507,6510,6511})clickDraftButton(tool.window(),id);
  require(starts==1&&stops==0&&discards==0&&tool.visible()&&draftOf(tool).uncertain,
    "Unknown Record allowed replay, Keep, Discard, setup loss or Close");
  require(IsWindowVisible(GetDlgItem(tool.window(),6512))&&IsWindowEnabled(GetDlgItem(tool.window(),6512)),"Native take review is not reachable");
  const auto original=draftOf(tool);readFails=true;clickDraftButton(tool.window(),6512);readFails=false;
  malformed=true;clickDraftButton(tool.window(),6512);malformed=false;
  require(draftOf(tool).sameWork(original)&&starts==1,"Failed/malformed readback cleared the uncertainty");
  tool.openAt();require(draftOf(tool).sameWork(original),"Reopen silently resolved the lifecycle operation");
  clickDraftButton(tool.window(),6512);
  require(!draftOf(tool).uncertain&&draftOf(tool).dirty&&tool.snapshot().at("take")=="original"&&tool.snapshot().at("capturing")==true&&starts==1,
    "Readback failed to expose active capture without repeating Record");

  failMethod="sample.recording.stop";clickDraftButton(tool.window(),6505);
  require(stops==1&&draftOf(tool).uncertain,"Unknown Stop was not retained");
  clickDraftButton(tool.window(),6505);clickDraftButton(tool.window(),6507);
  require(stops==1&&discards==0,"Unknown Stop replayed or discarded its take");
  clickDraftButton(tool.window(),6512);
  require(!draftOf(tool).uncertain&&!tool.snapshot().at("capturing").get<bool>()&&tool.snapshot().at("take")=="original"&&stops==1,
    "Stop readback did not retain the observed stopped take");

  failMethod="sample.recording.discard";clickDraftButton(tool.window(),6507);
  require(discards==1&&draftOf(tool).uncertain,"Unknown Discard lost intent");
  context.second="r2";take=makeTake("external-replacement",false);
  clickDraftButton(tool.window(),6512);
  const auto observed=tool.snapshot();
  require(!draftOf(tool).uncertain&&observed.at("take")=="external-replacement"&&observed.at("name")=="Newer sample name"&&discards==1,
    "Review discarded an externally created take or newer setup");
  require(observed.at("status").get<std::string>().find("different take")!=std::string::npos,"Review attributed another take to the earlier request");
  failMethod.clear();clickDraftButton(tool.window(),6507);
  require(discards==2&&!tool.hasRetainedTake(),"Explicit Discard after review did not address the observed take");

  refused=true;clickDraftButton(tool.window(),6504);
  require(!draftOf(tool).uncertain&&tool.snapshot().at("lifecycleReview").is_null()&&starts==1,
    "Typed preflight refusal became an uncertain capture");
  refused=false;failMethod="sample.recording.start";clickDraftButton(tool.window(),6504);
  take={{"take",nullptr}};clickDraftButton(tool.window(),6512);
  require(!draftOf(tool).uncertain&&!tool.hasRetainedTake()&&starts==2&&discards==2&&reads>=6,
    "Empty readback repeated a write or left recording unavailable");
}
