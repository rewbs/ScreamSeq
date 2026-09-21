#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
using namespace ScreamSeq::Project;
using namespace Tracker;
namespace {
size_t checks = 0;
void check(bool ok, const std::string &message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<typename F> void rejects(F operation, const std::string &message) {
  bool rejected = false; try { operation(); } catch (const std::exception &) { rejected = true; }
  check(rejected,message);
}
void rejectAt(const Json &source, const char *path, Json value) {
  auto bad = source; bad[Json::json_pointer(path)] = std::move(value);
  rejects([&] { (void)decodeNativeMetadata(bad); },std::string("accepted bad field: ")+path);
}
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
// Visit every known dictionary field/array member, including optional fields
// exercised by the rich model. Null is never a valid known metadata field.
void rejectNulls(const Json &whole, const Json &subtree, const std::string &path = "") {
  if (!path.empty()) {
    auto bad = whole; bad[Json::json_pointer(path)] = nullptr; rejects([&] { (void)decodeNativeMetadata(bad); },"accepted null at "+path);
    Json wrong = subtree.is_number() ? Json(true) : subtree.is_boolean() ? Json(1) : subtree.is_string() ? Json(0) : subtree.is_array() ? Json::object() : Json::array();
    bad[Json::json_pointer(path)] = std::move(wrong); rejects([&] { (void)decodeNativeMetadata(bad); },"accepted wrong type at "+path);
  }
  if (subtree.is_object()) for (auto it = subtree.begin(); it != subtree.end(); ++it) rejectNulls(whole,it.value(),path+"/"+it.key());
  if (subtree.is_array()) for (size_t i = 0; i < subtree.size(); ++i) rejectNulls(whole,subtree[i],path+"/"+std::to_string(i));
}
void addUnknowns(Json &j) { if (j.is_object()) { for (auto &v : j) addUnknowns(v); j["future-extension"] = {{"arbitrary",Json::array({nullptr,true,1,"opaque"})}}; } else if (j.is_array()) for (auto &v : j) addUnknowns(v); }
void versionTests(const Json &baseline, const Json &rich) {
  const auto native = decodeNativeMetadata(baseline);
  for (unsigned version = 1; version <= 14; ++version) { auto old = legacy(baseline,version); check(decodeNativeMetadata(old) == native,"legacy version "+std::to_string(version)); auto upgraded = encodeNativeMetadata(decodeNativeMetadata(old)); check(upgraded["version"] == 14,"legacy canonical upgrade"); }
  for (auto [first,key] : {std::pair{2,"automation"},{3,"mixer"},{5,"noteTracks"},{5,"columnMutes"},{7,"performance"},{9,"preciseNotes"},{10,"signalGraph"},{14,"envelopeBank"}}) { auto old = legacy(baseline,first-1); old[key] = baseline.at(key); rejects([&] { (void)decodeNativeMetadata(old); },std::string("legacy known field: ")+key); }
  auto old = legacy(rich,5); rejects([&] { (void)decodeNativeMetadata(old); },"scripts cannot appear in v5");
  for (Json badVersion : {Json(0),Json(15),Json(true),Json(1.0),Json("14"),Json(-1),Json(UINT64_MAX)}) rejectAt(baseline,"/version",badVersion);
  auto minimal = baseline;
  // Standalone nested version gates, with unrelated features stripped first.
  auto mixed = rich; mixed["automation"] = Json::array(); mixed["envelopeBank"] = {{"entries",Json::array()},{"links",Json::array()}};
  auto prepan = legacy(mixed,5); prepan["mixer"]["buses"][0]["prePan"] = 0.0;
  rejects([&] { (void)decodeNativeMetadata(prepan); },"prePan before v6");
  auto sidechain = legacy(baseline,3); sidechain["mixer"]["sidechains"] = Json::array(); rejects([&] { (void)decodeNativeMetadata(sidechain); },"sidechains before v4");
  auto pitch = legacy(mixed,7); rejects([&] { (void)decodeNativeMetadata(pitch); },"pitch before v8");
  auto effects = legacy(baseline,11); effects["preciseNotes"] = rich["preciseNotes"]; rejects([&] { (void)decodeNativeMetadata(effects); },"per-note effects before v12");
  auto scripts = legacy(baseline,10); scripts["automation"] = rich["automation"]; rejects([&] { (void)decodeNativeMetadata(scripts); },"scripts before v11");
  auto curves = legacy(mixed,12); rejects([&] { (void)decodeNativeMetadata(curves); },"graph curves before v13");
}
void historicalModels(Document &doc) {
  const auto original = doc.native();
  for (unsigned version = 1; version <= 14; ++version) {
    auto n = original;
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
    auto wire = legacy(encodeNativeMetadata(n),version);
    const auto decoded = decodeNativeMetadata(wire);
    check(decoded == n,"nonempty historical version "+std::to_string(version));
    doc.restoreNative(decoded);
    check(doc.native() == n,"shared historical restoration "+std::to_string(version));
    // Check each newly introduced nested feature against the immediately prior
    // version on an otherwise-valid tree, rather than an unrelated bad feature.
    if (version == 7) { auto bad = wire; bad["performance"]["commands"][0]["kind"] = 2; rejects([&]{ (void)decodeNativeMetadata(bad); },"v7 rejects pitch kind"); bad = wire; bad["performance"]["commands"][0]["pitchRange"] = 2; rejects([&]{ (void)decodeNativeMetadata(bad); },"v7 rejects pitchRange even at default"); }
    if (version == 10) { auto bad = wire; bad["automation"][0]["points"][0].push_back("t"); rejects([&]{ (void)decodeNativeMetadata(bad); },"v10 rejects formula field"); bad = wire; bad["automation"][0]["points"][0][2] = 8; rejects([&]{ (void)decodeNativeMetadata(bad); },"v10 rejects scripted enum"); }
    if (version == 11) { auto bad = wire; bad["preciseNotes"][0]["parameter"] = 0; rejects([&]{ (void)decodeNativeMetadata(bad); },"v11 rejects default note parameter field"); }
    if (version == 12) { auto bad = wire; bad["signalGraph"]["instrumentAssignments"] = encodeNativeMetadata(original)["signalGraph"]["instrumentAssignments"]; rejects([&]{ (void)decodeNativeMetadata(bad); },"v12 rejects instrument graph assignment"); }
  }
  doc.restoreNative(original);
}
void optionalDefaults(const Json &encoded) {
  auto compare = [&](const char *objectPath, const char *key, Json fallback) {
    auto missing = encoded, explicitDefault = encoded;
    missing[Json::json_pointer(objectPath)].erase(key);
    explicitDefault[Json::json_pointer(objectPath)][key] = std::move(fallback);
    check(decodeNativeMetadata(missing) == decodeNativeMetadata(explicitDefault),std::string("optional default ")+objectPath+"/"+key);
  };
  for (const char *key : {"name","path","classID","state"}) compare("/signalGraph/library/0/nodes/2/plugin",key,"");
  for (const char *key : {"type","subtype","manufacturer"}) compare("/signalGraph/library/0/nodes/2/plugin",key,0);
  compare("/signalGraph/library/0/nodes/0","name","");
  for (const char *key : {"x","y","phase"}) compare("/signalGraph/library/0/nodes/0",key,0);
  compare("/signalGraph/library/0/nodes/0","rate",1); compare("/signalGraph/library/0/nodes/0","attack",.01); compare("/signalGraph/library/0/nodes/0","release",.1); compare("/signalGraph/library/0/nodes/0","controller",1);
  compare("/signalGraph/library/0/nodes/9/envelopes/0","enabled",true);
  compare("/signalGraph/library/0/audio/0","input",0); compare("/signalGraph/library/0/audio/0","output",0); compare("/signalGraph/library/0/audio/0","gain",1);
  compare("/signalGraph/library/0/modulation/0","minimum",0); compare("/signalGraph/library/0/modulation/0","maximum",1); compare("/signalGraph/library/0/modulation/0","base",0); compare("/signalGraph/library/0/modulation/0","enabled",true);
  for (const char *key : {"amount","wet"}) { compare("/signalGraph/assignments/0",key,1); compare("/signalGraph/instrumentAssignments/0",key,1); compare("/signalGraph/commands/0",key,1); }
  compare("/signalGraph/commands/0","tails",false); compare("/signalGraph/commands/0","column",0);
  compare("/signalGraph/inputs/0","gainDB",0); compare("/signalGraph/inputs/0","preFader",false);
  compare("/signalGraph","layout",Json::array()); compare("/signalGraph","instrumentAssignments",Json::array());
  compare("/performance/commands/2","pitchRange",2);
  compare("/envelopeBank/entries/0/shape","rowsPerBeat",4); compare("/envelopeBank/entries/0/shape","instrument",false); compare("/envelopeBank/entries/0/shape","flags",1); compare("/envelopeBank/entries/0/shape","markers",Json::array({0,0,0,0,UINT32_MAX}));
  compare("/envelopeBank/entries/0/shape/points/0","curve","linear");
  compare("/envelopeBank/links/0/target","pattern","");
}
void boundaryTests(const NativeSong &model, const Json &encoded) {
  auto n = model;
  n.mixer.buses[0].pan = -0.0;
  const auto wire = encodeNativeMetadata(n);
  check(std::bit_cast<uint64_t>(decodeNativeMetadata(Json::parse(wire.dump())).mixer.buses[0].pan) == std::bit_cast<uint64_t>(-0.0),"negative-zero numeric bits");
  n = model; n.patterns.begin()->second.name = std::string();
  for (int i = 0; i < 128; ++i) n.patterns.begin()->second.name += "\xF0\x9F\x8E\xB5";
  check(decodeNativeMetadata(encodeNativeMetadata(n)) == n,"UTF-16 string boundary with supplementary code points");
  n.patterns.begin()->second.name += "a"; rejects([&]{ (void)encodeNativeMetadata(n); },"UTF-16 length overflow");
  for (const auto &format : {"Built-in","AU","VST3"}) for (size_t size : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(255),size_t(256),size_t(257)}) {
    n = model; auto &recipe = n.signal.library[0].nodes[2].plugin; recipe.format = format; recipe.state.resize(size);
    for (size_t i = 0; i < size; ++i) recipe.state[i] = std::byte(i%256);
    check(decodeNativeMetadata(encodeNativeMetadata(n)) == n,"plugin identity and all base64 padding lengths");
  }
  n = model; n.signal.library[0].nodes[2].plugin.state.resize(8*1024*1024,std::byte(0xa5));
  check(decodeNativeMetadata(encodeNativeMetadata(n)) == n,"8 MiB plugin state boundary");
  n.signal.library[0].nodes[2].plugin.state.push_back(std::byte(0)); rejects([&]{ (void)encodeNativeMetadata(n); },"plugin state over 8 MiB");
  n = model; n.signal.library[0].nodes[0].plugin.state = {std::byte(1)}; rejects([&]{ (void)encodeNativeMetadata(n); },"encoder must not drop conditionally unrepresentable model data");
  auto high = encoded; high["nextID"] = NativeSong::maximumID; high["patterns"][0][1]["id"] = "n999999999999";
  // A high ID must be accepted, but references to the replaced ID must move too.
  const auto oldID = encoded["patterns"][0][1]["id"];
  std::function<void(Json &)> replaceID = [&](Json &j) { if (j.is_string() && j == oldID) j = "n999999999999"; else if (j.is_structured()) for (auto &v : j) replaceID(v); };
  replaceID(high); check(decodeNativeMetadata(high).patterns.begin()->second.id == NativeSong::maximumID-1,"canonical maximum native identity");
}
void negativeTests(const Json &j) {
  auto duplicate = j; duplicate["tracks"][1][1]["id"] = duplicate["tracks"][0][1]["id"]; rejects([&] { (void)decodeNativeMetadata(duplicate); },"duplicate global identities");
  for (const auto &s : {"n0","n01","n-1","n+1","N1","n1 ","n1x","n1000000000000","n18446744073709551615","n999999999999999999999999999999999"}) rejectAt(j,"/patterns/0/1/id",s);
  for (Json v : {Json(true),Json(-1),Json(1.5),Json("1"),Json(UINT64_MAX)}) rejectAt(j,"/nextID",v);
  rejectAt(j,"/patterns/0/0",65536); rejectAt(j,"/patterns/0/1/color",0x1000000);
  rejectAt(j,"/patterns/0/1/name",std::string(257,'a')); rejectAt(j,"/patterns/0/1/name",std::string("a\0b",3)); rejectAt(j,"/patterns/0/1/name",std::string("\xC0\x80",2));
  rejectAt(j,"/automation/0/pattern","n999999"); rejectAt(j,"/automation/0/parameter",4294967296ULL); rejectAt(j,"/automation/0/enabled",1); rejectAt(j,"/automation/0/points/0/1",1.1); rejectAt(j,"/automation/0/points/1/0",0); rejectAt(j,"/automation/0/points/8/3","unrecognized_function(t)");
  rejectAt(j,"/automation/0/points/0/1",std::numeric_limits<double>::infinity()); rejectAt(j,"/automation/0/points/0/1",std::numeric_limits<double>::quiet_NaN());
  rejectAt(j,"/mixer/buses/0/output","n999999"); rejectAt(j,"/mixer/buses/0/preGainDB",25); rejectAt(j,"/mixer/buses/0/prePan",1.01); rejectAt(j,"/mixer/buses/0/timingMS",501); rejectAt(j,"/mixer/sidechains/0/input",0); rejectAt(j,"/mixer/instruments/0/output",64);
  rejectAt(j,"/noteTracks/0/columns/0","n999999"); rejectAt(j,"/columnMutes/0/0","n999999");
  rejectAt(j,"/performance/columns/0/1",0); rejectAt(j,"/performance/bindings/0/id",256); rejectAt(j,"/performance/commands/0/binding",2); rejectAt(j,"/performance/commands/0/value",2); rejectAt(j,"/performance/commands/1/duration",0); rejectAt(j,"/performance/commands/2/pitchRange",97); rejectAt(j,"/performance/commands/0/track","n999999");
  rejectAt(j,"/preciseNotes/0/position",4294967296ULL); rejectAt(j,"/preciseNotes/0/note",121); rejectAt(j,"/preciseNotes/0/velocity",0); rejectAt(j,"/preciseNotes/0/effect",255); rejectAt(j,"/preciseNotes/1/instrument",1); rejectAt(j,"/preciseNotes/0/pattern","n999999");
  rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/state","A==="); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/state","AA=A"); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/state","AB=="); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/state","AA==AA=="); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/state","AA\n=");
  rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/inputs/0",0); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/outputs/0",64); rejectAt(j,"/signalGraph/library/0/nodes/2/plugin/format","CLAP"); rejectAt(j,"/signalGraph/library/0/nodes/0/plugin",Json::object()); rejectAt(j,"/signalGraph/library/0/nodes/0/envelopes",Json::array());
  rejectAt(j,"/signalGraph/library/0/audio/0/source","n999999"); rejectAt(j,"/signalGraph/library/0/audio/0/input",62); rejectAt(j,"/signalGraph/library/0/modulation/0/minimum",-2); rejectAt(j,"/signalGraph/library/0/nodes/9/envelopes/0/pattern","n999999");
  rejectAt(j,"/signalGraph/assignments/0/graph","n999999"); rejectAt(j,"/signalGraph/instrumentAssignments/0/target","n999999"); rejectAt(j,"/signalGraph/inputs/0/input",62); rejectAt(j,"/signalGraph/outputs/0/output",62); rejectAt(j,"/signalGraph/layout/0/x",-1); rejectAt(j,"/signalGraph/lanes/0/count",9); rejectAt(j,"/signalGraph/commands/3/graph","n1");
  rejectAt(j,"/envelopeBank/entries/0/shape/span",0); rejectAt(j,"/envelopeBank/entries/0/shape/markers",Json::array({0,0,0,0})); rejectAt(j,"/envelopeBank/entries/0/shape/points/1/position",0); rejectAt(j,"/envelopeBank/entries/1/shape/markers/0",UINT32_MAX);
  rejectAt(j,"/envelopeBank/links/0/template","n999999"); rejectAt(j,"/envelopeBank/links/0/target/owner","n999999"); rejectAt(j,"/envelopeBank/links/0/target/pattern","n1");
  for (const char *path : {"/patterns","/columnMutes","/performance/columns","/performance/bindings","/performance/commands","/preciseNotes","/signalGraph/library","/signalGraph/lanes","/signalGraph/layout","/envelopeBank/links"}) { auto bad = j; auto &a = bad[Json::json_pointer(path)]; a.push_back(a[0]); rejects([&] { (void)decodeNativeMetadata(bad); },std::string("duplicate array entry ")+path); }
}
}
int main(int argc, char **argv) {
  try {
    auto doc = std::make_unique<Document>();
    const auto baseline = encodeNativeMetadata(doc->native());
    check(baseline.at("version") == 14,"canonical metadata version must be 14");
    check(decodeNativeMetadata(baseline) == doc->native(),"baseline model roundtrip");
    doc->restoreNative(decodeNativeMetadata(baseline));
    auto rich = richDocument(); const auto model = rich->native(); const auto encoded = encodeNativeMetadata(model);
    check(decodeNativeMetadata(encoded) == model,"all-field model roundtrip");
    rich->restoreNative(decodeNativeMetadata(encoded));
    check(encodeNativeMetadata(decodeNativeMetadata(encoded)) == encoded,"canonical encoding is stable");
    rejectNulls(encoded,encoded); auto future = encoded; addUnknowns(future); check(decodeNativeMetadata(future) == model,"unknown dictionary keys must be tolerated everywhere");
    versionTests(baseline,encoded); historicalModels(*rich); optionalDefaults(encoded); boundaryTests(model,encoded); negativeTests(encoded);
    // Snapshot-specific validation is deliberately the shared Document operation.
    auto outside = encoded; outside["preciseNotes"][0]["position"] = UINT32_MAX;
    rejects([&] { rich->restoreNative(decodeNativeMetadata(outside)); },"restore must reject note outside actual pattern"); check(rich->native() == model,"failed restore must preserve existing model");
    auto changedLink = encoded; changedLink["envelopeBank"]["links"][0]["span"] = 1;
    rejects([&] { rich->restoreNative(decodeNativeMetadata(changedLink)); },"restore must validate materialized envelope links");
    auto invalid = model; invalid.automation[0].points[0].value = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)encodeNativeMetadata(invalid); },"encoder must reject invalid models");
    if (argc > 1) {
      std::ifstream input(argv[1]); check(bool(input),"metadata fixture unavailable"); Json fixture; input >> fixture;
      const auto mac = decodeNativeMetadata(fixture); const auto canonical = encodeNativeMetadata(mac);
      check(canonical == fixture,"actual Mac fixture must preserve every known field"); check(decodeNativeMetadata(canonical) == mac,"actual Mac model roundtrip");
      check(mac.envelopeBank.at(0).shape.markers[4] == UINT32_MAX,"bank sentinel lost"); check(mac.automation.at(0).points.at(0).formula.source() == "mix(start,end,t^2)","Mac script source changed");
      std::cout << "PASS actual Mac v14 metadata: complete field equality and model roundtrip (snapshot restore not exercised here)\n";
    }
    std::cout << "PASS " << checks << " checks: versions 1-14, rich model, shared restore, known-field nulls, unknown keys, malformed fields and references\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
