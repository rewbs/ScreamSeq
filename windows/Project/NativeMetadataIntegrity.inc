// Metadata-local identity/reference checks. Actual snapshot shape, row bounds,
// format support and linked-envelope materialization remain NativeSong::validate's
// responsibility, reached by the loader through Document::restoreNative.
void validateMetadataReferences(const NativeSong &n) {
  std::set<uint64_t> ids, patterns, tracks, instruments, lanes, templates;
  auto identity = [&](uint64_t id) { need(id > 0 && id < n.nextID && n.nextID <= NativeSong::maximumID && ids.insert(id).second,"Invalid or duplicate native identity"); };
  auto map = [&](const auto &values, auto &out) { for (const auto &[index,e] : values) { identity(e.id); out.insert(e.id); } };
  map(n.patterns,patterns); map(n.tracks,tracks); map(n.instruments,instruments);
  for (const auto &[index,e] : n.samples) identity(e.id);
  for (const auto &s : n.sequences) { identity(s.info.id); for (const auto &e : s.orders) identity(e.id); }
  std::set<std::tuple<uint64_t,std::string,uint32_t>> automationTargets;
  size_t pointCount = 0;
  for (const auto &l : n.automation) {
    identity(l.id); lanes.insert(l.id);
    need(patterns.contains(l.pattern) && !l.plugin.empty() && l.plugin.size() <= 128 && automationTargets.emplace(l.pattern,l.plugin,l.parameter).second,"Invalid or duplicate automation target");
    need(!l.points.empty(),"Automation lanes require points");
    for (size_t i = 0; i < l.points.size(); ++i) { const auto &p = l.points[i]; need((!i || l.points[i-1].position < p.position) && (p.curve != AutomationCurve::Scripted || !p.formula.source().empty()),"Unordered automation or missing formula"); }
    pointCount += l.points.size();
  }
  need(pointCount <= 65536,"Automation exceeds 65536 points");
  const std::vector<uint64_t> trackIDs(tracks.begin(),tracks.end()); n.mixer.validate(trackIDs);
  std::map<uint64_t,const MixerBus *> buses;
  for (const auto &b : n.mixer.buses) { if (b.kind != MixerBusKind::Track) identity(b.id); buses.emplace(b.id,&b); }
  std::set<uint64_t> groups, columns;
  for (const auto &t : n.noteTracks) {
    need(buses.contains(t.bus) && buses.at(t.bus)->kind == MixerBusKind::Group && groups.insert(t.bus).second && !t.columns.empty(),"Note tracks need distinct existing groups and columns");
    int previous = -1;
    for (auto id : t.columns) {
      const auto found = std::find_if(n.tracks.begin(),n.tracks.end(),[&](const auto &entry){return entry.second.id == id;});
      need(found != n.tracks.end() && columns.insert(id).second && (previous < 0 || found->first == previous+1) && buses.contains(id) && buses.at(id)->output == t.bus,"Invalid note-column identity or routing"); previous = found->first;
    }
  }
  for (const auto &[id,muted] : n.columnMutes) need(tracks.contains(id),"Unknown column mute identity");
  for (auto [id,count] : n.performance.columns) need(tracks.contains(id),"Unknown effect-column track");
  for (const auto &[id,b] : n.performance.bindings) need(!b.plugin.empty() && b.plugin.size() <= 128,"Invalid stable parameter plugin identity");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>> commandCells;
  std::set<uint64_t> pitchTracks;
  for (const auto &c : n.performance.commands) {
    const bool parameter = c.kind == PatternCommandKind::ParameterSet || c.kind == PatternCommandKind::ParameterSlide;
    const bool slide = c.kind == PatternCommandKind::ParameterSlide || c.kind == PatternCommandKind::PitchSlide;
    need(patterns.contains(c.pattern) && tracks.contains(c.track) && c.column < (n.performance.columns.contains(c.track) ? n.performance.columns.at(c.track) : 1),"Unknown performance pattern, track or column");
    need(commandCells.emplace(c.pattern,c.track,c.position/performanceUnitsPerRow,c.column).second,"Duplicate performance cell");
    need(parameter ? (n.performance.bindings.contains(c.binding) && c.value >= 0 && c.value <= 1 && c.pitchRange == 2) : c.binding == 0,"Invalid performance binding or parameter value");
    need(slide ? c.duration > 0 : c.duration == 0,"Invalid command duration"); if (c.kind==PatternCommandKind::PitchSet || c.kind==PatternCommandKind::PitchSlide) pitchTracks.insert(c.track);
    const bool tracker=c.kind==PatternCommandKind::TrackerEffect,cut=c.kind==PatternCommandKind::NoteCut;
    need((!tracker || (c.column && !(c.position%performanceUnitsPerRow) && c.effect<OpenMPT::MAX_EFFECTS)) &&
      (!cut || c.pitchRange==2) && (!(tracker||cut) || (!c.value && !c.binding)) && (tracker || (!c.effect && !c.parameter)),"Invalid unified FX metadata");
  }
  need(pitchTracks.size() <= 16,"Too many native pitch tracks");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,bool>> notePositions;
  for (const auto &p : n.preciseNotes) {
    const bool on = p.note >= 1 && p.note <= 120;
    need(patterns.contains(p.pattern) && tracks.contains(p.track),"Unknown precise-note pattern or track");
    need((on || p.note == 254 || p.note == 255) && (on || (!p.instrument && p.velocity == 127 && !p.effect && !p.parameter)),"Invalid precise-note kind or release data");
    need(OpenMPT::NativeNoteEffectSupported(p.effect,p.parameter),"Unsupported precise-note effect");
    need(notePositions.emplace(p.pattern,p.track,p.position,on).second,"Duplicate precise-note event");
  }
  for (const auto &d : n.signal.library) { identity(d.id); for (const auto &node : d.nodes) identity(node.id); }
  std::vector<uint64_t> busIDs; for (const auto &b : n.mixer.buses) busIDs.push_back(b.id);
  // No snapshot is available here. UINT32_MAX rows defers only row bounds, not
  // references or graph compilation; restoreNative performs the real row check.
  std::map<uint64_t,uint32_t> patternRows; for (auto id : patterns) patternRows[id] = UINT32_MAX;
  n.signal.validate(busIDs,patternRows,std::vector<uint64_t>(instruments.begin(),instruments.end()));
  signalRoutingGraph(n.mixer,n.signal).validate(trackIDs);
  for (const auto &e : n.envelopeBank) { identity(e.id); templates.insert(e.id); need(!e.name.empty(),"Envelope templates need names"); }
  std::set<EnvelopeTarget> targets;
  for (const auto &l : n.envelopeLinks) {
    need(templates.contains(l.templateID) && targets.insert(l.target).second,"Missing template or duplicate envelope link");
    const auto &t = l.target;
    if (t.kind == EnvelopeTargetKind::Parameter) need(lanes.contains(t.owner),"Unknown envelope automation lane");
    else if (t.kind == EnvelopeTargetKind::Graph) {
      bool found = false;
      for (const auto &d : n.signal.library) for (const auto &node : d.nodes) if (node.id == t.owner && node.kind == SignalNodeKind::Automation) for (const auto &e : node.envelopes) if (e.pattern == t.pattern) found = true;
      need(found,"Unknown linked graph envelope");
    } else need(instruments.contains(t.owner),"Unknown linked instrument envelope");
  }
  need(n.bytes() <= 16*1024*1024,"Native metadata exceeds 16 MiB");
}
