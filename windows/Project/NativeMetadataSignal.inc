std::string base64(const std::vector<std::byte> &data) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result; result.reserve((data.size()+2)/3*4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const uint32_t a = std::to_integer<uint8_t>(data[i]);
    const uint32_t b = i+1 < data.size() ? std::to_integer<uint8_t>(data[i+1]) : 0;
    const uint32_t c = i+2 < data.size() ? std::to_integer<uint8_t>(data[i+2]) : 0;
    const auto v = (a<<16)|(b<<8)|c;
    result += alphabet[(v>>18)&63]; result += alphabet[(v>>12)&63];
    result += i+1 < data.size() ? alphabet[(v>>6)&63] : '='; result += i+2 < data.size() ? alphabet[v&63] : '=';
  }
  return result;
}
std::vector<std::byte> decodeBase64(const Json &v) {
  const auto s = text(v,12*1024*1024); need(s.size()%4 == 0,"Invalid graph state base64 length");
  auto digit = [](char c) -> uint32_t { if (c >= 'A' && c <= 'Z') return c-'A'; if (c >= 'a' && c <= 'z') return c-'a'+26; if (c >= '0' && c <= '9') return c-'0'+52; if (c == '+') return 62; if (c == '/') return 63; throw std::invalid_argument("Invalid graph state base64 character"); };
  std::vector<std::byte> data; data.reserve(s.size()/4*3);
  for (size_t i = 0; i < s.size(); i += 4) {
    const auto a = digit(s[i]), b = digit(s[i+1]); const bool p2 = s[i+2] == '=', p3 = s[i+3] == '=';
    need(!p2 || p3,"Invalid base64 padding"); need(!(p2 || p3) || i+4 == s.size(),"Base64 padding must be final");
    const auto c = p2 ? 0 : digit(s[i+2]), d = p3 ? 0 : digit(s[i+3]);
    need(!p2 || (b&15) == 0,"Noncanonical base64 padding bits"); need(p2 || !p3 || (c&3) == 0,"Noncanonical base64 padding bits");
    const auto bits = (a<<18)|(b<<12)|(c<<6)|d; data.push_back(std::byte((bits>>16)&255));
    if (!p2) data.push_back(std::byte((bits>>8)&255)); if (!p3) data.push_back(std::byte(bits&255));
  }
  need(data.size() <= 8*1024*1024,"Graph plugin state exceeds 8 MiB"); return data;
}
Json recipe(const GraphPluginRecipe &p) { return {{"format",p.format},{"name",p.name},{"path",p.path},{"classID",p.classID},{"type",p.type},{"subtype",p.subtype},{"manufacturer",p.manufacturer},{"state",base64(p.state)},{"inputs",p.inputs},{"outputs",p.outputs}}; }
GraphPluginRecipe decodeRecipe(const Json &v) {
  GraphPluginRecipe r; r.format = text(field(v,"format"),16); r.name = text(optional(v,"name",""),256); r.path = text(optional(v,"path",""),4096); r.classID = text(optional(v,"classID",""),256);
  r.type = uint32_t(integer(optional(v,"type",0),0,UINT32_MAX)); r.subtype = uint32_t(integer(optional(v,"subtype",0),0,UINT32_MAX)); r.manufacturer = uint32_t(integer(optional(v,"manufacturer",0),0,UINT32_MAX)); r.state = decodeBase64(optional(v,"state",""));
  for (const auto &p : items(v,"inputs",63,true)) r.inputs.push_back(uint32_t(integer(p,1,63)));
  for (const auto &p : items(v,"outputs",63,true)) r.outputs.push_back(uint32_t(integer(p,1,63))); return r;
}
#define NODE_NAMES {"input","output","plugin","lfo","follower","random","note-envelope","midi","amount","automation"}
#define SIGNAL_COMMAND_NAMES {"row","start","stop","clear","amount","wet"}
Json definition(const SignalDefinition &d) {
  Json nodes = Json::array(), audio = Json::array(), modulation = Json::array();
  for (const auto &n : d.nodes) {
    Json j{{"id",nativeID(n.id)},{"kind",enumName(size_t(n.kind),NODE_NAMES)},{"name",n.name},{"x",n.x},{"y",n.y},{"rate",n.rate},{"phase",n.phase},{"attack",n.attack},{"release",n.release},{"controller",n.controller}};
    if (n.kind == SignalNodeKind::Plugin) j["plugin"] = recipe(n.plugin);
    if (n.kind == SignalNodeKind::Automation) { Json envelopes = Json::array(); for (const auto &e : n.envelopes) { Json points = Json::array(); for (const auto &p : e.points) points.push_back(point(p)); envelopes.push_back({{"pattern",nativeID(e.pattern)},{"enabled",e.enabled},{"points",points}}); } j["envelopes"] = std::move(envelopes); }
    nodes.push_back(std::move(j));
  }
  for (const auto &e : d.audio) audio.push_back({{"source",nativeID(e.source)},{"target",nativeID(e.target)},{"output",e.output},{"input",e.input},{"gain",e.gain}});
  for (const auto &e : d.modulation) modulation.push_back({{"source",nativeID(e.source)},{"target",nativeID(e.target)},{"parameter",e.parameter},{"minimum",e.minimum},{"maximum",e.maximum},{"base",e.base},{"enabled",e.enabled}});
  return {{"id",nativeID(d.id)},{"number",d.number},{"name",d.name},{"nodes",nodes},{"audio",audio},{"modulation",modulation}};
}
SignalDefinition decodeDefinition(const Json &v, unsigned version) {
  SignalDefinition d; d.id = decodeID(field(v,"id")); d.number = uint16_t(u32(v,"number",1,999)); d.name = text(field(v,"name"),256);
  for (const auto &n : items(v,"nodes",64)) {
    SignalNode node; node.id = decodeID(field(n,"id")); node.kind = SignalNodeKind(enumIndex(field(n,"kind"),NODE_NAMES));
    need(version >= 13 || node.kind != SignalNodeKind::Automation,"Legacy metadata cannot contain graph automation");
    node.name = text(optional(n,"name",""),256); node.x = number(optional(n,"x",0),-100000,100000); node.y = number(optional(n,"y",0),-100000,100000);
    node.rate = number(optional(n,"rate",1),.0001,1024); node.phase = number(optional(n,"phase",0),0,1);
    node.attack = number(optional(n,"attack",.01),.00001,60); node.release = number(optional(n,"release",.1),.00001,60); node.controller = uint32_t(integer(optional(n,"controller",1),0,127));
    if (node.kind == SignalNodeKind::Plugin) node.plugin = decodeRecipe(field(n,"plugin")); else need(!n.contains("plugin"),"Only plugin nodes may have a plugin recipe");
    if (node.kind == SignalNodeKind::Automation) for (const auto &e : items(n,"envelopes",1024,true)) {
      SignalPatternEnvelope envelope; envelope.pattern = decodeID(field(e,"pattern")); envelope.enabled = boolean(optional(e,"enabled",true));
      for (const auto &p : items(e,"points",4096)) envelope.points.push_back(decodePoint(p)); node.envelopes.push_back(std::move(envelope));
    } else need(!n.contains("envelopes"),"Only automation sources may have pattern curves");
    d.nodes.push_back(std::move(node));
  }
  for (const auto &e : items(v,"audio",256)) d.audio.push_back({decodeID(field(e,"source")),decodeID(field(e,"target")),uint32_t(integer(optional(e,"output",0),0,63)),uint32_t(integer(optional(e,"input",0),0,63)),number(optional(e,"gain",1),-16,16)});
  for (const auto &e : items(v,"modulation",256)) d.modulation.push_back({decodeID(field(e,"source")),decodeID(field(e,"target")),u32(e,"parameter"),number(optional(e,"minimum",0),-1,1),number(optional(e,"maximum",1),-1,1),number(optional(e,"base",0),0,1),boolean(optional(e,"enabled",true))});
  compileSignal(d); return d;
}
Json signal(const SignalGraph &g) {
  Json library = Json::array(), assignments = Json::array(), instrumentAssignments = Json::array(), commands = Json::array(), lanes = Json::array(), inputs = Json::array(), outputs = Json::array(), layout = Json::array();
  for (const auto &d : g.library) library.push_back(definition(d));
  auto encodeAssignments = [](const auto &source, Json &out) { for (const auto &a : source) out.push_back({{"target",nativeID(a.target)},{"graph",nativeID(a.graph)},{"amount",a.amount},{"wet",a.wet}}); };
  encodeAssignments(g.assignments,assignments); encodeAssignments(g.instrumentAssignments,instrumentAssignments);
  for (const auto &c : g.commands) commands.push_back({{"pattern",nativeID(c.pattern)},{"target",nativeID(c.target)},{"graph",c.graph ? nativeID(c.graph) : ""},{"position",c.position},{"column",c.column},{"kind",enumName(size_t(c.kind),SIGNAL_COMMAND_NAMES)},{"amount",c.amount},{"wet",c.wet},{"tails",c.tails}});
  for (auto [id,count] : g.lanes) lanes.push_back({{"target",nativeID(id)},{"count",count}});
  for (const auto &r : g.inputs) inputs.push_back({{"source",nativeID(r.source)},{"target",nativeID(r.target)},{"input",r.input},{"gainDB",r.gainDB},{"preFader",r.preFader}});
  for (const auto &r : g.outputs) outputs.push_back({{"source",nativeID(r.source)},{"target",nativeID(r.target)},{"output",r.output}});
  for (const auto &[key,p] : g.layout) layout.push_back({{"node",key},{"x",p[0]},{"y",p[1]}});
  return {{"library",library},{"assignments",assignments},{"instrumentAssignments",instrumentAssignments},{"commands",commands},{"lanes",lanes},{"inputs",inputs},{"outputs",outputs},{"layout",layout}};
}
SignalGraph decodeSignal(const Json &v, unsigned version) {
  SignalGraph g;
  for (const auto &d : items(v,"library",128)) g.library.push_back(decodeDefinition(d,version));
  auto assignments = [&](const char *key, auto &out, size_t maximum, bool optionalField) { for (const auto &a : items(v,key,maximum,optionalField)) out.push_back({decodeID(field(a,"target")),decodeID(field(a,"graph")),number(optional(a,"amount",1),0,1),number(optional(a,"wet",1),0,1)}); };
  assignments("assignments",g.assignments,240,false); assignments("instrumentAssignments",g.instrumentAssignments,128,true);
  need(version >= 13 || g.instrumentAssignments.empty(),"Legacy metadata cannot contain instrument graphs");
  for (const auto &c : items(v,"commands",65536)) g.commands.push_back({decodeID(field(c,"pattern")),decodeID(field(c,"target")),field(c,"graph") == "" ? 0 : decodeID(c.at("graph")),u32(c,"position"),uint8_t(integer(optional(c,"column",0),0,7)),SignalCommandKind(enumIndex(field(c,"kind"),SIGNAL_COMMAND_NAMES)),number(optional(c,"amount",1),0,1),number(optional(c,"wet",1),0,1),boolean(optional(c,"tails",false))});
  for (const auto &l : items(v,"lanes",240)) need(g.lanes.emplace(decodeID(field(l,"target")),uint8_t(u32(l,"count",1,8))).second,"Duplicate graph lane");
  for (const auto &r : items(v,"inputs",128,true)) g.inputs.push_back({decodeID(field(r,"source")),decodeID(field(r,"target")),u32(r,"input",1,63),number(optional(r,"gainDB",0),-96,12),boolean(optional(r,"preFader",false))});
  for (const auto &r : items(v,"outputs",128,true)) g.outputs.push_back({decodeID(field(r,"source")),decodeID(field(r,"target")),u32(r,"output",1,63)});
  for (const auto &p : items(v,"layout",8192,true)) need(g.layout.emplace(text(field(p,"node"),256),std::array<double,2>{real(p,"x",0,100000),real(p,"y",0,100000)}).second,"Duplicate graph position");
  return g;
}
#undef NODE_NAMES
#undef SIGNAL_COMMAND_NAMES
