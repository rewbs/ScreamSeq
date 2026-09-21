#include "windows/Session/GraphOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#ifdef small
#undef small
#endif
#include <iostream>
#include <stdexcept>
using namespace Tracker;
using ScreamSeq::GraphOperations;
using ScreamSeq::Json;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": "+#x); } while(false)
struct Fixture {
  std::unique_ptr<Document> doc=std::make_unique<Document>(MOD_TYPE_MPT,4);
  unsigned stops=0;
  GraphOperations api{*doc,[this]{++stops;}};
};
void createReadHistory() {
  Fixture f;
  const auto original=f.doc->native();
  auto preview=f.api.invoke("graph.create",{{"name","Test"},{"dryRun",true}});
  CHECK(preview.at("wouldChange")==true); CHECK(f.doc->native()==original); CHECK(f.stops==0);
  CHECK(!f.doc->canUndo()); CHECK(f.doc->revision==0);
  auto result=f.api.invoke("graph.create",{{"name","Test"}});
  CHECK(result.at("graph")==preview.at("graph")); CHECK(f.stops==1);
  auto read=f.api.invoke("graph.get",Json::object());
  CHECK(read.at("library").size()==1); CHECK(read.at("library")[0].at("nodes").size()==2);
  CHECK(read.at("library")[0].at("name")=="Test"); CHECK(read.at("unitsPerRow")==65536);
  CHECK(read.at("plugins").empty()); CHECK(read.at("activity").empty());
  CHECK(read.at("patterns")[0].at("id")=="n"+std::to_string(original.patterns.at(0).id));
  const auto changed=f.doc->native(); CHECK(f.doc->canUndo());
  f.doc->undo(); auto undone=original; undone.nextID=changed.nextID;
  CHECK(f.doc->native()==undone); CHECK(f.doc->canRedo());
  const auto history=f.doc->historyBytes(),revision=f.doc->revision;
  f.api.invoke("graph.create",{{"dryRun",true}});
  CHECK(f.doc->historyBytes()==history); CHECK(f.doc->revision==revision); CHECK(f.doc->canRedo());
  f.doc->redo(); CHECK(f.doc->native()==changed);
}
void rejected(Fixture &f,const std::string &method,const Json &p,int code=-32602) {
  const auto native=f.doc->native(); const auto snapshot=f.doc->snapshotData();
  const auto revision=f.doc->revision,history=f.doc->historyBytes(); const auto stops=f.stops;
  const auto undo=f.doc->canUndo(),redo=f.doc->canRedo(); bool threw=false;
  try { f.api.invoke(method,p); } catch(const ScreamSeq::Api::ApiError &e) { CHECK(e.code==code); threw=true; }
  CHECK(threw); CHECK(f.doc->native()==native); CHECK(f.doc->snapshotData()==snapshot);
  CHECK(f.doc->revision==revision); CHECK(f.doc->historyBytes()==history); CHECK(f.stops==stops);
  CHECK(f.doc->canUndo()==undo); CHECK(f.doc->canRedo()==redo);
}
Json definition(Fixture &f,size_t index=0,bool state=true) { return f.api.invoke("graph.get",{{"includeState",state}}).at("library").at(index); }
Json recipe() { return {{"format","Built-in"},{"name","Gain"},{"classID","org.resonance.gain"},{"state","AAEC/w=="},{"inputs",{1}},{"outputs",{1}}}; }
void nodesAndCloning() {
  Fixture f; const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  auto d=definition(f); const auto input=d["nodes"][0]["id"],output=d["nodes"][1]["id"];
  const auto plugin=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()},{"insertAfter",input}}).at("node");
  const auto lfo=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","lfo"}}).at("node");
  d=definition(f,0,false); CHECK(!d["nodes"][2]["plugin"].contains("state"));
  d["modulation"].push_back({{"source",lfo},{"target",plugin},{"parameter",7},{"minimum",-0.5},{"maximum",0.5}});
  f.api.invoke("graph.update",{{"definition",d}});
  CHECK(definition(f)["nodes"][2]["plugin"]["state"]==recipe()["state"]);
  CHECK(f.doc->native().signal.library[0].audio.size()==2);
  CHECK(compileSignal(f.doc->native().signal.library[0]).order.size()==4);
  auto stops=f.stops; d=definition(f); d["name"]="Renamed"; d["number"]=7; d["nodes"][2]["x"]=42;
  f.api.invoke("graph.update",{{"definition",d}}); CHECK(f.stops==stops);
  auto history=f.doc->historyBytes(),rev=f.doc->revision;
  CHECK(f.api.invoke("graph.update",{{"definition",d}})["wouldChange"]==false);
  CHECK(f.doc->historyBytes()==history); CHECK(f.doc->revision==rev);
  const auto clone=f.api.invoke("graph.clone",{{"graph",graph}}).at("graph");
  auto copied=definition(f,1); CHECK(copied["id"]==clone); CHECK(copied["number"]==1);
  std::set<Json> oldIDs; for(const auto &n:d["nodes"]) oldIDs.insert(n["id"]);
  for(const auto &n:copied["nodes"]) CHECK(!oldIDs.contains(n["id"]));
  CHECK(copied["audio"][0]["source"]==copied["nodes"][2]["id"]);
  CHECK(copied["modulation"][0]["source"]==copied["nodes"][3]["id"]);
  auto invalid=d; invalid["nodes"][2]["kind"]="lfo"; invalid["nodes"][2].erase("plugin");
  rejected(f,"graph.update",{{"definition",invalid}});
  invalid=d; invalid["nodes"][3]["id"]="n999999"; rejected(f,"graph.update",{{"definition",invalid}});
  invalid=d; invalid["nodes"][3]["kind"]="follower"; rejected(f,"graph.update",{{"definition",invalid}});
  auto follower=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","follower"}}).at("node");
  invalid=definition(f); invalid["audio"].push_back({{"source",plugin},{"target",follower}});
  invalid["modulation"].push_back({{"source",follower},{"target",plugin},{"parameter",8}});
  rejected(f,"graph.update",{{"definition",invalid}}); // real audio/modulation dependency cycle
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0}});
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","input"}});
  rejected(f,"graph.node.remove",{{"graph",graph},{"node",input}});
  f.api.invoke("graph.node.remove",{{"graph",graph},{"node",plugin}});
  CHECK(definition(f)["audio"].empty()); CHECK(definition(f)["modulation"].empty());
  f.api.invoke("graph.remove",{{"graph",clone}}); CHECK(f.doc->native().signal.library.size()==1);
  f.doc->undo(); CHECK(f.doc->native().signal.library.size()==2); f.doc->redo(); CHECK(f.doc->native().signal.library.size()==1);
}
void automationAndBanks() {
  Fixture f; const auto second=f.doc->addPattern(32,false,0);
  f.doc->transaction([&](OpenMPT::CSoundFile &s){s.Patterns[second].SetSignature(3,12);});
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto node=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","automation"}}).at("node");
  Json target={{"graph",graph},{"node",node},{"pattern",0}};
  auto read=f.api.invoke("graph.automation.get",target); CHECK(read["points"].empty()); CHECK(read["enabled"]==true); CHECK(read["unitsPerRow"]==256);
  auto set=target; set["points"]=Json::array({{{"position",0},{"value",0.25},{"curve","scripted"},{"formula","start + (end-start)*t"}},{{"position",257},{"value",0.75}}});
  auto before=f.doc->native(); auto stops=f.stops; set["dryRun"]=true;
  CHECK(f.api.invoke("graph.automation.set",set)["wouldChange"]==true); CHECK(f.doc->native()==before); CHECK(f.stops==stops);
  set.erase("dryRun"); f.api.invoke("graph.automation.set",set); CHECK(f.stops==stops+1);
  const auto firstCurve=f.api.invoke("graph.automation.get",target);
  set["pattern"]=second; set["enabled"]=false; set["points"]=Json::array({{{"position",8191},{"value",0.5}}});
  f.api.invoke("graph.automation.set",set); CHECK(f.api.invoke("graph.automation.get",target)==firstCurve);
  auto secondTarget=target; secondTarget["pattern"]=second; read=f.api.invoke("graph.automation.get",secondTarget);
  CHECK(read["rowsPerBeat"]==3); CHECK(read["rows"]==32); CHECK(read["enabled"]==false); CHECK(read["points"][0]["position"]==8191);
  auto invalid=set; invalid["points"][0]["position"]=8192; rejected(f,"graph.automation.set",invalid);
  invalid=set; invalid["points"][0]["position"]=0.5; rejected(f,"graph.automation.set",invalid);
  invalid=set; invalid["points"][0]["value"]=true; rejected(f,"graph.automation.set",invalid);
  invalid=set; invalid["points"][0]["ignored"]=1; rejected(f,"graph.automation.set",invalid);
  invalid=set; invalid["points"][0]["curve"]="scripted"; invalid["points"][0]["formula"]="file('x')"; rejected(f,"graph.automation.set",invalid);
  const auto owner=f.doc->native().signal.library[0].nodes[2].id;
  const auto pattern=f.doc->native().patterns.at(0).id;
  const EnvelopeTarget bankTarget{EnvelopeTargetKind::Graph,owner,pattern};
  f.doc->annotate([&](NativeSong &n){ auto shape=captureEnvelope(n,f.doc->song(),bankTarget); const auto bank=n.makeEntity().id;
    n.envelopeBank.push_back({bank,"Linked",shape}); n.envelopeLinks.push_back({bankTarget,bank,shape.span}); });
  invalid=target; invalid["points"]=Json::array({{{"position",0},{"value",0.1}}}); rejected(f,"graph.automation.set",invalid);
  const auto clone=f.api.invoke("graph.clone",{{"graph",graph}}).at("graph");
  CHECK(f.doc->native().envelopeLinks.size()==2);
  CHECK(f.doc->native().envelopeLinks[1].target.owner==f.doc->native().signal.library[1].nodes[2].id);
  CHECK(f.doc->native().envelopeLinks[1].templateID==f.doc->native().envelopeLinks[0].templateID);
  auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());
  auto restored=std::make_unique<Document>(f.doc->snapshotData()); restored->restoreNative(ScreamSeq::Project::decodeNativeMetadata(encoded));
  CHECK(restored->native()==f.doc->native());
  f.api.invoke("graph.remove",{{"graph",clone}}); CHECK(f.doc->native().envelopeLinks.size()==1);
  auto replacement=definition(f); replacement["nodes"].erase(replacement["nodes"].begin()+2);
  f.api.invoke("graph.update",{{"definition",replacement}}); CHECK(f.doc->native().envelopeLinks.empty()); CHECK(f.doc->native().envelopeBank.size()==1);
  f.doc->undo(); CHECK(f.doc->native().envelopeLinks.size()==1);
  set=target; set["points"]=Json::array(); f.api.invoke("graph.automation.set",set);
  CHECK(f.doc->native().envelopeLinks.empty()); CHECK(f.api.invoke("graph.automation.get",target)["points"].empty());
  CHECK(f.api.invoke("graph.automation.get",secondTarget)==read);
  const auto rev=f.doc->revision,hist=f.doc->historyBytes(); CHECK(f.api.invoke("graph.automation.set",set)["wouldChange"]==false);
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==hist);
}
void automationOrderAndRedo() {
  Fixture f; const auto second=f.doc->addPattern(32,false,0);
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto node=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","automation"}}).at("node");
  Json p={{"graph",graph},{"node",node},{"pattern",second},{"points",Json::array({{{"position",0},{"value",0.25}}})}};
  f.api.invoke("graph.automation.set",p);p["pattern"]=0;f.api.invoke("graph.automation.set",p);
  // Legal persisted order is intentionally different from pattern identity order.
  const auto before=f.doc->native();
  CHECK(before.signal.library[0].nodes[2].envelopes[0].pattern==before.patterns.at(second).id);
  f.api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","probe"},{"x",4},{"y",5}}})}});f.doc->undo();
  const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  for(bool dry:{true,false}) {
    p["dryRun"]=dry;CHECK(f.api.invoke("graph.automation.set",p)["wouldChange"]==false);
    CHECK(f.doc->native()==before);CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);
    CHECK(f.stops==stops);CHECK(f.doc->canRedo());
  }
  f.doc->redo();CHECK(!f.doc->native().signal.layout.empty());f.doc->undo();
  p["points"][0]["value"]=0.75;f.api.invoke("graph.automation.set",p);
  const auto &lanes=f.doc->native().signal.library[0].nodes[2].envelopes;
  CHECK(lanes[0]==before.signal.library[0].nodes[2].envelopes[0]);
  CHECK(lanes[1].pattern==before.patterns.at(0).id);CHECK(lanes[1].points[0].value==0.75);
}
std::string nativeID(uint64_t n) { return "n"+std::to_string(n); }
void assignmentsRoutesLayoutCommands() {
  Fixture f; const auto second=f.doc->addPattern(16,false,0);
  f.doc->transaction([](OpenMPT::CSoundFile &s){ CHECK(s.AllocateInstrument(1)); });
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto target=nativeID(f.doc->native().tracks.at(0).id), source=nativeID(f.doc->native().tracks.at(1).id);
  Json assign={{"target",target},{"graph",graph},{"amount",0.5},{"wet",0.8}};
  f.api.invoke("graph.assign",assign); CHECK(f.doc->native().mixer.active());
  const auto output=nativeID(f.doc->native().mixer.buses.back().id);
  f.api.invoke("graph.assign",{{"target",source},{"graph",graph}});
  auto rev=f.doc->revision,hist=f.doc->historyBytes(); auto stops=f.stops;
  CHECK(f.api.invoke("graph.assign",assign)["wouldChange"]==false); CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==hist); CHECK(f.stops==stops);
  f.api.invoke("graph.instrument.assign",{{"instrument",1},{"graph",graph},{"wet",0.4}});
  CHECK(f.doc->native().signal.instrumentAssignments[0].target==f.doc->native().instruments.at(1).id);
  rejected(f,"graph.remove",{{"graph",graph}});
  auto d=definition(f); d["audio"].push_back({{"source",d["nodes"][0]["id"]},{"target",d["nodes"][1]["id"]},{"output",1},{"input",1}});
  f.api.invoke("graph.update",{{"definition",d}});
  Json inputs=Json::array({{{"source",source},{"target",target},{"input",1},{"gainDB",-6},{"preFader",true}}});
  Json outputs=Json::array({{{"source",target},{"target",output},{"output",1}}});
  f.api.invoke("graph.routes.set",{{"inputs",inputs}});
  f.api.invoke("graph.routes.set",{{"outputs",outputs}});
  CHECK(f.doc->native().signal.inputs.size()==1); CHECK(f.doc->native().signal.outputs.size()==1);
  CHECK(f.api.invoke("graph.routes.set",{{"outputs",outputs}})["wouldChange"]==false);
  rejected(f,"graph.routes.set",{{"inputs",Json::array({{{"source",output},{"target",target},{"input",1}}})}}); // routing cycle
  auto invalid=inputs; invalid[0]["input"]=2; rejected(f,"graph.routes.set",{{"inputs",invalid}});
  invalid=inputs; invalid[0]["extra"]=1; rejected(f,"graph.routes.set",{{"inputs",invalid}});
  stops=f.stops;
  f.api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","track-a"},{"x",100},{"y",200}}})}});
  f.api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","track-b"},{"x",300},{"y",400}}})}});
  CHECK(f.stops==stops); CHECK(f.doc->native().signal.layout.size()==2);
  CHECK(f.api.invoke("graph.layout.set",Json::object())["wouldChange"]==false);
  f.api.invoke("graph.layout.set",{{"reset",true}}); CHECK(f.doc->native().signal.layout.empty()); CHECK(f.stops==stops);
  Json commands=Json::array({{{"target",target},{"graph",graph},{"position",65537},{"kind","start"}}});
  Json lanes=Json::array({{{"target",target},{"count",2}}});
  f.api.invoke("graph.commands.set",{{"pattern",0},{"lanes",lanes},{"commands",commands}});
  CHECK(f.doc->native().signal.commands[0].position==65537); CHECK(f.stops==stops+1);
  Json clear=Json::array({{{"target",target},{"position",1048575},{"kind","clear"},{"column",1}}});
  f.api.invoke("graph.commands.set",{{"pattern",second},{"commands",clear}});
  CHECK(f.doc->native().signal.commands.size()==2);
  CHECK(f.doc->native().signal.commands[1].graph==0);
  rev=f.doc->revision; CHECK(f.api.invoke("graph.commands.set",{{"pattern",0},{"commands",commands}})["wouldChange"]==false); CHECK(f.doc->revision==rev);
  invalid=clear; invalid[0]["position"]=1048576; rejected(f,"graph.commands.set",{{"pattern",second},{"commands",invalid}});
  invalid=clear; invalid.push_back(invalid[0]); invalid[1]["position"]=1048574; rejected(f,"graph.commands.set",{{"pattern",second},{"commands",invalid}});
  invalid=clear; invalid[0]["pattern"]=nativeID(f.doc->native().patterns.at(second).id); rejected(f,"graph.commands.set",{{"pattern",second},{"commands",invalid}});
  invalid=clear; invalid[0]["position"]=true; rejected(f,"graph.commands.set",{{"pattern",second},{"commands",invalid}});
  rejected(f,"graph.commands.set",{{"pattern",0},{"lanes",Json::array({{{"target",target},{"count",0}}})}});
  const auto before=f.doc->native(); auto dry=assign; dry["graph"]=nullptr; dry["dryRun"]=true;
  // Routes still depend on graph assignment/commands; commands keep it exposed.
  f.api.invoke("graph.assign",dry); CHECK(f.doc->native()==before);
  f.api.invoke("graph.commands.set",{{"pattern",second},{"commands",Json::array()}});
  CHECK(f.doc->native().signal.commands.size()==1); CHECK(f.doc->native().signal.inputs==before.signal.inputs); CHECK(f.doc->native().signal.outputs==before.signal.outputs);
  f.api.invoke("graph.routes.set",{{"inputs",Json::array()},{"outputs",Json::array()}});
  f.api.invoke("graph.commands.set",{{"pattern",0},{"commands",Json::array()},{"lanes",Json::array({{{"target",target},{"count",0}}})}});
  f.api.invoke("graph.assign",{{"target",target},{"graph",nullptr}}); f.api.invoke("graph.assign",{{"target",source},{"graph",nullptr}});
  f.api.invoke("graph.instrument.assign",{{"instrument",1},{"graph",nullptr}});
  f.api.invoke("graph.remove",{{"graph",graph}}); CHECK(f.doc->native().signal.library.empty());
}
void hostHooks() {
  Fixture f; f.doc->transaction([](OpenMPT::CSoundFile &s){CHECK(s.AllocateInstrument(1));});
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  ScreamSeq::GraphRackClone baseline;
  baseline.recipe={"VST3","Effect","C:/Plugins/Effect.vst3","actual-test-class",0,0,0,{std::byte{0},std::byte{128},std::byte{255}},{1,4},{2}};
  ScreamSeq::GraphHostHooks hooks;
  hooks.cachedRack={{{{"format","VST3"},{"name","Instrument"}},"rack-actual-test",3,false,{1}}};
  hooks.cachedActivity={{f.doc->native().tracks.at(0).id,f.doc->native().signal.library[0].id,3,1,false,f.doc->native().instruments.at(1).id}};
  uint32_t requested=99;
  hooks.cloneRackSlot=[&](uint32_t slot){requested=slot; return baseline;};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  auto result=api.invoke("graph.get",Json::object()); CHECK(result["plugins"][0]["id"]=="rack-actual-test"); CHECK(result["instruments"][0]["plugin"]==true);
  CHECK(result["activity"][0]["role"]=="instrument"); CHECK(result["activity"][0]["instrument"]==nativeID(f.doc->native().instruments.at(1).id));
  const auto before=f.doc->native(); const auto revision=f.doc->revision; auto stops=f.stops;
  bool threw=false; try { api.invoke("graph.instrument.assign",{{"instrument",1},{"graph",graph}}); } catch(const ScreamSeq::Api::ApiError &e){ CHECK(e.code==-32602); threw=true; }
  CHECK(threw); CHECK(f.doc->native()==before); CHECK(f.doc->revision==revision); CHECK(f.stops==stops);
  const auto n=api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0}}).at("node"); CHECK(requested==0);
  CHECK(f.doc->native().signal.library[0].nodes.back().plugin==baseline.recipe);
  CHECK(definition(f)["nodes"].back()["name"]=="Effect");
  for(int condition=0;condition<3;++condition) {
    baseline.instrument=condition==0; baseline.instruments=condition==1?std::vector<uint16_t>{1}:std::vector<uint16_t>{}; baseline.recipe.type=condition==2?0x61756d75:0;
    auto original=f.doc->native(); stops=f.stops; threw=false;
    try { api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0}}); } catch(const ScreamSeq::Api::ApiError &e){ CHECK(e.code==-32602); threw=true; }
    CHECK(threw); CHECK(f.doc->native()==original); CHECK(f.stops==stops);
  }
  // Callback data supersedes cached records; fixtures are explicit, not live-host qualification.
  unsigned reads=0; hooks.rack=[&]{++reads; return std::vector<ScreamSeq::GraphRackRecord>{};};
  hooks.activity=[]{return std::vector<SignalActivity>{};};
  GraphOperations dynamic(*f.doc,{},hooks); result=dynamic.invoke("graph.get",Json::object());
  CHECK(reads==1); CHECK(result["plugins"].empty()); CHECK(result["activity"].empty()); CHECK(result["instruments"][0]["plugin"]==false);
  for(auto method:{"graph.controller","graph.plugin.get","graph.plugin.set","graph.plugin.editor.open","graph.plugin.editor.commit","graph.plugin.editor.close"}) rejected(f,method,Json::object(),-32601);
}
void strictValidation() {
  Fixture f; const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()}});
  f.api.invoke("graph.node.add",{{"graph",graph},{"kind","lfo"}});
  const auto a=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","automation"}}).at("node");
  f.api.invoke("graph.automation.set",{{"graph",graph},{"node",a},{"pattern",0},{"points",Json::array({{{"position",0},{"value",0.5}}})}});
  auto d=definition(f); d["modulation"].push_back({{"source",d["nodes"][3]["id"]},{"target",d["nodes"][2]["id"]},{"parameter",0}});
  f.api.invoke("graph.update",{{"definition",d}}); d=definition(f);
  for(const auto &path:{"","/nodes/0","/nodes/2/plugin","/audio/0","/modulation/0","/nodes/4/envelopes/0","/nodes/4/envelopes/0/points/0"}) {
    auto bad=d; bad[Json::json_pointer(path)]["unknown"]=true; rejected(f,"graph.update",{{"definition",bad}});
  }
  for(auto value:{Json(true),Json(-1),Json(1.1),Json("1"),Json(nullptr),Json(UINT64_MAX)}) rejected(f,"graph.create",{{"number",value}});
  for(auto value:{Json("n01"),Json("n0"),Json("n-1"),Json("n1x"),Json("n1000000000000"),Json(1),Json(true)}) rejected(f,"graph.clone",{{"graph",value}});
  for(auto format:{"Unknown","au",""}) { auto bad=recipe(); bad["format"]=format; rejected(f,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",bad}}); }
  for(auto kind:{"unknown","PLUGIN",""}) rejected(f,"graph.node.add",{{"graph",graph},{"kind",kind}});
  for(auto state:{"?===","AA=","Zh=="}) { auto bad=recipe(); bad["state"]=state; rejected(f,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",bad}}); }
  auto bad=d; bad["audio"][0]["output"]=1.5; rejected(f,"graph.update",{{"definition",bad}});
  bad=d; bad["nodes"][0]["rate"]=true; rejected(f,"graph.update",{{"definition",bad}});
  bad=d; bad["nodes"][0]["x"]=std::numeric_limits<double>::infinity(); rejected(f,"graph.update",{{"definition",bad}});
  bad=d; bad["nodes"][0]["envelopes"]=Json::array(); rejected(f,"graph.update",{{"definition",bad}});
  rejected(f,"graph.get",{{"includeState",1}}); rejected(f,"graph.create",{{"dryRun",1}});
  rejected(f,"graph.create",{{"name",std::string(257,'x')}}); rejected(f,"graph.create",{{"name",std::string("bad\0utf",7)}});
  rejected(f,"graph.create",{{"name",std::string("\xc0\x80",2)}});
  for(const auto &method:GraphOperations::writes()) { rejected(f,method,Json::array()); rejected(f,method,{{"unknown",1}}); rejected(f,method,{{"expectedRevision","caller-must-remove"}}); }
  for(const auto &method:GraphOperations::reads()) { rejected(f,method,Json::array()); rejected(f,method,{{"unknown",1}}); }
  // Integer-valued JSON doubles are accepted like Mac NSNumber, not booleans.
  bad=d; bad["number"]=1.0; bad["audio"][0]["input"]=0.0; bad["nodes"][2]["plugin"]["type"]=0.0;
  CHECK(f.api.invoke("graph.update",{{"definition",bad}})["wouldChange"]==false);
  f.doc->annotate([](NativeSong &n){n.nextID=NativeSong::maximumID;});
  rejected(f,"graph.create",Json::object());
  rejected(f,"graph.clone",{{"graph",graph}});
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","lfo"}});
  rejected(f,"graph.assign",{{"target",nativeID(f.doc->native().tracks.at(0).id)},{"graph",graph}});
}
void dryRunsAndRedo() {
  Fixture f; f.doc->transaction([](OpenMPT::CSoundFile &s){CHECK(s.AllocateInstrument(1));});
  const auto g=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto n=f.api.invoke("graph.node.add",{{"graph",g},{"kind","automation"}}).at("node");
  f.api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","x"},{"x",1},{"y",2}}})}});
  f.doc->undo(); CHECK(f.doc->canRedo());
  const auto native=f.doc->native(); const auto rev=f.doc->revision,hist=f.doc->historyBytes(); const auto stops=f.stops;
  const auto target=nativeID(native.tracks.at(0).id);
  std::vector<std::pair<std::string,Json>> cases={
    {"graph.create",Json::object()},{"graph.clone",{{"graph",g}}},{"graph.update",{{"definition",definition(f)}}},
    {"graph.remove",{{"graph",g}}},{"graph.node.add",{{"graph",g},{"kind","lfo"}}},{"graph.node.remove",{{"graph",g},{"node",n}}},
    {"graph.instrument.assign",{{"instrument",1},{"graph",g}}},
    {"graph.assign",{{"target",target},{"graph",g}}},{"graph.layout.set",{{"positions",Json::array({{{"node","x"},{"x",1},{"y",2}}})}}},
    {"graph.routes.set",Json::object()},{"graph.commands.set",{{"pattern",0},{"lanes",Json::array({{{"target",target},{"count",1}}})}}},
    {"graph.automation.set",{{"graph",g},{"node",n},{"pattern",0},{"points",Json::array({{{"position",0},{"value",0.25}}})}}}
  };
  for(auto &[method,p]:cases) { p["dryRun"]=true; f.api.invoke(method,p); CHECK(f.doc->native()==native); CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==hist); CHECK(f.doc->canRedo()); CHECK(f.stops==stops); }
  for(auto method:{"graph.routes.set","graph.layout.set"}) CHECK(f.api.invoke(method,Json::object())["wouldChange"]==false);
  CHECK(f.api.invoke("graph.update",{{"definition",definition(f)}})["wouldChange"]==false);
  CHECK(f.doc->canRedo()); CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==hist); CHECK(f.stops==stops);
  f.doc->redo(); CHECK(f.doc->native().signal.layout.size()==1);
}
void callbackOrderingAndUnrelatedData() {
  Fixture f;
  f.doc->edit({{0,2,1,{},Cell{49,0,0,0,0,0}}});
  f.doc->annotate([](NativeSong &n){ n.tracks.at(1).name="Preserved track"; n.patterns.at(0).annotation="Unrelated pattern"; n.columnMutes[n.tracks.at(2).id]=true; });
  const auto cells=f.doc->cell(0,2,1); auto expected=f.doc->native(); unsigned calls=0;
  GraphOperations api(*f.doc,[&]{CHECK(f.doc->native()==expected); CHECK(f.doc->cell(0,2,1)==cells); ++calls;});
  const auto graph=api.invoke("graph.create",Json::object()).at("graph"); CHECK(calls==1);
  expected=f.doc->native(); bool threw=false;
  try { api.invoke("graph.create",{{"number",1}}); } catch(const ScreamSeq::Api::ApiError &e) {CHECK(e.code==-32602); threw=true;}
  CHECK(threw); CHECK(calls==1); CHECK(f.doc->native()==expected);
  auto d=api.invoke("graph.get",Json::object())["library"][0]; d["name"]="Label only"; d["number"]=4; d["nodes"][0]["x"]=400;
  api.invoke("graph.update",{{"definition",d}}); CHECK(calls==1);
  expected=f.doc->native(); d["audio"][0]["gain"]=0.5;
  api.invoke("graph.update",{{"definition",d}}); CHECK(calls==2);
  CHECK(f.doc->native().tracks==expected.tracks); CHECK(f.doc->native().patterns==expected.patterns);
  CHECK(f.doc->native().columnMutes==expected.columnMutes); CHECK(f.doc->cell(0,2,1)==cells);
  const auto reads=GraphOperations::reads(),writes=GraphOperations::writes();
  CHECK((std::set<std::string>(reads.begin(),reads.end())==std::set<std::string>{"graph.get","graph.automation.get"}));
  CHECK((std::set<std::string>(writes.begin(),writes.end())==std::set<std::string>{"graph.create","graph.clone","graph.update","graph.remove","graph.node.add","graph.node.remove","graph.assign","graph.instrument.assign","graph.routes.set","graph.layout.set","graph.commands.set","graph.automation.set"}));
  CHECK(reads.size()==2); CHECK(writes.size()==12);
}
int main(int argc,char **argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--catalog") { std::cout<<Json{{"reads",GraphOperations::reads()},{"writes",GraphOperations::writes()}}.dump(2)<<'\n'; return 0; }
    const std::vector<std::pair<const char *,void(*)()>> tests={
      {"createReadHistory",createReadHistory},{"nodesAndCloning",nodesAndCloning},{"automationAndBanks",automationAndBanks},
      {"assignmentsRoutesLayoutCommands",assignmentsRoutesLayoutCommands},{"hostHooks",hostHooks},
      {"automationOrderAndRedo",automationOrderAndRedo},
      {"strictValidation",strictValidation},{"dryRunsAndRedo",dryRunsAndRedo},{"callbackOrderingAndUnrelatedData",callbackOrderingAndUnrelatedData}};
    unsigned ran=0;
    for(const auto &[name,test]:tests) if(argc==1||std::string(argv[1])==name) { test(); ++ran; std::cout<<"PASS "<<name<<'\n'; }
    CHECK(ran>0); std::cout<<"Passed "<<ran<<" scenario groups\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
