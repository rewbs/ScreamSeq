// The real Windows plugin/session history owner and shared timeline, with
// controlled input times. This does not exercise native UI/vendor ingestion.
void recordedGestureCommitTests(const std::filesystem::path &directory) {
  using namespace Tracker;
  using Capture=RecordedGestureCapture;
  auto document=Document::demo();auto project=Project::newProjectState(*document);
  unsigned stops=0,publications=0;
  PluginOperations operations(*document,project,[&]{++stops;});
  const auto library=operations.invoke("plugin.discover",{{"format","Built-in"}});
  const auto gain=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});
  need(gain!=library.end(),"Capture fixture Gainer missing");
  operations.invoke("plugin.add",{{"descriptor",*gain}});
  operations.invoke("plugin.add",{{"descriptor",*gain}});
  // Imported integer/real forms are deliberately mixed. Untouched rows must
  // retain their typed representation, not merely compare numerically equal.
  project.preserved["automation"]=Json::array({Json::array({0,1,-6,240}),Json::array({1,1,-3.,120})});
  project.preserved["captureFixtureExtension"]={{"data",Json::binary({2,4,6})},{"negativeZero",-0.}};
  const auto rack=projectPluginStates(project);
  const auto before=projectAbsoluteAutomation(project);
  const auto baseline=project.preserved;
  const auto projectBytes=[&]{return Project::encodePlist(Project::nativeProjectTree(*document,project));};
  const auto baselineBytes=projectBytes();
  PluginChain chain(rack,48000,true,before);
  operations.recordedPublication([&](const auto &points)->std::function<void()> {
    auto prepared=std::make_shared<std::unique_ptr<RecordedAutomationPlan>>(chain.prepareRecordedAutomation(points));
    return [&,prepared] {
      if(!chain.publishRecordedAutomation(std::move(*prepared)))throw Api::ApiError(-32002,"Controlled recorded publication is busy");
      ++publications;
    };
  });
  auto render=[&](float decibels) {
    std::array<float,256> pcm;
    for(unsigned n=0;n<32;++n){pcm.fill(.25f);need(chain.process(pcm.data(),128),"Captured timeline failed to render");}
    need(std::abs(pcm.back()-.25*std::pow(10.,decibels/20.))<1e-6,"Native capture commit lost a target in rendered PCM");
  };
  render(-9);
  const auto revision=[&]{return std::to_string(document->revision)+":"+std::to_string(project.pluginRevision);};
  auto clock=std::make_shared<RecordingFrameTimeline>();
  need(clock->publish(1000,1200,0,960,48000,7),"Capture commit clock fixture failed");
  const auto sharedBefore=std::make_shared<const std::vector<ParameterChange>>(before);
  std::vector<std::unique_ptr<Capture>> retained;
  std::vector<Capture *> owners;
  for(size_t i=0;i<rack.size();++i) {
    const auto source=operations.parameterEditSource(rack[i].instanceID);
    auto owner=std::make_unique<Capture>(Capture::Key{"native-gesture",revision(),rack[i].instanceID,1,7,source.incarnation,1000},
      clock,rack,sharedBefore,document->native(),operations.parameterMetadata(rack[i].instanceID),source.dropped);
    owner->append({1,i?-12.f:-9.f,i?1100u:1050u},7,source.incarnation);owner->seal(1150);
    owners.push_back(owner.get());retained.push_back(std::move(owner));
  }
  const auto sequence=document->historySequence(),pluginRevision=project.pluginRevision;
  const auto stopped=stops;
  // Saturate the real prepared timeline queue before asking the native owner
  // to commit. No fake success, relaxed assertions or device dependencies.
  for(unsigned n=0;n<3;++n)need(chain.publishRecordedAutomation(chain.prepareRecordedAutomation(before)),"Could not fill the recorded publication queue");
  bool rejected=false;
  try{operations.commitRecordedGestures(owners,revision(),7);}catch(const std::exception &){rejected=true;}
  need(rejected&&projectBytes()==baselineBytes&&document->historySequence()==sequence&&
    project.pluginRevision==pluginRevision&&!owners[0]->retired()&&!owners[1]->retired()&&stops==stopped,
    "Publication refusal changed saved music/history or consumed part of a gesture");
  render(-9);
  rejected=false;
  try{operations.commitRecordedGestures(owners,"another-revision",7);}catch(const std::exception &){rejected=true;}
  need(rejected&&projectBytes()==baselineBytes,"Stale capture changed project state");
  need(operations.commitRecordedGestures(owners,revision(),7)&&owners[0]->retired()&&owners[1]->retired(),
    "Native capture commit did not retire the complete accepted gesture");
  need(document->historySequence()==sequence+1&&project.pluginRevision==pluginRevision+1&&publications==1&&stops==stopped,
    "Several captured targets did not share one live Undo transaction");
  const auto captured=project.preserved;
  const auto capturedBytes=projectBytes();
  need(Project::encodePlist(captured.at("plugins"))==Project::encodePlist(baseline.at("plugins"))&&
    Project::encodePlist(captured.at("captureFixtureExtension"))==Project::encodePlist(baseline.at("captureFixtureExtension"))&&
    Project::encodePlist(captured.at("automation")[1])==Project::encodePlist(baseline.at("automation")[1]),
    "Captured movement normalized unrelated values or entered the opaque manual baseline");
  render(-21);
  const auto path=directory/"captured-multiple-parameters.screamseq";
  Project::saveNativeProject(*document,project,path,true);
  auto reopened=Project::openNativeProject(path);
  need(projectAbsoluteAutomation(reopened.state)==projectAbsoluteAutomation(project)&&
    Project::encodePlist(reopened.state.preserved.at("plugins"))==Project::encodePlist(captured.at("plugins")),
    "Captured points or opaque manual baselines failed native save/reopen");
  const auto history=[&](bool redo){operations.history(redo,[&](bool direction,bool){if(direction)document->redo();else document->undo();});};
  history(false);render(-9);
  need(projectBytes()==baselineBytes&&operations.canRedo()&&stops==stopped,
    "One native Undo did not restore the exact capture baseline");
  // An identical capture is retired without consuming the existing Redo.
  retained.clear();owners.clear();
  const auto source=operations.parameterEditSource(rack[0].instanceID);
  auto noOp=std::make_unique<Capture>(Capture::Key{"no-op",revision(),rack[0].instanceID,1,7,source.incarnation,1000},
    clock,rack,sharedBefore,document->native(),operations.parameterMetadata(rack[0].instanceID),source.dropped);
  noOp->append({1,-6,1050},7,source.incarnation);noOp->seal(1150);owners.push_back(noOp.get());
  const auto beforeNoOpRevision=revision();
  const auto beforeNoOpSequence=document->historySequence();
  need(!operations.commitRecordedGestures(owners,revision(),7)&&noOp->retired()&&operations.canRedo()&&
    revision()==beforeNoOpRevision&&document->historySequence()==beforeNoOpSequence,
    "No-op capture consumed Redo or added history");
  history(true);render(-21);
  need(projectBytes()==capturedBytes&&stops==stopped,
    "Captured gesture Redo did not restore all targets without stopping playback");
  std::cout<<"PASS Windows captured multi-parameter commit: queue refusal, one history entry, typed/opaque preservation, no-op Redo, PCM and save/reopen\n";
}
