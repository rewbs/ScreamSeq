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
  check(!live.endTime&&live.targets.size()==2&&live.targets[0].events.size()==1&&live.targets[1].events.size()==1&&
    live.targets[0].events[0].hostTime==firstTime&&live.targets[1].events[0].hostTime==secondTime&&
    live.targets[0].events[0].frame&&live.targets[1].events[0].frame&&
    *live.targets[0].events[0].frame<*live.targets[1].events[0].frame,
    "Real-session recovery lost original-time input or sealed the live take");
  check([recovering[@"plugins"] isEqual:baseline[@"plugins"]]&&
    [recovering[@"screamseqPluginGestureRecovery"] count]==1&&session.playing,
    "Recovery baked preview into plugin state, lost evidence or stopped playback");
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
  [session stop];check([session releaseStoppedParameterTake:NO],"Verify stopped native baselines after Keep");
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
    check([page[@"total"] unsignedIntegerValue]==1&&[page[@"events"][0][@"timestamp"] isEqual:timestamps[index]]&&
      [page[@"events"][0][@"frame"] isKindOfClass:NSString.class],"API event paging lost original timestamps or frame mapping");
  }
  NSDictionary *keepAPI=api(@"automation.recording.commit",@{@"take":token,@"acknowledgeLoss":@NO},true);
  NSString *keptAPIRevision=session.automationRevision;
  check([keepAPI[@"changed"] boolValue]&&[keepAPI[@"data"][@"completed"] boolValue]&&
    [keepAPI[@"data"][@"retained"] boolValue]&&keepAPI[@"data"][@"restorationError"]&&session.playing&&
    ![keptAPIRevision isEqual:apiRevision],"Live API Keep lost committed-but-retained outcome");
  error=nil;
  check(![session automationMethod:@"automation.recording.discard" params:@{@"take":token,@"expectedRevision":keptAPIRevision} error:&error]&&
    error.code==-32002&&[session.automationRevision isEqual:keptAPIRevision]&&session.playing,
    "Discard accepted a kept take or changed music/transport while refusing it");
  api(@"transport.stop",@{},true);
  NSDictionary *releasedAPI=api(@"automation.recording.commit",@{@"take":token},true);
  check(![releasedAPI[@"changed"] boolValue]&&![releasedAPI[@"data"][@"retained"] boolValue]&&
    [session.automationRevision isEqual:keptAPIRevision],"API restoration retry published duplicate history or lost completion");
  NSDictionary *apiSaved=tree();
  check([apiSaved[@"automation"] count]==2&&[apiSaved[@"plugins"] isEqual:baseline[@"plugins"]],"API Keep changed accepted plugin state");
  check([session historyUndo:NO error:&error]&&[tree()[@"automation"] isEqual:baseline[@"automation"]],"API take was not one Undo edit");
  check([session historyUndo:YES error:&error]&&[tree()[@"automation"] isEqual:apiSaved[@"automation"]],"API take Redo changed input");
  check([session savePath:path error:&error]&&[reopened openPath:path error:&error],"Save/reopen API-created native take");
  NSDictionary *apiReopened=[NSPropertyListSerialization propertyListWithData:reopened.serializedData options:0 format:nil error:nil];
  check([apiReopened[@"automation"] isEqual:apiSaved[@"automation"]]&&[apiReopened[@"plugins"] isEqual:baseline[@"plugins"]],
    "API take persistence changed frames, plugin state or stable identities");
  [reopened shutdown];[session shutdown];
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  std::cout<<"PASS real-session multi-target parameter take, direct admission, recovery, one-step Keep/Undo/Redo, stopped verification, save/reopen, empty and Discard\n";
}
