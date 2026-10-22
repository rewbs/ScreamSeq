// Native Mac adapter over the same detached input decoder used by Windows.
static void sessionPluginRecoveryReview() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  check([[NSFileManager defaultManager]createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:&error],"Create owned recovery fixture folder");
  NSString *source=[folder stringByAppendingPathComponent:@"original.screamseq"],*copy=[folder stringByAppendingPathComponent:@"copy.screamseq"];
  check([session savePath:source error:&error],"Save initial recovery fixture");
  NSMutableDictionary *tree=[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:source]
    options:NSPropertyListMutableContainersAndLeaves format:nil error:&error];
  check([tree isKindOfClass:NSMutableDictionary.class],"Read recovery fixture tree");
  NSDictionary *original=@{@"version":@1,@"reviewOnly":@YES,@"document":@"original-document",@"revision":@"original-revision",
    @"take":@"previous-process",@"accepted":@"9007199254740993",@"lost":@"2",@"previewError":@"",
    @"events":@[@{@"plugin":@"removed-plugin",@"parameter":@17,@"value":@(.123456789012345),
      @"hostTime":@"9007199254740995",@"state":@"unknown",@"error":@"Original delivery uncertain"}]};
  NSMutableDictionary *future=[original mutableCopy];future[@"version"]=@2;
  NSArray *archives=@[future,original];tree[@"screamseqPluginControlRecovery"]=archives;
  NSData *input=[NSPropertyListSerialization dataWithPropertyList:tree format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
  check(input&&[input writeToFile:source options:NSDataWritingAtomic error:&error]&&[session openPath:source error:&error],"Open detached input fixture");
  auto call=[&](NSString *method,NSDictionary *params)->NSDictionary * {
    NSError *failure=nil;auto reply=[session automationMethod:method params:params error:&failure];
    if(!reply)throw std::runtime_error(failure.localizedDescription.UTF8String);
    check([NSJSONSerialization isValidJSONObject:reply],"Recovery read returned non-JSON project data");return reply[@"data"];
  };
  const auto before=session.serializedData;NSString *revision=session.automationRevision;
  auto catalogue=call(@"automation.recovery.list",@{});
  check([catalogue[@"total"] unsignedIntValue]==2&&![catalogue[@"entries"][0][@"headerSupported"] boolValue]&&
    [catalogue[@"entries"][1][@"headerSupported"] boolValue],"Future archive hid independent valid input");
  NSDictionary *params=@{@"expectedRevision":revision,@"kind":@"nativeControls",@"index":@1};
  auto page=call(@"automation.recovery.get",params);NSDictionary *event=page[@"events"][0];
  for(NSString *key in original[@"events"][0])check([event[key] isEqual:original[@"events"][0][key]],"Recovery read changed original input");
  check([page[@"summary"][@"accepted"] isEqual:original[@"accepted"]]&&[page[@"summary"][@"document"] isEqual:original[@"document"]],"Recovery read narrowed counters or changed original identity");
  auto reject=[&](NSString *method,NSDictionary *p,int code){NSError *failure=nil;
    check(![session automationMethod:method params:p error:&failure]&&failure.code==code,"Invalid detached read was accepted or misclassified");};
  reject(@"automation.recovery.get",@{@"expectedRevision":revision,@"kind":@"nativeControls",@"index":@0},-32602);
  reject(@"automation.recovery.get",@{@"expectedRevision":@"stale",@"kind":@"nativeControls",@"index":@1},-32001);
  reject(@"automation.recovery.get",@{@"expectedRevision":@"",@"kind":@"nativeControls",@"index":@1},-32602);
  reject(@"automation.recovery.get",@{@"expectedRevision":revision,@"kind":@"nativeControls",@"index":@YES},-32602);
  reject(@"automation.recovery.list",@{@"limit":@YES},-32602);
  reject(@"automation.recovery.list",@{@"limit":@129},-32602);
  reject(@"automation.recovery.list",@{@"unexpected":@1},-32602);
  check([revision isEqual:session.automationRevision]&&[before isEqual:session.serializedData],"Recovery reads changed current music/history");
  check([session savePath:copy error:&error],"Save reviewed native copy");
  auto saved=[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:copy] options:0 format:nil error:&error];
  check([saved[@"screamseqPluginControlRecovery"] isEqual:archives]&&[input isEqual:[NSData dataWithContentsOfFile:source]],"Review rewrote archive/source evidence");
  [session shutdown];check([[NSFileManager defaultManager]removeItemAtPath:folder error:&error],"Remove owned recovery fixture folder");
}
