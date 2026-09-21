// Private codec helpers. Kept beside the Windows project adapter, not in DSP.
const Json &items(const Json &v, const char *key, size_t max, bool optionalField = false) {
  static const Json empty = Json::array(); object(v);
  return array(v.contains(key) ? v.at(key) : optionalField ? empty : field(v,key),max);
}
uint32_t u32(const Json &v, const char *key, uint64_t lo = 0, uint64_t hi = UINT32_MAX) { return uint32_t(integer(field(v,key),lo,hi)); }
double real(const Json &v, const char *key, double lo, double hi) { return number(field(v,key),lo,hi); }
size_t enumIndex(const Json &v, std::initializer_list<const char *> names) {
  const auto s = text(v,32); size_t i = 0;
  for (auto name : names) { if (s == name) return i; ++i; }
  throw std::invalid_argument("Unknown metadata enum name");
}
Json enumName(size_t index, std::initializer_list<const char *> names) { need(index < names.size(),"Invalid metadata enum"); return *(names.begin()+index); }
#define CURVE_NAMES {"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"}
Json point(const AutomationPoint &p) {
  Json j{{"position",p.position},{"value",p.value},{"curve",enumName(size_t(p.curve),CURVE_NAMES)}};
  if (!p.formula.source().empty()) j["formula"] = p.formula.source(); return j;
}
AutomationPoint decodePoint(const Json &p, uint32_t maximum = UINT32_MAX) {
  AutomationPoint out{u32(p,"position",0,maximum),real(p,"value",0,1),AutomationCurve(enumIndex(optional(p,"curve","linear"),CURVE_NAMES))};
  if (p.contains("formula")) out.formula = CurveFormula(text(p.at("formula"),2048));
  need(out.curve != AutomationCurve::Scripted || !out.formula.source().empty(),"Scripted points require a formula"); return out;
}
#undef CURVE_NAMES
Json lane(const MusicalAutomationLane &l) {
  Json points = Json::array();
  for (const auto &p : l.points) { Json a = Json::array({p.position,p.value,uint8_t(p.curve)}); if (!p.formula.source().empty()) a.push_back(p.formula.source()); points.push_back(std::move(a)); }
  return {{"id",nativeID(l.id)},{"pattern",nativeID(l.pattern)},{"plugin",l.plugin},{"parameter",l.parameter},{"enabled",l.enabled},{"points",points}};
}
MusicalAutomationLane decodeLane(const Json &v, unsigned version) {
  MusicalAutomationLane l; l.id = decodeID(field(v,"id")); l.pattern = decodeID(field(v,"pattern"));
  l.plugin = text(field(v,"plugin"),128); l.parameter = u32(v,"parameter"); l.enabled = boolean(field(v,"enabled"));
  for (const auto &p : items(v,"points",4096)) {
    array(p,4); need(p.size() == 3 || p.size() == 4,"Automation points require 3 or 4 fields");
    const auto curve = integer(p[2],0,8);
    need(version >= 11 || (p.size() == 3 && curve != 8),"Legacy metadata cannot contain scripted curves");
    l.points.push_back({uint32_t(integer(p[0],0,UINT32_MAX)),number(p[1],0,1),AutomationCurve(curve),p.size() == 4 ? CurveFormula(text(p[3],2048)) : CurveFormula{}});
  }
  return l;
}
Json shape(const EnvelopeShape &s) {
  Json points = Json::array(); for (const auto &p : s.points) points.push_back(point(p));
  return {{"span",s.span},{"rowsPerBeat",s.rowsPerBeat},{"points",points},{"instrument",s.instrument},{"flags",s.flags},{"markers",s.markers}};
}
EnvelopeShape decodeShape(const Json &v) {
  EnvelopeShape s; s.span = u32(v,"span",1,16777216);
  s.rowsPerBeat = uint32_t(integer(optional(v,"rowsPerBeat",4),1,65536));
  s.instrument = boolean(optional(v,"instrument",false)); s.flags = uint8_t(integer(optional(v,"flags",1),0,31));
  for (const auto &p : items(v,"points",4096)) s.points.push_back(decodePoint(p,s.span-1));
  if (v.contains("markers")) { const auto &a = array(v.at("markers"),5); need(a.size() == 5,"Envelope requires five markers"); for (size_t i = 0; i < 5; ++i) s.markers[i] = uint32_t(integer(a[i],0,UINT32_MAX)); }
  validateEnvelopeShape(s); return s;
}
#define TARGET_NAMES {"parameter","graph","volume","pan","pitch"}
Json target(const EnvelopeTarget &t) { return {{"kind",enumName(size_t(t.kind),TARGET_NAMES)},{"owner",nativeID(t.owner)},{"pattern",t.pattern ? nativeID(t.pattern) : ""}}; }
EnvelopeTarget decodeTarget(const Json &v) {
  EnvelopeTarget t{EnvelopeTargetKind(enumIndex(field(v,"kind"),TARGET_NAMES)),decodeID(field(v,"owner")),0};
  if (v.contains("pattern") && v.at("pattern") != "") t.pattern = decodeID(v.at("pattern"));
  need(t.kind == EnvelopeTargetKind::Graph ? t.pattern > 0 : t.pattern == 0,"Only graph envelope targets use a pattern identity"); return t;
}
#undef TARGET_NAMES
Json bank(const NativeSong &n) {
  Json entries = Json::array(), links = Json::array();
  for (const auto &e : n.envelopeBank) entries.push_back({{"id",nativeID(e.id)},{"name",e.name},{"shape",shape(e.shape)}});
  for (const auto &l : n.envelopeLinks) links.push_back({{"target",target(l.target)},{"template",nativeID(l.templateID)},{"span",l.span}});
  return {{"entries",entries},{"links",links}};
}
void decodeBank(NativeSong &n, const Json &v) {
  for (const auto &e : items(v,"entries",256)) n.envelopeBank.push_back({decodeID(field(e,"id")),text(field(e,"name"),256),decodeShape(field(e,"shape"))});
  for (const auto &l : items(v,"links",4096)) n.envelopeLinks.push_back({decodeTarget(field(l,"target")),decodeID(field(l,"template")),u32(l,"span",1,16777216)});
}
Json performance(const PatternPerformance &p) {
  Json columns = Json::array(), bindings = Json::array(), commands = Json::array();
  for (auto [id,count] : p.columns) columns.push_back(Json::array({nativeID(id),count}));
  for (const auto &[id,b] : p.bindings) bindings.push_back({{"id",id},{"plugin",b.plugin},{"parameter",b.parameter},{"name",b.name}});
  for (const auto &c : p.commands) {
    Json j{{"pattern",nativeID(c.pattern)},{"track",nativeID(c.track)},{"position",c.position},{"duration",c.duration},{"column",c.column},{"kind",uint8_t(c.kind)},{"binding",c.binding},{"value",c.value}};
    if (c.kind == PatternCommandKind::PitchSet || c.kind == PatternCommandKind::PitchSlide) j["pitchRange"] = c.pitchRange;
    commands.push_back(std::move(j));
  }
  return {{"columns",columns},{"bindings",bindings},{"commands",commands}};
}
PatternPerformance decodePerformance(const Json &v, unsigned version) {
  PatternPerformance p;
  for (const auto &c : items(v,"columns",127)) { array(c,2); need(c.size() == 2,"Invalid effect-column count"); need(p.columns.emplace(decodeID(c[0]),uint8_t(integer(c[1],1,maximumEffectColumns))).second,"Duplicate effect-column configuration"); }
  for (const auto &b : items(v,"bindings",255)) need(p.bindings.emplace(uint16_t(u32(b,"id",1,255)),ParameterBinding{text(field(b,"plugin"),128),u32(b,"parameter"),text(field(b,"name"),256)}).second,"Duplicate parameter binding");
  for (const auto &c : items(v,"commands",maximumPatternCommands)) {
    const auto kind = u32(c,"kind",0,3);
    need(version >= 8 || (kind < 2 && !c.contains("pitchRange")),"Legacy metadata cannot contain pitch commands");
    p.commands.push_back({decodeID(field(c,"pattern")),decodeID(field(c,"track")),u32(c,"position"),u32(c,"duration"),uint8_t(u32(c,"column",0,maximumEffectColumns-1)),PatternCommandKind(kind),uint16_t(u32(c,"binding",0,255)),real(c,"value",-96,96),uint8_t(integer(optional(c,"pitchRange",2),1,96))});
  }
  return p;
}
