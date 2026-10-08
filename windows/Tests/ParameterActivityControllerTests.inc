void parameterActivityControllerTests(const std::filesystem::path &directory) {
  unsigned stops=0;DocumentController c({},"activity",[&]{++stops;},[](const auto &){});
  auto initial=c.view();need(call(c,"parameter.activity.targets",Json::object()).at("targets").empty(),"Unprepared activity fabricated processors");
  need(call(c,"parameter.activity.get",Json::object()).at("token")=="none"&&c.view()==initial,"Empty activity changed document");
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});
  const auto gain=std::find_if(library.begin(),library.end(),[](const auto &v){return v.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Gainer fixture unavailable");
  invoke(c,"plugin.add",{{"descriptor",*gain}});
  const auto plugin=c.view()->session.document.at("nativePlugins").at(0).at("instanceID");
  auto edit=[&](uint64_t frame,float value,bool dry=false){return invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",frame},{"value",value},{"dryRun",dry}});};
  auto read=[&]{return call(c,"automation.recorded.get",{{"plugin",plugin},{"parameter",1}});};
  const auto clean=c.view();const auto beforeStops=stops;
  need(edit(0,-6,true).at("wouldChange")==true&&c.view()==clean&&stops==beforeStops,"Recorded dry run changed state");
  edit(0,-6);const auto one=read();need(one.at("points").size()==1&&one.at("sampleRate")==48000,"Recorded point units or insertion wrong");
  const auto unchanged=c.view();need(edit(0,-6).at("wouldChange")==false&&c.view()==unchanged,"Same recorded point consumed history");
  edit(4800,-12);const auto two=read();const auto old=c.view();bool rejected=false;
  try{invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",0},{"newFrame",4800},{"value",-9}});}catch(const Api::ApiError &e){rejected=e.code==-32602;}
  need(rejected&&c.view()==old&&read()==two,"Colliding move changed recorded lane");
  invoke(c,"automation.recorded.edit",{{"plugin",plugin},{"parameter",1},{"frame",0},{"newFrame",2400},{"value",-9}});
  const auto moved=read();need(moved.at("points").at(0).at("frame")==2400,"Recorded move missed its destination");
  invoke(c,"history.undo",{{"domain","document"}});need(read()==two,"Recorded move was not one chronological Undo");
  invoke(c,"history.redo",{{"domain","plugins"}});need(read()==moved,"Recorded Redo changed points");
  need(call(c,"automation.recorded.get",{{"plugin",plugin},{"parameter",1},{"offset",1},{"limit",1}}).at("points").size()==1,"Recorded pagination lost points");
  auto *playback=c.prepare(48000,Json::object(),true,true).get();const auto beforeObserve=c.view();const auto observeStops=stops;
  const auto targets=call(c,"parameter.activity.targets",Json::object()).at("targets");
  const auto rackTarget=std::find_if(targets.begin(),targets.end(),[&](const auto &v){return v.at("plugin")==plugin;});need(rackTarget!=targets.end(),"Prepared rack processor missing");
  const auto target=rackTarget->at("key");
  const auto catalog=call(c,"parameter.activity.parameters",{{"target",target}});need(!catalog.at("parameters").empty(),"Prepared parameter catalogue empty");
  const auto sources=call(c,"parameter.activity.sources",{{"target",target},{"parameter",1}});
  need(call(c,"parameter.activity.sources",{{"target",target},{"parameter",1.0}})==sources,"Integral JSON parameter differs from integer parameter");
  need(std::any_of(sources.at("sources").begin(),sources.at("sources").end(),[](const auto &v){return v.at("kind")=="recorded"&&v.at("count")==2;}),"Recorded source provenance missing");
  const auto watch=call(c,"parameter.activity.watch",{{"target",target},{"parameter",1}});
  need(call(c,"parameter.activity.watch",{{"target",target},{"parameter",1.0}})==watch,"Integral JSON watch reset capture");
  std::vector<float> pcm(9600*2);need(playback->render(pcm.data(),9600),"Activity offline render failed");
  const auto trace=call(c,"parameter.activity.get",{{"limit",8192}});need(trace.at("token")==watch.at("token")&&!trace.at("points").empty(),"Monitor did not expose actual rendered points");
  need(std::any_of(trace.at("points").begin(),trace.at("points").end(),[](const auto &v){return v.at("source").at("kind")=="recorded";}),"Rendered monitor lost recorded provenance");
  need(call(c,"parameter.activity.get",{{"after",trace.at("cursor")}}).at("points").empty(),"Trace cursor repeats delivered points");
  need(call(c,"parameter.activity.get",{{"after",0.0},{"limit",8192.0}})==trace,"Integral JSON activity pagination differs from integer pagination");
  need(c.view()==beforeObserve&&stops==observeStops,"Observation changed revision/history/transport");
  for(const auto &bad:std::vector<Json>{{{"target",target},{"parameter",true}},{{"target",target},{"parameter",1.5}},{{"target",target},{"parameter",1},{"clear",1}},{{"target","other/document"},{"parameter",1}}}) {
    rejected=false;try{call(c,"parameter.activity.watch",bad);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&c.view()==beforeObserve,"Invalid watch did not reject without mutation");
  }
  for(const auto &bad:std::vector<Json>{{{"after",true}},{{"after",.5}},{{"limit",true}},{{"limit",1.5}}}) {
    rejected=false;try{call(c,"parameter.activity.get",bad);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&c.view()==beforeObserve,"Non-integral activity pagination did not reject without mutation");
  }
  const auto path=directory/"recorded-activity.screamseq";invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});need(read()==moved,"Recorded native save/reopen changed exact points");
  need(call(c,"parameter.activity.targets",Json::object()).at("targets").empty(),"Opened document reused another document's prepared monitor");
  std::cout<<"PASS parameter activity actual render/provenance/cursors, strict transient reads, recorded dry/no-op/move/Undo/Redo/persistence\n";
}
