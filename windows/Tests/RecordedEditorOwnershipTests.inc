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
  std::function<void()> contextHook;
  const auto context=[&]{if(auto hook=std::exchange(contextHook,{}))hook();return std::pair{stale?std::string("stale"):revision(),uint64_t(41)};};
  const auto sequence=document->historySequence();
  const auto beforeStops=stops,beforeManual=manual;
  const auto module=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());
  need(module!=nullptr,"Recorded ownership fixture module was not retained");
  const auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(module,"ResonanceFixtureGesture"));
  need(gesture!=nullptr,"Recorded ownership fixture gesture export is missing");
  const auto gestureOnRead=reinterpret_cast<void(*)(double)>(GetProcAddress(module,"ResonanceFixtureGestureOnStateRead"));
  need(gestureOnRead!=nullptr,"Recorded ownership reentrant state fixture export is missing");
  const auto send=[&](double value){WindowsVST3::pluginMainCall([&]{need(gesture(value)==1,"Owned vendor gesture failed");});};
  const auto refuses=[&](auto work){
    bool rejected=false;try{work();}catch(const std::exception &){rejected=true;}
    need(rejected,"Recorded ownership allowed an unsafe operation");
  };
  const auto protectedRecovery=[&]{
    auto copy=project;
    need(!operations.overlayRecoveryState(copy)&&Project::encodePlist(Project::nativeProjectTree(*document,copy))==encoded(),
      "Recovery folded recorded vendor values into the saved manual baseline");
    need(operations.overlayRecordedRecovery(copy),"Recovery omitted an owned plugin gesture take");
    const auto archived=Project::serializeNativeProject(*document,copy);
    const auto &takes=copy.preserved.at("screamseqPluginGestureRecovery");
    need(takes.size()==1&&takes[0].at("reviewOnly")==true&&takes[0].at("version")==1&&
      takes[0].at("providers").size()==1&&takes[0].at("providers")[0].at("plugin")==id,
      "Recovery archive lost its review-only marker or stable provider identity");
    need(operations.overlayRecordedRecovery(copy)&&Project::serializeNativeProject(*document,copy)==archived,
      "Repeated recovery capture duplicated or changed the same retained take");
    auto reopened=Project::openNativeProjectBytes(archived);
    const auto &recoveredTake=reopened.state.preserved.at("screamseqPluginGestureRecovery")[0];
    need(recordedGestureRecovery(readRecordedGestureRecovery(recoveredTake))==recoveredTake,
      "Native plist reopen changed the shared detached review contract");
    need(Project::encodePlist(reopened.state.preserved.at("screamseqPluginGestureRecovery"))==Project::encodePlist(takes)&&
      Project::encodePlist(reopened.state.preserved.at("plugins"))==Project::encodePlist(project.preserved.at("plugins"))&&
      projectAbsoluteAutomation(reopened.state)==projectAbsoluteAutomation(project),
      "Recovery roundtrip changed raw gesture evidence, saved vendor state or accepted automation");
    copy.preserved.erase("screamseqPluginGestureRecovery");
    need(Project::serializeNativeProject(*document,copy)==encoded(),
      "Gesture recovery changed unrelated project content");
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
  protectedRecovery();
  {
    auto copy=project;operations.overlayRecordedRecovery(copy);
    const auto &target=copy.preserved.at("screamseqPluginGestureRecovery")[0].at("targets")[0];
    const auto &original=take->target(0);
    need(target.at("events").size()==2&&target.at("events")[0].at("value")==.25f&&
      target.at("events")[1].at("value")==.75f&&target.at("unmapped")==original.loss().unmapped&&
      !target.at("events")[1].contains("frame"),
      "Recovery after refused preview lost raw order or invented uncovered frames");
    for(size_t i=0;i<original.events().size();++i) {
      const auto &event=original.events()[i];const auto &saved=target.at("events")[i];
      need(saved.at("hostTime")==std::to_string(event.hostTime)&&saved.contains("frame")==event.frame.has_value()&&
        (!event.frame||saved.at("frame")==std::to_string(*event.frame)),
        "Recovery did not preserve exact original timestamps and mapped frames");
    }
    copy.preserved["screamseqPluginGestureRecovery"][0]["take"]="earlier-recovered-take";
    operations.overlayRecordedRecovery(copy);
    need(copy.preserved.at("screamseqPluginGestureRecovery").size()==2&&
      copy.preserved.at("screamseqPluginGestureRecovery")[0].at("take")=="earlier-recovered-take",
      "A new capture overwrote an earlier recovery archive");
  }
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
  uint64_t end=operations.sealRecordedEditors();
  need(operations.sealRecordedEditors()==end,"Repeated Stop moved the original native boundary");
  need(clock->publish(arm+1,end,1,959,48000,41),"Owned take coverage failed");clock->close(41,end);
  refuses([&]{(void)take->preparedOwners(revision(),41);});
  need(operations.pollRecordedEditors(),"Fixed native release cut was not acknowledged");
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
  // A fixed Stop cut must finish in bounded polls without consuming a later
  // gesture. Refused restoration must not even call the audible restore hook.
  stale=false;operations.invoke("plugin.editor.open",{{"slot",0}});operations.flushEditors(true);
  const auto fencedBaseline=encoded();const auto fencedSequence=document->historySequence();
  clock=std::make_shared<RecordingFrameTimeline>();const auto arm3=ScreamSeq::hostTime100ns();
  need(clock->publish(arm3,arm3+1,0,1,48000,41),"Fixed cut clock failed");
  const std::array<RecordedGestureTake::Provider,1> providers3{{{id,operations.parameterEditSource(id)}}};
  take=std::make_shared<RecordedGestureTake>("fixed-cut",revision(),arm3,clock,rack,projectAbsoluteAutomation(project),providers3);
  operations.beginRecordedEditors(take,context,[&](std::span<const ParameterChange> points){audible.assign(points.begin(),points.end());});
  WindowsVST3::pluginMainCall([&]{for(unsigned i=0;i<130;++i)need(gesture(i%2?.8:.2)==1,"Fixed cut gesture failed");});
  end=operations.sealRecordedEditors();
  need(clock->publish(arm3+1,end,1,959,48000,41),"Fixed cut coverage failed");clock->close(41,end);
  Sleep(2);send(.7);
  need(!operations.pollRecordedEditors()&&take->eventCount()==128,"Fixed cut bypassed bounded polling");
  need(operations.pollRecordedEditors()&&take->eventCount()==130&&audible.size()==1&&audible[0].value==.8f,
    "Stop consumed or previewed a later gesture");
  need(operations.pollRecordedEditors()&&take->eventCount()==130&&operations.sealRecordedEditors()==end,
    "Repeated stopped polling consumed later input or changed the boundary");
  operations.captureRecordedRecoveryInput();
  need(take->eventCount()==130&&take->endTime()==end,"Recovery crossed an already sealed Stop boundary");
  need(take->preparedOwners(revision(),41).size()==1,"Fixed cut did not complete its captured input drain");
  unsigned restores=0;
  refuses([&]{operations.releaseRecordedEditors(true,[&]{++restores;return true;});});
  need(!restores&&operations.hasRecordedEditors()&&encoded()==fencedBaseline&&document->historySequence()==fencedSequence,
    "Post-stop input was discarded or restoration changed state before refusing");
  operations.invoke("plugin.editor.close",{{"slot",0}});
  need(operations.releaseRecordedEditors(true,[&]{++restores;return true;})&&restores==1&&!operations.hasRecordedEditors()&&
    encoded()==fencedBaseline&&document->historySequence()==fencedSequence,
    "Explicit closed-provider discard failed to retain the accepted baseline");
  // A callback arriving after a caller's last manual flush must not be silently
  // claimed by a new take. Refused arming leaves both the notification and its
  // eventual ordinary history edit intact.
  operations.invoke("plugin.editor.open",{{"slot",0}});operations.flushEditors(true);
  const auto beforeArm=encoded();const auto beforeArmSequence=document->historySequence();
  clock=std::make_shared<RecordingFrameTimeline>();const auto arm4=ScreamSeq::hostTime100ns();
  need(clock->publish(arm4,arm4+1,0,1,48000,41),"Arming guard clock failed");
  const std::array<RecordedGestureTake::Provider,1> providers4{{{id,operations.parameterEditSource(id)}}};
  take=std::make_shared<RecordedGestureTake>("unflushed",revision(),arm4,clock,rack,projectAbsoluteAutomation(project),providers4);
  send(.375);unsigned armPreviews=0; // The fixture's attached baseline is .625.
  refuses([&]{operations.beginRecordedEditors(take,context,[&](auto){++armPreviews;});});
  need(!operations.hasRecordedEditors()&&!armPreviews&&!take->eventCount()&&encoded()==beforeArm&&
    document->historySequence()==beforeArmSequence,"Refused arm consumed input or changed state/history");
  operations.flushEditors(true);
  const auto manualParameters=operations.invoke("plugin.parameters.get",{{"slot",0}});
  const auto pendingManual=std::find_if(manualParameters.begin(),manualParameters.end(),[](const auto &p){return p.at("id")==7;});
  need(pendingManual!=manualParameters.end()&&pendingManual->at("manualValue")==.375f&&
    document->historySequence()==beforeArmSequence+1,"Arming refusal lost the earlier manual gesture or its one history edit");
  operations.history(false,[&](bool redo,bool){if(redo)document->redo();else document->undo();});
  need(encoded()==beforeArm,"Unflushed gesture did not retain exact manual Undo after refused arming");
  // Recovery captures more than one ordinary poll's input, without previewing
  // or turning the snapshot cut into Stop. Inject a later callback after the
  // fixed native cut and before the first drain; it belongs to the next poll.
  operations.invoke("plugin.editor.open",{{"slot",0}});operations.flushEditors(true);
  const auto beforeRecovery=encoded();const auto beforeRecoverySequence=document->historySequence();
  const auto beforeRecoveryManual=manual,beforeRecoveryStops=stops;
  clock=std::make_shared<RecordingFrameTimeline>();const auto arm5=ScreamSeq::hostTime100ns();
  need(clock->publish(arm5,arm5+1,0,1,48000,41),"Recovery prefix clock failed");
  const std::array<RecordedGestureTake::Provider,1> providers5{{{id,operations.parameterEditSource(id)}}};
  take=std::make_shared<RecordedGestureTake>("recovery-prefix",revision(),arm5,clock,rack,projectAbsoluteAutomation(project),providers5);
  unsigned recoveryPreviews=0;
  operations.beginRecordedEditors(take,context,[&](auto){++recoveryPreviews;throw Api::ApiError(-32002,"Controlled recovery preview queue refusal");});
  for(unsigned i=0;i<130;++i)send(i%2?.75:.25);
  contextHook=[&]{send(.875);};
  operations.captureRecordedRecoveryInput();
  auto recoveryCopy=project;need(operations.overlayRecordedRecovery(recoveryCopy),"Recovery prefix archive missing");
  const auto recoveryReview=readRecordedGestureRecovery(recoveryCopy.preserved.at("screamseqPluginGestureRecovery")[0]);
  need(take->eventCount()==130&&recoveryReview.targets.size()==1&&recoveryReview.targets[0].events.size()==130&&
    !recoveryReview.endTime&&!recoveryReview.sources[0].drained&&!recoveryPreviews&&
    encoded()==beforeRecovery&&manual==beforeRecoveryManual&&stops==beforeRecoveryStops&&
    document->historySequence()==beforeRecoverySequence,"Recovery changed playback/history, sealed Stop, previewed or crossed its prefix");
  for(size_t i=0;i<130;++i)need(recoveryReview.targets[0].events[i].value==(i%2?.75f:.25f)&&
    recoveryReview.targets[0].events[i].hostTime==take->target(0).events()[i].hostTime,
    "Recovery prefix changed raw input order, value or original timestamp");
  refuses([&]{operations.pollRecordedEditors();});
  need(take->eventCount()==131&&take->target(0).events().back().value==.875f&&recoveryPreviews==1,
    "Recovery consumed later input or lost preview retry ownership");
  operations.captureRecordedRecoveryInput();
  need(take->eventCount()==131&&recoveryPreviews==1,"Repeated recovery retried an audible preview or duplicated input");
  operations.sealRecordedEditors();operations.invoke("plugin.editor.close",{{"slot",0}});
  need(operations.releaseRecordedEditors(true,[]{return true;})&&encoded()==beforeRecovery,
    "Recovery-prefix discard changed the accepted baseline");
  // Baseline construction/state inspection may execute a nested native pump.
  // A callback then must refuse release before the audible restore callback.
  operations.invoke("plugin.editor.open",{{"slot",0}});operations.flushEditors(true);
  const auto beforeRestore=encoded();const auto beforeRestoreSequence=document->historySequence();
  clock=std::make_shared<RecordingFrameTimeline>();const auto arm6=ScreamSeq::hostTime100ns();
  need(clock->publish(arm6,arm6+1,0,1,48000,41),"Restore preparation clock failed");
  const std::array<RecordedGestureTake::Provider,1> providers6{{{id,operations.parameterEditSource(id)}}};
  take=std::make_shared<RecordedGestureTake>("restore-input",revision(),arm6,clock,rack,projectAbsoluteAutomation(project),providers6);
  operations.beginRecordedEditors(take,context,[](auto){});send(.25);operations.pollRecordedEditors();operations.sealRecordedEditors();
  WindowsVST3::pluginMainCall([&]{gestureOnRead(.875);});
  unsigned reentrantRestores=0;
  refuses([&]{operations.releaseRecordedEditors(true,[&]{++reentrantRestores;return true;});});
  need(!reentrantRestores&&operations.hasRecordedEditors()&&take->eventCount()==1&&encoded()==beforeRestore&&
    document->historySequence()==beforeRestoreSequence,"Baseline preparation swallowed input or restored audio before refusing");
  // A second attempt still sees the callback: it was not consumed as host
  // restoration noise. Only explicitly closing/discarding may abandon it.
  refuses([&]{operations.releaseRecordedEditors(true,[&]{++reentrantRestores;return true;});});
  need(!reentrantRestores,"Refused restoration consumed its later native input");
  operations.invoke("plugin.editor.close",{{"slot",0}});
  need(operations.releaseRecordedEditors(true,[]{return true;})&&encoded()==beforeRestore,
    "Explicit discard after nested native input changed the baseline");
  std::cout<<"PASS recovery recorded editor exclusion, guarded arming, fixed Stop cuts, bounded original intake, preview retry, native close/restore, one Undo and persistence\n";
}
