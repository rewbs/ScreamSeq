#include "windows/Session/ScratchGestureOperations.hpp"
#include "windows/Project/NativePatternJSON.hpp"
#include "windows/Project/ProjectPreservation.hpp"
#include "windows/Session/TimelineOperations.hpp"
#include <iostream>

using namespace Tracker;
using ScreamSeq::Api::Json;
using namespace ScreamSeq::Project;
#define CHECK(value) do{if(!(value))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": "+#value);}while(false)
struct Host final:ScreamSeq::Api::SessionHost {
  Document document;unsigned serial=0,prepared=0,published=0;bool reject=false;
  ScreamSeq::Api::SessionSnapshot snapshot()override{return {std::to_string(document.revision),"scratch-test",Json::object(),Json::object(),{{"playing",true}}};}
  bool supportsDocumentOperations()const override{return true;}
  std::vector<std::string> additionalDocumentReads()const override{return {"scratch.gestures.get"};}
  std::vector<std::string> additionalDocumentWrites()const override{return {"scratch.gestures.set","scratch.gestures.remove","scratch.gestures.clone"};}
  std::function<void()> prepare(const NativeSong &){++prepared;return [this]{if(reject)throw ScreamSeq::Api::ApiError(-32002,"controlled busy publication");++published;};}
  Json documentOperation(const std::string &method,const Json &input)override{auto p=input;p.erase("expectedRevision");return ScreamSeq::scratchGestureOperation(document,method,p,[this](const auto &n){return prepare(n);});}
  ScreamSeq::Api::PatternSnapshot pattern(unsigned)override{return {};}
  void play(const Json &)override{}
  void stop()override{throw std::runtime_error("Scratch bank edit stopped transport");}
};
int main(){try{
  Host host;ScreamSeq::Api::SessionAdapter adapter(host);
  auto call=[&](const std::string &method,Json p=Json::object(),bool guarded=true){if(guarded&&method!="scratch.gestures.get"&&method!="api.describe"&&!p.contains("expectedRevision"))p["expectedRevision"]=std::to_string(host.document.revision);return adapter.handle({{"jsonrpc","2.0"},{"id",std::to_string(++host.serial)},{"method",method},{"params",p}});};
  auto invoke=[&](const std::string &method,Json p=Json::object()){auto result=call(method,p);if(result.contains("error"))throw std::runtime_error(result.dump());return result.at("result").at("data");};
  auto reject=[&](Json p,int code=-32602,const std::string &method="scratch.gestures.set"){
    const auto native=host.document.native();const auto revision=host.document.revision;const auto history=host.document.historyBytes();const auto published=host.published;
    const auto result=call(method,std::move(p));CHECK(result.contains("error")&&result["error"]["code"]==code);
    CHECK(host.document.native()==native&&host.document.revision==revision&&host.document.historyBytes()==history&&host.published==published);
  };
  const auto base=host.document.native();const auto listed=invoke("scratch.gestures.get");CHECK(listed["unitsPerCycle"]==65536&&listed["limits"]["pointsPerLane"]==256&&listed["presets"].size()==7);
  const auto dry=invoke("scratch.gestures.set",{{"preset","chirp"},{"dryRun",true}});CHECK(dry["id"]==1&&dry["wouldChange"]==true&&host.document.native()==base&&host.prepared==0);
  invoke("scratch.gestures.set",{{"preset","chirp"}});CHECK(host.document.native().scratchGestures.at(1).name=="Chirp"&&host.document.native().nextID==base.nextID&&host.published==1);
  const auto saved=host.document.native();const auto revision=host.document.revision;
  invoke("scratch.gestures.set",{{"id",1},{"name","Chirp"}});CHECK(host.document.revision==revision&&host.prepared==1);
  reject({{"id",1},{"name","Stale"},{"expectedRevision","0"}},-32001);
  CHECK(call("scratch.gestures.set",{{"preset","baby"}},false)["error"]["code"]==-32602);
  for(Json invalid:{Json(true),Json(0),Json(256),Json(1.5),Json("1")})reject({{"id",invalid},{"preset","baby"}});
  reject({{"preset","unknown"}});reject({{"name","Incomplete"}});reject({{"preset","baby"},{"typo",1}});reject({{"preset","baby"},{"dryRun",1}});
  auto points=listed["presets"][0]["motion"];
  for(const auto &[key,value]:std::vector<std::pair<std::string,Json>>{{"position",true},{"position",-1},{"position",65537},{"value",true},{"value",1.1},{"curve","unknown"},{"extra",1}}){auto bad=points;bad[0][key]=value;reject({{"preset","baby"},{"motion",bad}});}
  auto duplicate=points;duplicate[1]["position"]=0;reject({{"preset","baby"},{"motion",duplicate}});
  auto missingEnd=points;missingEnd.back()["position"]=65535;reject({{"preset","baby"},{"motion",missingEnd}});
  auto formula=points;formula[0]["curve"]="scripted";formula[0]["formula"]="mix(start,end,t*t)";
  invoke("scratch.gestures.set",{{"id",7},{"preset","baby"},{"name","Custom"},{"motion",formula}});CHECK(host.document.native().scratchGestures.at(7).motion[0].formula.source()=="mix(start,end,t*t)");
  CHECK(invoke("scratch.gestures.set",{{"preset","crab"}})["id"]==2); // Stable first-free slot; no entity allocator use.
  const auto beforeUndo=host.document.native();host.document.undo(host.prepare(saved));CHECK(!host.document.native().scratchGestures.contains(2));host.document.redo(host.prepare(beforeUndo));CHECK(host.document.native()==beforeUndo);
  host.reject=true;reject({{"id",7},{"name","Rejected live update"}},-32002);host.reject=false;
  invoke("scratch.gestures.remove",{{"id",2},{"dryRun",true}});CHECK(host.document.native().scratchGestures.contains(2));invoke("scratch.gestures.remove",{{"id",2}});CHECK(!host.document.native().scratchGestures.contains(2));
  auto musical=host.document.native();PatternCommand command{musical.patterns.at(0).id,musical.tracks.at(0).id,0,0,0,PatternCommandKind::Native};command.native=NativePatternOp::Scratch;command.arguments=nativePatternDefaults(command.native);musical.performance.commands.push_back(command);host.document.annotate([&](auto &n){n=musical;});
  CHECK(invoke("scratch.gestures.get")["gestures"][0]["uses"]==1);reject({{"id",1}},-32602,"scratch.gestures.remove");
  const Json target={{"pattern",0},{"row",0},{"channel",0},{"column",0}};
  const auto beforeClone=host.document.native();const auto cloneRevision=host.document.revision;
  CHECK(invoke("scratch.gestures.clone",{{"id",1},{"target",target},{"dryRun",true}})["id"]==2&&host.document.native()==beforeClone&&host.document.revision==cloneRevision);
  reject({{"id",7},{"target",target}},-32602,"scratch.gestures.clone");
  auto wrongTarget=target;wrongTarget["row"]=1;reject({{"id",1},{"target",wrongTarget}},-32602,"scratch.gestures.clone");
  wrongTarget=target;wrongTarget.erase("column");reject({{"id",1},{"target",wrongTarget}},-32602,"scratch.gestures.clone");
  wrongTarget=target;wrongTarget["channel"]=true;reject({{"id",1},{"target",wrongTarget}},-32602,"scratch.gestures.clone");
  const auto copyID=invoke("scratch.gestures.clone",{{"id",1},{"target",target},{"name","Unique chirp"}})["id"].get<unsigned>();
  auto expected=beforeClone;expected.scratchGestures[copyID]=expected.scratchGestures.at(1);expected.scratchGestures[copyID].name="Unique chirp";expected.performance.commands[0].arguments[0]=copyID;
  CHECK(host.document.native()==expected);
  host.document.undo();CHECK(host.document.native()==beforeClone);host.document.redo();CHECK(host.document.native()==expected);host.document.undo();
  const auto encoded=encodeNativeMetadata(musical);CHECK(decodeNativeMetadata(encoded)==musical);
  Document reopened(host.document.snapshotData());reopened.restoreNative(decodeNativeMetadata(encoded));CHECK(reopened.native()==musical);
  auto old=encodeNativeMetadata(base);old.erase("scratchGestures");ProjectLoadRecovery report;CHECK(recoverNativeMetadata(old,reopened,report).scratchGestures.empty()&&!report.protectSource&&report.warnings.empty());
  auto damaged=encoded;damaged["scratchGestures"][0]["motion"][0]["value"]=2;
  auto second=damaged["performance"]["commands"][0];second["position"]=65536;second["parameters"]["gesture"]=7;damaged["performance"]["commands"].push_back(second);
  bool strict=false;try{decodeNativeMetadata(damaged);}catch(const std::invalid_argument &){strict=true;}CHECK(strict);
  report={};const auto recovered=recoverNativeMetadata(damaged,reopened,report);
  CHECK(recovered.scratchGestures.size()==1&&recovered.scratchGestures.contains(7));
  CHECK(recovered.performance.commands.size()==1&&recovered.performance.commands[0].arguments[0]==7&&report.lossy&&report.protectSource&&report.warnings.size()>=2);
  CHECK(decodeNativeMetadata(damaged)==recovered);
  auto unsorted=encoded;std::swap(unsorted["scratchGestures"][0],unsorted["scratchGestures"][1]);unsorted["scratchGestures"][0]["extension"]="seven";unsorted["scratchGestures"][1]["extension"]="one";
  report={};CHECK(recoverNativeMetadata(unsorted,reopened,report)==musical);CHECK(unsorted["scratchGestures"][0]["id"]==1&&unsorted["scratchGestures"][0]["extension"]=="one"&&unsorted["scratchGestures"][1]["extension"]=="seven");
  auto renamed=musical;renamed.scratchGestures[7].name="Renamed";const auto merged=mergePreserved(unsorted,encoded,encodeNativeMetadata(renamed));CHECK(merged["scratchGestures"][1]["extension"]=="seven"&&merged["scratchGestures"][1]["name"]=="Renamed");
  auto worst=encoded;worst["scratchGestures"]=Json::array();for(unsigned i=1;i<=255;++i){auto value=encoded["scratchGestures"][1];value["id"]=i;if(i!=255)value["motion"][0]["value"]=2;worst["scratchGestures"].push_back(std::move(value));}worst["performance"]["commands"][0]["parameters"]["gesture"]=255;
  report={};const auto tail=recoverNativeMetadata(worst,reopened,report);CHECK(tail.scratchGestures.size()==1&&tail.scratchGestures.contains(255)&&tail.performance.commands.size()==1&&report.lossy&&report.omittedWarnings>0);
  // Individually valid phrases beyond the aggregate budget must not discard
  // the recoverable prefix. A later small phrase can still use remaining room.
  ScratchGesture dense{"Dense",{}, {}};CurveFormula denseFormula("mix(start,end,0.5+0.5*sin(t*tau))");
  for(unsigned i=0;i<256;++i)dense.motion.push_back({uint32_t(uint64_t(i)*65536/255),.5,AutomationCurve::Scripted,denseFormula});dense.fader=dense.motion;
  auto oversized=encodeNativeMetadata(base);auto denseJSON=ScreamSeq::ScratchJSON::gesture(dense);size_t total=base.bytes();unsigned count=0;
  while(total<=16*1024*1024&&count<254){denseJSON["id"]=++count;oversized["scratchGestures"].push_back(denseJSON);total+=sizeof(uint16_t)+scratchGestureBytes(dense);}
  CHECK(total>16*1024*1024);auto small=ScreamSeq::ScratchJSON::gesture(scratchPresets()[0].gesture);small["id"]=255;oversized["scratchGestures"].push_back(small);
  report={};const auto bounded=recoverNativeMetadata(oversized,reopened,report);CHECK(!bounded.scratchGestures.empty()&&bounded.scratchGestures.size()<count+1&&bounded.scratchGestures.contains(1)&&bounded.scratchGestures.contains(255)&&bounded.bytes()<=16*1024*1024&&report.lossy&&report.protectSource);
  CHECK(decodeNativeMetadata(oversized)==bounded);
  auto badReference=encoded;badReference["performance"]["commands"][0]["parameters"]["gesture"]=6;strict=false;try{decodeNativeMetadata(badReference);}catch(const std::invalid_argument &){strict=true;}CHECK(strict);
  Document destination;auto original=destination.native();original.scratchGestures[1]=scratchPresets()[0].gesture;destination.restoreNative(original);
  const Json phrases=Json::array({encoded["scratchGestures"][0]});ScreamSeq::ScratchJSON::ClipboardGestures pasted(phrases);
  auto candidate=original;const auto mapped=pasted.remap(candidate.scratchGestures,1);CHECK(mapped==2&&candidate.scratchGestures[1]==original.scratchGestures[1]&&candidate.scratchGestures[2]==musical.scratchGestures.at(1));
  auto pastedCommand=command;pastedCommand.pattern=candidate.patterns.at(0).id;pastedCommand.track=candidate.tracks.at(0).id;pastedCommand.arguments[0]=mapped;candidate.performance.commands.push_back(pastedCommand);destination.editNative(candidate,{});
  destination.undo();CHECK(destination.native()==original);destination.redo();CHECK(destination.native()==candidate&&musical.scratchGestures.at(1).name=="Chirp");
  ScreamSeq::ScratchJSON::ClipboardGestures reuse(phrases);CHECK(reuse.remap(candidate.scratchGestures,1)==2&&candidate.scratchGestures.size()==2);
  ScreamSeq::ScratchJSON::ClipboardGestures missing(Json::array());strict=false;try{missing.remap(candidate.scratchGestures,1);}catch(const std::invalid_argument &){strict=true;}CHECK(strict);
  ScreamSeq::TimelineOperations timeline(host.document);
  auto preview=Json{{"rows",257},{"span",65537},{"start",0},{"end",65536},{"samples",3},{"scratchBeats",2},{"points",Json::array({{{"position",0},{"value",0},{"curve","scripted"},{"formula","beat/4"}},{{"position",65536},{"value",.5}}})}};
  const auto curve=timeline.invoke("automation.formula.preview",preview);CHECK(std::abs(curve["values"][1][1].get<double>()-.25)<1e-12);
  for(const auto &[key,value]:std::vector<std::pair<std::string,Json>>{{"scratchBeats",0},{"scratchBeats",true},{"rows",256},{"span",65536}}){auto invalid=preview;invalid[key]=value;strict=false;try{timeline.invoke("automation.formula.preview",invalid);}catch(const ScreamSeq::Api::ApiError &){strict=true;}CHECK(strict);}
  std::cout<<"PASS Windows scratch API: guarded edits, no-op, live rejection/history, presets, strict curves, persistence and independent recovery (no device/realtime audit)\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
