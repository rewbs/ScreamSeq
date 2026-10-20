// Configured import intent counts even before the musician edits a field.
// Bounded callbacks inject real native changes while a worker request is pending.
void multisampleRequestRetention(HWND main){
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);
  std::pair<std::string,std::string> context{"import-document","r1"};
  const Json family={{"name","Parity"},{"suggestedOctaveShift",0},{"explanation","Generated fixture"},
    {"samples",Json::array({{{"path","C:/fixture/Parity_C3.wav"},{"filename","Parity_C3.wav"},{"sourceNote","C3"},{"semitone",36}}})}};
  ScreamSeq::MultisampleImportWindow *owner=nullptr;
  enum class Inject{None,Read,Reject,Completion};Inject inject=Inject::None;
  unsigned checks=0,commits=0;bool pendingObserved=false,discardRefused=false,callbackPending=false;Json submitted;
  const auto request=[&](const std::string &method,const Json &params)->Json {
    if(method=="sample.library.preview.stop")return Json::object();
    if(method!="instrument.importMultisample")throw std::runtime_error("Unexpected import request");
    submitted=params;const auto state=host.registry.capture(context.first,context.second);
    pendingObserved=state.drafts.size()==1&&state.drafts.front().pending;
    discardRefused=host.registry.authorize(state,true).status==Tracker::DocumentDeparture::Status::Pending;
    if(params.at("dryRun").get<bool>()){
      ++checks;
      if(inject==Inject::Read)SetWindowTextW(GetDlgItem(owner->window(),5602),L"-");
      return {{"zones",Json::array({{{"rootNote",37},{"lowNote",37},{"highNote",37}}})}};
    }
    if(inject==Inject::Reject)throw ScreamSeq::Api::ApiError(-32003,"Fixture import refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    ++commits;context.second="r"+std::to_string(commits+1);return {{"instrument",3}};
  };
  const auto applied=[&](unsigned instrument,const std::string &document){
    callbackPending=instrument==3&&document=="import-document"&&draftOf(*owner).pending;
    if(inject==Inject::Completion)SetWindowTextW(GetDlgItem(owner->window(),5601),L"Later family name");
  };
  {
    ScreamSeq::MultisampleImportWindow tool(main,request,[&]{return context;},applied,[&](const auto &method,const auto &params){auto result=request(method,params);return ScreamSeq::Api::CompletedCall{method,context.first,context.second,std::move(result)};});owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());tool.open(family);
    const auto opened=draftOf(tool);require(opened.dirty,"Configured multisample family was omitted from the draft census");
    tool.hide();require(draftOf(tool).sameWork(opened),"Hiding an import changed its captured work");
    auto other=family;other["name"]="Other";tool.open(other);
    require(tool.snapshot().at("group")==family&&draftOf(tool).sameWork(opened),"Reopening replaced a hidden configured family");
    inject=Inject::Read;clickDraftButton(tool.window(),5603);
    require(checks==1&&pendingObserved&&discardRefused&&tool.snapshot().at("octaveShift")=="-"&&tool.snapshot().at("zones").empty()&&draftOf(tool).dirty&&!draftOf(tool).pending,"Late root text was overwritten, blessed by Check, or left pending");
    require(IsWindowEnabled(GetDlgItem(tool.window(),5603)),"Stale Check left the import controls disabled");
    SetWindowTextW(GetDlgItem(tool.window(),5602),L"0");inject=Inject::None;clickDraftButton(tool.window(),5603);
    const auto checked=draftOf(tool);inject=Inject::Reject;clickDraftButton(tool.window(),5604);
    require(commits==0&&draftOf(tool).sameWork(checked)&&!tool.snapshot().at("zones").empty(),"Rejected import changed reviewed intent");
    inject=Inject::Completion;clickDraftButton(tool.window(),5604);const auto newer=draftOf(tool);
    require(commits==1&&callbackPending&&submitted.at("name")=="Parity"&&submitted.at("expectedRevision")=="r1","Import changed submitted parameters or released pending before its callback");
    require(newer.dirty&&!newer.pending&&newer.generation>checked.generation&&newer.revision=="r1"&&tool.snapshot().at("group")==family&&tool.snapshot().at("name")=="Later family name"&&tool.snapshot().at("zones").empty(),"Import completion erased or rebased newer intent");
    clickDraftButton(tool.window(),5604);require(commits==1,"Stale import was submitted twice");
    inject=Inject::None;clickDraftButton(tool.window(),5609);clickDraftButton(tool.window(),5603);clickDraftButton(tool.window(),5604);
    require(commits==2&&!draftOf(tool).dirty&&!draftOf(tool).pending&&!tool.visible()&&tool.snapshot().at("group").is_null(),"Unchanged successful import failed to release its submitted family");
    tool.open(other);clickDraftButton(tool.window(),5610);
    require(!draftOf(tool).dirty&&!tool.visible()&&host.registry.capture(context.first,context.second).drafts.empty(),"Explicit import discard retained dirty work");
  }
  require(host.registry.capture(context.first,context.second).drafts.empty(),"Destroyed import left a census owner");
  std::cout<<"Multisample configured intent and pending completion retain exact family, roots and newer text\n";
}

void pluginPathRequestRetention(HWND main){
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);std::pair<std::string,std::string> context{"path-document","r1"};
  ScreamSeq::PluginPathWindow *owner=nullptr;
  enum class Inject{None,Text,Choice,Read,Scan,Reject};Inject inject=Inject::None;
  unsigned reads=0,writes=0,scans=0;bool pendingObserved=false,discardRefused=false;Json submitted;
  const Json target={{"plugin","stable-plugin"}};
  const auto request=[&](const std::string &method,const Json &params)->Json {
    if(method=="synchronizeView")return Json::object();
    if(method=="plugin.path.get"){
      ++reads;if(inject==Inject::Read)SetWindowTextW(GetDlgItem(owner->window(),3611),L"C:/later/read.vst3");
      return {{"plugin","stable-plugin"},{"descriptor",{{"name","Fixture"},{"path","C:/fixture/a.vst3"}}},{"moduleVerified",true},{"reason",""},
        {"candidates",Json::array({{{"descriptor",{{"name","Fixture A"},{"path","C:/fixture/a.vst3"}}},{"moduleSHA256","sha-a"}},
          {{"descriptor",{{"name","Fixture B"},{"path","C:/fixture/b.vst3"}}},{"moduleSHA256","sha-b"}}})}};
    }
    submitted=params;const auto state=host.registry.capture(context.first,context.second);
    pendingObserved=state.drafts.size()==1&&state.drafts.front().pending;
    discardRefused=host.registry.authorize(state,true).status==Tracker::DocumentDeparture::Status::Pending;
    if(method=="plugin.path.scan"){
      ++scans;if(inject==Inject::Scan)SetWindowTextW(GetDlgItem(owner->window(),3611),L"C:/later/scan.vst3");return Json::object();
    }
    if(method!="plugin.path.set")throw std::runtime_error("Unexpected plugin path request");
    if(inject==Inject::Reject)throw ScreamSeq::Api::ApiError(-32003,"Fixture reconnect refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    if(!params.at("dryRun").get<bool>()){
      ++writes;context.second="r"+std::to_string(writes+1);
      if(inject==Inject::Text)SetWindowTextW(GetDlgItem(owner->window(),3611),L"C:/later/write.vst3");
      if(inject==Inject::Choice)chooseDraftField(owner->window(),3601,0);
    }
    return {{"path",params.at("path")},{"wouldChange",true}};
  };
  {
    ScreamSeq::PluginPathWindow tool(main,target,request,[&]{return context;},[&](const auto &method,const auto &params)->ScreamSeq::Api::CompletedCall{auto result=request(method,params);return {method,context.first,context.second,std::move(result)};});owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());
    require(!tool.retainedDraft()&&host.registry.capture(context.first,context.second).drafts.empty(),"Initial plugin candidates fabricated dirty work");
    chooseDraftField(tool.window(),3601,1);SetWindowTextW(GetDlgItem(tool.window(),3611),L"C:/fixture/b.vst3");
    const auto selected=draftOf(tool);clickDraftButton(tool.window(),3602);
    require(writes==0&&pendingObserved&&discardRefused&&draftOf(tool).sameWork(selected),"Verify module cleared or rebased path intent");
    inject=Inject::Reject;clickDraftButton(tool.window(),3603);require(draftOf(tool).sameWork(selected),"Rejected reconnect changed its draft");
    inject=Inject::Text;clickDraftButton(tool.window(),3603);const auto newer=draftOf(tool);
    require(writes==1&&submitted.at("plugin")=="stable-plugin"&&submitted.at("path")=="C:/fixture/b.vst3"&&submitted.at("expectedRevision")=="r1","Reconnect lost its exact submitted target");
    require(newer.dirty&&!newer.pending&&newer.generation>selected.generation&&newer.revision=="r1"&&tool.snapshot().at("manualPath")=="C:/later/write.vst3","Reconnect erased or rebased newer raw input");
    clickDraftButton(tool.window(),3603);require(writes==1,"Stale plugin reconnect was retried");
    inject=Inject::Read;clickDraftButton(tool.window(),3604);
    require(draftOf(tool).revision=="r1"&&tool.snapshot().at("manualPath")=="C:/later/read.vst3"&&!draftOf(tool).pending,"Reload blessed or erased input arriving during read");
    inject=Inject::None;clickDraftButton(tool.window(),3604);SetWindowTextW(GetDlgItem(tool.window(),3611),L"C:/fixture/b.vst3");
    const auto beforeScanReads=reads;inject=Inject::Scan;clickDraftButton(tool.window(),3605);
    require(scans==1&&reads==beforeScanReads&&pendingObserved&&discardRefused&&tool.snapshot().at("manualPath")=="C:/later/scan.vst3","Scan completion silently refreshed a newer draft");
    require(draftOf(tool).uncertain,"Interrupted scan presentation lost its retained outcome");
    inject=Inject::None;clickDraftButton(tool.window(),3603);clickDraftButton(tool.window(),3604);
    require(scans==1&&!draftOf(tool).uncertain&&tool.snapshot().at("manualPath")=="C:/later/scan.vst3","Scan review replayed or consumed newer input");
    inject=Inject::Choice;SetWindowTextW(GetDlgItem(tool.window(),3611),L"");clickDraftButton(tool.window(),3603);
    require(writes==2&&tool.snapshot().at("selected")==0&&draftOf(tool).revision=="r2"&&draftOf(tool).dirty,"Late candidate selection was discarded or rebased");
    tool.hide();require(tool.retainedDraft()&&host.registry.capture(context.first,context.second).drafts.size()==1,"Hidden path repair became replaceable");
    inject=Inject::None;clickDraftButton(tool.window(),3604);clickDraftButton(tool.window(),3603);
    require(writes==3&&!tool.retainedDraft()&&draftOf(tool).revision==context.second,"Successful submitted reconnect retained false dirty work");
    SetWindowTextW(GetDlgItem(tool.window(),3611),L"C:/not/submitted.vst3");clickDraftButton(tool.window(),3603);
    require(tool.retainedDraft(),"Reconnect consumed a manual path that was not submitted");
    clickDraftButton(tool.window(),3612);require(!tool.retainedDraft()&&!tool.visible(),"Explicit path discard did not release local intent");
    context.first="new-document";require(!tool.matchesTarget(target),"Clean repair owner matched a different document");
  }
  require(host.registry.capture(context.first,context.second).drafts.empty(),"Destroyed plugin path owner stayed registered");
  std::cout<<"Plugin path Check, scan, Reload and reconnect protect captured and newer intent\n";
}

void recordingSetupRetention(HWND main){
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);std::pair<std::string,std::string> context{"record-document","r1"};
  ScreamSeq::SampleRecordingWindow *owner=nullptr;Json take={{"take",nullptr}},submitted;
  bool inject=false,reject=false,pendingObserved=false,discardRefused=false,callbackPending=false,selectionRetained=false;
  unsigned starts=0,commits=0;
  const auto request=[&](const std::string &method,const Json &params)->Json {
    const auto state=host.registry.capture(context.first,context.second);
    pendingObserved=state.drafts.size()==1&&state.drafts.front().pending;
    discardRefused=host.registry.authorize(state,true).status==Tracker::DocumentDeparture::Status::Pending;
    if(method=="sample.recording.devices")return {{"permission","authorized"},{"devices",Json::array({
      {{"id","input-a"},{"name","A"},{"channels",2},{"default",true}},{{"id","input-b"},{"name","B"},{"channels",2}}})}};
    if(method=="sample.recording.get")return take;
    if(method=="sample.recording.start"){
      ++starts;submitted=params;
      chooseDraftField(owner->window(),6501,1);chooseDraftField(owner->window(),6502,1);chooseDraftField(owner->window(),6514,0);
      selectionRetained=owner->snapshot().at("device")==params.at("device")&&owner->snapshot().at("firstChannel")==params.at("firstChannel")&&owner->snapshot().at("maxSeconds")==params.at("maxSeconds");
      take={{"take","take-"+std::to_string(starts)},{"documentId",context.first},{"baseRevision",context.second},{"device",params.at("device")},{"firstChannel",params.at("firstChannel")},{"channels",params.at("channels")},{"capturing",true},{"frames",24000},{"sampleRate",96000},{"maxSeconds",174.7626666667},{"maxFrames",16777216}};return take;
    }
    if(method=="sample.recording.stop"){take["capturing"]=false;return take;}
    if(method=="sample.recording.discard"){take={{"take",nullptr}};return Json::object();}
    if(method=="sample.recording.commit"){
      submitted=params;if(reject)throw ScreamSeq::Api::ApiError(-32003,"Fixture recording commit refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
      ++commits;context.second="r"+std::to_string(commits+1);take={{"take",nullptr}};return {{"sample",8},{"instrument",0},{"take",params.at("take")}};
    }
    throw std::runtime_error("Unexpected recording request");
  };
  const auto committed=[&](const std::string &document,const Json &){
    callbackPending=document=="record-document"&&draftOf(*owner).pending;
    if(inject){SetWindowTextW(GetDlgItem(owner->window(),6508),L"Later take name");chooseDraftField(owner->window(),6509,1);}
  };
  {
    ScreamSeq::SampleRecordingWindow tool(main,request,[&]{return context;},committed,[&](const auto &method,const auto &params){auto result=request(method,params);return ScreamSeq::Api::CompletedCall{method,context.first,context.second,std::move(result)};});owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());tool.openAt();
    require(pendingObserved&&discardRefused&&host.registry.capture(context.first,context.second).drafts.empty(),"Recording reads omitted pending work or fabricated a draft");
    require(tool.snapshot().at("maxSeconds")==60,"Recorder default duration changed");
    chooseDraftField(tool.window(),6501,1);chooseDraftField(tool.window(),6502,2);chooseDraftField(tool.window(),6514,1);
    require(host.registry.capture(context.first,context.second).drafts.empty(),"Global capture preferences counted as a song edit");
    chooseDraftField(tool.window(),6514,99);require(tool.snapshot().at("maxSeconds")==300,"Invalid duration choice changed the capture limit");
    // Restore the first endpoint/range so the injected choices differ.
    chooseDraftField(tool.window(),6501,0);chooseDraftField(tool.window(),6502,0);
    SetWindowTextW(GetDlgItem(tool.window(),6508),L"Submitted take");const auto configured=draftOf(tool);tool.hide();
    require(configured.dirty&&draftOf(tool).sameWork(configured)&&host.registry.capture(context.first,context.second).drafts.size()==1,"Hidden sample name intent was lost");
    tool.openAt();clickDraftButton(tool.window(),6504);
    require(starts==1&&pendingObserved&&discardRefused&&selectionRetained&&tool.hasRetainedTake(),"Pending recording start lost its captured input or census guard");
    require(submitted.at("maxSeconds")==300&&tool.snapshot().at("maxSeconds")==300&&!IsWindowEnabled(GetDlgItem(tool.window(),6514)),"Recorder lost the five-minute choice across reopen/start");
    require(tool.snapshot().at("state").at("maxFrames")==16777216&&text(GetDlgItem(tool.window(),6556)).find(L"174.76")!=std::wstring::npos,"Recorder did not display the actual frame-capped duration");
    chooseDraftField(tool.window(),6514,0);require(tool.snapshot().at("maxSeconds")==300,"Retained take allowed duration setup changes");
    clickDraftButton(tool.window(),6505);const auto stopped=draftOf(tool);
    reject=true;clickDraftButton(tool.window(),6506);
    require(commits==0&&tool.hasRetainedTake()&&draftOf(tool).sameWork(stopped),"Rejected Keep consumed the take or setup");
    reject=false;inject=true;clickDraftButton(tool.window(),6506);const auto newer=draftOf(tool);
    require(commits==1&&callbackPending&&!tool.hasRetainedTake()&&submitted.at("name")=="Submitted take"&&submitted.at("createInstrument")==false,"Recording completion did not preserve its exact submission");
    require(newer.dirty&&!newer.pending&&newer.generation>stopped.generation&&newer.revision=="r1"&&tool.snapshot().at("name")=="Later take name"&&tool.snapshot().at("createInstrument")==true,"Keep erased or rebased newer sample setup");
    inject=false;clickDraftButton(tool.window(),6504);clickDraftButton(tool.window(),6505);clickDraftButton(tool.window(),6506);
    require(commits==2&&!draftOf(tool).dirty&&!tool.hasRetainedTake(),"Successful unchanged Keep left false setup work");
    SetWindowTextW(GetDlgItem(tool.window(),6508),L"Original song intent");context.first="new-document";clickDraftButton(tool.window(),6504);
    require(starts==2&&draftOf(tool).document=="record-document","Old sample setup recorded into a replacement song");
    clickDraftButton(tool.window(),6511);clickDraftButton(tool.window(),6504);clickDraftButton(tool.window(),6505);
    SetWindowTextW(GetDlgItem(tool.window(),6508),L"Keep this setup");clickDraftButton(tool.window(),6507);
    require(!tool.hasRetainedTake()&&draftOf(tool).dirty,"Discarding a take consumed independent sample setup");
    clickDraftButton(tool.window(),6511);require(!draftOf(tool).dirty&&host.registry.capture(context.first,context.second).drafts.empty(),"Explicit setup discard left false song work");
  }
  require(host.registry.capture(context.first,context.second).drafts.empty(),"Destroyed recorder stayed registered");
  std::cout<<"Recorder distinguishes setup from takes/preferences and retains newer Keep fields\n";
}
