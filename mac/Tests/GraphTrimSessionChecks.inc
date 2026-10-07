static void graphTrimSessionChecks(){
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;};
  NSString *graph=call(@"graph.create",@{@"name":@"Port trim test"},true)[@"data"][@"graph"];
  NSString *node=call(@"graph.node.add",@{@"graph":graph,@"kind":@"plugin",@"plugin":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1"}},true)[@"data"][@"node"];
  auto target=@{@"graph":graph,@"node":node};
  auto read=[&](){return call(@"graph.trim.get",target)[@"data"];};
  auto set=[&](NSDictionary *values){auto p=[target mutableCopy];[p addEntriesFromDictionary:values];return call(@"graph.trim.set",p,true);};
  auto initial=read();check([initial[@"ports"] count]==2,"Recipe trim ports missing");NSString *revision=session.automationRevision;
  set(@{@"port":@"i:0",@"gainDB":@0});check([revision isEqual:session.automationRevision],"Unity trim created history");
  set(@{@"port":@"i:0",@"linkTo":@"o:0",@"gainDB":@(-12),@"dryRun":@YES});check([revision isEqual:session.automationRevision],"Trim dry run changed document");
  set(@{@"port":@"i:0",@"linkTo":@"o:0",@"gainDB":@(-12)});auto paired=read();check([paired[@"ports"][0][@"gainDB"] doubleValue]==-12&&[paired[@"ports"][1][@"gainDB"] doubleValue]==12,"Trim inverse edit failed");
  check(![session automationMethod:@"graph.trim.set" params:@{@"graph":graph,@"node":node,@"port":@"i:0",@"gainDB":@1,@"expectedRevision":revision} error:&error],"Stale trim revision accepted");
  auto reject=[&](NSDictionary *values){auto p=[target mutableCopy];[p addEntriesFromDictionary:values];p[@"expectedRevision"]=session.automationRevision;const auto before=session.automationRevision;check(![session automationMethod:@"graph.trim.set" params:p error:&error]&&[before isEqual:session.automationRevision]&&[paired isEqual:read()],"Invalid trim edit partially committed");};
  reject(@{@"port":@"i:0",@"gainDB":@YES});reject(@{@"port":@"i:0",@"gainDB":@49});reject(@{@"port":@"i:0",@"linkTo":@"i:0"});reject(@{@"port":@"i:9",@"gainDB":@1});reject(@{@"port":@"i:0",@"gainDB":@1,@"unexpected":@1});
  call(@"history.undo",@{},true);check([initial isEqual:read()],"Undo lost untrimmed state");call(@"history.redo",@{},true);check([paired isEqual:read()],"Redo lost linked trim");
  NSString *source=call(@"graph.node.add",@{@"graph":graph,@"kind":@"automation",@"name":@"Drive envelope"},true)[@"data"][@"node"];
  set(@{@"port":@"i:0",@"modulation":@[@{@"source":source,@"minimumDB":@0,@"maximumDB":@12}]});auto automated=read();check([automated[@"sources"] count]==1&&[automated[@"ports"][0][@"modulation"] count]==1,"Trim automation source missing");
  auto snapshot=call(@"graph.get",@{})[@"data"];NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"trim-test-%@.screamseq",NSUUID.UUID.UUIDString]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Trim project save/reopen failed");check([snapshot isEqual:call(@"graph.get",@{})[@"data"]],"Trim metadata roundtrip changed graph");[[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  NSString *bus=call(@"graph.get",@{@"includeImplicitMixer":@YES})[@"data"][@"mixer"][@"buses"][0][@"id"];
  call(@"graph.trim.set",@{@"graph":NSNull.null,@"node":bus,@"port":@"i:0",@"linkTo":@"o:0",@"gainDB":@6},true);
  check([call(@"graph.trim.get",@{@"graph":NSNull.null,@"node":bus})[@"data"][@"ports"][1][@"gainDB"] doubleValue]==-6,"Song bus trim pair missing");
  call(@"graph.assign",@{@"target":bus,@"graph":graph},true);
  NSString *stage=[@"stage:" stringByAppendingString:bus];call(@"graph.trim.set",@{@"graph":NSNull.null,@"node":stage,@"port":@"o:0",@"gainDB":@(-3)},true);
  check([call(@"graph.trim.get",@{@"graph":NSNull.null,@"node":stage})[@"data"][@"ports"][1][@"gainDB"] doubleValue]==-3,"Song stage trim missing");
}
