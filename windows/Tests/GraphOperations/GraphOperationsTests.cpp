#include "windows/Session/GraphOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "windows/App/GraphCableEdits.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#ifdef small
#undef small
#endif
#include <iostream>
#include <limits>
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
void stableImplicitMaster() {
  Fixture f;const auto before=f.doc->native();const auto master="n"+std::to_string(before.masterID);
  auto read=[&] {const auto result=f.api.invoke("graph.get",{{"includeImplicitMixer",true}});CHECK(result.at("mixer").at("buses").back().at("id")==master);return result;};
  CHECK(read().at("implicitMixer")==true);CHECK(f.doc->native()==before);CHECK(!f.doc->canUndo());CHECK(f.doc->revision==0);
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  CHECK(f.doc->native().nextID>before.nextID);CHECK(read().at("implicitMixer")==true);
  const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();
  f.api.invoke("graph.assign",{{"target",master},{"graph",graph},{"dryRun",true}});
  CHECK(f.doc->revision==revision&&f.doc->historyBytes()==history&&!f.doc->native().mixer.active());
  f.api.invoke("graph.assign",{{"target",master},{"graph",graph}});
  CHECK(read().at("implicitMixer")==false);CHECK(f.doc->native().masterID==before.masterID);
  f.doc->undo();CHECK(read().at("implicitMixer")==true);const auto allocator=f.doc->native().nextID;
  read();CHECK(f.doc->canRedo());CHECK(f.doc->native().nextID==allocator);
  f.doc->redo();CHECK(read().at("implicitMixer")==false);
  const auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());
  CHECK(encoded.at("masterID")==master);CHECK(ScreamSeq::Project::decodeNativeMetadata(encoded)==f.doc->native());
  auto missing=encoded;missing.erase("masterID");CHECK(ScreamSeq::Project::decodeNativeMetadata(missing)==f.doc->native());
  auto conflict=encoded;conflict["masterID"]=encoded.at("tracks")[0][1]["id"];bool rejected=false;
  try{ScreamSeq::Project::decodeNativeMetadata(conflict);}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);
  auto implicit=ScreamSeq::Project::encodeNativeMetadata(before);implicit.erase("masterID");
  const auto decoded=ScreamSeq::Project::decodeNativeMetadata(implicit);CHECK(decoded.masterID==before.nextID);CHECK(decoded.nextID==before.nextID+1);CHECK(!decoded.mixer.active());
}
void songProcessingGroups() {
  Fixture f;ScreamSeq::GraphHostHooks hooks;unsigned publications=0;
  hooks.preparePublication=[&](const NativeSong &){return [&]{++publications;};};
  hooks.cachedRack={{{{"name","Gain"},{"isInstrument",false}},"rack-a",0},{{{"name","Tone"},{"isInstrument",false}},"rack-b",1}};
  hooks.cachedRack[0].bypass=true;
  hooks.cloneRackSlot=[](uint32_t slot){ScreamSeq::GraphRackClone c;c.recipe.name=slot?"Tone":"Gain";c.recipe.classID="resonance.gainer.v1";c.recipe.parameters[1]=-6;return c;};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  const Json create={{"nodes",{"plugin:rack-a","plugin:rack-b"}},{"name","Pair"},{"positions",Json::array({{{"node","plugin:rack-a"},{"x",200},{"y",100}},{{"node","plugin:rack-b"},{"x",435},{"y",100}}})}};
  auto preview=create;preview["dryRun"]=true;const auto before=f.doc->native();api.invoke("graph.song.group.create",preview);CHECK(f.doc->native()==before);
  const auto group=api.invoke("graph.song.group.create",create)["group"];CHECK(f.stops==0&&publications==1);
  const auto grouped=f.doc->native();CHECK(grouped.signal.groups.size()==1);CHECK(grouped.mixer==before.mixer);
  f.doc->undo();CHECK(f.doc->native().signal.groups.empty());f.doc->redo();CHECK(f.doc->native()==grouped);
  const auto encoded=ScreamSeq::Project::encodeNativeMetadata(grouped);CHECK(ScreamSeq::Project::decodeNativeMetadata(encoded)==grouped);
  const auto rev=f.doc->revision;api.invoke("graph.song.group.update",{{"group",group},{"name","Pair"}});CHECK(f.doc->revision==rev);
  api.invoke("graph.layout.set",{{"groups",Json::array({{{"group",group},{"x",250},{"y",130}}})}});CHECK((f.doc->native().signal.layout.at("plugin:rack-b")==std::array<double,2>{485,130}));
  const auto beforeExport=f.doc->native();const auto exported=api.invoke("graph.song.group.export",{{"group",group}})["graph"];CHECK(f.stops==0);CHECK(f.doc->native().mixer==beforeExport.mixer);
  const auto &d=f.doc->native().signal.library.back();CHECK(d.nodes.size()==4);CHECK(d.nodes[2].plugin.parameters.at(1)==-6);CHECK(d.nodes[2].plugin.bypass&&!d.nodes[3].plugin.bypass);CHECK(d.audio.size()==3);
  f.doc->undo();CHECK(f.doc->native().signal.library.empty());f.doc->redo();CHECK(api.invoke("graph.get",Json::object())["library"][0]["id"]==exported);
  api.invoke("graph.song.group.remove",{{"group",group}});CHECK(f.doc->native().signal.groups.empty());CHECK(f.stops==0);
}
void createReadHistory() {
  Fixture f;
  const auto original=f.doc->native();
  auto preview=f.api.invoke("graph.create",{{"name","Test"},{"dryRun",true}});
  CHECK(preview.at("wouldChange")==true); CHECK(f.doc->native()==original); CHECK(f.stops==0);
  CHECK(!f.doc->canUndo()); CHECK(f.doc->revision==0);
  auto result=f.api.invoke("graph.create",{{"name","Test"}});
  CHECK(result.at("graph")==preview.at("graph")); CHECK(f.stops==0);
  auto read=f.api.invoke("graph.get",Json::object());
  CHECK(read.at("library").size()==1); CHECK(read.at("library")[0].at("nodes").size()==2);
  CHECK(read.at("library")[0].at("name")=="Test"); CHECK(read.at("unitsPerRow")==65536);
  CHECK(read.at("plugins").empty()); CHECK(read.at("activity").empty());
  CHECK(read.at("patterns")[0].at("id")=="n"+std::to_string(original.patterns.at(0).id));
  const auto changed=f.doc->native(); CHECK(f.doc->canUndo());
  f.doc->undo(); auto undone=original; undone.nextID=changed.nextID;
  CHECK(f.doc->native()==undone); CHECK(f.doc->canRedo());
  const auto history=f.doc->historyBytes();const auto revision=f.doc->revision;
  f.api.invoke("graph.create",{{"dryRun",true}});
  CHECK(f.doc->historyBytes()==history); CHECK(f.doc->revision==revision); CHECK(f.doc->canRedo());
  f.doc->redo(); CHECK(f.doc->native()==changed);
}
void rejected(Fixture &f,const std::string &method,const Json &p,int code=-32602) {
  const auto native=f.doc->native(); const auto snapshot=f.doc->snapshotData();
  const auto revision=f.doc->revision;const auto history=f.doc->historyBytes(); const auto stops=f.stops;
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
  auto history=f.doc->historyBytes();auto rev=f.doc->revision;
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
void cableInsertionAndDetachment() {
  Fixture f;const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto effect=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()},{"insertEdge",0}}).at("node");
  auto d=definition(f);CHECK(d["audio"].size()==2);
  const auto lfo=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","lfo"},{"connect",{{"node",effect},{"port",7},{"output",false},{"modulation",true},{"base",0.42},{"quantized",true}}}}).at("node");
  d=definition(f);CHECK(d["modulation"].size()==1);CHECK(d["modulation"][0]["source"]==lfo);
  CHECK(d["modulation"][0]["minimum"]==0);CHECK(d["modulation"][0]["maximum"]==0);CHECK(d["modulation"][0]["base"]==0.42);CHECK(d["modulation"][0]["quantized"]==true);
  const auto before=f.doc->native();
  const Json detach={{"graph",graph},{"nodes",Json::array({effect})},{"positions",Json::array({{{"node",effect},{"x",600},{"y",300}}})}};
  auto dry=detach;dry["dryRun"]=true;f.api.invoke("graph.nodes.detach",dry);CHECK(f.doc->native()==before);
  f.api.invoke("graph.nodes.detach",detach);d=definition(f);CHECK(d["audio"].size()==1);CHECK(d["modulation"].size()==1);
  const auto node=std::find_if(d["nodes"].begin(),d["nodes"].end(),[&](const auto &n){return n["id"]==effect;});CHECK(node!=d["nodes"].end());CHECK((*node)["x"]==600);
  f.doc->undo();CHECK(f.doc->native()==before);f.doc->redo();CHECK(definition(f)["audio"].size()==1);
  f.api.invoke("graph.nodes.insert",{{"graph",graph},{"nodes",Json::array({effect})},{"edge",0}});CHECK(definition(f)["audio"].size()==2);
  const auto connected=f.doc->native();
  f.api.invoke("graph.nodes.detach",{{"graph",graph},{"nodes",Json::array({effect})},{"remove",true}});
  d=definition(f);CHECK(d["audio"].size()==1);CHECK(d["modulation"].empty());CHECK(d["nodes"].size()==3);
  f.doc->undo();CHECK(f.doc->native()==connected);
  rejected(f,"graph.nodes.detach",{{"graph",graph},{"nodes",Json::array({effect,effect})}});
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()},{"insertEdge",255}});
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","lfo"},{"connect",{{"node",effect},{"port",7},{"output",true},{"modulation",true}}}});
  rejected(f,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()},{"connect",{{"node",effect},{"port",0},{"output",false},{"quantized",false}}}});
  const auto input=definition(f)["nodes"][0]["id"];const auto prior=f.doc->native();
  const Json follower={{"graph",graph},{"kind","follower"},{"audioInput",{{"node",input},{"port",0}}},{"connect",{{"node",effect},{"port",7},{"output",false},{"modulation",true},{"base",0.42},{"quantized",true}}}};
  auto preview=follower;preview["dryRun"]=true;f.api.invoke("graph.node.add",preview);CHECK(f.doc->native()==prior);
  const auto followerID=f.api.invoke("graph.node.add",follower).at("node");d=definition(f);
  CHECK(d["audio"].back()["source"]==input&&d["audio"].back()["target"]==followerID);
  CHECK(d["modulation"].back()["source"]==followerID&&d["modulation"].back()["target"]==effect&&d["modulation"].back()["maximum"]==0);
  const auto converted=f.doc->native();auto undone=prior;undone.nextID=converted.nextID;
  f.doc->undo();CHECK(f.doc->native()==undone);f.doc->redo();CHECK(f.doc->native()==converted);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(converted))==converted);
  auto bad=follower;bad["kind"]="lfo";rejected(f,"graph.node.add",bad);
  bad=follower;bad["audioInput"]["node"]=effect;rejected(f,"graph.node.add",bad);
  bad=follower;bad["audioInput"]["node"]="n9999999";rejected(f,"graph.node.add",bad);
  bad=follower;bad.erase("connect");rejected(f,"graph.node.add",bad);
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
  set.erase("dryRun"); f.api.invoke("graph.automation.set",set); CHECK(f.stops==stops);
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
  const auto rev=f.doc->revision;const auto hist=f.doc->historyBytes(); CHECK(f.api.invoke("graph.automation.set",set)["wouldChange"]==false);
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
  const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();const auto stops=f.stops;
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
  auto rev=f.doc->revision;auto hist=f.doc->historyBytes(); auto stops=f.stops;
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
  for(auto method:{"graph.controller","graph.plugin.get","graph.plugin.set","graph.plugin.bypass","graph.plugin.editor.open","graph.plugin.editor.commit","graph.plugin.editor.close"}) rejected(f,method,Json::object(),-32601);
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
  f.api.invoke("graph.assign",{{"target",nativeID(f.doc->native().tracks.at(0).id)},{"graph",graph}});
  CHECK(f.doc->native().nextID==NativeSong::maximumID && f.doc->native().mixer.active()); // Reserved Master needs no new ID.
}
void processingGroups() {
  Fixture f;unsigned publications=0;ScreamSeq::GraphHostHooks hooks;hooks.preparePublication=[&](const NativeSong &){return [&]{++publications;};};GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  const auto graph=api.invoke("graph.create",Json::object()).at("graph");
  const auto plugin=api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",recipe()},{"insertEdge",0}}).at("node");
  const auto lfo=api.invoke("graph.node.add",{{"graph",graph},{"kind","lfo"}}).at("node");
  api.invoke("graph.assign",{{"target",nativeID(f.doc->native().tracks.at(0).id)},{"graph",graph}});
  auto controlled=definition(f);controlled["nodes"][2]["plugin"]["parameters"]=Json::array({{{"id",7},{"value",.375}}});controlled["nodes"][2]["plugin"]["bypass"]=true;
  api.invoke("graph.update",{{"definition",controlled}});
  CHECK(f.doc->native().signal.library[0].nodes[2].plugin.parameters.at(7)==.375);
  const auto stops=f.stops;const auto revision=f.doc->revision;
  const auto before=f.doc->native();
  const auto draft=api.invoke("graph.group.create",{{"graph",graph},{"nodes",{plugin,lfo}},{"name","Motion"},{"dryRun",true}});
  CHECK(f.doc->native()==before&&f.doc->revision==revision&&f.stops==stops);
  const auto group=api.invoke("graph.group.create",{{"graph",graph},{"nodes",{plugin,lfo}},{"name","Motion"}}).at("group");
  CHECK(group==draft.at("group")&&f.stops==stops);
  const auto inner=api.invoke("graph.group.create",{{"graph",graph},{"parent",group},{"nodes",{lfo}},{"name","Modulation"}}).at("group");
  const auto d=definition(f);const auto x=d["nodes"][2]["x"].get<double>();
  const auto gx=d["groups"][0]["x"].get<double>(),gy=d["groups"][0]["y"].get<double>();
  api.invoke("graph.group.update",{{"graph",graph},{"group",group},{"x",gx+130},{"y",gy-25}});
  CHECK(definition(f)["nodes"][2]["x"]==x+130&&f.stops==stops);
  auto omitted=definition(f);omitted.erase("groups");
  CHECK(!api.invoke("graph.update",{{"definition",omitted}})["wouldChange"].get<bool>());
  const auto grouped=f.doc->native();
  api.invoke("graph.group.export",{{"graph",graph},{"group",group},{"dryRun",true}});CHECK(f.doc->native()==grouped);
  const auto exported=api.invoke("graph.group.export",{{"graph",graph},{"group",group}}).at("graph");
  CHECK(f.stops==stops&&definition(f)==ScreamSeq::Project::encodeNativeMetadata(grouped)["signalGraph"]["library"][0]);
  const auto copy=definition(f,1);CHECK(std::any_of(copy["nodes"].begin(),copy["nodes"].end(),[](const auto &n){return n.contains("plugin")&&n["plugin"].value("bypass",false);}));CHECK(copy["id"]==exported&&copy["groups"].size()==1&&copy["groups"][0]["parent"]=="");
  // Keep the JSON owner alive: in C++20 a subobject returned by operator[]
  // does not extend the lifetime of the temporary definition across a loop.
  const auto originalDefinition=definition(f);
  std::set<Json> ids;for(const auto &n:originalDefinition["nodes"])ids.insert(n["id"]);for(const auto &g:originalDefinition["groups"])ids.insert(g["id"]);
  for(const auto &n:copy["nodes"])CHECK(!ids.contains(n["id"]));for(const auto &g:copy["groups"])CHECK(!ids.contains(g["id"]));
  const auto saved=f.doc->native();CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(saved))==saved);
  f.doc->undo();CHECK(f.doc->native().signal.library.size()==1);f.doc->redo();CHECK(f.doc->native()==saved);
  api.invoke("graph.group.remove",{{"graph",graph},{"group",inner}});CHECK(definition(f)["groups"].size()==1&&definition(f)["groups"][0]["nodes"].size()==2);
  rejected(f,"graph.group.update",{{"graph",graph},{"group",inner},{"name","Gone"}});
  auto invalid=definition(f);invalid["groups"][0]["id"]="n900000";rejected(f,"graph.update",{{"definition",invalid}});
  const auto beforeDeletePublications=publications;
  api.invoke("graph.group.remove",{{"graph",graph},{"group",group},{"deleteContents",true}});
  CHECK(definition(f)["groups"].empty()&&definition(f)["nodes"].size()==2&&f.stops==stops&&publications==beforeDeletePublications+1);
}
void dryRunsAndRedo() {
  Fixture f; f.doc->transaction([](OpenMPT::CSoundFile &s){CHECK(s.AllocateInstrument(1));});
  const auto g=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto n=f.api.invoke("graph.node.add",{{"graph",g},{"kind","automation"}}).at("node");
  f.api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","x"},{"x",1},{"y",2}}})}});
  f.doc->undo(); CHECK(f.doc->canRedo());
  const auto native=f.doc->native(); const auto rev=f.doc->revision;const auto hist=f.doc->historyBytes(); const auto stops=f.stops;
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
void songCableCuts() {
  Fixture f;const auto original=f.doc->native();const auto a=original.tracks.at(0).id,b=original.tracks.at(1).id,master=original.masterID;
  const auto cutOutput=Json{{"kind","output"},{"source",nativeID(a)},{"target",nativeID(master)}};
  CHECK(f.api.invoke("graph.connections.remove",{{"connections",Json::array()}})["wouldChange"]==false);CHECK(f.doc->revision==0&&!f.doc->canUndo());
  f.api.invoke("graph.connections.remove",{{"connections",Json::array({cutOutput})},{"dryRun",true}});CHECK(f.doc->native()==original&&!f.doc->canUndo());
  f.api.invoke("graph.connections.remove",{{"connections",Json::array({cutOutput})}});CHECK(f.doc->native().mixer.buses[0].output==0);CHECK(f.doc->revision==1);f.doc->undo();CHECK(f.doc->native()==original);
  f.doc->annotate([&](NativeSong &n){n.ensureMixer();n.mixer.buses[0].sends.push_back({b,-9,true,false});n.mixer.buses[1].inserts={"fx"};n.mixer.sidechains={{a,"fx",1,-3,true,true}};n.mixer.instruments={{"synth",b,0},{"synth",master,0}};});
  const auto before=f.doc->native();const auto revision=f.doc->revision;
  const Json cuts=Json::array({cutOutput,{{"kind","send"},{"source",nativeID(a)},{"target",nativeID(b)}},{{"kind","plugin-input"},{"source",nativeID(a)},{"plugin","fx"},{"input",1}},{{"kind","plugin-output"},{"plugin","synth"},{"target",nativeID(b)},{"output",0}}});
  auto invalid=cuts;invalid.push_back({{"kind","output"},{"source",nativeID(b)},{"target","n999999"}});rejected(f,"graph.connections.remove",{{"connections",invalid}});
  invalid=cuts;invalid.push_back(cutOutput);rejected(f,"graph.connections.remove",{{"connections",invalid}});
  invalid=cuts;invalid.push_back({{"kind","insert"},{"plugin","fx"}});rejected(f,"graph.connections.remove",{{"connections",invalid}});
  invalid=cuts;invalid[2]["input"]=true;rejected(f,"graph.connections.remove",{{"connections",invalid}});
  // The portable primitive is independently atomic even without an API adapter.
  auto model=before;bool failed=false;try{removeSongConnections(model,{{SongConnectionKind::Output,a,master},{SongConnectionKind::Output,b,999999}});}catch(const std::invalid_argument &){failed=true;}CHECK(failed&&model==before);
  f.api.invoke("graph.connections.remove",{{"connections",cuts}});CHECK(f.doc->revision==revision+1);const auto after=f.doc->native();CHECK(after.mixer.buses[0].output==0&&after.mixer.buses[0].sends.empty()&&after.mixer.sidechains.empty());CHECK(after.mixer.instruments.size()==1&&after.mixer.instruments[0].target==master);CHECK(after.mixer.buses[1].inserts==before.mixer.buses[1].inserts);
  f.doc->undo();CHECK(f.doc->native()==before);f.doc->redo();CHECK(f.doc->native()==after);CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(after))==after);
  const auto rackBefore=f.doc->native();const Json exactCuts=Json::array({{{"kind","insert"},{"source",nativeID(b)},{"plugin","fx"}},{{"kind","master-output"},{"source",nativeID(master)}}});
  f.api.invoke("graph.connections.remove",{{"connections",exactCuts},{"dryRun",true}});CHECK(f.doc->native()==rackBefore);
  f.api.invoke("graph.connections.remove",{{"connections",exactCuts}});const auto exactCut=f.doc->native();CHECK(exactCut.mixer.disconnectedMainInputs==std::vector<std::string>{"fx"}&&exactCut.mixer.masterOutputDisconnected&&exactCut.mixer.buses[1].inserts==rackBefore.mixer.buses[1].inserts);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(exactCut))==exactCut);rejected(f,"graph.connections.remove",{{"connections",exactCuts}});
  f.doc->undo();CHECK(f.doc->native()==rackBefore);f.doc->redo();CHECK(f.doc->native()==exactCut);f.doc->undo();
  const Json last={{"kind","plugin-output"},{"plugin","synth"},{"target",nativeID(master)},{"output",0}};
  f.api.invoke("graph.connections.remove",{{"connections",Json::array({last})}});CHECK(f.doc->native().mixer.instruments.size()==1&&f.doc->native().mixer.instruments[0].target==0);rejected(f,"graph.connections.remove",{{"connections",Json::array({last})}});
  Fixture implicit;ScreamSeq::GraphHostHooks hooks;hooks.cachedRack={{{{"isInstrument",true}},"synth",0}};GraphOperations instrumentAPI(*implicit.doc,[&]{++implicit.stops;},hooks);
  instrumentAPI.invoke("graph.connections.remove",{{"connections",Json::array({last})}});CHECK(implicit.doc->native().mixer.instruments.size()==1&&implicit.doc->native().mixer.instruments[0].target==0);implicit.doc->undo();CHECK(implicit.doc->native()==original);
  f.doc->annotate([&](NativeSong &n){SignalSongSource source;source.node.id=n.makeEntity().id;source.node.kind=SignalNodeKind::Follower;source.audioBus=a;n.signal.songSources.push_back(source);n.signal.songModulation.push_back({source.node.id,"fx",UINT32_MAX,-.2,.2});});
  const auto mixedBefore=f.doc->native();const auto sourceID=nativeID(mixedBefore.signal.songSources[0].node.id);
  const Json mixed=Json::array({{{"kind","output"},{"source",nativeID(b)},{"target",nativeID(master)}},{{"kind","follower-input"},{"node",sourceID},{"source",nativeID(a)}},{{"kind","modulation"},{"source",sourceID},{"plugin","fx"},{"parameter",UINT32_MAX}}});
  auto mixedInvalid=mixed;mixedInvalid[2]["plugin"]="missing";rejected(f,"graph.connections.remove",{{"connections",mixedInvalid}});
  f.api.invoke("graph.connections.remove",{{"connections",mixed}});CHECK(f.doc->native().signal.songSources[0].audioBus==0&&f.doc->native().signal.songModulation.empty());f.doc->undo();CHECK(f.doc->native()==mixedBefore);
  Fixture modulationOnly;modulationOnly.doc->annotate([](NativeSong &n){SignalSongSource source;source.node.id=n.makeEntity().id;source.node.kind=SignalNodeKind::LFO;n.signal.songSources.push_back(source);n.signal.songModulation.push_back({source.node.id,"fx",7});});
  modulationOnly.api.invoke("graph.connections.remove",{{"connections",Json::array({{{"kind","modulation"},{"source",nativeID(modulationOnly.doc->native().signal.songSources[0].node.id)},{"plugin","fx"},{"parameter",7}}})}});CHECK(!modulationOnly.doc->native().mixer.active());
  Fixture external;const auto g=external.api.invoke("graph.create",Json::object())["graph"];auto d=definition(external);auto input=d["nodes"][0]["id"],output=d["nodes"][1]["id"];d["audio"].push_back({{"source",input},{"target",output},{"output",1},{"input",1}});external.api.invoke("graph.update",{{"definition",d}});external.api.invoke("graph.assign",{{"target",nativeID(b)},{"graph",g}});
  external.api.invoke("graph.routes.set",{{"inputs",Json::array({{{"source",nativeID(a)},{"target",nativeID(b)},{"input",1}}})},{"outputs",Json::array({{{"source",nativeID(b)},{"target",nativeID(master)},{"output",1}}})}});
  const auto externalBefore=external.doc->native();external.api.invoke("graph.connections.remove",{{"connections",Json::array({{{"kind","graph-input"},{"source",nativeID(a)},{"target",nativeID(b)},{"input",1}},{{"kind","graph-output"},{"source",nativeID(b)},{"target",nativeID(master)},{"output",1}}})}});CHECK(external.doc->native().signal.inputs.empty()&&external.doc->native().signal.outputs.empty());external.doc->undo();CHECK(external.doc->native()==externalBefore);
}
void songModulationSources() {
  Fixture f;ScreamSeq::GraphHostHooks hooks;
  hooks.cachedRack={{{{"name","Gain"},{"isInstrument",false}},"rack-a",0},{{{"name","Tone"},{"isInstrument",false}},"rack-b",1}};
  PluginParameter continuous{7,"Gain",0,1,.5,0,"",{}},stepped{8,"Mode",0,3,0,0,"",{}},readOnly{9,"Meter",0,1,0,0,"",{}};
  stepped.step=1;stepped.continuous=false;readOnly.writable=false;
  hooks.cachedParameters["rack-a"]={continuous,stepped,readOnly};hooks.cachedParameters["rack-b"]={continuous};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  auto reject=[&](const std::string &method,const Json &p) {
    const auto before=f.doc->native();const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();const auto stops=f.stops;bool failed=false;
    try{api.invoke(method,p);}catch(const ScreamSeq::Api::ApiError &e){CHECK(e.code==-32602);failed=true;}
    CHECK(failed);CHECK(f.doc->native()==before);CHECK(f.doc->revision==revision&&f.doc->historyBytes()==history);CHECK(f.stops==stops);
  };
  Json add={{"source",{{"kind","lfo"},{"name","Wobble"},{"rate",.5}}},{"connect",{{"plugin","rack-a"},{"parameter",7}}}};
  const auto initial=f.doc->native();auto dry=add;dry["dryRun"]=true;const auto preview=api.invoke("graph.song.source.add",dry);
  CHECK(preview["wouldChange"]==true&&f.doc->native()==initial&&!f.doc->canUndo());
  const auto source=api.invoke("graph.song.source.add",add)["node"];CHECK(source==preview["node"]);
  CHECK(f.doc->native().signal.songSources.size()==1&&!f.doc->native().mixer.active());
  CHECK(f.doc->native().signal.songModulation[0].minimum==0&&f.doc->native().signal.songModulation[0].maximum==0);
  const auto added=f.doc->native();f.doc->undo();CHECK(f.doc->native().signal.songSources.empty());f.doc->redo();CHECK(f.doc->native()==added);
  api.invoke("graph.song.source.update",{{"node",source},{"source",{{"name","Slow wobble"},{"phase",.25}}}});
  CHECK(f.doc->native().signal.songSources[0].node.rate==.5&&f.doc->native().signal.songSources[0].node.phase==.25);
  const auto revision=f.doc->revision;CHECK(api.invoke("graph.song.source.update",{{"node",source},{"source",{{"phase",.25}}}})["wouldChange"]==false);CHECK(f.doc->revision==revision);
  reject("graph.song.source.update",{{"node",source},{"source",{{"kind","random"}}}});
  reject("graph.song.source.update",{{"node",source},{"source",{{"audioPlugin","missing"}}}});
  reject("graph.song.source.add",{{"source",{{"kind","input"}}}});
  reject("graph.song.source.add",{{"source",{{"kind","midi"},{"controller",true}}}});
  reject("graph.song.source.add",{{"source",{{"kind","lfo"},{"id","n10000"}}}});
  const Json edge={{"source",source},{"plugin","rack-a"},{"parameter",7}};
  auto edit=edge;edit["minimum"]=-.4;edit["maximum"]=.6;api.invoke("graph.song.modulation.set",edit);
  edit=edge;edit["enabled"]=false;api.invoke("graph.song.modulation.set",edit);
  CHECK(f.doc->native().signal.songModulation[0].minimum==-.4&&f.doc->native().signal.songModulation[0].maximum==.6&&!f.doc->native().signal.songModulation[0].enabled);
  for(auto parameter:{8,9,99}){auto bad=edge;bad["parameter"]=parameter;reject("graph.song.modulation.set",bad);}
  edit=edge;edit["quantized"]=true;reject("graph.song.modulation.set",edit);
  edit["parameter"]=8;api.invoke("graph.song.modulation.set",edit);CHECK(f.doc->native().signal.songModulation.back().quantized);
  auto bad=add;bad["connect"]["plugin"]="missing";reject("graph.song.source.add",bad);
  bad=add;bad["connect"]["parameter"]=9;reject("graph.song.source.add",bad);
  bad=edge;bad["parameter"]=true;reject("graph.song.modulation.set",bad);
  bad=edge;bad["maximum"]=1.01;reject("graph.song.modulation.set",bad);
  // Endpoint dragging is a single edit and preserves the old depth/mode.
  Json reroute={{"source",source},{"plugin","rack-b"},{"parameter",7},{"replace",edge}};
  const auto beforeReroute=f.doc->native();api.invoke("graph.song.modulation.set",reroute);
  CHECK(f.doc->native().signal.songModulation.size()==2);CHECK(f.doc->native().signal.songModulation[0].plugin=="rack-b");
  CHECK(f.doc->native().signal.songModulation[0].minimum==-.4&&!f.doc->native().signal.songModulation[0].enabled);
  f.doc->undo();CHECK(f.doc->native()==beforeReroute);f.doc->redo();
  auto self=reroute;self["replace"]["plugin"]="rack-b";const auto sameRevision=f.doc->revision;CHECK(api.invoke("graph.song.modulation.set",self)["wouldChange"]==false);CHECK(f.doc->revision==sameRevision);
  auto collision=reroute;collision["plugin"]="rack-a";collision["parameter"]=8;collision["replace"]["plugin"]="rack-b";reject("graph.song.modulation.set",collision);
  reject("graph.song.modulation.set",reroute); // stale source endpoint
  auto gone=edge;gone["plugin"]="rack-b";
  reject("graph.song.modulation.remove",{{"connections",Json::array({gone,edge})}});
  reject("graph.song.modulation.remove",{{"connections",Json::array({gone,gone})}});
  api.invoke("graph.song.modulation.remove",{{"connections",Json::array({gone})}});CHECK(f.doc->native().signal.songModulation.size()==1);f.doc->undo();
  const auto track=nativeID(f.doc->native().tracks.at(0).id);
  const auto follower=api.invoke("graph.song.source.add",{{"source",{{"kind","follower"},{"audioBus",track},{"preFader",true}}}})["node"];
  CHECK(!f.doc->native().mixer.active());CHECK(f.doc->native().signal.songSources.back().audioBus==f.doc->native().tracks.at(0).id);
  const auto scopeBefore=f.doc->native();
  api.invoke("graph.song.source.add",{{"source",{{"kind","note-envelope"},{"noteTarget",track}}}});
  api.invoke("graph.song.source.add",{{"source",{{"kind","note-envelope"},{"noteTarget",nativeID(f.doc->native().masterID)}}}});
  CHECK(!f.doc->native().mixer.active());
  const auto scoped=f.doc->native();f.doc->undo();f.doc->undo();
  auto scopeRestored=scopeBefore;scopeRestored.nextID=scoped.nextID;
  CHECK(f.doc->native()==scopeRestored); // Undo never makes an allocated stable ID reusable.
  CHECK(f.doc->native().nextID>scopeBefore.nextID);
  f.doc->redo();f.doc->redo();CHECK(f.doc->native()==scoped);
  f.doc->addPattern(32,false,0);CHECK(f.doc->native().signal.songSources==scoped.signal.songSources&&!f.doc->native().mixer.active());
  reject("graph.song.source.add",{{"source",{{"kind","follower"},{"audioBus","n999999"}}}});
  reject("graph.song.source.add",{{"source",{{"kind","note-envelope"},{"noteTarget","n999999"}}}});
  for(auto kind:{"random","note-envelope","midi","amount"})api.invoke("graph.song.source.add",{{"source",{{"kind",kind}}}});
  const auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());CHECK(ScreamSeq::Project::decodeNativeMetadata(encoded)==f.doc->native());
  api.invoke("graph.layout.set",{{"positions",Json::array({{{"node","source:"+source.get<std::string>()},{"x",400},{"y",200}}})}});
  reject("graph.song.source.remove",{{"nodes",Json::array({source,"n999999"})}});reject("graph.song.source.remove",{{"nodes",Json::array({source,source})}});
  const auto beforeRemove=f.doc->native();api.invoke("graph.song.source.remove",{{"nodes",Json::array({source,follower})}});
  CHECK(f.doc->native().signal.songModulation.empty()&&!f.doc->native().signal.layout.contains("source:"+source.get<std::string>()));
  f.doc->undo();CHECK(f.doc->native()==beforeRemove);f.doc->redo();
  // A host provider is authoritative over fixture/cache metadata and receives stable IDs.
  unsigned lookups=0;hooks.parameters=[&](const std::string &id){CHECK(id=="rack-a");++lookups;return std::vector<PluginParameter>{readOnly};};
  GraphOperations live(*f.doc,{},hooks);bool unavailable=false;
  try{live.invoke("graph.song.source.add",add);}catch(const ScreamSeq::Api::ApiError &){unavailable=true;}CHECK(unavailable&&lookups==1);
  // Broken vendor metadata must fail before creating a source or history entry.
  for(const auto range:std::vector<std::pair<float,float>>{{0,0},{1,0},{0,std::numeric_limits<float>::infinity()},{std::numeric_limits<float>::quiet_NaN(),1}}) {
    auto invalid=continuous;invalid.min=range.first;invalid.max=range.second;
    hooks.parameters=[invalid](const std::string &){return std::vector<PluginParameter>{invalid};};
    GraphOperations invalidHost(*f.doc,{},hooks);const auto before=f.doc->native();const auto revision=f.doc->revision;bool failed=false;
    try{invalidHost.invoke("graph.song.source.add",add);}catch(const ScreamSeq::Api::ApiError &){failed=true;}
    CHECK(failed&&f.doc->native()==before&&f.doc->revision==revision);
  }
}
void songAutomationAndBanks() {
  Fixture f;const auto second=f.doc->addPattern(32,false,0);
  const auto source=f.api.invoke("graph.song.source.add",{{"source",{{"kind","automation"},{"name","Song curve"}}}})["node"];
  Json target={{"graph",nullptr},{"node",source},{"pattern",0}};
  CHECK(f.api.invoke("graph.automation.get",target)["graph"].is_null());
  Json set=target;set["points"]=Json::array({{{"position",0},{"value",.2}},{{"position",4096},{"value",.8}}});
  f.api.invoke("graph.automation.set",set);const auto first=f.api.invoke("graph.automation.get",target);
  set["pattern"]=second;set["enabled"]=false;set["points"]=Json::array({{{"position",256},{"value",.6}}});f.api.invoke("graph.automation.set",set);
  CHECK(f.api.invoke("graph.automation.get",target)==first);
  const auto revision=f.doc->revision;CHECK(f.api.invoke("graph.automation.set",set)["wouldChange"]==false);CHECK(f.doc->revision==revision);
  auto invalid=set;invalid["points"][0]["position"]=.25;rejected(f,"graph.automation.set",invalid);invalid=set;invalid.erase("graph");rejected(f,"graph.automation.set",invalid);
  const auto owner=f.doc->native().signal.songSources[0].node.id;const EnvelopeTarget bankTarget{EnvelopeTargetKind::Graph,owner,f.doc->native().patterns.at(0).id};
  f.doc->annotate([&](NativeSong &n){const auto shape=captureEnvelope(n,f.doc->song(),bankTarget);const auto bank=n.makeEntity().id;n.envelopeBank.push_back({bank,"Song shape",shape});n.envelopeLinks.push_back({bankTarget,bank,shape.span});});
  invalid=target;invalid["points"]=Json::array({{{"position",0},{"value",.5}}});rejected(f,"graph.automation.set",invalid);
  const auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());CHECK(ScreamSeq::Project::decodeNativeMetadata(encoded)==f.doc->native());
  const auto linked=f.doc->native();f.api.invoke("graph.song.source.remove",{{"nodes",Json::array({source})}});CHECK(f.doc->native().envelopeLinks.empty());CHECK(f.doc->native().envelopeBank.size()==1);
  f.doc->undo();CHECK(f.doc->native()==linked);
  set=target;set["points"]=Json::array();f.api.invoke("graph.automation.set",set);CHECK(f.doc->native().envelopeLinks.empty());CHECK(f.doc->native().signal.songSources[0].node.envelopes.size()==1);
  CHECK(f.doc->native().signal.songSources[0].node.envelopes[0].pattern==f.doc->native().patterns.at(second).id);
}
void graphProvenance() {
  Fixture f;auto metadata=f.doc->native();const auto pattern=metadata.patterns.at(0).id,track=metadata.tracks.at(0).id;
  MusicalAutomationLane lane;lane.id=metadata.makeEntity().id;lane.pattern=pattern;lane.plugin="unloaded";lane.parameter=77;lane.enabled=false;lane.points={{0,.1},{256,.7}};metadata.automation.push_back(lane);
  metadata.performance.bindings[1]={"unloaded",77,"Mix"};metadata.performance.commands.push_back({pattern,track,16*65536,65536,0,PatternCommandKind::ParameterSlide,1,.8});
  f.doc->annotate([&](NativeSong &n){n=metadata;});const auto before=f.doc->native();const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();
  ScreamSeq::GraphHostHooks hooks;hooks.cachedRecordings[{"unloaded",77}]={2,48000,96000};GraphOperations api(*f.doc,[&]{f.stops++;},hooks);
  const auto page=api.invoke("graph.provenance.get",{{"plugin","unloaded"},{"parameter",77}});CHECK(page.at("sources").size()==3&&page.at("total")==3);CHECK(page["sources"][0]["kind"]=="envelope"&&!page["sources"][0]["enabled"].get<bool>());CHECK(page["sources"][1]["commands"][0]["kind"]=="pattern-slide");CHECK(page["sources"][2]["sampleRate"]==48000);
  CHECK(api.invoke("graph.provenance.get",{{"plugin","unloaded"},{"parameter",77},{"offset",1},{"limit",1}})["sources"].size()==1);CHECK(f.doc->native()==before&&f.doc->revision==revision&&f.doc->historyBytes()==history&&f.stops==0);
  bool rejected=false;try{api.invoke("graph.provenance.get",{{"plugin","unloaded"},{"parameter",true}});}catch(const ScreamSeq::Api::ApiError &e){rejected=e.code==-32602;}CHECK(rejected);
}
void graphPresentation() {
  Fixture f;auto before=f.doc->native();const auto track="n"+std::to_string(before.tracks.at(0).id),master="n"+std::to_string(before.masterID);
  Json visual={{"regions",Json::array({{{"id","frame-1"},{"kind","frame"},{"title","Rhythm"},{"x",10},{"y",20},{"nodes",Json::array({track})}},{{"id","comment-1"},{"kind","comment"},{"text","Keep transients"},{"x",400},{"y",20}}})},{"cables",Json::array({{{"source",track},{"target",master},{"points",Json::array({Json::array({300,180})})}}})}};
  visual["collapsedNodes"]=Json::array({track});
  auto params=Json{{"graph",nullptr},{"presentation",visual},{"positions",Json::array({{{"node",track},{"x",70},{"y",90}}})}};
  auto preview=params;preview["dryRun"]=true;f.api.invoke("graph.presentation.set",preview);CHECK(f.doc->native()==before&&f.stops==0);
  f.api.invoke("graph.presentation.set",params);auto saved=f.doc->native();CHECK(saved.signal.presentation.regions.size()==2&&saved.signal.presentation.cables.size()==1&&saved.signal.presentation.collapsedNodes==std::vector<std::string>{track});CHECK(saved.mixer==before.mixer&&f.stops==0);CHECK(sameSignalProcessing(before.signal,saved.signal));CHECK((saved.signal.layout.at(track)==std::array<double,2>{70,90}));
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(saved))==saved);
  const auto rev=f.doc->revision;f.api.invoke("graph.presentation.set",params);CHECK(f.doc->revision==rev);
  auto bad=params;bad["presentation"]["regions"][1]["nodes"]=Json::array({track});rejected(f,"graph.presentation.set",bad);CHECK(f.doc->native()==saved);
  bad=params;bad["presentation"]["cables"][0]["points"]=Json::array({Json::array({3})});rejected(f,"graph.presentation.set",bad);CHECK(f.doc->native()==saved);
  bad=params;bad["presentation"]["collapsedNodes"]=Json::array({track,track});rejected(f,"graph.presentation.set",bad);CHECK(f.doc->native()==saved);
  bad=params;bad["presentation"]["collapsedNodes"]=Json::array({true});rejected(f,"graph.presentation.set",bad);CHECK(f.doc->native()==saved);
  bad=params;bad["positions"].push_back(bad["positions"][0]);rejected(f,"graph.presentation.set",bad);CHECK(f.doc->native()==saved);
  f.doc->undo();CHECK(f.doc->native()==before&&f.doc->canRedo());f.api.invoke("graph.presentation.set",{{"presentation",Json::object()}});CHECK(f.doc->canRedo());f.doc->redo();CHECK(f.doc->native()==saved);
  auto removal=params;removal["presentation"]={{"regions",Json::array()},{"cables",Json::array()}};f.api.invoke("graph.presentation.set",removal);CHECK(f.doc->native().signal.presentation.empty()&&f.doc->native().mixer==before.mixer);CHECK(f.stops==0);
  const auto graph=f.api.invoke("graph.create",Json::object())["graph"];const auto d=f.api.invoke("graph.get",Json::object())["library"][0];const auto input=d["nodes"][0]["id"],output=d["nodes"][1]["id"];
  visual["collapsedNodes"]=Json::array({input});visual["regions"][0]["nodes"]=Json::array({input});visual["cables"][0]["source"]=input;visual["cables"][0]["target"]=output;
  f.api.invoke("graph.presentation.set",{{"graph",graph},{"presentation",visual}});auto original=f.doc->native().signal.library[0];f.api.invoke("graph.clone",{{"graph",graph}});const auto &copy=f.doc->native().signal.library[1];CHECK(copy.presentation.regions[0].nodes[0]=="n"+std::to_string(copy.nodes[0].id));CHECK(copy.presentation.cables[0].source!=original.presentation.cables[0].source);CHECK(copy.presentation.collapsedNodes==std::vector<std::string>{copy.presentation.cables[0].source});CHECK(copy.audio.size()==original.audio.size());CHECK(f.stops==0);
  auto edited=copy.presentation;removeSignalPresentationNode(edited,copy.presentation.cables[0].source);CHECK(edited.cables.empty()&&edited.regions[0].nodes.empty()&&edited.collapsedNodes.empty());CHECK(copy.audio.size()==1);
  auto detached=saved;detached.mixer.detached={"loose"};detached.signal.presentation.regions[0].nodes={"plugin:loose"};detached.signal.presentation.regions[0].scope="n909";
  detached.signal.presentation.collapsedNodes={"plugin:loose"};detached.signal.presentation.cables[0].source="plugin:loose";detached.signal.groups.push_back({909,0,"Loose",0,0,{"plugin:loose"}});
  detached.removePluginRoutes("loose");CHECK(detached.mixer.detached.empty()&&detached.signal.groups.empty());CHECK(detached.signal.presentation.collapsedNodes.empty()&&detached.signal.presentation.cables.empty()&&detached.signal.presentation.regions[0].nodes.empty()&&detached.signal.presentation.regions[0].scope.empty());
  auto pruned=copy.presentation;pruneSignalPresentation(pruned,{});CHECK(pruned.collapsedNodes.empty());
  auto compactOnly=before;compactOnly.signal.presentation.collapsedNodes={track};CHECK(sameSignalProcessing(before.signal,compactOnly.signal)&&compactOnly.signal.presentation.bytes()>before.signal.presentation.bytes());
  auto rerouted=original;auto &path=rerouted.presentation.cables[0];path.target="n999"; // Retarget helper preserves points and resolves collisions without duplicate paths.
  const auto oldPath=path;auto newPath=path;newPath.target="n998";newPath.input=UINT32_MAX;newPath.modulation=true;retargetSignalCableGeometry(rerouted.presentation,oldPath,newPath);CHECK(rerouted.presentation.cables[0].target=="n998"&&rerouted.presentation.cables[0].points==oldPath.points);rerouted.presentation.validate();
  auto recipe=original;recipe.nodes.push_back({999,SignalNodeKind::Plugin,"Extra",500,100});recipe.audio[0].target=999;reconcileSignalPresentation(recipe,original);CHECK(recipe.presentation.cables[0].target=="n999"&&recipe.presentation.cables[0].points==original.presentation.cables[0].points);
  recipe.nodes.pop_back();recipe.audio.clear();pruneSignalGroups(recipe);CHECK(recipe.presentation.cables.empty());
  auto loose=saved;loose.ensureMixer();loose.mixer.detached={"loose"};loose.signal.groups.push_back({910,0,"Loose",0,0,{"plugin:loose"}});GraphPluginRecipe effect;effect.name="Loose";uint64_t nextID=1000;bool rejectedLoose=false;
  try{extractSongSignalGroup(loose.signal,loose.mixer,910,{{"loose",effect}},[&]{return nextID++;});}catch(const std::invalid_argument &){rejectedLoose=true;}CHECK(rejectedLoose);

}
void callbackOrderingAndUnrelatedData() {
  Fixture f;
  f.doc->edit({{0,2,1,{},Cell{49,0,0,0,0,0}}});
  f.doc->annotate([](NativeSong &n){ n.tracks.at(1).name="Preserved track"; n.patterns.at(0).annotation="Unrelated pattern"; n.columnMutes[n.tracks.at(2).id]=true; });
  const auto cells=f.doc->cell(0,2,1); auto expected=f.doc->native(); unsigned calls=0;
  GraphOperations api(*f.doc,[&]{CHECK(f.doc->native()==expected); CHECK(f.doc->cell(0,2,1)==cells); ++calls;});
  const auto graph=api.invoke("graph.create",Json::object()).at("graph"); CHECK(calls==0);
  expected=f.doc->native();
  api.invoke("graph.assign",{{"target",nativeID(expected.tracks.at(0).id)},{"graph",graph}});CHECK(calls==1);
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
  CHECK((std::set<std::string>(reads.begin(),reads.end())==std::set<std::string>{"graph.note.activity","graph.get","graph.selection.copy","graph.group.boundary","graph.automation.get","graph.provenance.get"}));
  CHECK((std::set<std::string>(writes.begin(),writes.end())==std::set<std::string>{"graph.audio.connection.set","graph.note.connect","graph.note.update","graph.note.disconnect","graph.note.restoreAssignment","graph.makeIndependent","graph.selection.paste","graph.selection.cut","graph.selection.duplicate","graph.source.mute","graph.group.bypass","graph.create","graph.clone","graph.song.source.add","graph.song.source.update","graph.song.source.remove","graph.song.modulation.set","graph.song.modulation.remove","graph.song.group.create","graph.song.group.update","graph.song.group.remove","graph.song.group.export","graph.group.create","graph.group.update","graph.group.remove","graph.group.export","graph.update","graph.remove","graph.node.add","graph.node.remove","graph.nodes.insert","graph.nodes.detach","graph.assign","graph.instrument.assign","graph.routes.set","graph.connections.remove","graph.layout.set","graph.presentation.set","graph.commands.set","graph.automation.set"}));
  CHECK(reads.size()==6); CHECK(writes.size()==40);
}
#include "FanConnectionOperationsTests.inc"
#include "StageConnectionOperationsTests.inc"
#include "NoteRoutingOperationsTests.inc"
#include "GraphEditingOperationsTests.inc"
#include "SongSourceGroupOperationsTests.inc"
int main(int argc,char **argv) {
  try {
    if(argc==2&&std::string(argv[1])=="--catalog") { std::cout<<Json{{"reads",GraphOperations::reads()},{"writes",GraphOperations::writes()}}.dump(2)<<'\n'; return 0; }
    const std::vector<std::pair<const char *,void(*)()>> tests={
      {"audioFanConnections",audioFanConnections},{"stageConnectionOperations",stageConnectionOperations},{"noteRoutingOperations",noteRoutingOperations},{"graphEditingOperations",graphEditingOperations},{"groupBypassOperations",groupBypassOperations},
      {"graphProvenance",graphProvenance},{"graphPresentation",graphPresentation},{"songModulationSources",songModulationSources},{"songAutomationAndBanks",songAutomationAndBanks},{"stableImplicitMaster",stableImplicitMaster},{"songCableCuts",songCableCuts},
      {"createReadHistory",createReadHistory},{"nodesAndCloning",nodesAndCloning},{"automationAndBanks",automationAndBanks},
      {"assignmentsRoutesLayoutCommands",assignmentsRoutesLayoutCommands},{"hostHooks",hostHooks},
      {"automationOrderAndRedo",automationOrderAndRedo},{"cableInsertionAndDetachment",cableInsertionAndDetachment},
      {"songSourceGroupOperations",songSourceGroupOperations},{"songProcessingGroups",songProcessingGroups},{"processingGroups",processingGroups},{"strictValidation",strictValidation},{"dryRunsAndRedo",dryRunsAndRedo},{"callbackOrderingAndUnrelatedData",callbackOrderingAndUnrelatedData}};
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    unsigned ran=0;
    for(const auto &[name,test]:tests) if(argc==1||std::string(argv[1])==name) { std::cout<<"RUN "<<name<<'\n'; test(); ++ran; std::cout<<"PASS "<<name<<'\n'; }
    CHECK(ran>0); std::cout<<"Passed "<<ran<<" scenario groups\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
