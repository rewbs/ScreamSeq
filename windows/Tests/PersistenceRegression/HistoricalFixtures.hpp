// Generated test models, NOT historical Mac-produced projects.
// Rich setup and reductions reused from ProjectNative/NativeMetadataTests.cpp.
Json legacy(Json j, unsigned version) {
  j["version"] = version;
  for (auto [minimum,key] : {std::pair{2,"automation"},{3,"mixer"},{5,"noteTracks"},{5,"columnMutes"},{7,"performance"},{9,"preciseNotes"},{10,"signalGraph"},{14,"envelopeBank"}}) if (version < unsigned(minimum)) j.erase(key);
  if (version >= 3 && version < 4) j["mixer"].erase("sidechains");
  if (version >= 3 && version < 6) for (auto &b : j["mixer"]["buses"]) b.erase("prePan");
  return j;
}
std::unique_ptr<Document> richDocument() {
  auto doc = Document::demo();
  doc->transaction([](auto &song) { check(song.AllocateInstrument(1) != nullptr,"test instrument allocation"); song.m_nInstruments = 1; });
  auto n = doc->native();
  const auto pattern = n.patterns.begin()->second.id;
  const auto track = n.tracks.at(0).id, second = n.tracks.at(1).id, third = n.tracks.at(2).id, fourth = n.tracks.at(3).id;
  const auto instrument = n.instruments.at(1).id;
  auto id = [&] { return n.makeEntity().id; };
  n.patterns.begin()->second.name = "Pattern \xE2\x99\xAB";
  n.patterns.begin()->second.annotation = "line one\nline two"; n.patterns.begin()->second.color = 0xabcdef;
  const auto master = id(), group = id(), ret = id();
  for (const auto &[index,e] : n.tracks) n.mixer.buses.push_back({e.id,index < 2 ? group : master,MixerBusKind::Track,"Track"});
  n.mixer.buses.push_back({group,master,MixerBusKind::Group,"Group"});
  n.mixer.buses.push_back({ret,master,MixerBusKind::Return,"Return"});
  n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});
  auto &b = n.mixer.buses.at(0); b.preGainDB = -2; b.prePan = -.3; b.gainDB = -8; b.pan = .4; b.width = 1.3; b.timingMS = -12; b.mute = true; b.solo = true; b.color = 0xffffff; b.sends.push_back({ret,-5,true,false});
  n.mixer.buses.at(n.tracks.size()).inserts = {"fx-identity"};
  n.mixer.instruments = {{"instrument-identity",fourth,0},{"fx-identity",ret,2}};
  n.mixer.sidechains = {{third,"fx-identity",3,-7,true,false}};
  n.noteTracks = {{group,{track,second}}}; n.columnMutes = {{track,true},{second,false}};
  const auto span = uint32_t(doc->song().Patterns[n.patterns.begin()->first].GetNumRows())*256;
  EnvelopeShape shape; shape.span = span; shape.rowsPerBeat = 7;
  for (uint8_t curve = 0; curve <= 8; ++curve) shape.points.push_back({uint32_t(curve)*256,double(curve)/8,AutomationCurve(curve),curve == 8 ? CurveFormula(" mix(start,end,t^2) ") : CurveFormula{}});
  const auto lane = id(), templ = id();
  n.automation.push_back({lane,pattern,"persistent-plugin-uuid",UINT32_MAX,false,shape.points});
  n.envelopeBank.push_back({templ,"All curves",shape}); n.envelopeLinks.push_back({{EnvelopeTargetKind::Parameter,lane,0},templ,span});
  n.performance.columns[track] = 8; n.performance.bindings[255] = {"persistent-plugin-uuid",UINT32_MAX,"A parameter"};
  for (uint8_t kind = 0; kind < 4; ++kind) n.performance.commands.push_back({pattern,track,uint32_t(kind)*65536+17,kind%2 ? 1234u : 0u,kind,PatternCommandKind(kind),uint16_t(kind < 2 ? 255 : 0),kind < 2 ? .75 : -12,uint8_t(kind < 2 ? 2 : 48)});
  n.preciseNotes = {{pattern,track,77,1,60,91,uint8_t(OpenMPT::CMD_VIBRATO),0x34},{pattern,track,999,0,254,127},{pattern,track,2000,0,255,127}};
  SignalDefinition d; d.id = id(); d.number = 999; d.name = "All graph kinds";
  for (uint8_t kind = 0; kind <= 9; ++kind) { SignalNode node; node.id = id(); node.kind = SignalNodeKind(kind); node.name = "Node "+std::to_string(kind); node.x = -123.5+kind; node.y = 987.25; node.rate = .25; node.phase = .7; node.attack = .125; node.release = .75; node.controller = 127; d.nodes.push_back(node); }
  auto &plugin = d.nodes[2].plugin; plugin.format = "VST3"; plugin.name = "Cross-platform plugin"; plugin.path = "C:/Plugins/\xE9\x9F\xB3.vst3"; plugin.classID = "0123456789ABCDEF0123456789ABCDEF"; plugin.type = UINT32_MAX; plugin.subtype = 0x12345678; plugin.manufacturer = 0x87654321; plugin.inputs = {1,63}; plugin.outputs = {2,63};
  for (unsigned byte = 0; byte < 256; ++byte) plugin.state.push_back(std::byte(byte));
  auto node = [&](size_t i) { return d.nodes.at(i).id; };
  d.audio = {{node(0),node(2),0,0,.75},{node(0),node(2),1,1,-.5},{node(2),node(1),0,0,1},{node(2),node(1),2,2,.25},{node(0),node(4),0,0,1}};
  for (size_t i = 3; i < d.nodes.size(); ++i) d.modulation.push_back({node(i),node(2),uint32_t(i),-.5,.7,.2,i%2 != 0});
  d.nodes[9].envelopes = {{pattern,false,shape.points}};
  n.envelopeLinks.push_back({{EnvelopeTargetKind::Graph,node(9),pattern},templ,span});
  n.signal.library.push_back(d); n.signal.assignments = {{track,d.id,.25,.75}}; n.signal.instrumentAssignments = {{instrument,d.id,.3,.4}}; n.signal.lanes[track] = 8;
  n.signal.inputs = {{fourth,track,1,-4,true}}; n.signal.outputs = {{track,ret,2}}; n.signal.layout["stable-layout-key"] = {123.5,456.25};
  for (uint8_t kind = 0; kind < 6; ++kind) n.signal.commands.push_back({pattern,track,kind == 3 ? 0 : d.id,uint32_t(kind)*65536+7,kind,SignalCommandKind(kind),.4,.6,true});
  EnvelopeShape instrumentShape; instrumentShape.span = 2561; instrumentShape.instrument = true; instrumentShape.flags = 7; instrumentShape.markers = {0,2560,256,1024,UINT32_MAX}; instrumentShape.points = {{0,0},{256,1},{1024,.5},{2560,0}};
  const auto instrumentTemplate = id(); n.envelopeBank.push_back({instrumentTemplate,"Instrument template",instrumentShape});
  for (auto kind : {EnvelopeTargetKind::Volume,EnvelopeTargetKind::Pan,EnvelopeTargetKind::Pitch}) { EnvelopeTarget t{kind,instrument,0}; applyEnvelope(n,doc->song(),t,instrumentShape,11); n.envelopeLinks.push_back({t,instrumentTemplate,11}); }
  doc->restoreNative(n); return doc;
}
NativeSong historicalModel(NativeSong n,unsigned version) {
    if (version < 14) { n.envelopeBank.clear(); n.envelopeLinks.clear(); }
    if (version < 13) {
      n.signal.instrumentAssignments.clear();
      for (auto &d : n.signal.library) {
        std::set<uint64_t> removed; for (const auto &node : d.nodes) if (node.kind == SignalNodeKind::Automation) removed.insert(node.id);
        std::erase_if(d.nodes,[&](const auto &node) { return removed.contains(node.id); });
        std::erase_if(d.modulation,[&](const auto &m) { return removed.contains(m.source); });
      }
    }
    if (version < 12) for (auto &note : n.preciseNotes) { note.effect = 0; note.parameter = 0; }
    if (version < 11) for (auto &lane : n.automation) for (auto &point : lane.points) { if (point.curve == AutomationCurve::Scripted) point.curve = AutomationCurve::Linear; point.formula = {}; }
    if (version < 10) { n.signal = {}; std::erase_if(n.mixer.instruments,[](const auto &r) { return r.plugin == "fx-identity"; }); }
    if (version < 9) n.preciseNotes.clear();
    if (version < 8) std::erase_if(n.performance.commands,[](const auto &c) { return c.kind == PatternCommandKind::PitchSet || c.kind == PatternCommandKind::PitchSlide; });
    if (version < 7) n.performance = {};
    if (version < 6) for (auto &bus : n.mixer.buses) bus.prePan = 0;
    if (version < 5) { n.noteTracks.clear(); n.columnMutes.clear(); }
    if (version < 4) n.mixer.sidechains.clear();
    if (version < 3) n.mixer = {};
    if (version < 2) n.automation.clear();
  return n;
}
