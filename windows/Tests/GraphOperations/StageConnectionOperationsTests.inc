void stageConnectionOperations() {
  Fixture f;ScreamSeq::GraphHostHooks hooks;bool refuse=false;unsigned published=0;
  hooks.cachedRack={{{{"name","Effect"},{"isInstrument",false}},"effect",0}};
  hooks.cachedAudioBuses["effect"]={{0,2,"Main",true,true,true},{1,2,"Detector",true,false,true},{0,2,"Main",false,true,true},{1,2,"Auxiliary",false,false,true}};
  hooks.preparePublication=[&](const NativeSong &){return [&]{if(refuse)throw ScreamSeq::Api::ApiError(-32002,"Controlled refusal");++published;};};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  const auto graph=api.invoke("graph.create",Json::object()).at("graph");
  auto definition=api.invoke("graph.get",Json::object())["library"][0];
  const auto input=definition["nodes"][0]["id"],output=definition["nodes"][1]["id"];
  for(unsigned port:{1u,2u})definition["audio"].push_back({{"source",input},{"target",output},{"output",port},{"input",port}});
  api.invoke("graph.update",{{"definition",definition}});
  const auto a=nativeID(f.doc->native().tracks.at(0).id),b=nativeID(f.doc->native().tracks.at(1).id);
  api.invoke("graph.assign",{{"target",a},{"graph",graph}});api.invoke("graph.assign",{{"target",b},{"graph",graph}});
  const Json request={{"source",{{"stage",a}}},{"target",{{"plugin","effect"}}},{"output",1},{"input",1},{"gainDB",-7},{"enabled",true}};
  const auto before=f.doc->native();const auto revision=f.doc->revision;const auto publications=published;
  auto dry=request;dry["dryRun"]=true;CHECK(api.invoke("graph.audio.connection.set",dry).at("wouldChange")==true);
  CHECK(f.doc->native()==before&&f.doc->revision==revision&&published==publications);
  refuse=true;bool failed=false;try{api.invoke("graph.audio.connection.set",request);}catch(const ScreamSeq::Api::ApiError &e){failed=e.code==-32002;}
  CHECK(failed&&f.doc->native()==before&&f.doc->revision==revision);refuse=false;
  api.invoke("graph.audio.connection.set",request);const auto connected=f.doc->native();
  CHECK(connected.signal.stageConnections.size()==1&&published==publications+1&&f.stops==0);
  CHECK(api.invoke("graph.get",Json::object()).at("stageConnections")[0]==request);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(connected))==connected);
  f.doc->undo();auto undone=before;undone.nextID=connected.nextID;CHECK(f.doc->native()==undone);f.doc->redo();CHECK(f.doc->native()==connected);
  const auto unchanged=f.doc->revision;CHECK(api.invoke("graph.audio.connection.set",request).at("wouldChange")==false);CHECK(f.doc->revision==unchanged);
  auto invalid=[&](Json value){const auto old=f.doc->native();const auto rev=f.doc->revision;bool rejected=false;try{api.invoke("graph.audio.connection.set",value);}catch(const ScreamSeq::Api::ApiError &e){rejected=e.code==-32602;}CHECK(rejected&&f.doc->native()==old&&f.doc->revision==rev);};
  auto bad=request;bad["source"]["plugin"]="effect";invalid(bad);
  bad=request;bad["source"]["unknown"]=a;invalid(bad);
  bad=request;bad["source"]={{"stage","n999999"}};invalid(bad);
  for(const auto &port:{Json(0),Json(3),Json(true)}){bad=request;bad["output"]=port;invalid(bad);}
  bad=request;bad["target"]={{"plugin","missing"}};invalid(bad);
  bad=request;bad["input"]=2;invalid(bad);
  bad=request;bad["source"]={{"plugin","effect"}};invalid(bad);
  bad={{"source",{{"plugin","effect"}}},{"target",{{"stage",a}}},{"output",0},{"input",1},{"dryRun",true}};invalid(bad); // Feedback through the existing stage output.
  // A repatch preserves its controls and exact bend geometry.
  f.doc->annotate([&](NativeSong &n){n.signal.presentation.cables.push_back({"stage:"+a,"plugin:effect",1,1,false,{{10,20},{30,40}}});});
  const Json key={{"source",request.at("source")},{"target",request.at("target")},{"output",1},{"input",1}};
  Json replacement={{"source",{{"stage",b}}},{"target",request.at("target")},{"output",2},{"input",0},{"replace",key}};
  api.invoke("graph.audio.connection.set",replacement);const auto repatched=f.doc->native();
  CHECK(repatched.signal.stageConnections[0].gainDB==-7&&repatched.signal.stageConnections[0].enabled);
  CHECK(repatched.signal.presentation.cables[0].source=="stage:"+b&&repatched.signal.presentation.cables[0].output==2&&repatched.signal.presentation.cables[0].input==0);
  CHECK(repatched.signal.presentation.cables[0].points.size()==2);
  bad=replacement;bad["replace"]=key;invalid(bad); // A stale cable cannot be repatched.
  const Json cut={{"kind","stage-connection"},{"source",replacement.at("source")},{"target",replacement.at("target")},{"output",2},{"input",0}};
  api.invoke("graph.connections.remove",{{"connections",Json::array({cut})}});
  CHECK(f.doc->native().signal.stageConnections.empty()&&f.doc->native().signal.presentation.cables.empty());
  f.doc->undo();CHECK(f.doc->native()==repatched);f.doc->redo();CHECK(f.doc->native().signal.stageConnections.empty());
  // Stage-to-stage is distinct from the rack route and needs no fabricated rack endpoint.
  api.invoke("graph.audio.connection.set",{{"source",{{"stage",a}}},{"target",{{"stage",b}}},{"output",1},{"input",2}});
  CHECK(f.doc->native().signal.stageConnections[0].target.stage==f.doc->native().tracks.at(1).id);
  auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());
  encoded["signalGraph"]["stageConnections"][0]["source"]["plugin"]="effect";failed=false;
  try{ScreamSeq::Project::decodeNativeMetadata(encoded);}catch(const std::invalid_argument &){failed=true;}CHECK(failed);
  // Rack instruments participate in the actual dependency DAG when their
  // physical catalogue exposes an audio input, including main input zero.
  hooks.cachedRack.push_back({{{"isInstrument",true}},"synth",1});
  hooks.cachedAudioBuses["synth"]={{0,2,"Main input",true,true,true},{0,2,"Main output",false,true,true}};
  GraphOperations instrumentAPI(*f.doc,[]{},hooks);const auto beforeInstrument=f.doc->native();
  Json instrumentCable={{"source",{{"stage",a}}},{"target",{{"plugin","synth"}}},{"output",1},{"input",0}};
  dry=instrumentCable;dry["dryRun"]=true;instrumentAPI.invoke("graph.audio.connection.set",dry);CHECK(f.doc->native()==beforeInstrument);
  instrumentAPI.invoke("graph.audio.connection.set",instrumentCable);CHECK(f.doc->native().signal.stageConnections.back().target.plugin=="synth");
  CHECK(std::find(f.doc->native().mixer.detached.begin(),f.doc->native().mixer.detached.end(),"synth")==f.doc->native().mixer.detached.end());
  f.doc->undo();CHECK(f.doc->native()==beforeInstrument);f.doc->redo();

  // The audio-to-parameter converter captures a typed stage output and its
  // zero-depth parameter edge in the same document transaction.
  PluginParameter gain{7,"Gain",0,1,.5,0,"",{}};
  hooks.cachedParameters["effect"]={gain};GraphOperations followerAPI(*f.doc,[]{},hooks);
  const Json followerRequest={{"source",{{"kind","follower"},{"audioStage",a},{"output",1}}},{"connect",{{"plugin","effect"},{"parameter",7}}}};
  const auto beforeFollower=f.doc->native();const auto followerRevision=f.doc->revision;
  dry=followerRequest;dry["dryRun"]=true;followerAPI.invoke("graph.song.source.add",dry);CHECK(f.doc->native()==beforeFollower&&f.doc->revision==followerRevision);
  const auto follower=followerAPI.invoke("graph.song.source.add",followerRequest).at("node");const auto followed=f.doc->native();
  CHECK(followed.signal.songSources.back().audioStage==f.doc->native().tracks.at(0).id&&followed.signal.songSources.back().audioPlugin.empty());
  CHECK(followed.signal.songModulation.back().minimum==0&&followed.signal.songModulation.back().maximum==0);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(followed))==followed);
  f.doc->undo();auto restored=beforeFollower;restored.nextID=followed.nextID;CHECK(f.doc->native()==restored);f.doc->redo();CHECK(f.doc->native()==followed);
  CHECK(!followerAPI.invoke("graph.song.source.update",{{"node",follower},{"source",{{"audioStage",a},{"output",1}}}}).at("wouldChange").get<bool>());
  auto rejectFollower=[&](const char *method,const Json &value){const auto before=f.doc->native();const auto rev=f.doc->revision;bool failed=false;try{followerAPI.invoke(method,value);}catch(const ScreamSeq::Api::ApiError &e){failed=e.code==-32602;}CHECK(failed&&f.doc->native()==before&&f.doc->revision==rev);};
  for(const auto &patch:{Json{{"output",0}},Json{{"output",3}},Json{{"output",true}},Json{{"preFader",true}},Json{{"audioStage","n99999999"}},Json{{"audioPlugin","effect"}},Json{{"audioBus",a}}})
    rejectFollower("graph.song.source.update",{{"node",follower},{"source",patch}});
  const Json followerCut={{"kind","follower-input"},{"node",follower},{"stage",a},{"output",1}};
  auto staleCut=followerCut;staleCut["stage"]=b;
  rejectFollower("graph.connections.remove",{{"connections",Json::array({followerCut,staleCut})}});
  auto ambiguousCut=followerCut;ambiguousCut["plugin"]="effect";
  rejectFollower("graph.connections.remove",{{"connections",Json::array({ambiguousCut})}});
  followerAPI.invoke("graph.connections.remove",{{"connections",Json::array({followerCut})}});
  CHECK(f.doc->native().signal.songSources.back().audioStage==0&&f.doc->native().signal.songSources.back().output==0);
  CHECK(f.doc->native().signal.songModulation==followed.signal.songModulation&&f.doc->native().signal.stageConnections==followed.signal.stageConnections);
  f.doc->undo();CHECK(f.doc->native()==followed);f.doc->redo();
  followerAPI.invoke("graph.song.source.update",{{"node",follower},{"source",{{"audioStage",b},{"output",2}}}});
  CHECK(f.doc->native().signal.songSources.back().audioStage==f.doc->native().tracks.at(1).id);

}
