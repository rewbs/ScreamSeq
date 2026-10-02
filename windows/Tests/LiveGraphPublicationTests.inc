// Actual Windows document worker + shared render chain; no device or desktop.
void liveGraphPublicationTests(const std::filesystem::path &directory) {
  bool active=false;unsigned stops=0;PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback result;result.playing=active;result.audioActive=active;return result;};
  DocumentController c({},"live-graph-publication",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  invoke(c,"mixer.enable");const auto master=call(c,"mixer.get",Json::object()).at("buses").back().at("id");
  const auto graph=invoke(c,"graph.create").at("graph");
  const auto node=invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",{{"format","Built-in"},{"classID","resonance.gainer.v1"}}},{"insertEdge",0}}).at("node");
  invoke(c,"graph.assign",{{"target",master},{"graph",graph}});
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;
  std::array<float,2048> pcm{};auto render=[&]{need(playback->render(pcm.data(),1024),"Live graph rendering failed");};
  auto value=[&]{const auto parameters=call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("parameters");for(const auto &p:parameters)if(p.at("id")==1)return p.at("value").get<double>();throw std::runtime_error("Gainer parameter missing");};
  auto set=[&](double gain){return invoke(c,"graph.plugin.set",{{"graph",graph},{"node",node},{"parameters",Json::array({{{"id",1},{"value",gain}}})}});};
  render();set(-12);set(-6);set(-3);const auto before=c.view();bool rejected=false;
  try{set(-18);}catch(const Api::ApiError &e){rejected=e.code==-32002;}
  need(rejected&&c.view()==before&&active&&stops==stopped&&value()==-3,"Full graph queue changed model/history/transport");
  render();invoke(c,"history.undo");need(value()==-6&&active&&stops==stopped,"Graph Undo lost baseline or stopped playback");render();
  invoke(c,"history.redo");need(value()==-3&&active&&stops==stopped,"Graph Redo lost baseline or stopped playback");render();
  invoke(c,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});render();
  need(call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("bypass")==true&&active&&stops==stopped,"Recipe bypass was not published live");
  const auto bypassed=c.view();rejected=false;
  try{invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",{{"format","Built-in"},{"classID","missing.processor"}}},{"insertEdge",0}});}catch(const std::exception &){rejected=true;}
  need(rejected&&c.view()==bypassed&&active&&stops==stopped,"Processor preparation failure changed model or stopped playback");render();
  invoke(c,"history.undo");render();need(call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("bypass")==false,"Preparation failure created phantom history");
  const auto saved=call(c,"graph.get",Json::object());const auto path=directory/"live-graph-publication.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});need(active&&stops==stopped,"Save interrupted graph playback");
  active=false;invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(call(c,"graph.get",Json::object())==saved,"Live graph edits did not survive native persistence");
  std::cout<<"PASS Windows worker live recipe controls/bypass, queue and vendor-preparation failure recovery, Undo/Redo and persistence\n";
}
void liveRecordedAutomationTests(const std::filesystem::path &directory) {
  bool active=false;unsigned stops=0;PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback result;result.playing=active;result.audioActive=active;return result;};
  DocumentController c({},"live-recorded-publication",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});const auto gain=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Gainer missing");
  invoke(c,"plugin.add",{{"descriptor",*gain}});const auto plugin=c.view()->session.document.at("nativePlugins")[0].at("instanceID");
  auto lane=[&]{return call(c,"automation.recorded.get",{{"plugin",plugin},{"parameter",1}});};
  auto edit=[&](uint64_t frame,float value,bool dry=false){return invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",frame},{"value",value},{"dryRun",dry}});};
  edit(0,-6);edit(48000,-12);const auto initial=lane();const auto rack=call(c,"plugin.state.get",{{"plugin",plugin}});
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;
  std::array<float,2048> pcm{};auto render=[&]{need(playback->render(pcm.data(),1024),"Live recorded rendering failed");};render();
  auto before=c.view();edit(0,-18,true);need(c.view()==before&&active&&stops==stopped,"Recorded dry run mutated or stopped playback");
  edit(0,-18);edit(48000,0);edit(24000,-9);before=c.view();const auto filled=lane();bool rejected=false;
  try{edit(96000,-24);}catch(const Api::ApiError &e){rejected=e.code==-32002;}
  need(rejected&&c.view()==before&&lane()==filled&&active&&stops==stopped,"Full recorded queue changed model/history/transport");render();
  invoke(c,"history.undo");need(lane().at("total")==2&&active&&stops==stopped,"Recorded Undo stopped or retained rejected history");render();
  invoke(c,"history.redo");need(lane()==filled&&active&&stops==stopped,"Recorded Redo failed");render();
  before=c.view();rejected=false;
  try{invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",0},{"newFrame",24000},{"value",-3}});}catch(const Api::ApiError &){rejected=true;}
  need(rejected&&c.view()==before&&active&&stops==stopped,"Colliding point move was not atomic");
  invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",24000},{"newFrame",32000},{"value",-9}});render();
  need(lane().at("points")[1].at("frame")==32000,"Recorded point move changed time units");
  invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",32000},{"remove",true}});render();
  need(call(c,"plugin.state.get",{{"plugin",plugin}})==rack,"Recorded editing baked effective values into the saved plugin state");
  const auto saved=lane();const auto path=directory/"live-recorded-publication.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});need(active&&stops==stopped,"Recorded save stopped playback");
  active=false;invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});need(lane()==saved,"Recorded points did not survive native save/reopen");
  std::cout<<"PASS Windows recorded point preview/add/move/remove, live queue refusal, Undo/Redo, opaque baseline and persistence\n";
}
void liveBusMetadataTests() {
  bool active=false;unsigned stops=0;PlaybackHooks hooks;hooks.feedback=[&]{PlaybackFeedback r;r.playing=active;r.audioActive=active;return r;};
  DocumentController c({},"live-bus-metadata",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}}),initial=call(c,"mixer.get",{{"includeImplicit",true}});
  auto add=[&](const char *classID,size_t track){const auto found=std::find_if(library.begin(),library.end(),[&](const auto &p){return p.at("classID")==classID;});need(found!=library.end(),"Bus metadata fixture plugin missing");invoke(c,"plugin.add",{{"descriptor",*found},{"target",initial.at("buses")[track].at("id")}});return c.view()->session.document.at("nativePlugins").back().at("instanceID");};
  const auto source=add("resonance.gainer.v1",0),target=add("resonance.compressor.v1",1);
  auto connected=[&]{const auto info=call(c,"plugin.buses.get",{{"plugin",target}});for(const auto &bus:info.at("buses"))if(bus.at("direction")=="input"&&bus.at("index")==1)return bus.at("active").get<bool>();throw std::runtime_error("Detector bus missing");};
  Json route={{"source",source},{"output",0},{"target",target},{"input",1},{"enabled",false}};
  invoke(c,"mixer.plugin.connection.set",route);need(!connected(),"Stopped editor unexpectedly inferred an enabled detector");
  const auto baseline=call(c,"plugin.state.get",{{"plugin",target}});
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;std::array<float,2048> pcm{};
  auto settle=[&]{for(unsigned i=0;i<16;++i)need(playback->render(pcm.data(),1024),"Bus metadata transition render failed");};settle();
  need(!connected(),"Disabled direct cable must not activate prepared capacity");
  route["enabled"]=true;invoke(c,"mixer.plugin.connection.set",route);settle();
  need(connected()&&active&&stops==stopped&&call(c,"plugin.state.get",{{"plugin",target}})==baseline,"Accepted detector cable did not update bus metadata independently of saved plugin state");
  const auto mixer=call(c,"mixer.get",Json::object());bool mirrored=false;
  for(const auto &plugin:mixer.at("plugins"))if(plugin.at("id")==target)for(const auto &bus:plugin.at("audioBuses"))if(bus.at("direction")=="input"&&bus.at("index")==1)mirrored=bus.at("active").get<bool>();
  need(mirrored,"Mixer and plugin bus reads disagree about accepted direct routing");
  const auto before=c.view();auto invalid=route;invalid["input"]=2;bool rejected=false;try{invoke(c,"mixer.plugin.connection.set",invalid);}catch(const std::exception &){rejected=true;}
  need(rejected&&c.view()==before&&connected(),"Rejected route changed bus membership");
  invoke(c,"history.undo");settle();need(!connected(),"Undo to disabled direct route retained inferred bus membership");
  invoke(c,"history.redo");settle();need(connected(),"Redo did not restore inferred detector membership");
  invoke(c,"graph.connections.remove",{{"connections",Json::array({{{"kind","plugin-connection"},{"source",source},{"output",0},{"target",target},{"input",1}}})}});
  settle();need(!connected()&&active&&stops==stopped,"Removing the last detector cable retained membership or stopped playback");
  std::cout<<"PASS Windows accepted playback bus metadata: disabled/direct detector, mixer parity, rejection, Undo/Redo, last-cable removal and saved baseline isolation\n";
}
void liveRackPublicationTests(const std::filesystem::path &directory) {
  liveBusMetadataTests();
  bool active=false;unsigned stops=0;PlaybackHooks hooks;hooks.feedback=[&]{PlaybackFeedback r;r.playing=active;r.audioActive=active;return r;};
  DocumentController c({},"live-rack-publication",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});const auto gain=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Gainer missing");
  auto graph=[&]{return call(c,"graph.get",{{"includeImplicitMixer",true}});};const auto initial=graph(),target=initial.at("mixer").at("buses")[0].at("id");
  auto *playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;std::array<float,2048> pcm{};
  auto settle=[&]{for(unsigned i=0;i<32;++i)need(playback->render(pcm.data(),1024),"Rack transition render failed");};settle();
  auto rack=[&]{return c.view()->session.document.at("nativePlugins");};
  invoke(c,"plugin.add",{{"descriptor",*gain},{"target",target}});need(active&&stops==stopped&&rack().size()==1,"Live add stopped playback");
  const auto added=graph(),first=rack()[0].at("instanceID");const auto beforeBusy=c.view();bool rejected=false;
  try{invoke(c,"plugin.add",{{"descriptor",*gain},{"target",target}});}catch(const Api::ApiError &e){rejected=e.code==-32002;}
  need(rejected&&c.view()==beforeBusy&&active&&stops==stopped,"Busy rack preparation mutated history or stopped playback");settle();
  invoke(c,"history.undo");need(rack().empty()&&active&&stops==stopped,"Grouped rack Undo stopped playback or retained plugin");settle();
  invoke(c,"history.redo");need(rack().size()==1&&rack()[0].at("instanceID")==first&&active&&stops==stopped,"Grouped rack Redo lost identity or stopped");settle();
  const auto duplicate=invoke(c,"plugin.duplicate",{{"plugin",first},{"position",{{"x",440},{"y",220}}}});need(duplicate.at("detached")==true&&active&&stops==stopped,"Live duplicate is not detached/active");settle();
  const auto copy=duplicate.at("plugin");invoke(c,"plugin.remove",{{"plugins",Json::array({first,copy})}});need(rack().empty()&&active&&stops==stopped,"Live batch removal stopped playback");settle();
  invoke(c,"history.undo");need(rack().size()==2&&active&&stops==stopped,"Batch removal Undo did not publish live");settle();
  const auto saved=graph();const auto path=directory/"live-rack-publication.screamseq";invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});need(active&&stops==stopped,"Live rack save stopped playback");
  active=false;invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});need(graph()==saved,"Live rack state did not persist");
  std::cout<<"PASS Windows live implicit rack preparation, effect add/duplicate/remove, busy rejection, grouped Undo/Redo and persistence\n";
}
