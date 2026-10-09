void noteRoutingOperations() {
  Fixture f;f.doc->transaction([](auto &song){song.m_nInstruments=1;song.Instruments[1]=new OpenMPT::ModInstrument(1);});
  const auto channel=nativeID(f.doc->native().tracks.at(0).id),instrument=nativeID(f.doc->native().instruments.at(1).id);
  ScreamSeq::GraphHostHooks hooks;hooks.cachedRack={{{{"isInstrument",true}},"synth-a",0,false,{1}},{{{"isInstrument",true}},"synth-b",1},{{{"isInstrument",false}},"effect",2}};
  hooks.cachedRack[0].assignments={{1,7}};
  bool refuse=false;unsigned prepared=0,published=0;
  hooks.preparePublication=[&](const NativeSong &){++prepared;const auto before=f.doc->native();return [&,before]{CHECK(f.doc->native()==before);if(refuse)throw ScreamSeq::Api::ApiError(-32002,"Controlled publication refusal");++published;};};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  CHECK(api.invoke("graph.get",Json::object()).at("plugins")[0].at("assignments")==Json::array({{{"instrument",1},{"instrumentID",instrument},{"channel",7}}}));
  const auto unavailable=api.invoke("graph.note.activity",Json::object());CHECK(!unavailable.at("available").get<bool>()&&unavailable.at("routes").empty()&&unavailable.at("engine").is_null());
  const Json request={{"sourceKind","instrument"},{"source",instrument},{"plugin","synth-b"},{"midiChannel",3},{"suppressAssignment",true}};
  auto dry=request;dry["dryRun"]=true;const auto initial=f.doc->native();const auto revision=f.doc->revision;
  api.invoke("graph.note.connect",dry);CHECK(f.doc->native()==initial&&f.doc->revision==revision&&prepared==0&&published==0);
  refuse=true;bool rejected=false;try{api.invoke("graph.note.connect",request);}catch(const ScreamSeq::Api::ApiError &e){rejected=e.code==-32002;}
  CHECK(rejected&&f.doc->native()==initial&&f.doc->revision==revision&&published==0&&f.stops==0);refuse=false;
  const auto route=api.invoke("graph.note.connect",request).at("route");const auto connected=f.doc->native();
  CHECK(connected.signal.noteRouting.routes.size()==1&&connected.signal.noteRouting.routes[0].midiChannel==3);
  CHECK(connected.signal.noteRouting.suppressedAssignments==std::vector<uint64_t>{f.doc->native().instruments.at(1).id});
  CHECK(published==1&&f.stops==0);CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(connected))==connected);
  f.doc->undo();auto undone=initial;undone.nextID=connected.nextID;CHECK(f.doc->native()==undone);f.doc->redo();CHECK(f.doc->native()==connected);
  auto invalid=[&](const char *method,Json p){const auto before=f.doc->native();const auto rev=f.doc->revision;bool failed=false;try{api.invoke(method,p);}catch(const ScreamSeq::Api::ApiError &e){failed=e.code==-32602;}CHECK(failed&&f.doc->native()==before&&f.doc->revision==rev);};
  invalid("graph.note.connect",request); // Exact duplicate destination and channel.
  for(const auto &destination:{"effect","missing"}){auto p=request;p["plugin"]=destination;invalid("graph.note.connect",p);}
  invalid("graph.note.connect",{{"sourceKind","channel"},{"source",channel},{"plugin","synth-a"},{"suppressAssignment",true}});
  invalid("graph.note.update",{{"id",route},{"midiChannel",17}});
  invalid("graph.note.update",{{"id",route},{"enabled",1}});
  invalid("graph.note.disconnect",{{"id",route},{"instrument",instrument}});
  api.invoke("graph.note.update",{{"id",route},{"plugin","synth-a"},{"enabled",false}});CHECK(f.doc->native().signal.noteRouting.routes[0].plugin=="synth-a"&&!f.doc->native().signal.noteRouting.routes[0].enabled);
  api.invoke("graph.note.disconnect",{{"id",route}});CHECK(f.doc->native().signal.noteRouting.routes.empty());CHECK(!f.doc->native().signal.noteRouting.suppressedAssignments.empty());
  api.invoke("graph.note.restoreAssignment",{{"instrument",instrument}});CHECK(f.doc->native().signal.noteRouting.empty());
  api.invoke("graph.note.disconnect",{{"instrument",instrument}});const auto disconnected=f.doc->native();const auto count=published;
  api.invoke("graph.note.disconnect",{{"instrument",instrument}});CHECK(f.doc->native()==disconnected&&published==count);
  const auto wire=api.invoke("graph.note.connect",{{"sourceKind","channel"},{"source",channel},{"plugin","synth-b"}}).at("route");
  const auto snapshot=api.invoke("graph.get",Json::object()).at("noteRouting");CHECK(snapshot.at("routes")[0].at("id")==wire);
  auto metadata=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());metadata["signalGraph"]["noteRouting"]["routes"][0]["id"]=channel;
  rejected=false;try{(void)ScreamSeq::Project::decodeNativeMetadata(metadata);}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);
  for(const auto &scope:{"routing","route"}){metadata=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());auto &value=scope==std::string("routing")?metadata["signalGraph"]["noteRouting"]:metadata["signalGraph"]["noteRouting"]["routes"][0];value["unexpected"]=true;rejected=false;try{(void)ScreamSeq::Project::decodeNativeMetadata(metadata);}catch(const std::invalid_argument &){rejected=true;}CHECK(!rejected);}
}
