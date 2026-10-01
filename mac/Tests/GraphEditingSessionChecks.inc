static void graphEditingSessionChecks() {
  TrackerSession *s=[TrackerSession new];NSError *error=nil;
  auto api=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=s.automationRevision;auto r=[s automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;};
  auto snapshot=[&](){return api(@"graph.get",@{@"includeImplicitMixer":@YES})[@"data"];};
  NSString *graph=api(@"graph.create",@{@"name":@"Editing actions"},true)[@"data"][@"graph"];
  NSString *source=api(@"graph.node.add",@{@"graph":graph,@"kind":@"lfo"},true)[@"data"][@"node"];
  NSString *plugin=api(@"graph.node.add",@{@"graph":graph,@"kind":@"plugin",@"plugin":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1"},@"insertEdge":@0},true)[@"data"][@"node"];
  auto findSource=[&](NSString *which)->NSDictionary *{for(NSDictionary *d in snapshot()[@"library"])if([d[@"id"] isEqual:which])for(NSDictionary *n in d[@"nodes"])if([n[@"id"] isEqual:source])return n;return nil;};
  auto before=snapshot();NSString *revision=s.automationRevision;
  api(@"graph.source.mute",@{@"graph":graph,@"node":source,@"muted":@YES,@"dryRun":@YES},true);check([before isEqual:snapshot()]&&[revision isEqual:s.automationRevision],"Source mute preview changes history");
  api(@"graph.source.mute",@{@"graph":graph,@"node":source,@"muted":@YES},true);check([findSource(graph)[@"muted"] boolValue],"Recipe source mute not persisted");
  auto muted=snapshot();revision=s.automationRevision;api(@"graph.source.mute",@{@"graph":graph,@"node":source,@"muted":@YES},true);check([revision isEqual:s.automationRevision],"Identical source mute creates Undo");
  check(![s automationMethod:@"graph.source.mute" params:@{@"graph":graph,@"node":plugin,@"muted":@YES,@"expectedRevision":revision} error:&error]&&[muted isEqual:snapshot()],"Invalid source mute partially commits");
  api(@"history.undo",@{},true);check(![findSource(graph)[@"muted"] boolValue],"Source mute Undo failed");api(@"history.redo",@{},true);check([muted isEqual:snapshot()],"Source mute Redo failed");
  NSString *target=snapshot()[@"mixer"][@"buses"][0][@"id"];api(@"graph.assign",@{@"target":target,@"graph":graph},true);
  before=snapshot();revision=s.automationRevision;api(@"graph.makeIndependent",@{@"graph":graph,@"target":target,@"scope":@"channel",@"dryRun":@YES},true);check([before isEqual:snapshot()]&&[revision isEqual:s.automationRevision],"Independent-use preview changes model");
  NSString *independent=api(@"graph.makeIndependent",@{@"graph":graph,@"target":target,@"scope":@"channel"},true)[@"data"][@"graph"];check(![independent isEqual:graph]&&[snapshot()[@"assignments"][0][@"graph"] isEqual:independent],"Independent use did not rebind the selected channel");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Independent use must have one complete Undo");api(@"history.redo",@{},true);
  auto fragment=api(@"graph.selection.copy",@{@"graph":graph,@"nodes":@[source,plugin]})[@"data"][@"fragment"];check([fragment[@"audio"] count]==0&&[fragment[@"nodes"] count]==4,"Clipboard copies internal data without boundary audio routes");
  before=snapshot();api(@"graph.selection.paste",@{@"graph":graph,@"fragment":fragment,@"x":@300,@"y":@200},true);check([snapshot()[@"library"][0][@"nodes"] count]==6,"Pasted nodes missing");api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Paste must have one complete Undo");
  auto cut=api(@"graph.selection.cut",@{@"graph":graph,@"nodes":@[source,plugin]},true)[@"data"];check([cut[@"fragment"] isEqual:fragment]&&[snapshot()[@"library"][0][@"nodes"] count]==2,"Cut did not return exact fragment or remove originals");api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Cut must have one complete Undo");
  api(@"graph.plugin.set",@{@"graph":graph,@"node":plugin,@"parameters":@[@{@"id":@1,@"value":@(-6)}]},true);api(@"graph.plugin.bypass",@{@"graph":graph,@"node":plugin,@"bypass":@YES},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq-preset"]];
  revision=s.automationRevision;api(@"graph.plugin.preset.save",@{@"graph":graph,@"node":plugin,@"path":path,@"name":@"Saved baseline"},true);check([revision isEqual:s.automationRevision],"Preset save changes song history");
  api(@"graph.plugin.set",@{@"graph":graph,@"node":plugin,@"parameters":@[@{@"id":@1,@"value":@(-12)}]},true);before=snapshot();auto preset=api(@"plugin.preset.inspect",@{@"path":path})[@"data"];
  api(@"graph.plugin.preset.load",@{@"graph":graph,@"node":plugin,@"path":path,@"expectedPresetRevision":preset[@"presetRevision"]},true);
  auto metadata=api(@"graph.plugin.get",@{@"graph":graph,@"node":plugin})[@"data"];check([metadata[@"bypass"] boolValue]&&[metadata[@"parameters"][1][@"manualValue"] floatValue]==-6,"Recipe preset must restore configured baseline while preserving bypass");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Recipe preset load must have one complete Undo");api(@"history.redo",@{},true);
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  NSDictionary *gainer=@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1",@"name":@"Gainer",@"type":@0,@"subtype":@0,@"manufacturer":@0};
  api(@"plugin.add",@{@"descriptor":gainer},true);NSString *rack=snapshot()[@"plugins"][0][@"id"];
  api(@"plugin.parameters.set",@{@"plugin":rack,@"values":@[@{@"id":@1,@"value":@(-9)}]},true);api(@"plugin.bypass",@{@"plugin":rack,@"bypass":@YES},true);
  before=snapshot();revision=s.automationRevision;api(@"plugin.duplicate",@{@"plugin":rack,@"position":@{@"x":@300,@"y":@500},@"dryRun":@YES},true);check([revision isEqual:s.automationRevision]&&[before isEqual:snapshot()],"Plugin duplicate dryRun mutated the song");
  auto duplicated=api(@"plugin.duplicate",@{@"plugin":rack,@"position":@{@"x":@300,@"y":@500}},true)[@"data"];NSString *copy=duplicated[@"plugin"];check(![copy isEqual:rack]&&[duplicated[@"detached"] boolValue]&&[snapshot()[@"mixer"][@"detached"] containsObject:copy],"Plugin duplicate must create a fresh detached effect");
  auto values=api(@"plugin.parameters.get",@{@"plugin":copy})[@"data"];check([values[1][@"manualValue"] doubleValue]==-9&&[snapshot()[@"plugins"][1][@"bypass"] boolValue]&&[snapshot()[@"plugins"][1][@"assignments"] count]==0,"Plugin duplicate must preserve baseline and bypass without assignments");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Plugin duplicate must have one complete Undo");api(@"history.redo",@{},true);
  // A rack-only instrument copy has identical NativeSong metadata: the
  // plugin transaction must run even when Document::annotate would be a no-op.
  api(@"plugin.add",@{@"descriptor":@{@"format":@"AU",@"name":@"Apple DLSMusicDevice",@"type":@(kAudioUnitType_MusicDevice),@"subtype":@(kAudioUnitSubType_DLSSynth),@"manufacturer":@(kAudioUnitManufacturer_Apple),@"isInstrument":@YES}},true);
  before=snapshot();NSString *synth=before[@"plugins"][2][@"id"];
  auto synthCopy=api(@"plugin.duplicate",@{@"plugin":synth},true)[@"data"];
  check([snapshot()[@"plugins"] count]==4&&![synthCopy[@"plugin"] isEqual:synth]&&![synthCopy[@"detached"] boolValue]&&[snapshot()[@"plugins"][3][@"assignments"] count]==0,"Rack-only instrument duplicate must publish without a native annotation");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Rack-only instrument duplicate must have one Undo");api(@"history.redo",@{},true);check([snapshot()[@"plugins"] count]==4,"Rack-only instrument duplicate Redo failed");
  NSString *songPath=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];before=snapshot();check([s savePath:songPath error:&error]&&[s openPath:songPath error:&error]&&[before isEqual:snapshot()],"Graph editing actions did not survive native project round trip");[[NSFileManager defaultManager] removeItemAtPath:songPath error:nil];
}
