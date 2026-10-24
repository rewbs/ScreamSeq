// Internal lifecycle qualification on an explicitly selected virtual device.
// This uses the real session clock and native VST3 providers; no fabricated
// timeline, private ivar injection or test-only production installation path.
@interface TrackerSession (ParameterTakeSessionChecks)
- (void)startParameterTake:(NSArray<NSString *> *)identities expectedRevision:(NSString *)revision;
- (void)pollParameterTake:(BOOL)stop;
- (BOOL)captureParameterTake:(NSString *)identity parameter:(NSInteger)parameter value:(double)value timestamp:(uint64_t)timestamp;
- (Tracker::RecordedGestureTake::Review)parameterTakeReview;
- (Tracker::NativeParameterTake::CommitResult)commitParameterTake:(BOOL)acknowledgeLoss;
- (BOOL)releaseStoppedParameterTake:(BOOL)discard;
- (BOOL)releaseParameterTake:(BOOL)discard;
- (BOOL)discardStoppedParameterTake;
@end
static void parameterTakeSessionLifecycle(const PluginDescriptor &descriptor,uint32_t device,uint32_t frames) {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  check([session addPlugin:dictionary(descriptor) error:&error]&&[session addPlugin:dictionary(descriptor) error:&error],
    "Prepare two native session take providers");
  check([session pluginParameter:0 identifier:7 value:.2 record:NO error:&error]&&
        [session pluginParameter:1 identifier:7 value:.3 record:NO error:&error],"Prepare independent manual baselines");
  check([session editPattern:0 row:7 channel:3 values:@[@55,@0,@0,@0,@0,@0] error:&error],"Prepare unrelated musical edit");
  NSDictionary *snapshot=[session snapshot:0];
  NSArray *identities=@[snapshot[@"nativePlugins"][0][@"instanceID"],snapshot[@"nativePlugins"][1][@"instanceID"]];
  auto tree=[&]() -> NSDictionary * {
    NSData *data=[session recoveryData:&error];check(data!=nil,"Serialize retained take recovery");
    NSDictionary *value=[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:&error];
    check(value!=nil,"Decode retained take recovery");return value;
  };
  NSDictionary *baseline=tree();
  check(session.parameterRecordingTakeID==nil,"A session without a retained take exposes no recovery owner");
  check([session configureDevice:device buffer:frames error:&error],"Select explicit virtual output for session take");
  [session setPlaybackLoop:YES];
  auto start=[&] {
    check([session playOrder:0 error:&error],"Start timed native session playback");
    // Real Core Audio callbacks must establish timing. Only the initial absent
    // clock is retryable here; any other admission failure fails the test.
    for(unsigned attempt=0;;++attempt) {
      try{[session startParameterTake:identities expectedRevision:session.automationRevision];break;}
      catch(const std::exception &e){
        if(attempt>=200||std::string(e.what())!="Start timed song playback before recording plugin parameters")throw;
        [NSThread sleepForTimeInterval:.005];
      }
    }
  };
  auto denied=[&](BOOL result) {
    check(!result&&error&&[error.localizedDescription containsString:@"retained parameter take"],
      "Native editing bypassed retained take admission or failed for an unrelated reason");
    error=nil;
  };
  start();const auto armed=[session parameterTakeReview];NSString *revision=session.automationRevision;
  NSString *retainedID=session.parameterRecordingTakeID;
  for(id recoveryFlag in @[@YES,@1,@"true",NSNull.null]) {
    error=nil;
    check(![session automationMethod:@"automation.recording.commit"
      params:@{@"expectedRevision":revision,@"take":retainedID,@"restoreSavedState":recoveryFlag} error:&error]&&
      error.code==([recoveryFlag isEqual:@YES]&&CFGetTypeID((__bridge CFTypeRef)recoveryFlag)==CFBooleanGetTypeID()?-32002:-32602)&&
      [session.automationRevision isEqual:revision]&&![session parameterTakeReview].completed,
      "Saved-state recovery accepted an unkept take or malformed boolean, or changed history");
  }
  error=nil;
  check([retainedID isEqual:@(armed.token.c_str())]&&session.recordingTakeID==nil,
    "Parameter recovery identity is missing or incorrectly exposed as resumable MIDI");
  NSDictionary *emptyRecovery=tree();
  denied([session editPattern:0 row:0 channel:0 values:@[@62,@0,@0,@0,@0,@0] error:&error]);
  denied([session editCells:@[@{@"pattern":@0,@"row":@0,@"channel":@0,@"values":@[@62,@0,@0,@0,@0,@0]}] error:&error]);
  denied([session editOrder:0 pattern:0 operation:@"insert" error:&error]);
  denied([session selectSequence:0 error:&error]);
  denied([session addPattern:32 duplicate:NO source:0 error:&error]>=0);
  denied([session removeOrder:0 error:&error]);
  denied([session songTitle:@"Refused" tempo:140 speed:6 channels:8 error:&error]);
  denied([session importSample:@"/nonexistent/take.wav" slot:1 error:&error]>=0);
  denied([session processSample:1 operation:@"reverse" start:0 end:0 error:&error]);
  denied([session sampleSettings:1 values:@{@"volume":@32} error:&error]);
  denied([session addInstrument:1 error:&error]>=0);
  denied([session importInstrument:@"/nonexistent/take.iti" slot:1 error:&error]>=0);
  denied([session instrumentSettings:1 values:@{} error:&error]);
  denied([session refreshDevice:&error]);
  denied([session configureDevice:device buffer:frames error:&error]);
  denied([session createSong:NO error:&error]);
  denied([session historyUndo:NO error:&error]);
  denied([session pluginParameter:0 identifier:7 value:.9 record:NO error:&error]);
  check(session.playing&&[session.automationRevision isEqual:revision]&&
    [[session snapshot:0][@"cells"] isEqual:snapshot[@"cells"]]&&[session parameterTakeReview].token==armed.token,
    "Refused direct edits changed music, playback, history or the take owner");
  const auto firstTime=mach_absolute_time();
  check([session captureParameterTake:identities[0] parameter:7 value:.4 timestamp:firstTime],"Capture first native control");
  [NSThread sleepForTimeInterval:.03];
  const auto secondTime=mach_absolute_time();
  check([session captureParameterTake:identities[1] parameter:7 value:.6 timestamp:secondTime],"Capture second native control");
  [NSThread sleepForTimeInterval:.03];
  NSDictionary *recovering=tree();const auto live=[session parameterTakeReview];
  const bool recoveryIntact=!live.endTime&&live.targets.size()==2&&live.targets[0].events.size()==1&&live.targets[1].events.size()==1&&
    live.targets[0].events[0].hostTime==firstTime&&live.targets[1].events[0].hostTime==secondTime&&
    live.targets[0].events[0].frame&&live.targets[1].events[0].frame&&
    *live.targets[0].events[0].frame<*live.targets[1].events[0].frame;
  if(!recoveryIntact) {
    // Keep the original strict gate. An aggregate failure does not establish
    // whether input was lost, timing was absent, or ownership changed. Emit the
    // already captured review; do not poll, retry, extend sleeps or remap time.
    const auto stamp=[](std::optional<uint64_t> value){return value?std::to_string(*value):std::string("none");};
    std::cerr<<"RECOVERY_DIAGNOSTIC start="<<live.startTime<<" end="<<stamp(live.endTime)
      <<" stoppedEnd="<<stamp(live.stoppedEndTime)<<" epoch="<<live.playbackEpoch
      <<" completed="<<live.completed<<" failure="<<int(live.failure)<<" rejected="<<live.rejected
      <<" expectedTimes="<<firstTime<<","<<secondTime<<" targets="<<live.targets.size()<<"\n";
    for(const auto &source:live.sources)std::cerr<<"RECOVERY_SOURCE plugin="<<source.initial.plugin
      <<" incarnation="<<source.initial.source.incarnation<<" initialDropped="<<source.initial.source.dropped
      <<" dropped="<<source.dropped<<" drained="<<source.drained<<"\n";
    for(const auto &target:live.targets) {
      std::cerr<<"RECOVERY_TARGET plugin="<<target.key.plugin<<" parameter="<<target.key.parameter
        <<" events="<<target.events.size()<<" sourceLoss="<<target.loss.source
        <<" overflow="<<target.loss.overflow<<" unmapped="<<target.loss.unmapped<<"\n";
      for(size_t i=0;i<std::min(size_t(4),target.events.size());++i){const auto &event=target.events[i];
        std::cerr<<"RECOVERY_INPUT index="<<i<<" originalTime="<<event.hostTime
          <<" frame="<<stamp(event.frame)<<" value="<<event.value<<"\n";}
      // This existing read does not poll or resolve the take. Inspect original
      // timestamp coverage after capturing the failed review, without changing
      // its result or manufacturing a clock for a slow native device startup.
      NSError *timingError=nil;
      NSDictionary *timing=[session automationMethod:@"automation.recording.events"
        params:@{@"take":retainedID,@"plugin":@(target.key.plugin.c_str()),
          @"parameter":@(target.key.parameter),@"offset":@0,@"limit":@4,@"includeTiming":@YES}
        error:&timingError];
      NSData *timingJSON=timing?[NSJSONSerialization dataWithJSONObject:timing options:0 error:&timingError]:nil;
      if(timingJSON){std::cerr<<"RECOVERY_CLOCK ";std::cerr.write(static_cast<const char *>(timingJSON.bytes),timingJSON.length);std::cerr<<"\n";}
      else std::cerr<<"RECOVERY_CLOCK_UNAVAILABLE "<<(timingError.localizedDescription.UTF8String?:"No timing projection")<<"\n";
    }
  }
  check(recoveryIntact,"Real-session recovery lost original-time input or sealed the live take");
  check([recovering[@"plugins"] isEqual:baseline[@"plugins"]]&&
    [recovering[@"screamseqPluginGestureRecovery"] count]==1&&session.playing,
    "Recovery baked preview into plugin state, lost evidence or stopped playback");
  check([session.parameterRecordingTakeID isEqual:retainedID]&&[session.automationRevision isEqual:revision]&&
    ![recovering[@"screamseqPluginGestureRecovery"] isEqual:emptyRecovery[@"screamseqPluginGestureRecovery"]],
    "Consecutive recovery snapshots must retain new input without requiring a musical revision");
  NSString *recoveryPath=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@"-retained-recovery.screamseq"]];
  NSDictionary *controlInput=@{@"version":@1,@"reviewOnly":@YES,
    @"document":[revision componentsSeparatedByString:@":"].firstObject,@"revision":revision,@"take":retainedID,
    @"accepted":@"0",@"lost":@"0",@"previewError":@"",
    @"events":@[@{@"plugin":identities[0],@"parameter":@7,@"value":@(0.4),@"hostTime":@(std::to_string(firstTime).c_str()),
      @"state":@"unknown",@"error":@"Completion unavailable"}]};
  NSData *recoveryBytes=[session recoveryDataWithNativeInput:controlInput error:&error];
  check(recoveryBytes!=nil,"Snapshot original native control delivery evidence with the matching session take");
  NSDictionary *withInput=[NSPropertyListSerialization propertyListWithData:recoveryBytes options:0 format:nil error:&error];
  check([withInput[@"screamseqPluginControlRecovery"] isEqual:@[controlInput]]&&
    [session.automationRevision isEqual:revision]&&[session parameterTakeReview].targets[0].events.size()==1,
    "Pending/unknown control evidence was lost, replayed into the take, or published as music");
  for(NSString *key in @[@"document",@"revision",@"take"]) {
    NSMutableDictionary *wrong=[controlInput mutableCopy];wrong[key]=@"different-owner";error=nil;
    check(![session recoveryDataWithNativeInput:wrong error:&error]&&error&&
      [error.localizedDescription containsString:@"another parameter take"]&&[session.automationRevision isEqual:revision]&&
      [session.parameterRecordingTakeID isEqual:retainedID],"Stale native input evidence bypassed its document/take/revision guard");
  }
  error=nil;
  check([recoveryBytes writeToFile:recoveryPath options:NSDataWritingAtomic error:&error],
    "Write retained-input recovery fixture");
  TrackerSession *recovered=[TrackerSession new];
  check([recovered openPath:recoveryPath error:&error]&&recovered.parameterRecordingTakeID==nil&&recovered.recordingTakeID==nil,
    "Recovery must not recreate live plugin ownership or a resumable MIDI take");
  NSDictionary *detached=[NSPropertyListSerialization propertyListWithData:[recovered recoveryData:&error] options:0 format:nil error:&error];
  check([detached[@"screamseqPluginGestureRecovery"] isEqual:withInput[@"screamseqPluginGestureRecovery"]]&&
    [detached[@"screamseqPluginControlRecovery"] isEqual:@[controlInput]]&&
    [detached[@"plugins"] isEqual:baseline[@"plugins"]],"Reopened recovery changed detached input evidence or accepted plugin baselines");
  [recovered shutdown];[[NSFileManager defaultManager] removeItemAtPath:recoveryPath error:nil];
  [session pollParameterTake:YES];const auto stopped=[session parameterTakeReview];
  [session pollParameterTake:YES];check([session parameterTakeReview].endTime==stopped.endTime&&session.playing,
    "Take Stop moved its cut or stopped transport");
  const auto kept=[session commitParameterTake:NO];NSString *committed=session.automationRevision;
  check(kept.accepted&&kept.changed&&![committed isEqual:revision]&&[session parameterTakeReview].completed&&session.playing,
    "Whole-take Keep failed to publish a single live musical edit");
  const auto retry=[session commitParameterTake:NO];
  check(retry.accepted&&!retry.changed&&[session.automationRevision isEqual:committed],"Completed Keep published twice");
  bool refused=false;try{[session releaseStoppedParameterTake:NO];}catch(const std::exception &){refused=true;}
  check(refused&&session.playing&&[session parameterTakeReview].completed,
    "Stopped restoration touched an active vendor or lost committed ownership");
  check([session.parameterRecordingTakeID isEqual:retainedID],"Kept-but-unrestored input lost its recovery owner");
  error=nil;
  check(![session automationMethod:@"automation.recording.commit"
    params:@{@"expectedRevision":committed,@"take":retainedID,@"restoreSavedState":@YES} error:&error]&&
    error.code==-32002&&[session.automationRevision isEqual:committed]&&session.playing,
    "Saved-state recovery touched an active completed take");
  [session stop];error=nil;
  NSDictionary *repaired=[session automationMethod:@"automation.recording.commit"
    params:@{@"expectedRevision":committed,@"take":retainedID,@"restoreSavedState":@YES} error:&error];
  check(repaired&&!error&&![repaired[@"data"][@"retained"] boolValue]&&
    [repaired[@"data"][@"completed"] boolValue]&&![repaired[@"changed"] boolValue]&&
    !repaired[@"data"][@"restorationError"]&&[session.automationRevision isEqual:committed],
    "Explicit saved-state API recovery changed kept music/history or failed to release the stopped take");
  check(session.parameterRecordingTakeID==nil,"Finished restoration retained a stale recovery owner");
  NSDictionary *saved=tree();
  check([saved[@"automation"] count]==2&&[saved[@"plugins"] isEqual:baseline[@"plugins"]]&&
    [[session snapshot:0][@"cells"] isEqual:snapshot[@"cells"]],"Keep lost unrelated music or accepted manual state");
  const auto firstFrame=*live.targets[0].events[0].frame,secondFrame=*live.targets[1].events[0].frame;
  check([saved[@"automation"][0][0] unsignedIntegerValue]==0&&[saved[@"automation"][0][3] unsignedLongLongValue]==firstFrame&&
    std::abs([saved[@"automation"][0][2] doubleValue]-.4)<1e-6&&
    [saved[@"automation"][1][0] unsignedIntegerValue]==1&&[saved[@"automation"][1][3] unsignedLongLongValue]==secondFrame&&
    std::abs([saved[@"automation"][1][2] doubleValue]-.6)<1e-6,"Keep did not preserve both original-time targets");
  check([session historyUndo:NO error:&error],"Undo complete session take");
  check([tree()[@"automation"] isEqual:baseline[@"automation"]]&&
    [[session snapshot:0][@"cells"] isEqual:snapshot[@"cells"]],"Take Undo split targets or undid unrelated music");
  check([session historyUndo:YES error:&error]&&[tree()[@"automation"] isEqual:saved[@"automation"]],"Redo complete session take");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@"-take.screamseq"]];
  check([session savePath:path error:&error],"Save committed session take");
  TrackerSession *reopened=[TrackerSession new];
  check([reopened openPath:path error:&error],"Reopen committed session take");
  NSDictionary *recalled=[NSPropertyListSerialization propertyListWithData:reopened.serializedData options:0 format:nil error:nil];
  check([recalled[@"automation"] isEqual:saved[@"automation"]]&&[recalled[@"plugins"] isEqual:baseline[@"plugins"]],
    "Reopen changed take points, stable plugin identities or opaque baselines");
  // A stopped empty take is a no-op, then explicit Discard preserves the
  // preceding kept timeline and the chronological Undo head.
  start();NSString *beforeEmpty=session.automationRevision;
  [session pollParameterTake:YES];const auto empty=[session commitParameterTake:NO];
  check(empty.accepted&&!empty.changed&&[session.automationRevision isEqual:beforeEmpty],"Empty take created history");
  [session stop];check([session releaseStoppedParameterTake:NO],"Release empty native session take");
  start();NSString *beforeDiscard=session.automationRevision;
  check([session captureParameterTake:identities[0] parameter:7 value:.8 timestamp:mach_absolute_time()],"Capture discarded session input");
  [NSThread sleepForTimeInterval:.03];[session stop];
  check([session releaseStoppedParameterTake:YES]&&[session.automationRevision isEqual:beforeDiscard]&&
    [tree()[@"automation"] isEqual:saved[@"automation"]]&&[tree()[@"plugins"] isEqual:baseline[@"plugins"]],
    "Discard changed accepted music or manual state");
  check([session historyUndo:NO error:&error]&&[tree()[@"automation"] isEqual:baseline[@"automation"]],
    "Empty/Discard inserted history above the last kept take");
  // The explicit closed-editor release is independently callable and retains
  // the same musical/history contract; normal Keep never chooses it implicitly.
  start();NSString *beforeExplicit=session.automationRevision;
  check([session captureParameterTake:identities[0] parameter:7 value:.7 timestamp:mach_absolute_time()],"Capture explicitly discarded session input");
  [NSThread sleepForTimeInterval:.03];[session stop];
  check([session discardStoppedParameterTake]&&[session.automationRevision isEqual:beforeExplicit]&&
    [tree()[@"automation"] isEqual:baseline[@"automation"]]&&[tree()[@"plugins"] isEqual:baseline[@"plugins"]],
    "Explicit Discard changed the session baseline or music");
  // Exercise the published API adapter against the real device/session clock.
  // Musical success plus deferred restoration is a successful retained result,
  // not an error that encourages replaying original input or creating history twice.
  const auto api=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    NSMutableDictionary *request=[params mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    error=nil;NSDictionary *reply=[session automationMethod:method params:request error:&error];
    check(reply!=nil,error.localizedDescription.UTF8String?:"Native parameter API request failed");return reply;
  };
  check([session playOrder:0 error:&error],"Start API take playback");NSDictionary *armedAPI=nil;
  for(unsigned attempt=0;;++attempt) {
    error=nil;armedAPI=[session automationMethod:@"automation.recording.start"
      params:@{@"expectedRevision":session.automationRevision,@"plugins":identities} error:&error];
    if(armedAPI)break;
    check(attempt<200&&[error.localizedDescription isEqual:@"Start timed song playback before recording plugin parameters"],
      "API take admission failed for a reason other than the initial real clock");[NSThread sleepForTimeInterval:.005];
  }
  NSString *token=armedAPI[@"data"][@"take"],*apiRevision=session.automationRevision;
  NSMutableArray *timestamps=[NSMutableArray array];
  for(NSUInteger index=0;index<identities.count;++index) {
    NSString *stamp=api(@"automation.recording.get",@{})[@"data"][@"hostTime"];[timestamps addObject:stamp];
    NSDictionary *capturedAPI=api(@"automation.recording.capture",@{@"take":token,@"plugin":identities[index],
      @"parameter":@7,@"value":@(index ? .65 : .45),@"timestamp":stamp},true);
    check([capturedAPI[@"data"][@"accepted"] boolValue],"API original-time native control was not accepted");
    [NSThread sleepForTimeInterval:.03];
  }
  NSDictionary *stopAPI=api(@"automation.recording.stop",@{@"take":token},true);
  check([stopAPI[@"data"][@"retained"] boolValue]&&![stopAPI[@"data"][@"capturing"] boolValue]&&session.playing,
    "API Stop dropped the take or stopped playback");
  for(NSUInteger index=0;index<identities.count;++index) {
    NSDictionary *page=api(@"automation.recording.events",@{@"take":token,@"plugin":identities[index],@"parameter":@7,@"limit":@1})[@"data"];
    const bool pageIntact=[page[@"total"] unsignedIntegerValue]==1&&[page[@"events"][0][@"timestamp"] isEqual:timestamps[index]]&&
      [page[@"events"][0][@"frame"] isKindOfClass:NSString.class];
    if(!pageIntact) {
      // Read-only failure evidence, before the unchanged strict gate. A missing
      // frame alone cannot distinguish absent coverage from lost original input.
      NSError *timingError=nil;
      NSDictionary *timing=[session automationMethod:@"automation.recording.events"
        params:@{@"take":token,@"plugin":identities[index],@"parameter":@7,@"limit":@1,@"includeTiming":@YES} error:&timingError];
      NSDictionary *evidence=@{@"plugin":identities[index],@"expectedTimestamp":timestamps[index],@"page":page,
        @"stopped":stopAPI,@"timing":timing?:@{},@"timingError":timingError.localizedDescription?:@""};
      NSData *json=[NSJSONSerialization dataWithJSONObject:evidence options:0 error:&timingError];
      if(json){std::cerr<<"API_PAGE_CLOCK ";std::cerr.write(static_cast<const char *>(json.bytes),json.length);std::cerr<<"\n";}
      else std::cerr<<"API_PAGE_CLOCK_UNAVAILABLE "<<(timingError.localizedDescription.UTF8String?:"No timing projection")<<"\n";
    }
    check(pageIntact,"API event paging lost original timestamps or frame mapping");
    NSDictionary *inspection=api(@"automation.recording.events",@{@"take":token,@"plugin":identities[index],
      @"parameter":@7,@"limit":@1,@"includeTiming":@YES})[@"data"];
    NSMutableDictionary *original=[inspection[@"events"][0] mutableCopy];
    check([original[@"timing"][@"status"] isEqual:@"retained-frame"],"Mac API omitted shared timing inspection");
    [original removeObjectForKey:@"timing"];
    check([original isEqual:page[@"events"][0]]&&[session.automationRevision isEqual:apiRevision],
      "Mac timing read changed original input or document history");
    for(NSDictionary *invalid in @[@{@"includeTiming":@1,@"limit":@1},@{@"includeTiming":@YES,@"limit":@17}]) {
      NSMutableDictionary *request=[@{@"take":token,@"plugin":identities[index],@"parameter":@7} mutableCopy];
      [request addEntriesFromDictionary:invalid];error=nil;
      check(![session automationMethod:@"automation.recording.events" params:request error:&error]&&error.code==-32602&&
        [session.automationRevision isEqual:apiRevision],"Invalid Mac timing inspection changed history or passed admission");
    }
  }
  NSDictionary *beforeRelease=api(@"transport.get",@{})[@"data"];
  NSDictionary *keepAPI=api(@"automation.recording.commit",@{@"take":token,@"acknowledgeLoss":@NO},true);
  NSString *keptAPIRevision=session.automationRevision;
  check([keepAPI[@"changed"] boolValue]&&[keepAPI[@"data"][@"completed"] boolValue]&&
    !keepAPI[@"data"][@"restorationError"]&&session.playing&&![keptAPIRevision isEqual:apiRevision],
    "Live API Keep failed to commit without an opaque-state restoration error");
  if([keepAPI[@"data"][@"retained"] boolValue]) {
    check([keepAPI[@"data"][@"restorationPending"] boolValue],"Kept VST3 take retained without a processing receipt");
    error=nil;
    check(![session automationMethod:@"automation.recording.discard" params:@{@"take":token,@"expectedRevision":keptAPIRevision} error:&error]&&
      error.code==-32002&&[session.automationRevision isEqual:keptAPIRevision]&&session.playing,
      "Discard accepted a kept take or changed music/transport while refusing it");
  }
  const auto finishLive=[&](NSString *method,NSDictionary *initial,NSString *identity,NSString *expectedRevision) {
    NSDictionary *reply=initial;
    for(unsigned attempt=0;[reply[@"data"][@"retained"] boolValue];++attempt) {
      check(attempt<200&&[reply[@"data"][@"restorationPending"] boolValue]&&!reply[@"data"][@"restorationError"]&&session.playing,
        "Live native release failed, stopped playback or exceeded bounded processing retries");
      [NSThread sleepForTimeInterval:.01];reply=api(method,@{@"take":identity},true);
      check(![reply[@"changed"] boolValue]&&[session.automationRevision isEqual:expectedRevision],
        "Live restoration retry changed chronological history");
    }
    check(session.parameterRecordingTakeID==nil&&session.playing,"Live release lost playback or retained a stale input owner");
  };
  finishLive(@"automation.recording.commit",keepAPI,token,keptAPIRevision);
  [NSThread sleepForTimeInterval:.02];NSDictionary *afterRelease=api(@"transport.get",@{})[@"data"];
  check([afterRelease[@"audioActive"] boolValue]&&![afterRelease[@"pluginFailure"] boolValue]&&![afterRelease[@"fault"] boolValue]&&
    [afterRelease[@"frames"] unsignedLongLongValue]>[beforeRelease[@"frames"] unsignedLongLongValue]&&
    [afterRelease[@"callbacks"] unsignedLongLongValue]>[beforeRelease[@"callbacks"] unsignedLongLongValue]&&
    [afterRelease[@"region"] isEqual:beforeRelease[@"region"]],"Native Keep/release interrupted transport progress or its playback region");
  NSDictionary *apiSaved=tree();
  check([apiSaved[@"automation"] count]==2&&[apiSaved[@"plugins"] isEqual:baseline[@"plugins"]],"API Keep changed accepted plugin state");
  check([session historyUndo:NO error:&error]&&session.playing&&[tree()[@"automation"] isEqual:baseline[@"automation"]],"API take was not one live Undo edit");
  check([session historyUndo:YES error:&error]&&session.playing&&[tree()[@"automation"] isEqual:apiSaved[@"automation"]],"API take live Redo changed input");
  check([session savePath:path error:&error]&&[reopened openPath:path error:&error],"Save/reopen API-created native take");
  NSDictionary *apiReopened=[NSPropertyListSerialization propertyListWithData:reopened.serializedData options:0 format:nil error:nil];
  check([apiReopened[@"automation"] isEqual:apiSaved[@"automation"]]&&[apiReopened[@"plugins"] isEqual:baseline[@"plugins"]],
    "API take persistence changed frames, plugin state or stable identities");
  NSDictionary *discardArm=api(@"automation.recording.start",@{@"plugins":identities},true);
  NSString *discardToken=discardArm[@"data"][@"take"],*discardRevision=session.automationRevision;
  NSString *discardStamp=api(@"automation.recording.get",@{})[@"data"][@"hostTime"];
  check([api(@"automation.recording.capture",@{@"take":discardToken,@"plugin":identities[0],@"parameter":@7,
    @"value":@.85,@"timestamp":discardStamp},true)[@"data"][@"accepted"] boolValue],"Capture live API Discard input");
  [NSThread sleepForTimeInterval:.03];api(@"automation.recording.stop",@{@"take":discardToken},true);
  NSDictionary *discardAPI=api(@"automation.recording.discard",@{@"take":discardToken},true);
  check(![discardAPI[@"changed"] boolValue]&&[session.automationRevision isEqual:discardRevision],"Live Discard changed the accepted revision");
  finishLive(@"automation.recording.discard",discardAPI,discardToken,discardRevision);
  NSDictionary *discarded=tree();
  check([discarded[@"automation"] isEqual:apiSaved[@"automation"]]&&[discarded[@"plugins"] isEqual:apiSaved[@"plugins"]]&&
    [[session snapshot:0][@"cells"] isEqual:snapshot[@"cells"]],"Live Discard altered accepted plugin state or unrelated music");
  [reopened shutdown];[session shutdown];
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  std::cout<<"PASS real-session multi-target parameter take, direct admission, recovery, one-step live Keep/Undo/Redo, live release/Discard, stopped verification, save/reopen and empty take"
    <<"; live frames "<<[beforeRelease[@"frames"] unsignedLongLongValue]<<" -> "<<[afterRelease[@"frames"] unsignedLongLongValue]
    <<", callbacks "<<[beforeRelease[@"callbacks"] unsignedLongLongValue]<<" -> "<<[afterRelease[@"callbacks"] unsignedLongLongValue]<<'\n';
}
