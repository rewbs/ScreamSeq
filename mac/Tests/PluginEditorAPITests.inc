// Presentation requests share the normal revision guard, but must not consume
// an unrelated parameter preview or create musical history.
static void pluginEditorAPIAdmission(TrackerSession *session) {
  const auto ensure=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
  NSError *error=nil;NSString *revision=session.automationRevision;
  NSData *baseline=session.serializedData;
  NSString *identity=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  NSDictionary *parameter=[session pluginParameters:0][0];
  const auto call=[&](NSString *method,NSDictionary *params)->NSDictionary * {
    error=nil;auto result=[session automationMethod:method params:params error:&error];
    ensure(result&&!error,error.localizedDescription.UTF8String ?: "Plugin editor API failed");return result;
  };
  const auto described=call(@"api.describe",@{});
  for(NSString *method in @[@"plugin.editor.open",@"plugin.editor.close"])
    ensure([described[@"data"][@"writes"] containsObject:method],"Plugin editor method missing from API inventory");
  NSDictionary *previewParams=@{@"expectedRevision":revision,@"plugin":identity,@"parameter":parameter[@"id"],
    @"gesture":@"editor-admission-preview",@"value":parameter[@"value"]};
  const auto preview=call(@"plugin.parameters.preview.set",previewParams)[@"data"];
  for(NSString *method in @[@"plugin.editor.open",@"plugin.editor.close"]) {
    for(NSDictionary *params in @[
      @{@"expectedRevision":@"stale",@"slot":@0},
      @{@"expectedRevision":revision,@"slot":@YES},
      @{@"expectedRevision":revision,@"slot":@0.5},
      @{@"expectedRevision":revision,@"slot":@(-1)},
      @{@"expectedRevision":revision,@"slot":@64},
      @{@"expectedRevision":revision},
      @{@"expectedRevision":revision,@"slot":@0,@"dryRun":@YES},
      @{@"expectedRevision":revision,@"plugin":identity}]) {
      error=nil;const auto result=[session automationMethod:method params:params error:&error];
      const auto code=[params[@"expectedRevision"] isEqual:@"stale"]?-32001:-32602;
      ensure(!result&&error.code==code,"Invalid plugin editor request accepted or returned the wrong error");
      ensure([call(@"plugin.parameters.preview.get",@{})[@"data"] isEqual:preview],"Rejected presentation request consumed preview");
    }
  }
  for(int attempt=0;attempt<2;++attempt) {
    const auto closed=call(@"plugin.editor.close",@{@"expectedRevision":revision,@"slot":@0});
    ensure([closed[@"data"] isEqual:@{@"open":@NO,@"plugin":identity}]&&![closed[@"changed"] boolValue]&&
      ![closed[@"playbackStopped"] boolValue],"Closing an already closed editor changed music or fabricated an open window");
    ensure([call(@"plugin.parameters.preview.get",@{})[@"data"] isEqual:preview],"Editor close consumed unrelated preview");
  }
  ensure([session.automationRevision isEqual:revision]&&[session.serializedData isEqual:baseline],
    "Editor admission/close changed song bytes or history");
  NSMutableDictionary *cancel=[previewParams mutableCopy];[cancel removeObjectForKey:@"value"];
  call(@"plugin.parameters.preview.cancel",cancel);
}
