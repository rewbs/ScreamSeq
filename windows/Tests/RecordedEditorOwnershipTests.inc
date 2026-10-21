// Real retained Windows vendor/editor ownership. Runs on the existing owned
// private desktop through the recovery-manual worker qualification fixture.
void recordedEditorOwnershipTests(const std::filesystem::path &scanner,const std::filesystem::path &cache,
    const std::filesystem::path &directory) {
  using namespace Tracker;
  WindowsVST3::configure(scanner.generic_string(),cache.generic_string());
  auto document=Document::demo();auto project=Project::newProjectState(*document);
  unsigned stops=0,manual=0,previews=0,publications=0;bool rejectPreview=true,stale=false;
  std::vector<ParameterChange> audible;
  PluginOperations operations(*document,project,[&]{++stops;},[&](auto){++manual;});
  Json descriptor;
  for(const auto &p:operations.invoke("plugin.discover",{{"format","VST3"}}))
    if(p.at("classID")=="5245534F4E414E434546464543540001")descriptor=p;
  need(!descriptor.is_null(),"Recorded ownership fixture is unavailable");
  operations.invoke("plugin.add",{{"descriptor",descriptor}});
  operations.invoke("plugin.editor.open",{{"slot",0}});
  operations.flushEditors(true); // Attached fixture gesture is manual, before arm.
  const auto rack=projectPluginStates(project);const auto id=rack[0].instanceID;
  const auto baseline=project.preserved;
  const auto encoded=[&]{return Project::encodePlist(Project::nativeProjectTree(*document,project));};
  const auto baselineBytes=encoded();
  const auto revision=[&]{return std::to_string(document->revision)+":"+std::to_string(project.pluginRevision);};
  const auto context=[&]{return std::pair{stale?std::string("stale"):revision(),uint64_t(41)};};
  const auto sequence=document->historySequence();
  const auto beforeStops=stops,beforeManual=manual;
  const auto module=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());
  need(module!=nullptr,"Recorded ownership fixture module was not retained");
  const auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(module,"ResonanceFixtureGesture"));
  need(gesture!=nullptr,"Recorded ownership fixture gesture export is missing");
  const auto send=[&](double value){WindowsVST3::pluginMainCall([&]{need(gesture(value)==1,"Owned vendor gesture failed");});};
  const auto refuses=[&](auto work){
    bool rejected=false;try{work();}catch(const std::exception &){rejected=true;}
    need(rejected,"Recorded ownership allowed an unsafe operation");
  };
  const auto protectedRecovery=[&]{
    auto copy=project;
    need(!operations.overlayRecoveryState(copy)&&Project::encodePlist(Project::nativeProjectTree(*document,copy))==encoded(),
      "Recovery folded recorded vendor values into the saved manual baseline");
  };
  auto clock=std::make_shared<RecordingFrameTimeline>();const auto arm=ScreamSeq::hostTime100ns();
  need(clock->publish(arm,arm+1,0,1,48000,41),"Owned recording clock failed");
  const auto source=operations.parameterEditSource(id);
  const std::array<RecordedGestureTake::Provider,1> providers{{{id,source}}};
  auto take=std::make_shared<RecordedGestureTake>("owned","revision-placeholder",arm,clock,rack,projectAbsoluteAutomation(project),providers);
  refuses([&]{operations.beginRecordedEditors(take,context,[](auto){});});
  need(!operations.hasRecordedEditors(),"Rejected arming partially claimed vendor state");
  take=std::make_shared<RecordedGestureTake>("owned",revision(),arm,clock,rack,projectAbsoluteAutomation(project),providers);
  operations.beginRecordedEditors(take,context,[&](std::span<const ParameterChange> points){
    ++previews;if(rejectPreview)throw Api::ApiError(-32002,"Controlled transient preview refusal");
    audible.assign(points.begin(),points.end());
  });
  need(operations.hasRecordedEditors(),"Native worker did not retain its capture owner");
  send(.25);Sleep(2);send(.75);
  protectedRecovery(); // Before notification consumption, too.
  refuses([&]{operations.flushEditors(true);});
  need(take->eventCount()==2&&previews==1&&manual==beforeManual&&encoded()==baselineBytes&&
    document->historySequence()==sequence&&operations.openEditorCount()==1&&stops==beforeStops,
    "Preview refusal lost input, captured manual state, closed the editor or changed history");
  send(.5);rejectPreview=false;operations.flushEditors(true);
  need(take->eventCount()==3&&previews==2&&audible==std::vector<ParameterChange>{{0,7,.5f,0}}&&
    encoded()==baselineBytes&&manual==beforeManual,"Preview retry coalesced raw input or leaked into manual publication");
  protectedRecovery();
  const auto beforeIdle=previews;operations.flushEditors(true);
  need(previews==beforeIdle&&encoded()==baselineBytes,"Idle poll repeated transient publication or captured opaque state");

  // One worker poll may consume at most 128 notifications from this provider.
  WindowsVST3::pluginMainCall([&]{for(unsigned i=0;i<130;++i)need(gesture(i%2?.8:.2)==1,"Dense owned gesture failed");});
  need(!operations.pollRecordedEditors()&&take->eventCount()==131,"First native drain did not enforce its 128-event work bound");
  need(operations.pollRecordedEditors()&&take->eventCount()==133,"Bounded continuation lost or repeated native notifications");
  uint64_t end=0;WindowsVST3::pluginMainCall([&]{end=ScreamSeq::hostTime100ns();});
  take->seal(end);
  need(clock->publish(arm+1,end,1,959,48000,41),"Owned take coverage failed");clock->close(41,end);
  refuses([&]{(void)take->preparedOwners(revision(),41);});
  need(operations.pollRecordedEditors(true),"Native empty drain after release was not acknowledged");
  need(take->target(0).loss().unmapped==0,"Native provider polls did not resolve original timestamps");
  refuses([&]{operations.invoke("plugin.parameters.set",{{"plugin",id},{"values",Json::array({{{"id",7},{"value",.1}}})}});});
  refuses([&]{operations.invoke("plugin.remove",{{"plugins",Json::array({id})}});});
  refuses([&]{operations.invokeAutomation("automation.recorded.clear",Json::object());});
  refuses([&]{operations.history(false,[&](bool,bool){throw std::runtime_error("History must not execute");});});
  need(encoded()==baselineBytes&&document->historySequence()==sequence&&manual==beforeManual&&stops==beforeStops,
    "An owned recording allowed an unrelated plugin mutation or history movement");

  const auto catalog=operations.parameterMetadata(id);
  RecordedGestureCapture other(take->target(0).key(),clock,rack,projectAbsoluteAutomation(project),document->native(),catalog,source.dropped);
  other.append({7,.4f,arm+1},41,source.incarnation);other.seal(end);
  const std::array<RecordedGestureCapture *,1> substitute{{&other}};
  refuses([&]{operations.commitRecordedGestures(substitute,revision(),41);});
  bool rejectPublication=true;
  operations.recordedPublication([&](const auto &)->std::function<void()>{
    return [&]{
      if(rejectPublication)throw Api::ApiError(-32002,"Controlled recorded publication refusal");
      ++publications;
    };
  });
  refuses([&]{take->commit(revision(),41,false,[&](auto owners,bool loss){
    operations.commitRecordedGestures(owners,revision(),41,loss);return true;
  });});
  need(!take->completed()&&!take->target(0).retired()&&take->eventCount()==133&&
    operations.hasRecordedEditors()&&encoded()==baselineBytes&&
    document->historySequence()==sequence&&publications==0&&stops==beforeStops,
    "Refused owned publication lost its raw take, baseline, history or provider protection");
  // The temporary publication exception must not leave the manual-write guard
  // open after a failed commit.
  refuses([&]{operations.invoke("plugin.parameters.set",{{"plugin",id},{"values",Json::array({{{"id",7},{"value",.1}}})}});});
  protectedRecovery();rejectPublication=false;
  bool changed=false;
  need(take->commit(revision(),41,false,[&](auto owners,bool loss){
    changed=operations.commitRecordedGestures(owners,revision(),41,loss);return true;
  })&&changed&&publications==1&&document->historySequence()==sequence+1&&stops==beforeStops,
    "Owned native recording did not commit once through ordinary recorded history");
  need(Project::encodePlist(project.preserved.at("plugins"))==Project::encodePlist(baseline.at("plugins")),
    "Recorded commit changed the saved opaque plugin baseline");
  const auto committed=encoded();protectedRecovery();
  need(!operations.releaseRecordedEditors(false,[]{return false;})&&operations.hasRecordedEditors()&&
    encoded()==committed&&operations.parameterEditSource(id)==source,
    "Busy preview restoration released or replaced a protected provider");
  protectedRecovery();
  need(operations.releaseRecordedEditors(false,[]{return true;})&&!operations.hasRecordedEditors()&&
    operations.openEditorCount()==1&&operations.parameterEditSource(id)==source,
    "Ordinary parameter restoration lost the retained native editor or provider identity");
  operations.flushEditors(true);
  need(encoded()==committed&&document->historySequence()==sequence+1&&manual==beforeManual,
    "Release leaked a recorded or restoration notification into another manual edit");
  const auto path=directory/"recorded-editor-ownership.screamseq";
  Project::saveNativeProject(*document,project,path,true);
  auto reopened=Project::openNativeProject(path);
  need(Project::encodePlist(reopened.state.preserved.at("plugins"))==Project::encodePlist(project.preserved.at("plugins"))&&
    projectAbsoluteAutomation(reopened.state)==projectAbsoluteAutomation(project),
    "Owned editor capture lost recorded points or manual baselines on save/reopen");
  operations.history(false,[&](bool redo,bool){if(redo)document->redo();else document->undo();});
  need(encoded()==baselineBytes&&operations.canRedo(),"Owned capture did not restore the exact baseline in one Undo");
  operations.history(true,[&](bool redo,bool){if(redo)document->redo();else document->undo();});
  need(encoded()==committed,"Owned capture did not restore the exact committed tree in one Redo");
  operations.history(false,[&](bool redo,bool){if(redo)document->redo();else document->undo();});
  need(encoded()==baselineBytes&&operations.canRedo(),"Preparing discard coverage lost the exact baseline or Redo");

  // A context failure still permits explicit native Close and stopped discard;
  // it must never drop the provider into ordinary manual/recovery capture.
  clock=std::make_shared<RecordingFrameTimeline>();const auto arm2=ScreamSeq::hostTime100ns();
  need(clock->publish(arm2,arm2+1,0,1,48000,41),"Discard ownership clock failed");
  const auto source2=operations.parameterEditSource(id);
  const std::array<RecordedGestureTake::Provider,1> providers2{{{id,source2}}};
  take=std::make_shared<RecordedGestureTake>("discard",revision(),arm2,clock,rack,projectAbsoluteAutomation(project),providers2);
  operations.beginRecordedEditors(take,context,[](auto){});
  send(.875);operations.pollRecordedEditors();stale=true;
  refuses([&]{operations.flushEditors(true);});
  need(take->failure()==RecordedGestureTake::Failure::Context&&take->eventCount()==1&&encoded()==baselineBytes,
    "Context failure discarded the captured event or changed the baseline");
  protectedRecovery();
  end=ScreamSeq::hostTime100ns();take->seal(end);
  operations.invoke("plugin.editor.close",{{"slot",0}});
  need(operations.hasRecordedEditors()&&operations.openEditorCount()==0,
    "Closing a failed recorded editor dropped its protected retained provider");
  need(operations.releaseRecordedEditors(true,[]{return true;})&&!operations.hasRecordedEditors()&&
    operations.parameterEditSource(id).incarnation!=source2.incarnation&&encoded()==baselineBytes&&operations.canRedo(),
    "Explicit stopped discard lost the baseline/Redo or kept the closed transient vendor");
  std::cout<<"PASS recovery recorded editor exclusion, bounded original intake, preview retry, native close/restore, one Undo and persistence\n";
}
