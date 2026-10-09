// Real HWND owners with deterministic failures after the write has returned.
// A Review action may read/repair presentation, but must never repeat the write.
void completionClassification(){
  using namespace ScreamSeq;
  using Json=Api::Json;
  for(unsigned mode=0;mode<4;++mode){
    NativeWriteCompletion completion;unsigned writes=0;
    const NativeWriteCompletion::Write write=[&](const auto &method,const auto &)->Api::CompletedCall{
      ++writes;
      if(mode==0)throw std::runtime_error("Unclassified callback failure");
      if(mode==1)throw Api::ApiError(-32003,"Unknown",Tracker::WriteOutcome{});
      if(mode==2)return {method,"other-document","r2",{{"sample",99}}};
      throw Api::ApiError(-32001,"Rejected",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    };
    rejected([&]{completion.submit(write,"sample.renderSelection",Json::object(),"document",7,Json::array({"raw -"}));},"Fault fixture returned success");
    require(completion.retained()==(mode!=3)&&!completion.returned(),"Unclassified or mismatched result was treated as a proven rejection");
    if(mode!=3){
      rejected([&]{completion.submit(write,"sample.renderSelection",Json::object(),"document",8,Json::array());},"Uncertain write was resubmitted");
      require(writes==1&&completion.generation()==7&&completion.fields()==Json::array({"raw -"}),"Repeat changed original submission");
    }
  }
  NativeWriteCompletion committed;
  const auto receipt=std::make_shared<const Api::CompletedCall>(Api::CompletedCall{"sample.renderSelection","document","r2",{{"sample",7}}});
  rejected([&]{committed.submit([&](const auto &,const auto &)->Api::CompletedCall{
    throw Api::ApiError(-32003,"Worker publication failed",Tracker::WriteOutcome{Tracker::CommitOutcome::Committed,"document","r2"},receipt);
  },"sample.renderSelection",Json::object(),"document",9,Json::array({"name"}));},"Committed error returned success");
  require(committed.retained()&&committed.returned()==receipt&&committed.generation()==9,"Worker publication error lost its known result");
  committed.finish();require(!committed.retained()&&!committed.returned(),"Reviewed result remained unresolved");
}

void renderCompletionReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  DraftRegistryHost host(main);
  PatternSampleRenderWindow::Target target{"render-result","r1",0,0,3,0,0};
  unsigned writes=0,reads=0,reveals=0;PatternSampleRenderWindow *owner=nullptr;
  const auto read=[&](const std::string &method,const Json &)->Json{require(method=="synchronizeView","Review called a render write");++reads;return Json::object();};
  const auto write=[&](const std::string &method,const Json &)->Api::CompletedCall{
    ++writes;require(draftOf(*owner).pending,"Render submission was not pending");target.revision="r2";
    return {method,target.document,target.revision,{{"sample",7},{"instrument",2}}};
  };
  const auto reveal=[&](const std::string &,const Json &){++reveals;SetWindowTextW(GetDlgItem(owner->window(),6602),L"Later render");throw std::runtime_error("Failed reveal");};
  PatternSampleRenderWindow tool(main,read,[&]{return target;},reveal,write);owner=&tool;Tests::ownGuiWindow(tool.window());tool.openAt(true,target);
  clickDraftButton(tool.window(),6606);
  require(writes==1&&reveals==1&&draftOf(tool).uncertain&&!draftOf(tool).pending,"Postcommit render failure lost its receipt");
  auto census=host.registry.capture(target.document,target.revision);
  require(host.registry.authorize(census,true).status==Tracker::DocumentDeparture::Status::Uncertain,"Uncertain render allowed departure consent");
  tool.dock(main);tool.floatWindow();tool.hide();tool.openAt(false,target);
  target.revision="r3";clickDraftButton(tool.window(),6606);
  require(writes==1&&reads==1&&reveals==1&&!draftOf(tool).uncertain&&draftOf(tool).dirty,"Review repeated render, revealed stale slots or cleared newer fields");
  require(tool.snapshot().at("name")=="Later render"&&tool.snapshot().at("report").at("sample")==7,"Review lost exact result or raw draft");

  // Unknown without a result is deliberately unresolved, including after reads.
  PatternSampleRenderWindow unknown(main,read,[&]{return target;},reveal,[&](const auto &,const auto &)->Api::CompletedCall{++writes;throw std::runtime_error("Lost response");});
  Tests::ownGuiWindow(unknown.window());unknown.openAt(false,target);clickDraftButton(unknown.window(),6606);
  const auto before=draftOf(unknown);clickDraftButton(unknown.window(),6601);clickDraftButton(unknown.window(),6605);clickDraftButton(unknown.window(),6606);
  require(writes==2&&draftOf(unknown).sameWork(before)&&draftOf(unknown).uncertain,"Unknown render was retried, retargeted or discarded by Review");
}

void importCompletionReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  DraftRegistryHost host(main);std::pair<std::string,std::string> context{"import-result","r1"};
  const Json family={{"name","Parity"},{"suggestedOctaveShift",0},{"explanation","Generated fixture"},
    {"samples",Json::array({{{"path","C:/fixture/Parity_C3.wav"},{"filename","Parity_C3.wav"},{"sourceNote","C3"},{"semitone",36}}})}};
  unsigned writes=0,reveals=0,reads=0;bool stopFails=true;MultisampleImportWindow *owner=nullptr;
  const auto request=[&](const std::string &method,const Json &params)->Json{
    if(method=="synchronizeView"){++reads;return Json::object();}
    if(method=="sample.library.preview.stop"){if(stopFails&&writes)throw std::runtime_error("Preview completion failed");return Json::object();}
    require(method=="instrument.importMultisample"&&params.at("dryRun")==true,"Review repeated import");
    return {{"zones",Json::array({{{"rootNote",37},{"lowNote",37},{"highNote",37}}})}};
  };
  MultisampleImportWindow tool(main,request,[&]{return context;},[&](unsigned,const std::string &){++reveals;},
    [&](const auto &method,const auto &)->Api::CompletedCall{++writes;context.second="r2";SetWindowTextW(GetDlgItem(owner->window(),5601),L"New family draft");return {method,context.first,context.second,{{"instrument",8}}};});
  owner=&tool;Tests::ownGuiWindow(tool.window());tool.open(family);clickDraftButton(tool.window(),5603);clickDraftButton(tool.window(),5604);
  require(writes==1&&reveals==1&&draftOf(tool).uncertain,"Preview-stop failure lost committed import");
  const auto retained=draftOf(tool);clickDraftButton(tool.window(),5609);clickDraftButton(tool.window(),5610);clickDraftButton(tool.window(),5603);
  require(draftOf(tool).sameWork(retained),"Unresolved import allowed synthetic rebase/discard/check");
  stopFails=false;tool.hide();auto other=family;other["name"]="Other";tool.open(other);context.first="new-document";context.second="new-revision";
  clickDraftButton(tool.window(),5604);
  require(writes==1&&reads==1&&reveals==1&&!draftOf(tool).uncertain&&draftOf(tool).dirty,"Import Review repeated a write or selected stale instrument");
  require(tool.snapshot().at("group")==family&&tool.snapshot().at("name")=="New family draft","Review erased newer family intent");
}

void recordingCompletionReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  DraftRegistryHost host(main);std::pair<std::string,std::string> context{"record-result","r1"};
  const auto makeTake=[&](const char *id){return Json{{"take",id},{"documentId",context.first},{"baseRevision",context.second},{"device","input"},{"firstChannel",0},{"channels",1},{"capturing",false},{"frames",24000},{"sampleRate",24000}};};
  Json take=makeTake("original");unsigned writes=0,reveals=0,syncs=0;SampleRecordingWindow *owner=nullptr;
  const auto request=[&](const std::string &method,const Json &)->Json{
    if(method=="sample.recording.devices")return {{"devices",Json::array({{{"id","input"},{"name","Test input"},{"channels",1},{"default",true}}})}};
    if(method=="sample.recording.get")return take;
    if(method=="synchronizeView"){++syncs;return Json::object();}
    throw std::runtime_error("Keep review sent an unexpected lifecycle write");
  };
  SampleRecordingWindow tool(main,request,[&]{return context;},[&](const std::string &,const Json &){
    ++reveals;SetWindowTextW(GetDlgItem(owner->window(),6508),L"Later recording name");throw std::runtime_error("Keep reveal failed");
  },[&](const auto &method,const auto &params)->Api::CompletedCall{
    require(params.at("take")=="original","Keep changed take identity");++writes;context.second="r2";take={{"take",nullptr}};
    return {method,context.first,context.second,{{"take","original"},{"sample",9},{"instrument",0}}};
  });owner=&tool;Tests::ownGuiWindow(tool.window());tool.openAt();clickDraftButton(tool.window(),6506);
  require(writes==1&&reveals==1&&draftOf(tool).uncertain,"Failed Keep reveal lost the consumed take result");
  const auto retained=draftOf(tool);clickDraftButton(tool.window(),6507);clickDraftButton(tool.window(),6511);clickDraftButton(tool.window(),6504);
  require(draftOf(tool).sameWork(retained),"Unresolved Keep allowed discard/setup/reset or another capture");
  context.second="r3";take=makeTake("external-new-take");tool.hide();tool.openAt();
  require(tool.snapshot().at("take")=="original","Reopening replaced an unresolved Keep owner");
  clickDraftButton(tool.window(),6506);
  require(writes==1&&reveals==1&&syncs==1&&!draftOf(tool).uncertain,"Keep Review repeated import or revealed an old slot");
  require(tool.snapshot().at("take")=="external-new-take"&&tool.snapshot().at("report").at("take")=="original","Review consumed another take or confused its result");
  require(draftOf(tool).dirty&&tool.snapshot().at("name")=="Later recording name","Review cleared newer recording setup");
}
