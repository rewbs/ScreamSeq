void audioFanConnections() {
  using namespace ScreamSeq::GraphCableEdits;
  Json parameters=Json::array({{{"id",7},{"min",-96},{"max",24},{"manualValue",-12},{"writable",true},{"canSlide",true},{"step",0}}});
  const auto manual=modulationDefault(parameters,7);CHECK(manual.base==.7&&!manual.quantized);
  auto zero=connect(Json::array(),{"source",true,true,0},{"target",false,true,7},{},manual);
  CHECK(zero.edges[0]["minimum"]==0&&zero.edges[0]["maximum"]==0&&zero.edges[0]["base"]==.7);
  CHECK(connect(zero.edges,{"target",false,true,7},{"source",true,true,0}).edges==zero.edges);
  parameters[0]["canSlide"]=false;parameters[0]["step"]=1;
  CHECK(modulationDefault(parameters,7).quantized);
  parameters[0]["step"]=0;bool missingStep=false;try{modulationDefault(parameters,7);}catch(const std::invalid_argument &){missingStep=true;}CHECK(missingStep);
  bool missingManual=false;try{connect(Json::array(),{"source",true,true,0},{"target",false,true,7});}catch(const std::invalid_argument &){missingManual=true;}CHECK(missingManual);
  auto disabled=zero.edges;disabled[0]["enabled"]=false;disabled[0]["base"]=.1;disabled[0]["quantized"]=true;
  const auto fresh=connect(disabled,{"other",true,true,0},{"target",false,true,7},{},manual);
  CHECK(fresh.edges[0]==disabled[0]&&fresh.edges[1]["base"]==.7&&fresh.edges[1]["quantized"]==false);
  Fixture f;const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto a=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()}}).at("node").get<std::string>();
  const auto b=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()}}).at("node").get<std::string>();
  const auto c=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()}}).at("node").get<std::string>();
  auto d=definition(f);const auto input=d["nodes"][0]["id"].get<std::string>(),output=d["nodes"][1]["id"].get<std::string>();
  auto edges=d["audio"];edges[0]["gain"]=.125;
  auto add=[&](Endpoint first,Endpoint second){auto edit=connect(edges,first,second);CHECK(edit.changed);edges=std::move(edit.edges);};
  add({input,true,false,0},{a,false,false,0});add({input,true,false,0},{b,false,false,0});
  add({a,true,false,0},{output,false,false,0});add({output,false,false,0},{b,true,false,0}); // Input-first fan-in.
  add({a,false,false,0},{b,true,false,0});
  CHECK(edges.size()==6&&edges[0]["gain"]==.125);
  const auto duplicate=connect(edges,{input,true,false,0},{a,false,false,0});CHECK(!duplicate.changed&&duplicate.edges==edges);
  d["audio"]=edges;const auto before=f.doc->native();f.api.invoke("graph.update",{{"definition",d},{"dryRun",true}});CHECK(f.doc->native()==before);
  f.api.invoke("graph.update",{{"definition",d}});const auto branched=f.doc->native();const auto revision=f.doc->revision;f.api.invoke("graph.update",{{"definition",d}});CHECK(f.doc->revision==revision);
  f.doc->undo();CHECK(f.doc->native()==before);f.doc->redo();CHECK(f.doc->native()==branched);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(branched))==branched);
  edges[3]["gain"]=-.375;const auto selected=edges[3];const auto originalEdges=edges;
  auto edited=connect(edges,{a,true,false,0},{c,false,false,0},std::optional<Json>{std::in_place,selected});CHECK(edited.changed&&edited.edges[3]["gain"]==-.375);
  for(size_t i=0;i<edges.size();++i)if(i!=3)CHECK(edited.edges[i]==edges[i]);
  edges=std::move(edited.edges);add({c,true,false,0},{output,false,false,0});d["audio"]=edges;f.api.invoke("graph.update",{{"definition",d}});
  const auto rerouted=f.doc->native();f.doc->undo();CHECK(f.doc->native()==branched);f.doc->redo();CHECK(f.doc->native()==rerouted);
  bool collision=false;try{connect(originalEdges,{input,true,false,0},{b,false,false,0},std::optional<Json>{std::in_place,selected});}catch(const std::invalid_argument &){collision=true;}CHECK(collision&&originalEdges[3]==selected);
  auto stale=selected;stale["gain"]=.75;bool rejectedStale=false;try{connect(originalEdges,{a,true,false,0},{c,false,false,0},std::optional<Json>{std::in_place,stale});}catch(const std::invalid_argument &){rejectedStale=true;}CHECK(rejectedStale);
  auto cycle=d;cycle["audio"].push_back({{"source",c},{"target",b},{"output",0},{"input",0},{"gain",1}});rejected(f,"graph.update",{{"definition",cycle}});
  auto cut=d;cut["audio"].erase(3);f.api.invoke("graph.update",{{"definition",cut}});CHECK(definition(f)["audio"]==cut["audio"]);f.doc->undo();CHECK(f.doc->native()==rerouted);

  const Json routes=Json::array({{{"plugin","P"},{"output",1},{"target","n2"}},{{"plugin","P"},{"output",1},{"target","n3"}},{{"plugin","P"},{"output",2},{"target","n8"}},{{"plugin","Q"},{"output",1},{"target","n7"}}});
  CHECK(pluginTargets(routes,"P",1,"",{},"n4")==Json::array({"n2","n3","n4"}));
  CHECK(pluginTargets(routes,"P",1,"","n2","n4")==Json::array({"n4","n3"}));
  CHECK(pluginTargets(routes,"P",1,"","n2",{})==Json::array({"n3"}));
  CHECK(pluginTargets(routes,"P",0,"n9",{},"n4")==Json::array({"n9","n4"}));
  CHECK(pluginTargets(routes,"P",0,"n9","n9",{})==Json::array());
  CHECK(pluginTargets(routes,"P",1,"",{},"n3")==Json::array({"n2","n3"}));
  const auto sends=Json::array({{{"target","n2"},{"gainDB",-9},{"preFader",true},{"enabled",false}}});
  CHECK(addSend(sends,"n2")==sends);const auto more=addSend(sends,"n3");CHECK(more[0]==sends[0]&&more[1]["gainDB"]==0&&more[1]["enabled"]==true);
  CHECK(freshSongRoute(true,false,false,false,1,false)==5); // A selected Send cannot redirect a plugin socket to its owner bus.
  CHECK(freshSongRoute(true,true,false,false,1,true)==6);
  CHECK(freshSongRoute(false,true,false,false,5,false)==4);
  CHECK(freshSongRoute(false,false,false,false,0,false)==1);
  CHECK(freshSongRoute(false,false,false,false,1,true)==1);
  CHECK(freshSongRoute(false,false,false,true,2,true)==2);
  bool copyRejected=false;try{freshSongRoute(false,false,true,false,1,false);}catch(const std::invalid_argument &){copyRejected=true;}CHECK(copyRejected);

  Fixture q;const auto qg=q.api.invoke("graph.create",Json::object()).at("graph");
  const auto qp=q.api.invoke("graph.node.add",{{"graph",qg},{"kind","plugin"},{"plugin",recipe()}}).at("node").get<std::string>();
  const auto lfo=q.api.invoke("graph.node.add",{{"graph",qg},{"kind","lfo"},{"connect",{{"node",qp},{"port",7},{"output",false},{"modulation",true},{"base",.42},{"quantized",true}}}}).at("node").get<std::string>();
  const auto lfo2=q.api.invoke("graph.node.add",{{"graph",qg},{"kind","lfo"}}).at("node").get<std::string>();
  auto quantized=definition(q);const auto first=quantized["modulation"][0];
  auto fan=connect(quantized["modulation"],{lfo2,true,true,0},{qp,false,true,7});
  CHECK(fan.edges[0]==first&&fan.edges[1]["base"]==.42&&fan.edges[1]["quantized"]==true&&fan.edges[1]["minimum"]==0&&fan.edges[1]["maximum"]==0);
  quantized["modulation"]=fan.edges;q.api.invoke("graph.update",{{"definition",quantized}});
  CHECK(definition(q)["modulation"].size()==2);q.doc->undo();CHECK(definition(q)["modulation"].size()==1);
  q.doc->redo();const auto lfo3=q.api.invoke("graph.node.add",{{"graph",qg},{"kind","lfo"}}).at("node").get<std::string>();
  quantized=definition(q);quantized["modulation"][0]["enabled"]=false;quantized["modulation"][0]["quantized"]=false;
  q.api.invoke("graph.update",{{"definition",quantized}}); // Disabled peers may retain a different mode.
  const auto disabledFirst=quantized["modulation"];
  fan=connect(disabledFirst,{lfo3,true,true,0},{qp,false,true,7});
  CHECK(fan.edges[0]==disabledFirst[0]&&fan.edges[1]==disabledFirst[1]&&fan.edges[2]["quantized"]==true);
  quantized["modulation"]=fan.edges;q.api.invoke("graph.update",{{"definition",quantized}});
  CHECK(definition(q)["modulation"].size()==3);
}
