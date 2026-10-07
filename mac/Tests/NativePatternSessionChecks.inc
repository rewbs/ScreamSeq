static void nativePatternSessionChecks(){
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *p,bool write=false)->NSDictionary *{
    auto request=[p mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    auto response=[session automationMethod:method params:request error:&error];
    if(!response)throw std::runtime_error(error.localizedDescription.UTF8String);return response;
  };
  const auto catalog=call(@"pattern.commands",@{})[@"data"][@"native"];
  check([catalog count]==nativePatternCommands().size()+7,"Shared native catalog must include every operation and existing native alias");
  const auto baseline=call(@"pattern.effects.get",@{@"pattern":@0})[@"data"];
  NSDictionary *effect=@{@"kind":@"native",@"native":@"vibrato",@"parameters":@{@"depth":@0.123456789012345,@"rate":@2.3456789012345,@"rateMode":@"beat",@"shape":@"triangle",@"phase":@0.125,@"reset":@YES},@"offset":@1234,@"duration":@70001};
  NSDictionary *edit=@{@"pattern":@0,@"row":@3,@"channel":@1,@"column":@0,@"command":effect};
  auto preview=[edit mutableCopy];preview[@"dryRun"]=@YES;
  check(![call(@"pattern.effect.set",preview,true)[@"changed"] boolValue]&&[baseline isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Native single-cell dry run must preserve all existing commands");
  NSString *stale=session.automationRevision;
  check([call(@"pattern.effect.set",edit,true)[@"changed"] boolValue],"Native multi-field command must commit atomically");
  const auto accepted=call(@"pattern.effects.get",@{@"pattern":@0})[@"data"];
  check(![call(@"pattern.effect.set",edit,true)[@"changed"] boolValue],"Identical native command must be a no-op");
  auto old=[edit mutableCopy];old[@"expectedRevision"]=stale;NSError *failure=nil;
  check(![session automationMethod:@"pattern.effect.set" params:old error:&failure]&&failure.code==-32001,"Stale native write must reject");
  for(NSDictionary *parameters in @[@{@"depth":@YES},@{@"rateMode":@0},@{@"shape":@"unknown"},@{@"typo":@1},@{@"reset":@1}]){
    auto badEffect=[effect mutableCopy];badEffect[@"parameters"]=parameters;auto bad=[edit mutableCopy];bad[@"command"]=badEffect;bad[@"expectedRevision"]=session.automationRevision;
    failure=nil;check(![session automationMethod:@"pattern.effect.set" params:bad error:&failure]&&failure.code==-32602,"Native parameter types and unknown names must reject before mutation");
    check([accepted isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Invalid native fields changed unrelated commands");
  }
  call(@"history.undo",@{},true);check([baseline isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Native multi-field Undo must be one transaction");
  call(@"history.redo",@{},true);check([accepted isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Native Redo must preserve every double and selector");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Native multi-field project must save and reopen");
  check([accepted isEqual:call(@"pattern.effects.get",@{@"pattern":@0})[@"data"]],"Native project must retain named parameters exactly");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
