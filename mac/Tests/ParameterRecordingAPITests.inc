static void parameterRecordingAPIAdmission(TrackerSession *session,NSString *identity) {
  NSError *error=nil;NSString *revision=session.automationRevision;NSData *baseline=session.serializedData;
  NSDictionary *description=[session automationMethod:@"api.describe" params:@{} error:&error];
  NSArray *methods=description[@"data"][@"pluginParameterRecording"][@"methods"];
  for(NSString *name in @[@"get",@"events",@"start",@"capture",@"stop",@"commit",@"discard"])
    require([methods containsObject:[@"automation.recording." stringByAppendingString:name]],"Mac API omitted retained recording method");
  NSDictionary *empty=[session automationMethod:@"automation.recording.get" params:@{} error:&error];
  require(empty&&!error&&![empty[@"data"][@"retained"] boolValue]&&
    [empty[@"data"][@"clock"] isEqual:@"mach-absolute-time"]&&[empty[@"data"][@"hostTime"] isKindOfClass:NSString.class]&&
    [empty[@"data"][@"timebaseDenominator"] unsignedLongLongValue]>0,
    "Empty Mac take read omitted original native clock or fabricated an owner");
  const auto reject=[&](NSString *method,NSDictionary *params,NSInteger code) {
    error=nil;NSDictionary *reply=[session automationMethod:method params:params error:&error];
    require(!reply&&error.code==code&&[session.automationRevision isEqual:revision]&&[session.serializedData isEqual:baseline],
      "Invalid retained recording request changed music/state or returned the wrong error");
  };
  reject(@"automation.recording.get",@{@"unknown":@1},-32602);
  reject(@"automation.recording.start",@{@"expectedRevision":@"stale",@"plugins":@[identity]},-32001);
  reject(@"automation.recording.start",@{@"expectedRevision":@"",@"plugins":@[identity]},-32602);
  for(id plugins in @[@[],@[identity,identity],@[@"missing"],@[@YES],@YES])
    reject(@"automation.recording.start",@{@"expectedRevision":revision,@"plugins":plugins},-32602);
  reject(@"automation.recording.start",@{@"expectedRevision":revision,@"plugins":@[identity],@"dryRun":@YES},-32602);
  reject(@"automation.recording.start",@{@"expectedRevision":revision,@"plugins":@[identity]},-32002);
  for(NSString *method in @[@"automation.recording.stop",@"automation.recording.commit",@"automation.recording.discard"])
    reject(method,@{@"expectedRevision":revision,@"take":@"missing"},-32001);
  reject(@"automation.recording.events",@{@"take":@"missing",@"plugin":identity,@"parameter":@1},-32001);
  reject(@"automation.recording.capture",@{@"expectedRevision":revision,@"take":@"missing",@"plugin":identity,
    @"parameter":@1,@"value":@0.5,@"timestamp":@"1"},-32001);
}
