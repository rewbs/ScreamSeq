static void graphStageSessionChecks() {
  TrackerSession *s=[TrackerSession new];NSError *error=nil;
  auto api=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=s.automationRevision;auto r=[s automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;};
  auto snapshot=[&](){return api(@"graph.get",@{@"includeImplicitMixer":@YES})[@"data"];};
  NSString *g=api(@"graph.create",@{@"name":@"Aux stage"},true)[@"data"][@"graph"];
  NSMutableDictionary *d=[snapshot()[@"library"][0] mutableCopy];NSString *in=d[@"nodes"][0][@"id"],*out=d[@"nodes"][1][@"id"];
  d[@"audio"]=@[@{@"source":in,@"target":out,@"output":@0,@"input":@0,@"gain":@1},@{@"source":in,@"target":out,@"output":@1,@"input":@2,@"gain":@1}];
  api(@"graph.update",@{@"definition":d},true);NSString *bus=snapshot()[@"mixer"][@"buses"][0][@"id"];
  api(@"graph.assign",@{@"target":bus,@"graph":g},true);
  NSDictionary *descriptor=@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1",@"name":@"Gainer",@"type":@0,@"subtype":@0,@"manufacturer":@0};
  api(@"plugin.add",@{@"descriptor":descriptor,@"detached":@YES},true);api(@"plugin.add",@{@"descriptor":descriptor,@"detached":@YES},true);
  NSString *a=snapshot()[@"plugins"][0][@"id"],*b=snapshot()[@"plugins"][1][@"id"];
  NSDictionary *input=@{@"source":@{@"plugin":a},@"output":@0,@"target":@{@"stage":bus},@"input":@1,@"gainDB":@(-6),@"enabled":@NO};
  auto before=snapshot();auto revision=s.automationRevision;auto preview=[input mutableCopy];preview[@"dryRun"]=@YES;api(@"graph.audio.connection.set",preview,true);
  check([before isEqual:snapshot()]&&[revision isEqual:s.automationRevision],"Stage route dry run must not mutate or allocate saved history");
  api(@"graph.audio.connection.set",input,true);auto connected=snapshot();revision=s.automationRevision;api(@"graph.audio.connection.set",input,true);check([revision isEqual:s.automationRevision],"Identical stage route must not create Undo");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Stage cable must have one complete Undo including silent root");api(@"history.redo",@{},true);check([connected isEqual:snapshot()],"Stage cable Redo must preserve typed endpoints");
  auto reject=[&](NSDictionary *value){auto old=snapshot();auto rev=s.automationRevision;auto p=[value mutableCopy];p[@"expectedRevision"]=rev;check(![s automationMethod:@"graph.audio.connection.set" params:p error:&error]&&[old isEqual:snapshot()]&&[rev isEqual:s.automationRevision],"Rejected stage route partially committed");};
  auto invalid=[input mutableCopy];invalid[@"input"]=@0;reject(invalid);invalid[@"input"]=@3;reject(invalid);invalid[@"input"]=@YES;reject(invalid);invalid=[input mutableCopy];invalid[@"source"]=@{@"plugin":a,@"stage":bus};reject(invalid);invalid=[input mutableCopy];invalid[@"source"]=@{@"plugin":@"missing"};reject(invalid);
  NSDictionary *output=@{@"source":@{@"stage":bus},@"output":@2,@"target":@{@"plugin":b},@"input":@0};api(@"graph.audio.connection.set",output,true);
  reject(@{@"source":@{@"plugin":b},@"output":@0,@"target":@{@"stage":bus},@"input":@1,@"dryRun":@YES}); // Disabled cables participate in DAG validation too.
  // Mixer edits must include existing typed stage routes in the same dry-run DAG.
  { auto prior=snapshot();auto rev=s.automationRevision;NSDictionary *p=@{@"source":b,@"output":@0,@"target":a,@"input":@0,@"dryRun":@YES,@"expectedRevision":rev};
    check(![s automationMethod:@"mixer.plugin.connection.set" params:p error:&error]&&[prior isEqual:snapshot()]&&[rev isEqual:s.automationRevision],"Mixed rack/stage cycle dry run must reject atomically"); }
  api(@"mixer.bus.set",@{@"bus":bus,@"gainDB":@(-3),@"dryRun":@YES},true);
  NSDictionary *old=@{@"source":@{@"plugin":a},@"output":@0,@"target":@{@"stage":bus},@"input":@1};
  auto enabling=[old mutableCopy];enabling[@"enabled"]=@YES;api(@"graph.audio.connection.set",enabling,true);check([snapshot()[@"stageConnections"][0][@"gainDB"] doubleValue]==-6,"Stage cable control update must preserve omitted gain");
  before=snapshot();auto cut=[output mutableCopy];cut[@"kind"]=@"stage-connection";api(@"graph.connections.remove",@{@"connections":@[cut]},true);check([snapshot()[@"stageConnections"] count]==1&&[snapshot()[@"mixer"] isEqual:before[@"mixer"]],"Stage cable cut must not heal/reorder ownership");api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Stage cable cut is not one complete Undo");
  // A stage auxiliary output can drive a follower without a synthetic plugin
  // identity or an audio cable. The source and zero-depth target are one edit.
  NSDictionary *followerSpec=@{@"kind":@"follower",@"audioStage":bus,@"output":@2};
  NSDictionary *followerAdd=@{@"source":followerSpec,@"connect":@{@"plugin":b,@"parameter":@1}};
  before=snapshot();revision=s.automationRevision;auto followerPreview=[followerAdd mutableCopy];followerPreview[@"dryRun"]=@YES;api(@"graph.song.source.add",followerPreview,true);
  check([before isEqual:snapshot()]&&[revision isEqual:s.automationRevision],"Stage follower dry run mutated song/history");
  NSString *follower=api(@"graph.song.source.add",followerAdd,true)[@"data"][@"node"];auto withFollower=snapshot();
  check([withFollower[@"songSources"][0][@"audioStage"] isEqual:bus]&&[withFollower[@"songSources"][0][@"audioPlugin"] length]==0&&[withFollower[@"songModulation"][0][@"minimum"] doubleValue]==0&&[withFollower[@"songModulation"][0][@"maximum"] doubleValue]==0,"Stage follower lost explicit identity or zero-depth connection");
  api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Stage follower creation must undo source and connection together");api(@"history.redo",@{},true);check([withFollower isEqual:snapshot()],"Stage follower redo did not restore exact tap");
  revision=s.automationRevision;api(@"graph.song.source.update",@{@"node":follower,@"source":@{@"audioStage":bus,@"output":@2}},true);check([revision isEqual:s.automationRevision],"Identical stage follower update created history");
  auto rejectFollower=[&](NSDictionary *patch){auto prior=snapshot();auto rev=s.automationRevision;NSDictionary *p=@{@"node":follower,@"source":patch,@"expectedRevision":rev};check(![s automationMethod:@"graph.song.source.update" params:p error:&error]&&[prior isEqual:snapshot()]&&[rev isEqual:s.automationRevision],"Rejected stage follower partially committed");};
  rejectFollower(@{@"output":@0});rejectFollower(@{@"output":@3});rejectFollower(@{@"audioStage":@"n999999"});rejectFollower(@{@"audioBus":bus});rejectFollower(@{@"audioPlugin":a});rejectFollower(@{@"preFader":@YES});
  NSDictionary *followerCut=@{@"kind":@"follower-input",@"node":follower,@"stage":bus,@"output":@2};
  before=snapshot();revision=s.automationRevision;auto staleCut=[followerCut mutableCopy];staleCut[@"output"]=@3;
  check(![s automationMethod:@"graph.connections.remove" params:@{@"connections":@[followerCut,staleCut],@"expectedRevision":revision} error:&error]&&[before isEqual:snapshot()]&&[revision isEqual:s.automationRevision],"Stale mixed follower cut partially committed");
  api(@"graph.connections.remove",@{@"connections":@[followerCut]},true);check([snapshot()[@"songSources"][0][@"audioStage"] length]==0&&[snapshot()[@"songModulation"] isEqual:before[@"songModulation"]],"Cutting follower input changed its modulation target");api(@"history.undo",@{},true);check([before isEqual:snapshot()],"Stage follower input cut must have one Undo");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];before=snapshot();check([s savePath:path error:&error]&&[s openPath:path error:&error]&&[before isEqual:snapshot()],"Typed graph stage routes did not survive native save/reopen");[[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
