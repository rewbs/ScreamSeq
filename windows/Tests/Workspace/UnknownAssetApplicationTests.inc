void applicationUnknownAssetRecovery(){
  const auto click=[](HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;unsigned writes=0;bool malformed=false;const auto before=app.view->samples.size();
    ScreamSeq::PatternSampleRenderWindow tool(app.window,[&](const std::string &method,const RestoreJson &params){if(method=="document.get"){auto result=app.view->session.document;if(malformed)result["samples"]=false;return result;}return app.documentOperation(method,params);},
      [&]{return app.patternSampleTarget();},[&](const auto &,const auto &){throw std::runtime_error("Unknown result must not select a guessed sample");},
      [&](const auto &method,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(method,params);throw std::runtime_error("Owned unknown selection render");});
    ScreamSeq::Tests::ownGuiWindow(tool.window());auto target=app.patternSampleTarget();target.firstRow=target.lastRow=target.firstChannel=target.lastChannel=0;tool.openAt(false,target);click(tool.window(),6606);
    restoreCheck(writes==1&&app.view->samples.size()==before+1&&!tool.snapshot().at("completion").at("returned").get<bool>(),"Unknown render fixture did not commit once without attribution");
    malformed=true;click(tool.window(),6606);restoreCheck(tool.snapshot().at("observation").is_null()&&!tool.snapshot().at("completion").is_null(),"Malformed assets released render uncertainty");
    malformed=false;click(tool.window(),6606);rawDraftText(GetDlgItem(tool.window(),6602),L"Later render name");click(tool.window(),6608);
    restoreCheck(!tool.snapshot().at("completion").is_null(),"Stale render acknowledgement consumed newer options");click(tool.window(),6606);click(tool.window(),6608);
    restoreCheck(tool.snapshot().at("completion").is_null()&&tool.snapshot().at("needsCapture")&&tool.snapshot().at("name")=="Later render name"&&writes==1,"Render acknowledgement replayed or rebased the retained selection");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.view->samples.size()==before,"Unknown render Review added history");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=fixture.folder/L"Parity_C3.wav";
    {auto source=Tracker::Document::demo();std::ofstream file(path,std::ios::binary);restoreCheck(source->song().SaveWAVSample(1,file),"Write owned multisample fixture");}
    const auto upper=fixture.folder/L"Parity_C4.wav";
    {auto source=Tracker::Document::demo();std::ofstream file(upper,std::ios::binary);restoreCheck(source->song().SaveWAVSample(2,file),"Write second owned multisample fixture");}
    const RestoreJson family={{"name","Parity"},{"suggestedOctaveShift",0},{"explanation","Owned unknown-result fixture"},{"samples",RestoreJson::array({
      {{"path",utf8Path(path)},{"filename","Parity_C3.wav"},{"sourceNote","C3"},{"semitone",36}},
      {{"path",utf8Path(upper)},{"filename","Parity_C4.wav"},{"sourceNote","C4"},{"semitone",48}}})}};
    const auto before=app.view->instruments;unsigned writes=0;
    ScreamSeq::MultisampleImportWindow tool(app.window,[&](const std::string &method,const RestoreJson &params){if(method=="document.get")return app.view->session.document;if(method=="sample.library.preview.stop")return RestoreJson::object();return app.documentOperation(method,params);},
      [&]{return std::pair(app.documentId,app.view->session.revision);},[](unsigned,const auto &){throw std::runtime_error("Unknown import must not select a guessed instrument");},
      [&](const auto &method,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(method,params);throw std::runtime_error("Owned unknown family import");});
    ScreamSeq::Tests::ownGuiWindow(tool.window());tool.open(family);click(tool.window(),5603);
    restoreCheck(tool.snapshot().at("zones").size()==2,"Real multisample preview did not validate both owned note files");click(tool.window(),5604);
    restoreCheck(writes==1&&!tool.snapshot().at("completion").at("returned").get<bool>(),"Family import did not retain its unknown result");
    click(tool.window(),5604);rawDraftText(GetDlgItem(tool.window(),5601),L"Later family name");click(tool.window(),5611);restoreCheck(!tool.snapshot().at("completion").is_null(),"Family acknowledgement consumed newer fields");
    click(tool.window(),5604);click(tool.window(),5611);restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("needsRebase")&&tool.snapshot().at("name")=="Later family name"&&tool.snapshot().at("draft"),"Family acknowledgement repeated import or discarded its raw draft");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.view->instruments==before,"Unknown family Review added history");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto before=app.view->samples.size();unsigned writes=0;
    RestoreJson params={{"pattern",0},{"firstRow",0},{"lastRow",0},{"firstChannel",0},{"lastChannel",0},{"name","Unknown direct render"},{"tailSeconds",0},{"createInstrument",false},{"dryRun",false},{"expectedRevision",app.view->session.revision}};
    app.directSampleRenderTarget=params;app.directSampleRenderTarget["documentId"]=app.documentId;++app.directSampleRenderGeneration;
    restoreRejected([&]{app.directSampleRenderCompletion.submit([&](const auto &method,const auto &values,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(method,values);throw std::runtime_error("Owned unknown direct render");},"sample.renderSelection",params,app.documentId,app.directSampleRenderGeneration,app.directSampleRenderTarget);});
    app.nativeCommandChoice=IDYES;app.duringCommandAcknowledgement=[&]{app.edit("document.patch",{{"title","Later than direct render observation"}});};
    restoreRejected([&]{app.reviewDirectSampleRender();});restoreCheck(app.directSampleRenderCompletion.retained(),"Direct render acknowledged a stale song");app.reviewDirectSampleRender();
    restoreCheck(writes==1&&!app.directSampleRenderCompletion.retained()&&app.directSampleRenderReport.at("outcome")=="unverified"&&app.view->samples.size()==before+1,"Direct Review repeated render or fabricated success");
    app.edit("history.undo",{{"domain","all"}});app.edit("history.undo",{{"domain","all"}});restoreCheck(app.view->samples.size()==before,"Direct Review added history beyond render and later title edit");
  }
  {
    // Native owner with controlled session callbacks, no audio device. Actual
    // capture/PCM qualification remains in the C6 integration gate.
    RestoreFixture fixture;auto &app=*fixture.app;std::pair<std::string,std::string> context{app.documentId,"owned-r1"};unsigned writes=0;
    const auto makeTake=[&](const char *id){return RestoreJson{{"take",id},{"documentId",context.first},{"baseRevision",context.second},{"device","owned-input"},{"firstChannel",0},{"channels",1},{"capturing",false},{"frames",32},{"sampleRate",48000}};};
    auto take=makeTake("original");ScreamSeq::SampleRecordingWindow *owner=nullptr;
    ScreamSeq::SampleRecordingWindow tool(app.window,[&](const std::string &method,const RestoreJson &)->RestoreJson{
      if(method=="sample.recording.devices")return {{"devices",RestoreJson::array({{{"id","owned-input"},{"name","Owned fixture"},{"channels",1},{"default",true}}})}};
      if(method=="sample.recording.get")return take;if(method=="document.get")return app.view->session.document;if(method=="synchronizeView")return RestoreJson::object();throw std::runtime_error("Unknown Keep review sent another lifecycle operation");},
      [&]{return context;},[](const auto &,const auto &){throw std::runtime_error("Unknown Keep must not select a guessed result");},
      [&](const auto &,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{restoreCheck(params.at("take")=="original","Keep changed its captured take");++writes;context.second="owned-r2";take={{"take",""},{"capturing",false},{"frames",0}};rawDraftText(GetDlgItem(owner->window(),6508),L"Later recording name");throw std::runtime_error("Owned unknown Keep");});
    owner=&tool;ScreamSeq::Tests::ownGuiWindow(tool.window());tool.openAt();click(tool.window(),6506);click(tool.window(),6506);
    take=makeTake("newer-take");click(tool.window(),6513);restoreCheck(!tool.snapshot().at("completion").is_null(),"New take bypassed Keep acknowledgement guard");
    click(tool.window(),6506);click(tool.window(),6513);restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("take")=="newer-take"&&tool.snapshot().at("name")=="Later recording name","Keep observation consumed another take or raw setup");
  }
}
