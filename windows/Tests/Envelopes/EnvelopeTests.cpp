#include "editor/TrackerDocument.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "windows/Project/ProjectIO.hpp"
#include "editor/InstrumentEnvelopeTools.hpp"
#include "soundlib/mod_specifications.h"
#include "mpt/crypto/hash.hpp"
#include <filesystem>
#ifdef small
#undef small
#endif
#include <iostream>
#include <stdexcept>
#include "windows/Session/EnvelopeOperations.hpp"
using namespace Tracker;
using ScreamSeq::Api::Json;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": "+#x); } while(false)
struct Fixture {
  std::unique_ptr<Document> doc=std::make_unique<Document>(MOD_TYPE_MPT,4);
  unsigned stops=0;
  ScreamSeq::EnvelopeOperations api{*doc,[this]{++stops;}};
  explicit Fixture(OpenMPT::MODTYPE type=MOD_TYPE_MPT):doc(std::make_unique<Document>(type,4)) {}
};
Json shape(double a=0.25,double b=0.75) {
  return {{"span",16384},{"points",Json::array({{{"position",0},{"value",a}},{{"position",16383},{"value",b}}})}};
}
void bankHistory() {
  Fixture f; const auto original=f.doc->native();
  CHECK(f.api.invoke("envelope.bank.list",Json::object())==Json({{"entries",Json::array()},{"links",Json::array()}}));
  Json p={{"name","First"},{"shape",shape()},{"dryRun",true}};
  auto preview=f.api.invoke("envelope.bank.save",p);
  CHECK(preview["wouldChange"]==true); CHECK(f.doc->native()==original); CHECK(!f.doc->canUndo()); CHECK(f.stops==0);
  p.erase("dryRun"); auto saved=f.api.invoke("envelope.bank.save",p);
  CHECK(saved["id"]==preview["id"]); CHECK(f.stops==1); CHECK(f.doc->revision==1);
  auto read=f.api.invoke("envelope.bank.list",Json::object()); CHECK(read["entries"].size()==1);
  CHECK(read["entries"][0]["name"]=="First"); CHECK(read["entries"][0]["shape"]["markers"][4]==UINT32_MAX);
  auto changed=f.doc->native(); p["id"]=saved["id"];
  CHECK(f.api.invoke("envelope.bank.save",p)["wouldChange"]==false); CHECK(f.doc->revision==1); CHECK(f.stops==1);
  f.doc->undo(); auto undone=original; undone.nextID=changed.nextID; CHECK(f.doc->native()==undone);
  const auto undoRevision=f.doc->revision,undoHistory=f.doc->historyBytes();
  auto again=p;again.erase("id");again["dryRun"]=true;f.api.invoke("envelope.bank.save",again);
  CHECK(f.doc->native()==undone);CHECK(f.doc->canRedo());CHECK(f.doc->revision==undoRevision);CHECK(f.doc->historyBytes()==undoHistory);
  f.doc->redo(); CHECK(f.doc->native()==changed);
}
std::string nid(uint64_t n) { return "n"+std::to_string(n); }
void rejected(Fixture &f,const std::string &method,const Json &p,int code=-32602) {
  const auto before=f.doc->native();
  const auto bytes=f.doc->snapshotData(); const auto revision=f.doc->revision,history=f.doc->historyBytes();
  const auto stops=f.stops; const auto undo=f.doc->canUndo(),redo=f.doc->canRedo(); bool threw=false;
  try { f.api.invoke(method,p); } catch(const ScreamSeq::Api::ApiError &e) { CHECK(e.code==code); threw=true; }
  CHECK(threw); CHECK(f.doc->native()==before); CHECK(f.doc->snapshotData()==bytes);
  CHECK(f.doc->revision==revision); CHECK(f.doc->historyBytes()==history); CHECK(f.stops==stops);
  CHECK(f.doc->canUndo()==undo); CHECK(f.doc->canRedo()==redo);
}
std::vector<Json> targets(Fixture &f) {
  f.doc->transaction([](OpenMPT::CSoundFile &s){
    s.m_nInstruments=2; s.Instruments[1]=new OpenMPT::ModInstrument(1); s.Instruments[2]=new OpenMPT::ModInstrument(1);
    auto &e=s.Instruments[1]->VolEnv; e.push_back(0,0); e.push_back(4,64); e.push_back(12,0);
    e.dwFlags.set(ENV_ENABLED|ENV_SUSTAIN); e.nSustainStart=e.nSustainEnd=1;
  });
  auto n=f.doc->native(); SignalDefinition g; g.id=n.makeEntity().id; g.name="Envelope test";
  g.nodes={{n.makeEntity().id,SignalNodeKind::Input,"In"},{n.makeEntity().id,SignalNodeKind::Output,"Out"},{n.makeEntity().id,SignalNodeKind::Automation,"Curve"}};
  g.audio={{g.nodes[0].id,g.nodes[1].id}}; n.signal.library.push_back(g);
  n.automation.push_back({n.makeEntity().id,n.patterns.at(0).id,"saved-offline-instance",1,false,{{0,0.5}}});
  f.doc->restoreNative(n);
  return {{{"kind","parameter"},{"pattern",0},{"plugin","saved-offline-instance"},{"parameter",1}},
    {{"kind","graph"},{"pattern",0},{"graph",nid(g.id)},{"node",nid(g.nodes[2].id)}},
    {{"kind","volume"},{"instrument",nid(n.instruments.at(1).id)}},
    {{"kind","volume"},{"instrument",nid(n.instruments.at(2).id)}}};
}
void linkedUses() {
  Fixture f; auto t=targets(f);
  CHECK(!f.api.invoke("envelope.bank.list",{{"target",t[1]}}).contains("shape"));
  const auto master=f.api.invoke("envelope.bank.save",{{"name","Pluck"},{"target",t[2]}})["id"];
  for(size_t i=0;i<t.size();++i) f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[i]},{"linked",i!=3}});
  CHECK(f.doc->native().envelopeLinks.size()==3); CHECK(!f.doc->native().automation[0].enabled);
  const auto independent=f.api.invoke("envelope.bank.list",{{"target",t[3]}})["shape"];
  auto before=f.doc->native(); auto bytes=f.doc->snapshotData(); auto rev=f.doc->revision,history=f.doc->historyBytes(); auto stops=f.stops;
  CHECK(!f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[2]},{"linked",true}})["wouldChange"].get<bool>());
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==history); CHECK(f.stops==stops);
  Json update={{"id",master},{"name","Inverse"},{"shape",shape(1,0)},{"dryRun",true}};
  CHECK(f.api.invoke("envelope.bank.save",update)["wouldChange"]==true);
  CHECK(f.doc->native()==before); CHECK(f.doc->snapshotData()==bytes); CHECK(f.doc->revision==rev); CHECK(f.stops==stops);
  update.erase("dryRun"); f.api.invoke("envelope.bank.save",update);
  CHECK(f.doc->revision==rev+1); CHECK(f.stops==stops+1); CHECK(f.doc->native().automation[0].points[0].value==1);
  CHECK(f.doc->native().signal.library[0].nodes[2].envelopes[0].points[0].value==1);
  CHECK(f.doc->song().Instruments[1]->VolEnv[0].value==64);
  CHECK(f.api.invoke("envelope.bank.list",{{"target",t[3]}})["shape"]==independent);
  const auto after=f.doc->native(); const auto afterBytes=f.doc->snapshotData();
  f.doc->undo(); CHECK(f.doc->native()==before); CHECK(f.doc->snapshotData()==bytes);
  f.doc->redo(); CHECK(f.doc->native()==after); CHECK(f.doc->snapshotData()==afterBytes);
  rejected(f,"envelope.bank.remove",{{"id",master}});
  for(size_t i=0;i<3;++i) {
    const auto points=f.api.invoke("envelope.bank.list",{{"target",t[i]}})["shape"];
    f.api.invoke("envelope.bank.unlink",{{"target",t[i]}});
    auto read=f.api.invoke("envelope.bank.list",{{"target",t[i]}}); CHECK(read["shape"]==points); CHECK(read["linkedTemplate"]=="");
  }
  CHECK(f.doc->native().envelopeLinks.empty()); f.api.invoke("envelope.bank.remove",{{"id",master}});
  CHECK(f.doc->native().envelopeBank.empty()); f.doc->undo(); CHECK(f.doc->native().envelopeBank.size()==1);
}
std::filesystem::path testScratch() {
  std::filesystem::path root;
  for(const auto *name:{L"TMPDIR",L"TEMP",L"TMP"}) {
    wchar_t value[32768]{};const auto count=GetEnvironmentVariableW(name,value,32768);CHECK(count>0&&count<32768);
    const auto path=std::filesystem::path(value).lexically_normal();CHECK(path.is_absolute());
    CHECK(path.filename()==L"scratch");CHECK(path.parent_path().filename()==L"cache");CHECK(path.parent_path().parent_path().filename()==L"hermes");
    if(root.empty()) root=path;else CHECK(path==root);
  }
  return root;
}
struct Files {
  std::filesystem::path folder,path;
  Files() {
    folder=testScratch()/(L"envelopes-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    path=folder/L"envelope-catalogue-v1.plist";
  }
  ~Files() { std::error_code e; std::filesystem::remove_all(folder,e); }
};
void expectError(ScreamSeq::EnvelopeOperations &api,const std::string &method,const Json &p,int code=-32602) {
  bool threw=false; try { api.invoke(method,p); } catch(const ScreamSeq::Api::ApiError &e) { CHECK(e.code==code); threw=true; } CHECK(threw);
}
void catalogueCopies() {
  using namespace ScreamSeq::Project;
  Fixture f; Files files;files.path=files.folder/L"envelope-catalogue-v1.json";
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  const auto master=api.invoke("envelope.bank.save",{{"name","Local"},{"shape",shape()}})["id"];
  auto catalogue=api.invoke("envelope.catalogue.list",Json::object()); CHECK(catalogue["revision"]=="catalogue:0"); CHECK(!std::filesystem::exists(files.folder));
  // This scenario qualifies existing binary storage, even at a .json suffix.
  std::filesystem::create_directories(files.folder);writeProjectFile(files.path,encodePlist(catalogue),false);
  catalogue=api.invoke("envelope.catalogue.list",Json::object());
  const auto before=f.doc->native(); const auto revision=f.doc->revision,history=f.doc->historyBytes(); const auto stops=f.stops;
  Json publish={{"template",master},{"expectedCatalogueRevision",catalogue["revision"]},{"dryRun",true}};
  const auto seeded=readProjectBytes(files.path);
  CHECK(api.invoke("envelope.catalogue.publish",publish)["wouldChange"]==true); CHECK(readProjectBytes(files.path)==seeded);
  publish.erase("dryRun"); const auto catalogueID=api.invoke("envelope.catalogue.publish",publish)["id"];
  CHECK(catalogueID.get<std::string>().size()==36); CHECK(f.doc->native()==before); CHECK(f.doc->revision==revision); CHECK(f.doc->historyBytes()==history); CHECK(f.stops==stops);
  auto bytes=readProjectBytes(files.path); auto disk=decodePlist(bytes); CHECK(disk["version"]==1); CHECK(disk["entries"][0]["id"]==catalogueID);
  CHECK(disk["entries"][0]["shape"]["markers"][4]==UINT32_MAX);
  catalogue=api.invoke("envelope.catalogue.list",Json::object()); CHECK(catalogue["revision"]!="catalogue:0"); CHECK(catalogue["entries"][0]["name"]=="Local");
  publish["catalogueID"]=catalogueID; publish["expectedCatalogueRevision"]=catalogue["revision"];
  const auto modified=std::filesystem::last_write_time(files.path);
  CHECK(api.invoke("envelope.catalogue.publish",publish)["wouldChange"]==false);
  CHECK(readProjectBytes(files.path)==bytes); CHECK(std::filesystem::last_write_time(files.path)==modified);
  api.invoke("envelope.bank.save",{{"id",master},{"name","Song only"},{"shape",shape(1,0)}});
  CHECK(readProjectBytes(files.path)==bytes);
  Json import={{"catalogueID",catalogueID},{"expectedCatalogueRevision",catalogue["revision"]}};
  import["dryRun"]=true; auto dry=api.invoke("envelope.catalogue.import",import); CHECK(f.doc->native().envelopeBank.size()==1); import.erase("dryRun");
  auto imported=api.invoke("envelope.catalogue.import",import); CHECK(imported["id"]==dry["id"]); CHECK(imported["id"]!=master); CHECK(f.doc->native().envelopeBank.size()==2);
  CHECK(f.doc->native().envelopeBank.back().name=="Local"); f.doc->undo(); CHECK(f.doc->native().envelopeBank.size()==1); f.doc->redo(); CHECK(f.doc->native().envelopeBank.size()==2);
  const auto copied=f.doc->native().envelopeBank.back();
  CHECK(api.invoke("envelope.catalogue.publish",publish)["wouldChange"]==true);
  CHECK(f.doc->native().envelopeBank.back()==copied); CHECK(api.invoke("envelope.catalogue.list",Json::object())["entries"][0]["name"]=="Song only");
  bytes=readProjectBytes(files.path); expectError(api,"envelope.catalogue.publish",publish); expectError(api,"envelope.catalogue.import",import);
  CHECK(readProjectBytes(files.path)==bytes); CHECK(f.doc->native().envelopeBank.back()==copied);
  // Unknown typed root fields survive an ordinary explicit publication.
  disk=decodePlist(bytes); disk["futureData"]=Json::binary({0,1,0,255});
  disk["futureDate"]=Json::binary({0,0,0,0,0,0,0,0},uint64_t(OpaqueType::Date));
  disk["futureUID"]=Json::binary({0,0,255,1},uint64_t(OpaqueType::UID));
  writeProjectFile(files.path,encodePlist(disk),true);
  publish["expectedCatalogueRevision"]=api.invoke("envelope.catalogue.list",Json::object())["revision"]; publish["name"]="Published";
  api.invoke("envelope.catalogue.publish",publish); const auto preserved=decodePlist(readProjectBytes(files.path));
  CHECK(preserved["futureData"]==disk["futureData"]); CHECK(!preserved["futureData"].get_binary().has_subtype());
  CHECK(preserved["futureDate"]==disk["futureDate"]); CHECK(preserved["futureDate"].get_binary().has_subtype());
  CHECK(preserved["futureUID"]==disk["futureUID"]); CHECK(preserved["futureUID"].get_binary().has_subtype());
}
void strictShapesAndTargets() {
  Fixture f; auto t=targets(f); const auto master=f.api.invoke("envelope.bank.save",{{"name","Test"},{"shape",shape()}})["id"];
  Json save={{"id",master},{"name","Test"},{"shape",shape()}};
  auto bad=[&](const Json &s){auto p=save;p["shape"]=s;rejected(f,"envelope.bank.save",p);};
  auto s=shape();s["span"]=true;bad(s);s["span"]=0;bad(s);s["span"]=16777217;bad(s);s["span"]=4.5;bad(s);
  s=shape();s["points"]=Json::array();bad(s);s["points"]=Json::object();bad(s);
  s=shape();s["points"][1]["position"]=0;bad(s);s["points"][1]["position"]=16384;bad(s);s["points"][1]["position"]=true;bad(s);
  s=shape();s["points"][0]["value"]=true;bad(s);s["points"][0]["value"]=-0.1;bad(s);s["points"][0]["value"]=std::numeric_limits<double>::infinity();bad(s);
  s=shape();s["instrument"]=1;bad(s);s["instrument"]=true;s["markers"]=Json::array({0,0,0,0,nullptr});bad(s);
  s=shape();s["flags"]=32;bad(s);s["flags"]=1;s["rowsPerBeat"]=0;bad(s);
  s=shape();s["points"][0]["curve"]="scripted";bad(s);s["points"][0]["formula"]="read('file')";bad(s);
  s["points"][0]["formula"]=std::string(2049,'t');bad(s);
  std::string tooMany="t";for(int i=0;i<65;++i)tooMany+="+t";s["points"][0]["formula"]=tooMany;bad(s);
  s=shape();s["unknown"]=1;bad(s);s=shape();s["points"][0]["unknown"]=1;bad(s);
  auto p=save;p["name"]="";rejected(f,"envelope.bank.save",p);p=save;p["target"]=t[0];rejected(f,"envelope.bank.save",p);
  p=save;p["name"]=std::string("A\0B",3);rejected(f,"envelope.bank.save",p);p["name"]=std::string("\xc0\x80",2);rejected(f,"envelope.bank.save",p);
  for(const auto &id:{"n0","n01","n-1","1","n999999999999999999999999999999999"}) { p=save;p["id"]=id;rejected(f,"envelope.bank.save",p); }
  Json apply={{"template",master},{"target",t[0]},{"linked",false}};
  p=apply;p.erase("linked");rejected(f,"envelope.bank.apply",p);p=apply;p["linked"]=1;rejected(f,"envelope.bank.apply",p);
  p=apply;p["span"]=10;rejected(f,"envelope.bank.apply",p);p=apply;p["target"]["pattern"]=true;rejected(f,"envelope.bank.apply",p);
  p=apply;p["target"]["plugin"]="";rejected(f,"envelope.bank.apply",p);p=apply;p["target"]["parameter"]=uint64_t(UINT32_MAX)+1;rejected(f,"envelope.bank.apply",p);
  p=apply;p["target"]["kind"]="other";rejected(f,"envelope.bank.apply",p);p=apply;p["target"]=t[1];p["target"]["node"]="n1";rejected(f,"envelope.bank.apply",p);
  p=apply;p["target"]=t[2];p["target"]["instrument"]=1;rejected(f,"envelope.bank.apply",p);
  p=apply;p["dryRun"]=0;rejected(f,"envelope.bank.apply",p);p=apply;p["expectedRevision"]="unremoved";rejected(f,"envelope.bank.apply",p);
  rejected(f,"envelope.unknown",Json::object(),-32601);rejected(f,"envelope.catalogue.list",Json::object());
  // Colliding fit positions reject rather than merging corners.
  s={{"span",16777216},{"points",Json::array({{{"position",0},{"value",0}},{{"position",1},{"value",1}}})}};
  const auto collision=f.api.invoke("envelope.bank.save",{{"name","Collision"},{"shape",s}})["id"];
  p=apply;p["template"]=collision;rejected(f,"envelope.bank.apply",p);
  // Removing an independent entry and unlinking an independent target respect dryRun/redo.
  const auto rev=f.doc->revision,history=f.doc->historyBytes();const auto before=f.doc->native();
  CHECK(!f.api.invoke("envelope.bank.unlink",{{"target",t[0]}})["wouldChange"].get<bool>());
  CHECK(f.api.invoke("envelope.bank.remove",{{"id",collision},{"dryRun",true}})["wouldChange"]==true);
  CHECK(f.doc->native()==before);CHECK(f.doc->revision==rev);CHECK(f.doc->historyBytes()==history);
}
void parameterHooks() {
  Fixture f; auto t=targets(f);const auto master=f.api.invoke("envelope.bank.save",{{"name","Curve"},{"shape",shape()}})["id"];
  auto target=t[0];target["plugin"]="real-instance";target["parameter"]=7;
  Json apply={{"template",master},{"target",target},{"linked",true}};
  rejected(f,"envelope.bank.apply",apply);
  unsigned resolutions=0,conflicts=0;bool available=false,conflict=false;
  ScreamSeq::EnvelopeHostHooks hooks;
  hooks.parameterAvailable=[&](const std::string &plugin,uint32_t parameter){++resolutions;CHECK(plugin=="real-instance");CHECK(parameter==7);return available;};
  hooks.parameterAutomationConflicts=[&](const std::string &,uint32_t){++conflicts;return conflict;};
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},hooks);
  const auto native=f.doc->native();const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  auto performance=native;const auto track=performance.tracks.at(0).id;
  performance.performance.columns[track]=1;performance.performance.bindings[1]={"real-instance",7,"Parameter"};
  performance.performance.commands.push_back({performance.patterns.at(0).id,track,0,0,0,PatternCommandKind::ParameterSet,1,0.5});
  f.doc->restoreNative(performance);expectError(api,"envelope.bank.apply",apply);CHECK(resolutions==0&&conflicts==0);
  f.doc->restoreNative(native);
  auto partial=hooks;partial.parameterAutomationConflicts={};ScreamSeq::EnvelopeOperations noConflicts(*f.doc,{},partial);
  expectError(noConflicts,"envelope.bank.apply",apply);CHECK(resolutions==0);
  expectError(api,"envelope.bank.apply",apply); CHECK(resolutions==1&&conflicts==0);
  available=true;conflict=true;expectError(api,"envelope.bank.apply",apply);CHECK(resolutions==2&&conflicts==1);
  conflict=false;auto invalid=apply;invalid["span"]=5;expectError(api,"envelope.bank.apply",invalid);CHECK(resolutions==2);
  CHECK(f.doc->native()==native);CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  apply["dryRun"]=true;CHECK(api.invoke("envelope.bank.apply",apply)["wouldChange"]==true);CHECK(f.doc->native()==native);CHECK(f.stops==stops);
  apply.erase("dryRun");api.invoke("envelope.bank.apply",apply);
  CHECK(f.doc->native().automation.size()==2);CHECK(f.doc->native().automation.back().parameter==7);
  const auto lane=f.doc->native().automation.back().id;CHECK(f.doc->native().envelopeLinks.back().target.owner==lane);
  CHECK(f.doc->native().automation.back().pattern==f.doc->native().patterns.at(0).id);
  // Existing saved lanes are usable offline and never request host resolution.
  CHECK(!f.api.invoke("envelope.bank.apply",apply)["wouldChange"].get<bool>());
  f.api.invoke("envelope.bank.save",{{"id",master},{"name","Changed"},{"shape",shape(1,0)}});
  CHECK(f.doc->native().automation.back().points.front().value==1);
  auto old=f.doc->native().automation.back().points;f.api.invoke("envelope.bank.unlink",{{"target",target}});CHECK(f.doc->native().automation.back().points==old);
}
void instrumentBaking() {
  Fixture f(MOD_TYPE_IT);auto t=targets(f);
  const auto original=f.api.invoke("envelope.bank.list",{{"target",t[2]}})["shape"];
  CHECK(original["instrument"]==true);CHECK(original["markers"][2]==4*256);CHECK(original["markers"][4]==UINT32_MAX);
  const auto master=f.api.invoke("envelope.bank.save",{{"name","Linked"},{"shape",shape()}})["id"];
  f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[0]},{"linked",true}});
  f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[2]},{"linked",true},{"span",256}});
  Json difficult={{"span",256},{"points",Json::array({{{"position",0},{"value",0.5},{"curve","scripted"},{"formula","0.5+0.5*sin(t*1000)"}}})}};
  // A failing instrument conversion must not partially update the parameter use.
  rejected(f,"envelope.bank.save",{{"id",master},{"name","Impossible"},{"shape",difficult}});
  for(const char *kind:{"volume","pan","pitch"}) {
    auto target=t[3];target["kind"]=kind;
    f.api.invoke("envelope.bank.apply",{{"template",master},{"target",target},{"linked",false},{"span",49}});
    const auto *i=f.doc->song().Instruments[2]; const auto &e=std::string(kind)=="volume"?i->VolEnv:std::string(kind)=="pan"?i->PanEnv:i->PitchEnv;
    CHECK(sameInstrumentEnvelope(e,bakeInstrumentEnvelope(f.doc->native().envelopeBank[0].shape,49,f.doc->song().GetModSpecifications().envelopePointsMax)));
  }
  Fixture xm(MOD_TYPE_XM); auto xt=targets(xm);const auto xmaster=xm.api.invoke("envelope.bank.save",{{"name","XM"},{"shape",shape()}})["id"];
  auto pitch=xt[2];pitch["kind"]="pitch";rejected(xm,"envelope.bank.apply",{{"template",xmaster},{"target",pitch},{"linked",false}});
  Json corners={{"span",256},{"points",Json::array()}};
  for(uint32_t i=0;i<=xm.doc->song().GetModSpecifications().envelopePointsMax;++i) corners["points"].push_back({{"position",i},{"value",0.5}});
  auto over=xm.api.invoke("envelope.bank.save",{{"name","Too many"},{"shape",corners}})["id"];
  rejected(xm,"envelope.bank.apply",{{"template",over},{"target",xt[2]},{"linked",false},{"span",256}});
  auto smooth=shape(0,1);smooth["points"][0]["curve"]="smooth";
  const auto curved=f.api.invoke("envelope.bank.save",{{"name","Smooth"},{"shape",smooth}})["id"];
  f.api.invoke("envelope.bank.apply",{{"template",curved},{"target",t[3]},{"linked",false},{"span",49}});
  const auto &e=f.doc->song().Instruments[2]->VolEnv;const auto &source=f.doc->native().envelopeBank.back().shape;
  CHECK(e.size()<=f.doc->song().GetModSpecifications().envelopePointsMax);
  for(uint32_t tick=0;tick<49;++tick) {
    double baked=e.back().value;
    for(size_t i=1;i<e.size();++i) if(tick<=e[i].tick) { baked=e[i-1].value+(e[i].value-double(e[i-1].value))*(tick-e[i-1].tick)/(e[i].tick-e[i-1].tick);break; }
    const auto exact=automationValue(source.points,double(tick)*(source.span-1)/48,source.span,source.rowsPerBeat)*64;
    CHECK(std::abs(baked-exact)<=0.500001);
  }
}
void refitAndIndependentCopies() {
  Fixture f;auto t=targets(f);auto scripted=shape();scripted["points"][0]["curve"]="scripted";scripted["points"][0]["formula"]="start+(end-start)*t";
  const auto master=f.api.invoke("envelope.bank.save",{{"name","Script"},{"shape",scripted}})["id"];
  for(size_t i=0;i<2;++i) f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[i]},{"linked",true}});
  const auto revision=f.doc->revision;
  CHECK(!f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[0]},{"linked",true}})["wouldChange"].get<bool>());CHECK(f.doc->revision==revision);
  CHECK(f.doc->native().automation.front().points[0].formula.source()=="start+(end-start)*t");
  f.doc->transaction([](OpenMPT::CSoundFile &s){s.Patterns[0].Resize(32);CHECK(s.Patterns[0].SetSignature(3,12));});
  auto capture=f.api.invoke("envelope.bank.list",{{"target",t[0]}});CHECK(capture["shape"]["span"]==32*256);CHECK(capture["shape"]["rowsPerBeat"]==3);
  CHECK(f.doc->native().automation[0].points.back().position==32*256-1);
  CHECK(f.doc->native().envelopeLinks[0].span==32*256);CHECK(f.doc->native().envelopeLinks[1].span==32*256);
  const auto sourceLane=f.doc->native().automation[0].id;const auto copy=f.doc->addPattern(32,true,0);
  CHECK(f.doc->native().envelopeLinks.size()==4);CHECK(f.doc->native().automation.back().id!=sourceLane);
  CHECK(f.doc->native().automation.back().pattern==f.doc->native().patterns.at(copy).id);
  auto independent=t[0];independent["pattern"]=copy;
  f.api.invoke("envelope.bank.apply",{{"template",master},{"target",independent},{"linked",false}});
  const auto copyPoints=f.doc->native().automation.back().points;
  CHECK(f.doc->native().envelopeLinks.size()==3);
  f.api.invoke("envelope.bank.save",{{"id",master},{"name","Update master"},{"shape",shape(1,0)}});
  CHECK(f.doc->native().automation.back().points==copyPoints);CHECK(f.doc->native().automation[0].points[0].value==1);
  const auto sourcePattern=f.doc->native().patterns.at(0).id;
  f.doc->transaction([](OpenMPT::CSoundFile &s){s.Patterns.Remove(0);});
  CHECK(f.doc->native().envelopeLinks.size()==1);CHECK(f.doc->native().envelopeLinks[0].target.pattern!=sourcePattern);
  // The bounded song bank rejects a new entry before allocator/history changes.
  auto n=f.doc->native();while(n.envelopeBank.size()<256) { const auto e=n.envelopeBank[0];n.envelopeBank.push_back({n.makeEntity().id,"Full",e.shape}); }
  f.doc->restoreNative(n);rejected(f,"envelope.bank.save",{{"name","Overflow"},{"shape",shape()}});
}
void capacityBeforePlayback() {
  Fixture f; auto t=targets(f); auto n=f.doc->native(); n.signal.library.clear();n.automation.clear();
  EnvelopeShape s;s.points={{0,0.5}};const auto master=n.makeEntity().id;n.envelopeBank.push_back({master,"Capacity",s});
  for(uint16_t graph=0;graph<128;++graph) {
    SignalDefinition g;g.id=n.makeEntity().id;g.number=graph+1;
    g.nodes={{n.makeEntity().id,SignalNodeKind::Input},{n.makeEntity().id,SignalNodeKind::Output}};
    g.audio={{g.nodes[0].id,g.nodes[1].id}};
    for(unsigned node=0;node<32;++node) {
      SignalNode v;v.id=n.makeEntity().id;v.kind=SignalNodeKind::Automation;v.envelopes={{n.patterns.at(0).id,true,s.points}};
      n.envelopeLinks.push_back({{EnvelopeTargetKind::Graph,v.id,n.patterns.at(0).id},master,s.span});g.nodes.push_back(v);
    }
    n.signal.library.push_back(g);
  }
  f.doc->restoreNative(n);CHECK(n.envelopeLinks.size()==4096);
  rejected(f,"envelope.bank.apply",{{"template",nid(master)},{"target",t[2]},{"linked",true}});
}
void instrumentMetadataBudget() {
  Fixture f; const auto t=targets(f);
  const auto master=f.api.invoke("envelope.bank.save",{{"name","Budget"},{"shape",shape()}}).at("id");
  auto n=f.doc->native();
  for(unsigned i=0;i<2;++i) {
    SignalNode v;v.id=n.makeEntity().id;v.kind=SignalNodeKind::Plugin;
    v.plugin.classID="org.resonance.gain";n.signal.library.front().nodes.push_back(std::move(v));
  }
  auto &nodes=n.signal.library.front().nodes;
  nodes[nodes.size()-2].plugin.state.resize(8*1024*1024);
  constexpr size_t limit=16*1024*1024;
  nodes.back().plugin.state.resize(limit-n.bytes()-sizeof(EnvelopeLink)+1);
  CHECK(n.bytes()==limit-sizeof(EnvelopeLink)+1);f.doc->restoreNative(n);
  Json p={{"template",master},{"target",t[2]},{"linked",true}};
  auto dry=p;dry["dryRun"]=true;
  rejected(f,"envelope.bank.apply",dry);rejected(f,"envelope.bank.apply",p);
  // Exactly at the shared budget is valid, including the pending baked link.
  n.signal.library.front().nodes.back().plugin.state.pop_back();f.doc->restoreNative(n);
  const auto before=f.doc->native();const auto revision=f.doc->revision;const auto stops=f.stops;
  CHECK(f.api.invoke("envelope.bank.apply",dry)["wouldChange"]==true);
  CHECK(f.doc->native()==before);CHECK(f.doc->revision==revision);CHECK(f.stops==stops);
  CHECK(f.api.invoke("envelope.bank.apply",p)["wouldChange"]==true);
  CHECK(f.doc->native().bytes()==limit);CHECK(f.stops==stops+1);
  f.doc->native().validate(f.doc->song());f.doc->undo();CHECK(f.doc->native()==before);
  f.doc->redo();CHECK(f.doc->native().bytes()==limit);
}
void catalogueValidation() {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;std::filesystem::create_directories(files.folder);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  const auto master=api.invoke("envelope.bank.save",{{"name","Source"},{"shape",shape()}})["id"];
  writeProjectFile(files.path,encodePlist({{"version",1},{"revision","catalogue:0"},{"entries",Json::array()}}),false);
  const auto c=api.invoke("envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision",api.invoke("envelope.catalogue.list",Json::object())["revision"]}})["id"];
  const auto valid=decodePlist(readProjectBytes(files.path));
  const auto source=f.doc->native();const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  auto rejectRoot=[&](const Json &root) {
    const auto bytes=encodePlist(root);writeProjectFile(files.path,bytes,true);
    expectError(api,"envelope.catalogue.list",Json::object());
    expectError(api,"envelope.catalogue.publish",{{"template",master},{"catalogueID",c},{"expectedCatalogueRevision","catalogue:0"}});
    CHECK(readProjectBytes(files.path)==bytes);CHECK(f.doc->native()==source);CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  };
  auto bad=valid;bad["version"]=true;rejectRoot(bad);bad=valid;bad["version"]=2;rejectRoot(bad);
  bad=valid;bad["entries"][0]["name"]="";rejectRoot(bad);bad=valid;bad["entries"][0]["id"]="";rejectRoot(bad);
  bad=valid;bad["entries"].push_back(bad["entries"][0]);rejectRoot(bad);
  bad=valid;bad["entries"][0]["futureEntryField"]=Json::binary({1,2});rejectRoot(bad);
  bad=valid;bad["entries"][0]["shape"]["futureShapeField"]=1;rejectRoot(bad);
  bad=valid;bad["entries"][0]["shape"]["points"][0]["position"]=true;rejectRoot(bad);
  bad=valid;bad["entries"][0]["shape"]["markers"][4]="unset";rejectRoot(bad);
  bad=valid;bad["entries"][0]["shape"]["points"][0]["curve"]="scripted";rejectRoot(bad);
  // Byte revisions detect a same-size edit even with the original stored token
  // and exact modification time restored by a noncooperating external writer.
  writeProjectFile(files.path,encodePlist(valid),true);
  const auto token=api.invoke("envelope.catalogue.list",Json::object())["revision"];
  const auto time=std::filesystem::last_write_time(files.path);const auto size=std::filesystem::file_size(files.path);
  bad=valid;bad["entries"][0]["name"]="Edited";writeProjectFile(files.path,encodePlist(bad),true);
  CHECK(std::filesystem::file_size(files.path)==size);std::filesystem::last_write_time(files.path,time);
  CHECK(api.invoke("envelope.catalogue.list",Json::object())["revision"]!=token);
  auto changedBytes=readProjectBytes(files.path);
  expectError(api,"envelope.catalogue.publish",{{"template",master},{"catalogueID",c},{"expectedCatalogueRevision",token},{"dryRun",true}});
  expectError(api,"envelope.catalogue.import",{{"catalogueID",c},{"expectedCatalogueRevision",token}});
  CHECK(readProjectBytes(files.path)==changedBytes);
  // Exactly 256 entries can be read, but creating a 257th leaves bytes intact.
  bad=valid;for(unsigned i=1;i<256;++i) { auto entry=bad["entries"][0];entry["id"]="catalogue-fixture-"+std::to_string(i);bad["entries"].push_back(entry); }
  writeProjectFile(files.path,encodePlist(bad),true);auto full=api.invoke("envelope.catalogue.list",Json::object());CHECK(full["entries"].size()==256);
  changedBytes=readProjectBytes(files.path);expectError(api,"envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision",full["revision"]}});CHECK(readProjectBytes(files.path)==changedBytes);
  auto root=bad;root["entries"].push_back(valid["entries"][0]);rejectRoot(root);
  // Corruption fails closed, not interpreted as an empty/new catalogue.
  auto corrupt=encodePlist(valid);corrupt.resize(12);writeProjectFile(files.path,corrupt,true);
  expectError(api,"envelope.catalogue.list",Json::object());CHECK(readProjectBytes(files.path)==corrupt);
  const std::vector<std::byte> oversized(16u*1024u*1024u+1);writeProjectFile(files.path,oversized,true);
  expectError(api,"envelope.catalogue.list",Json::object());CHECK(readProjectBytes(files.path)==oversized);
  // Existing Mac JSON is accepted even under the legacy test .plist suffix.
  const auto legacy=valid.dump();const auto legacyBytes=std::as_bytes(std::span(legacy.data(),legacy.size()));
  writeProjectFile(files.path,legacyBytes,true);CHECK(api.invoke("envelope.catalogue.list",Json::object())["entries"]==valid["entries"]);
  CHECK(readProjectBytes(files.path)==std::vector<std::byte>(legacyBytes.begin(),legacyBytes.end()));
}
std::wstring wideASCII(const std::string &s) { return std::wstring(s.begin(),s.end()); }
std::string ascii(const std::wstring &s) { std::string out;for(auto c:s) { CHECK(c<=127);out+=static_cast<char>(c); } return out; }
void childWriter(const std::filesystem::path &path,const std::string &revision,const std::string &catalogueID,bool busy) {
  Fixture f;ScreamSeq::EnvelopeOperations api(*f.doc,{}, {},path);
  const auto master=api.invoke("envelope.bank.save",{{"name","Child copy"},{"shape",shape(0,1)}})["id"];
  Json p={{"template",master},{"expectedCatalogueRevision",revision},{"catalogueID",catalogueID}};
  if(busy) expectError(api,"envelope.catalogue.publish",p,-32002);
  else CHECK(api.invoke("envelope.catalogue.publish",p)["wouldChange"]==true);
}
void runWriter(const std::filesystem::path &path,const std::string &revision,const std::string &catalogueID,bool busy) {
  wchar_t executable[32768]{};CHECK(GetModuleFileNameW(nullptr,executable,32768)>0);
  auto command=L"\""+std::wstring(executable)+L"\" "+(busy?L"busyWriter":L"writer")+L" \""+path.native()+L"\" "+wideASCII(revision)+L" "+wideASCII(catalogueID);
  STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
  CHECK(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process));
  ScreamSeq::Project::FileDetail::Handle child(process.hProcess),thread(process.hThread);
  const auto wait=WaitForSingleObject(child.value,30000);
  if(wait!=WAIT_OBJECT_0) { TerminateProcess(child.value,1); WaitForSingleObject(child.value,5000); }
  CHECK(wait==WAIT_OBJECT_0);DWORD result=1;CHECK(GetExitCodeProcess(child.value,&result));CHECK(result==0);
}
void catalogueWriters(bool binary=false) {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;files.path=files.folder/L"目録-🎵.plist";
  ScreamSeq::EnvelopeOperations initial(*f.doc,{}, {},files.path);
  const auto master=initial.invoke("envelope.bank.save",{{"name","Parent copy"},{"shape",shape()}})["id"];
  if(binary) {
    std::filesystem::create_directories(files.folder);
    writeProjectFile(files.path,encodePlist({{"version",1},{"revision","catalogue:0"},{"entries",Json::array()}}),false);
  }
  const auto catalogueID=initial.invoke("envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision",initial.invoke("envelope.catalogue.list",Json::object())["revision"]}})["id"].get<std::string>();
  const auto revision=initial.invoke("envelope.catalogue.list",Json::object())["revision"].get<std::string>();
  const auto bytes=readProjectBytes(files.path);const auto before=f.doc->native();unsigned stopped=0;
  // Import retains the catalogue lock through its commit. A real second process
  // tries to publish during the stop callback, not a same-thread recursive lock.
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{
    ++stopped;CHECK(f.doc->native()==before);runWriter(files.path,revision,catalogueID,true);
    runWriter(files.path.parent_path()/L"."/files.path.filename(),revision,catalogueID,true);
    auto folded=files.path.native();for(auto &c:folded) if(c>=L'a'&&c<=L'z') c-=L'a'-L'A';
    runWriter(folded,revision,catalogueID,true);
  },{},files.path);
  api.invoke("envelope.catalogue.import",{{"catalogueID",catalogueID},{"expectedCatalogueRevision",revision}});
  CHECK(stopped==1);CHECK(readProjectBytes(files.path)==bytes);CHECK(f.doc->native().envelopeBank.size()==2);
  runWriter(files.path,revision,catalogueID,false);
  const auto changed=initial.invoke("envelope.catalogue.list",Json::object());CHECK(changed["revision"]!=revision);CHECK(changed["entries"][0]["id"]==catalogueID);CHECK(changed["entries"][0]["name"]=="Child copy");
  CHECK(f.doc->native().envelopeBank.back().name=="Parent copy");
  expectError(initial,"envelope.catalogue.publish",{{"template",master},{"catalogueID",catalogueID},{"expectedCatalogueRevision",revision}});
  const auto latest=readProjectBytes(files.path);
  CHECK((latest.size()>=8&&std::memcmp(latest.data(),"bplist00",8)==0)==binary);
  // Deny replacement after the candidate was serialized; staged file cleanup
  // and original preservation are exercised in the real atomic writer.
  {
    FileDetail::Handle held(CreateFileW(files.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    CHECK(held.value!=INVALID_HANDLE_VALUE);bool threw=false;
    try { initial.invoke("envelope.catalogue.publish",{{"template",master},{"catalogueID",catalogueID},{"expectedCatalogueRevision",changed["revision"]}}); }
    catch(const std::system_error &) { threw=true; } CHECK(threw);
  }
  CHECK(readProjectBytes(files.path)==latest);CHECK(std::distance(std::filesystem::directory_iterator(files.folder),std::filesystem::directory_iterator())==1);
  CHECK(initial.invoke("envelope.catalogue.list",Json::object())==changed);
}
void bankPersistence() {
  using namespace ScreamSeq::Project;
  Fixture f;auto t=targets(f);const std::string title=std::string(100,'T')+" 日本語 🎵";
  f.doc->transaction([&](OpenMPT::CSoundFile &s){s.SetTitle(title);CHECK(s.Patterns.Insert(2,32));s.Patterns[2].GetpModCommand(4,1)->note=61;});
  const auto master=f.api.invoke("envelope.bank.save",{{"name","Persisted"},{"target",t[2]}})["id"];
  t[1]["pattern"]=2;
  for(size_t i=0;i<3;++i) f.api.invoke("envelope.bank.apply",{{"template",master},{"target",t[i]},{"linked",true}});
  const auto metadata=f.doc->native();const auto snapshot=f.doc->snapshotData();
  Files files;std::filesystem::create_directories(files.folder);
  Json wrapper={{"native",encodeNativeMetadata(metadata)},{"song",Json::binary(std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(snapshot.data()),reinterpret_cast<const uint8_t *>(snapshot.data())+snapshot.size()))}};
  writeProjectFile(files.path,encodePlist(wrapper),false);
  const auto reopened=decodePlist(readProjectBytes(files.path));const auto &raw=reopened.at("song").get_binary();CHECK(!raw.has_subtype());
  std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());
  auto loaded=std::make_unique<Document>(bytes);
  CHECK(loaded->song().m_songName==title);CHECK(!loaded->song().Patterns.IsValidPat(1));
  loaded->restoreNative(decodeNativeMetadata(reopened.at("native")));CHECK(loaded->native()==metadata);
  ScreamSeq::EnvelopeOperations loadedAPI(*loaded);
  CHECK(loadedAPI.invoke("envelope.bank.list",Json::object())==f.api.invoke("envelope.bank.list",Json::object()));
  for(const auto &target:t) CHECK(loadedAPI.invoke("envelope.bank.list",{{"target",target}})==f.api.invoke("envelope.bank.list",{{"target",target}}));
  f.api.invoke("envelope.bank.save",{{"id",master},{"name","Updated"},{"shape",shape(1,0)}});
  CHECK(f.doc->song().m_songName==title);CHECK(!f.doc->song().Patterns.IsValidPat(1));
  f.doc->undo();CHECK(f.doc->native()==metadata);CHECK(f.doc->song().m_songName==title);CHECK(!f.doc->song().Patterns.IsValidPat(1));
  f.doc->redo();CHECK(f.doc->song().m_songName==title);CHECK(!f.doc->song().Patterns.IsValidPat(1));
}
#include "JsonCatalogueTests.inc"
int wmain(int argc,wchar_t **argv) {
  try {
    testScratch(); // Every probe, including real child writers, verifies isolation.
    const std::string scenario=argc>1?ascii(argv[1]):"bankHistory";
    if(scenario=="oraclePublish") { CHECK(argc==3);oraclePublish(argv[2]);return 0; }
    if(scenario=="busyWriter"||scenario=="writer") {
      CHECK(argc==5);childWriter(argv[2],ascii(argv[3]),ascii(argv[4]),scenario=="busyWriter");return 0;
    }
    if(scenario=="bankHistory") bankHistory();
    else if(scenario=="linkedUses") linkedUses();
    else if(scenario=="catalogueCopies") catalogueCopies();
    else if(scenario=="strictShapesAndTargets") strictShapesAndTargets();
    else if(scenario=="parameterHooks") parameterHooks();
    else if(scenario=="instrumentBaking") instrumentBaking();
    else if(scenario=="refitAndIndependentCopies") refitAndIndependentCopies();
    else if(scenario=="capacityBeforePlayback") capacityBeforePlayback();
    else if(scenario=="instrumentMetadataBudget") instrumentMetadataBudget();
    else if(scenario=="catalogueValidation") catalogueValidation();
    else if(scenario=="catalogueWriters") catalogueWriters();
    else if(scenario=="plistWriters") catalogueWriters(true);
    else if(scenario=="bankPersistence") bankPersistence();
    else if(scenario=="jsonRead"||scenario=="jsonImport"||scenario=="jsonPublish") jsonCompatibility(scenario);
    else if(scenario=="jsonDefault") jsonDefault();
    else if(scenario=="jsonStrict"||scenario=="jsonLimits") jsonStrict(scenario);
    else if(scenario=="jsonOutputBudget") jsonOutputBudget();
    else if(scenario=="jsonGuards") jsonGuards();
    else if(scenario=="jsonResponseBudget") jsonResponseBudget();
    else throw std::runtime_error("Unknown scenario");
    std::cout<<"PASS "<<scenario<<"\n";
    return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<"\n"; return 1; }
}
