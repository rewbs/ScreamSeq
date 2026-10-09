// Original user-supplied assets; retain owned outputs for reciprocal qualification.
void applicationParityFixtureRecovery(){
  wchar_t directory[32768]{};const auto length=GetEnvironmentVariableW(L"SCREAMSEQ_PARITY_FIXTURE_DIRECTORY",directory,DWORD(std::size(directory)));
  restoreCheck(length&&length<std::size(directory),"Required parity fixture directory unavailable");
  const auto root=std::filesystem::path(directory)/L"fixtures";
  RestoreFixture fixture;auto &app=*fixture.app;const auto input=root/L"04-complete-reference.screamseq";
  wchar_t outputRoot[32768]{};const auto outputLength=GetEnvironmentVariableW(L"SCREAMSEQ_PARITY_OUTPUT_DIRECTORY",outputRoot,DWORD(std::size(outputRoot)));
  restoreCheck(outputLength&&outputLength<std::size(outputRoot),"Required parity output directory unavailable");
  std::filesystem::create_directories(outputRoot);
  const auto retained=std::filesystem::path(outputRoot)/fixture.folder.filename();
  restoreCheck(std::filesystem::create_directory(retained),"Parity evidence directory already exists");
  restoreCheck(std::filesystem::copy_file(input,retained/L"original.screamseq"),"Retain original parity input");
  const auto originalBytes=ScreamSeq::Project::readProjectBytes(input);
  app.documentOperation("document.open",departureOpenRequest(app,input));
  const auto pcm=[&](unsigned slot){
    auto result=RestoreJson::array();uint64_t start=0,total=0;
    do{auto block=app.documentOperation("sample.pcm.get",{{"sample",slot},{"start",start}});total=block.at("totalFrames").get<uint64_t>();
      const auto frames=block.at("frames").get<uint64_t>();restoreCheck(frames||start==total,"PCM read made no progress");start+=frames;result.push_back(std::move(block));
    }while(start<total);return result;
  };
  const auto assets=[&]{auto result=RestoreJson::array();for(const auto &sample:app.view->session.document.at("samples"))result.push_back({{"id",sample.at("id")},{"index",sample.at("index")},{"pcm",pcm(sample.at("index").get<unsigned>())}});return result;};
  const auto before=assets();restoreCheck(before.size()==7,"F04 sample inventory differs from supplied reference");
  auto target=app.patternSampleTarget();target.pattern=0;target.firstRow=target.lastRow=target.firstChannel=target.lastChannel=0;
  const auto revision=app.view->session.revision;
  app.documentOperation("sample.renderSelection",{{"expectedRevision",revision},{"pattern",0},{"firstRow",0},{"lastRow",0},{"firstChannel",0},{"lastChannel",0},{"tailSeconds",0},{"createInstrument",false},{"dryRun",true}});
  restoreCheck(app.view->session.revision==revision&&assets()==before,"F04 render dry run mutated assets");
  unsigned renders=0;app.sampleMutationCompletion=[&](const std::string &method){if(method=="sample.renderSelection"){++renders;throw std::runtime_error("Owned F04 render completion loss");}};
  restoreRejected([&]{app.renderPatternSample(false,target);});
  restoreCheck(renders==1&&app.directSampleRenderCompletion.returned()&&app.view->samples.size()==before.size()+1,"F04 render lost its exact receipt");
  app.reviewDirectSampleRender();restoreCheck(renders==1&&!app.directSampleRenderCompletion.retained(),"F04 Review repeated render");app.sampleMutationCompletion={};
  const auto rendered=assets();app.edit("history.undo",{{"domain","all"}});restoreCheck(assets()==before,"F04 render did not have exact one-step PCM/identity Undo");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(assets()==rendered,"F04 render Redo changed PCM or identities");
  const auto reopened=[&](const wchar_t *name,const RestoreJson &expected){
    const auto output=retained/name;app.documentOperation("document.save",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(output)},{"overwrite",false}});
    app.documentOperation("document.open",departureOpenRequest(app,output));restoreCheck(assets()==expected,"F04 save/reopen changed exact PCM or stable sample identities");
  };
  reopened(L"rendered.screamseq",rendered);
  unsigned choices=0,imports=0;app.sampleImportChooser=[&](bool mapped){++choices;restoreCheck(!mapped,"Parity WAVs unexpectedly mapped to instruments");return std::vector{root/L"audio/Parity_C3.wav",root/L"audio/Parity_G3.wav",root/L"audio/Parity_C4.wav"};};
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="sample.importMany"){++imports;throw std::runtime_error("Owned Parity WAV import completion loss");}};
  restoreRejected([&]{app.importSample(false);});
  restoreCheck(choices==1&&imports==1&&app.view->samples.size()==rendered.size()+3&&!app.sampleBrowser->snapshot().at("importCompletion").is_null(),"Supplied WAV import lost its retained result");
  const auto window=app.sampleBrowser->window();ScreamSeq::Tests::ownGuiWindow(window);
  SendMessageW(window,WM_COMMAND,5414,reinterpret_cast<LPARAM>(GetDlgItem(window,5414)));
  restoreCheck(choices==1&&imports==1&&app.sampleBrowser->snapshot().at("importCompletion").is_null(),"Supplied WAV Review repeated chooser or import");app.sampleMutationCompletion={};
  const auto imported=assets();app.edit("history.undo",{{"domain","all"}});restoreCheck(assets()==rendered,"Supplied WAV import did not have one exact Undo");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(assets()==imported,"Supplied WAV Redo changed exact PCM/identities");reopened(L"imported.screamseq",imported);
  restoreCheck(ScreamSeq::Project::readProjectBytes(input)==originalBytes,"Original Mac reference was modified");
  std::ofstream report(retained/L"assets.json",std::ios::binary);
  report<<RestoreJson({{"before",before},{"rendered",rendered},{"imported",imported},
    {"renders",renders},{"imports",imports},{"scope","Windows owned native render/import recovery; reciprocal qualification pending"}}).dump(2)<<'\n';
  report.close();restoreCheck(bool(report),"Retain exact parity PCM/identity observations");
  std::cout<<"Retained parity outputs: "<<utf8Path(retained)<<'\n';fixture.close();
}
