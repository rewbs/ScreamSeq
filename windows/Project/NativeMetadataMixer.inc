#define MIXER_NAMES {"track","group","return","master"}
Json mixer(const MixerGraph &g) {
  Json buses = Json::array(), instruments = Json::array(), sidechains = Json::array();
  for (const auto &b : g.buses) {
    Json sends = Json::array(); for (const auto &s : b.sends) sends.push_back({{"target",nativeID(s.target)},{"gainDB",s.gainDB},{"preFader",s.preFader},{"enabled",s.enabled}});
    buses.push_back({{"id",nativeID(b.id)},{"kind",enumName(size_t(b.kind),MIXER_NAMES)},{"name",b.name},{"color",b.color},{"output",b.output ? nativeID(b.output) : ""},
      {"preGainDB",b.preGainDB},{"prePan",b.prePan},{"gainDB",b.gainDB},{"pan",b.pan},{"width",b.width},{"timingMS",b.timingMS},{"mute",b.mute},{"solo",b.solo},{"inserts",b.inserts},{"sends",sends}});
  }
  for (const auto &r : g.instruments) instruments.push_back({{"plugin",r.plugin},{"target",nativeID(r.target)},{"output",r.output}});
  for (const auto &r : g.sidechains) sidechains.push_back({{"source",nativeID(r.source)},{"plugin",r.plugin},{"input",r.input},{"gainDB",r.gainDB},{"preFader",r.preFader},{"enabled",r.enabled}});
  return {{"buses",buses},{"instruments",instruments},{"sidechains",sidechains}};
}
MixerGraph decodeMixer(const Json &v, unsigned version) {
  MixerGraph g;
  for (const auto &b : items(v,"buses",240)) {
    MixerBus bus; bus.id = decodeID(field(b,"id")); bus.kind = MixerBusKind(enumIndex(field(b,"kind"),MIXER_NAMES));
    bus.name = text(field(b,"name"),256); bus.color = u32(b,"color",0,0xffffff);
    bus.output = field(b,"output") == "" ? 0 : decodeID(b.at("output"));
    bus.preGainDB = real(b,"preGainDB",-96,24); bus.gainDB = real(b,"gainDB",-96,24);
    if (version >= 6) bus.prePan = real(b,"prePan",-1,1); else need(!b.contains("prePan"),"Legacy mixer cannot contain input balance");
    bus.pan = real(b,"pan",-1,1); bus.width = real(b,"width",0,2); bus.timingMS = real(b,"timingMS",-500,500);
    bus.mute = boolean(field(b,"mute")); bus.solo = boolean(field(b,"solo"));
    for (const auto &i : items(b,"inserts",32)) bus.inserts.push_back(text(i,128));
    for (const auto &s : items(b,"sends",16)) bus.sends.push_back({decodeID(field(s,"target")),real(s,"gainDB",-96,12),boolean(field(s,"preFader")),boolean(field(s,"enabled"))});
    g.buses.push_back(std::move(bus));
  }
  for (const auto &r : items(v,"instruments",128)) g.instruments.push_back({text(field(r,"plugin"),128),decodeID(field(r,"target")),u32(r,"output",0,63)});
  if (version >= 4) for (const auto &r : items(v,"sidechains",128)) g.sidechains.push_back({decodeID(field(r,"source")),text(field(r,"plugin"),128),u32(r,"input",1,63),real(r,"gainDB",-96,12),boolean(field(r,"preFader")),boolean(field(r,"enabled"))});
  else need(!v.contains("sidechains"),"Legacy mixer cannot contain sidechains");
  if (version < 10) for (const auto &r : g.instruments) for (const auto &b : g.buses) need(std::find(b.inserts.begin(),b.inserts.end(),r.plugin) == b.inserts.end(),"Legacy mixer cannot contain effect-output routes");
  return g;
}
#undef MIXER_NAMES
