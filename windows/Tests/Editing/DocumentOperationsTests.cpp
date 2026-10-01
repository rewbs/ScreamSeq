#include "windows/Session/DocumentOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "soundlib/mod_specifications.h"
#ifdef small
#undef small
#endif
#include <iostream>
#include <stdexcept>
using namespace Tracker;
using ScreamSeq::DocumentOperations;
using ScreamSeq::Json;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__) + ": " + #x); } while(false)
struct Fixture {
  std::unique_ptr<Document> doc;
  unsigned stops = 0, publications = 0;
  std::vector<Edit> published;
  DocumentOperations api{*doc,[this]{++stops;},[this](const auto &edits){
    ++publications; published=edits;
    for(const auto &e:edits) CHECK(doc->cell(e.pattern,e.row,e.channel)==e.after);
  }};
  explicit Fixture(MODTYPE type=MOD_TYPE_MPT) : doc(std::make_unique<Document>(type,4)) {}
};
Json patch(int row=0,int note=49) {
  return {{"cells",Json::array({{{"pattern",0},{"row",row},{"channel",0},{"note",note}}})}};
}
void patternApply() {
  Fixture f;
  const auto initial=f.doc->cell(0,0,0);
  auto request=patch(); request["cells"][0]["effect"]=CMD_VIBRATO; request["cells"][0]["parameter"]=0x37;
  const auto result=f.api.invoke("pattern.apply",request);
  CHECK(result.at("changedCells")==1); CHECK(result.at("dryRun")==false);
  CHECK(result.at("changes")[0].at("before").at("note")==initial.note);
  CHECK(result.at("changes")[0].at("after").at("parameter")==0x37);
  CHECK(f.doc->cell(0,0,0).note==49); CHECK(f.doc->canUndo()); CHECK(f.doc->revision==1);
  CHECK(f.stops==0); CHECK(f.publications==1); CHECK(f.published.size()==1);
  CHECK(f.published[0].before==initial);
  f.api.invoke("pattern.apply",patch(0,51));
  CHECK(f.doc->cell(0,0,0).effect==CMD_VIBRATO); CHECK(f.doc->cell(0,0,0).parameter==0x37);
  CHECK(f.doc->cell(0,1,0)==Cell{});
}
void rejected(Fixture &f,const std::string &method,const Json &p,int code=-32602) {
  const auto bytes=f.doc->snapshotData(); const auto native=f.doc->native();
  const auto revision=f.doc->revision, history=f.doc->historyBytes();
  const auto undo=f.doc->canUndo(), redo=f.doc->canRedo();
  const auto sequence=f.doc->song().Order.GetCurrentSequenceIndex();
  const auto stops=f.stops, publications=f.publications;
  bool threw=false;
  try { f.api.invoke(method,p); }
  catch(const ScreamSeq::Api::ApiError &e) { CHECK(e.code==code); threw=true; }
  CHECK(threw); CHECK(f.doc->snapshotData()==bytes); CHECK(f.doc->native()==native);
  CHECK(f.doc->revision==revision); CHECK(f.doc->historyBytes()==history);
  CHECK(f.doc->canUndo()==undo); CHECK(f.doc->canRedo()==redo);
  CHECK(f.doc->song().Order.GetCurrentSequenceIndex()==sequence);
  CHECK(f.stops==stops); CHECK(f.publications==publications);
}
void historyAndPreview() {
  Fixture f;
  f.api.invoke("history.undo",{{"domain","document"}});
  f.api.invoke("history.redo",{{"domain","document"}});
  CHECK(f.doc->revision==0); CHECK(f.stops==0); CHECK(f.publications==0);
  f.api.invoke("pattern.apply",patch());
  const auto native=f.doc->native();
  CHECK(f.api.invoke("history.undo",{{"domain","document"}})==Json::object());
  CHECK(f.doc->cell(0,0,0)==Cell{}); CHECK(f.doc->canRedo()); CHECK(!f.doc->canUndo());
  CHECK(f.published[0].before.note==49); CHECK(f.published[0].after.note==0);
  auto dry=patch(); dry["dryRun"]=true;
  const auto rev=f.doc->revision, bytes=f.doc->historyBytes(); const auto published=f.publications;
  CHECK(f.api.invoke("pattern.apply",dry).at("changedCells")==1);
  CHECK(f.api.invoke("pattern.apply",patch(0,0)).at("changedCells")==0);
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==bytes); CHECK(f.doc->canRedo());
  CHECK(f.publications==published); CHECK(f.stops==0);
  f.api.invoke("history.redo",{{"domain","document"}});
  CHECK(f.doc->cell(0,0,0).note==49); CHECK(f.doc->native()==native);
  CHECK(f.stops==0); CHECK(f.publications==3);
  rejected(f,"history.undo",{{"domain","plugins"}});
  rejected(f,"history.undo",{{"domain","document"},{"dryRun",true}});
  rejected(f,"history.redo",Json::object());
  rejected(f,"history.undo",{{"domain",true}});
}
void invalidBatches() {
  Fixture f;
  f.api.invoke("pattern.apply",patch(1,53));
  f.api.invoke("history.undo",{{"domain","document"}});
  for(const Json bad: {Json(true),Json(-1),Json(1.5),Json(256),Json(nullptr),Json("49"),Json(UINT64_MAX)}) {
    auto p=patch(); p["cells"][0]["note"]=bad; rejected(f,"pattern.apply",p);
  }
  for(const auto &key:{"pattern","row","channel"}) {
    auto p=patch(); p["cells"][0][key]=true; rejected(f,"pattern.apply",p);
    p["cells"][0][key]=65536; rejected(f,"pattern.apply",p);
    p["cells"][0].erase(key); rejected(f,"pattern.apply",p);
  }
  auto bad=patch(); bad["cells"].push_back(patch(1,130)["cells"][0]); rejected(f,"pattern.apply",bad);
  bad=patch(); bad["cells"].push_back(bad["cells"][0]); rejected(f,"pattern.apply",bad);
  bad=patch(); bad["cells"][0]["before"]={{"note",0}}; rejected(f,"pattern.apply",bad);
  bad=patch(); bad["cells"][0].erase("note"); rejected(f,"pattern.apply",bad);
  bad=patch(); bad["cells"][0]["effect"]=MAX_EFFECTS; rejected(f,"pattern.apply",bad);
  bad=patch(); bad["cells"][0]["volumeCommand"]=VOLCMD_VOLUME; bad["cells"][0]["volume"]=65; rejected(f,"pattern.apply",bad);
  bad=patch(); bad["dryRun"]=1; rejected(f,"pattern.apply",bad);
  rejected(f,"pattern.apply",{{"cells",Json::array()}});
  rejected(f,"pattern.apply",{{"cells",Json::array_t(4097,patch()["cells"][0])}});
  rejected(f,"pattern.apply",Json::array());
  rejected(f,"pattern.apply",{{"expectedRevision","already-validated-by-host"},{"cells",patch()["cells"]}});
  rejected(f,"waveform.get",Json::object(),-32601);
}
void commandCatalogAndFormatLimits() {
  for(const auto format:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    Fixture f(format);
    const auto catalog=f.api.invoke("pattern.commands",Json::object());
    CHECK(catalog.size()==2);
    for(bool volume:{false,true}) {
      const auto source=patternCommands(format,volume);
      const auto &actual=catalog.at(volume ? "volume" : "effect"); CHECK(actual.size()==source.size());
      for(size_t i=0;i<source.size();++i) {
        const auto &c=source[i];
        const Json expected={{"command",c.command},{"parameterMask",c.mask},{"parameterValue",c.value},
          {"suggestedParameter",c.suggested},{"label",c.label},{"name",c.name},
          {"family",c.family},{"description",c.description},{"minimum",c.minimum},{"maximum",c.maximum}};
        CHECK(actual[i]==expected);
      }
    }
    CHECK(f.doc->revision==0); CHECK(!f.doc->canUndo()); CHECK(f.stops==0);
    const auto &spec=f.doc->song().GetModSpecifications();
    for(int note:{0,1,49,120,128,130,int(NOTE_NOTECUT),int(NOTE_KEYOFF),int(NOTE_FADE)}) {
      auto p=patch(0,note); p["dryRun"]=true;
      if(note==0 || spec.HasNote(uint8_t(note))) f.api.invoke("pattern.apply",p);
      else rejected(f,"pattern.apply",p);
    }
    for(int effect=1;effect<MAX_EFFECTS;++effect) {
      auto p=patch(); p["dryRun"]=true; p["cells"][0]["effect"]=effect; p["cells"][0]["parameter"]=0xf3;
      if(spec.HasCommand(EffectCommand(effect))) f.api.invoke("pattern.apply",p);
      else rejected(f,"pattern.apply",p);
    }
    for(int volume=1;volume<MAX_VOLCMDS;++volume) {
      auto p=patch(); p["dryRun"]=true; p["cells"][0]["volumeCommand"]=volume; p["cells"][0]["volume"]=32;
      if(spec.HasVolCommand(VolumeCommand(volume))) f.api.invoke("pattern.apply",p);
      else rejected(f,"pattern.apply",p);
    }
    rejected(f,"pattern.commands",{{"format","IT"}});
    CHECK(f.doc->revision==0); CHECK(!f.doc->canUndo()); CHECK(f.stops==0);
  }
}
void documentPatch() {
  Fixture f;
  f.api.invoke("pattern.apply",patch(3,61));
  const auto native=f.doc->native();
  const auto title=f.doc->song().m_songName;
  const auto tempo=f.doc->song().Order().GetDefaultTempo();
  const Json p={{"title","Café – Étude"},{"tempo",137.5},{"speed",4},{"channels",6}};
  CHECK(f.api.invoke("document.patch",p)==Json::object());
  const auto expected=::OpenMPT::mpt::ToCharset(f.doc->song().GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,std::string("Café – Étude"));
  CHECK(f.doc->song().m_songName==expected);
  CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(137.5));
  CHECK(f.doc->song().Order().GetDefaultSpeed()==4); CHECK(f.doc->song().GetNumChannels()==6);
  CHECK(f.doc->cell(0,3,0).note==61); CHECK(f.doc->cell(0,3,5)==Cell{}); CHECK(f.stops==1);
  CHECK(f.doc->native().patterns==native.patterns);
  f.api.invoke("history.undo",{{"domain","document"}});
  CHECK(f.doc->song().m_songName==title); CHECK(f.doc->song().Order().GetDefaultTempo()==tempo);
  CHECK(f.doc->song().GetNumChannels()==4); CHECK(f.doc->canRedo());
  const auto rev=f.doc->revision, bytes=f.doc->historyBytes(); const auto stops=f.stops;
  f.api.invoke("document.patch",Json::object());
  f.api.invoke("document.patch",{{"channels",4},{"title",title},{"tempo",tempo.ToDouble()}});
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==bytes); CHECK(f.doc->canRedo()); CHECK(f.stops==stops);
  for(const auto &bad:std::vector<Json>{
      {{"title","valid"},{"speed",false}},{{"tempo",31.9}},{{"tempo",513}},{{"tempo",true}},
      {{"speed",1.5}},{{"speed",32}},{{"channels",0}},{{"channels",193}},
      {{"title",std::string(201,'a')}},{{"title",std::string("a\0b",3)}},{{"title",std::string("\xc0\xaf",2)}},
      {{"dryRun",true}},{{"title",nullptr}},{{"title","valid"},{"unknown",0}}}) rejected(f,"document.patch",bad);
  f.api.invoke("history.redo",{{"domain","document"}});
  CHECK(f.doc->song().m_songName==expected); CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(137.5));
  // NSString's contract counts UTF-16 code units, not UTF-8 bytes.
  std::string boundary; for(int i=0;i<100;++i) boundary+="🎵";
  f.api.invoke("document.patch",{{"title",boundary}});
  rejected(f,"document.patch",{{"title",boundary+"🎵"}});
}
void patternCreate() {
  Fixture f;
  f.api.invoke("pattern.apply",patch(3,61));
  f.doc->annotate([](NativeSong &n){n.patterns.at(0).annotation="Original phrase";});
  const auto source=f.doc->native().patterns.at(0);
  const auto orders=f.doc->song().Order().size();
  const auto rev=f.doc->revision;
  const auto result=f.api.invoke("pattern.create",{{"rows",32},{"source",0}});
  CHECK(result==Json({{"pattern",1}}));
  CHECK(f.doc->song().Patterns[1].GetNumRows()==32); CHECK(f.doc->cell(1,3,0).note==61);
  CHECK(f.doc->native().patterns.at(1).id!=source.id);
  CHECK(f.doc->native().patterns.at(1).annotation==source.annotation);
  CHECK(f.doc->native().patterns.at(0)==source);
  CHECK(f.doc->song().Order().size()==orders+1); CHECK(f.doc->song().Order().back()==1);
  CHECK(f.doc->revision==rev+1); CHECK(f.stops==1);
  f.api.invoke("history.undo",{{"domain","document"}});
  CHECK(!f.doc->song().Patterns.IsValidPat(1)); CHECK(f.doc->song().Order().size()==orders);
  f.api.invoke("history.redo",{{"domain","document"}});
  CHECK(f.doc->cell(1,3,0).note==61);
  const auto empty=f.api.invoke("pattern.create",{{"rows",16}});
  CHECK(f.doc->cell(empty.at("pattern").get<int>(),3,0)==Cell{});
  for(const auto &bad:std::vector<Json>{{{"rows",0}},{{"rows",65536}},{{"rows",64},{"source",999}},
      {{"rows",64},{"source",true}},{{"rows",64},{"dryRun",true}},{{"rows",32.5}},Json::object()})
    rejected(f,"pattern.create",bad);
  Fixture mod(MOD_TYPE_MOD);
  rejected(mod,"pattern.create",{{"rows",32}});
  mod.doc->transaction([](CSoundFile &s){s.Order().assign(s.GetModSpecifications().ordersMax,0);});
  rejected(mod,"pattern.create",{{"rows",64}});
  Fixture holes(MOD_TYPE_IT);
  holes.doc->transaction([](CSoundFile &s){
    for(PATTERNINDEX i=1;i<s.GetModSpecifications().patternsMax;++i)
      if(i!=5) CHECK(s.Patterns.Insert(i,64));
  });
  CHECK(holes.api.invoke("pattern.create",{{"rows",64}}).at("pattern")==5);
  rejected(holes,"pattern.create",{{"rows",64}});
}
void orderEdit() {
  Fixture f;
  f.api.invoke("pattern.create",{{"rows",64}});
  auto edit=[&](int order,const char *operation,int pattern=0) {
    return f.api.invoke("order.edit",{{"order",order},{"operation",operation},{"pattern",pattern}});
  };
  const auto original=f.doc->native().sequences[0].orders;
  CHECK(edit(0,"after",1)==Json::object());
  CHECK(f.doc->song().Order().size()==3); CHECK(f.doc->song().Order()[1]==1);
  const auto inserted=f.doc->native().sequences[0].orders[1].id;
  CHECK(inserted!=original[0].id && inserted!=original[1].id);
  edit(1,"up"); CHECK(f.doc->native().sequences[0].orders[0].id==inserted);
  edit(0,"down"); CHECK(f.doc->native().sequences[0].orders[1].id==inserted);
  edit(1,"assign",0); CHECK(f.doc->song().Order()[1]==0); CHECK(f.doc->native().sequences[0].orders[1].id==inserted);
  f.api.invoke("history.undo",{{"domain","document"}});
  const auto rev=f.doc->revision, history=f.doc->historyBytes(); const auto stops=f.stops;
  edit(1,"assign",1);
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==history); CHECK(f.doc->canRedo()); CHECK(f.stops==stops);
  for(const auto &bad:std::vector<Json>{
      {{"order",0},{"operation","up"}},{{"order",2},{"operation","down"}},
      {{"order",3},{"operation","before"}},{{"order",0},{"operation","assign"},{"pattern",999}},
      {{"order",0},{"operation","remove"},{"pattern",false}},{{"order",false},{"operation","remove"}},
      {{"order",0},{"operation","shuffle"}},{{"order",0},{"operation","remove"},{"dryRun",true}}})
    rejected(f,"order.edit",bad);
  f.api.invoke("history.redo",{{"domain","document"}}); CHECK(f.doc->song().Order()[1]==0);
  edit(1,"before",1); CHECK(f.doc->song().Order()[1]==1); CHECK(f.doc->native().sequences[0].orders[2].id==inserted);
  edit(1,"remove"); CHECK(f.doc->native().sequences[0].orders[1].id==inserted);
  edit(1,"remove"); edit(1,"remove"); CHECK(f.doc->song().Order().size()==1);
  rejected(f,"order.edit",{{"order",0},{"operation","remove"}});
  Fixture full(MOD_TYPE_MOD);
  full.doc->transaction([](CSoundFile &s){s.Order().assign(s.GetModSpecifications().ordersMax,0);});
  rejected(full,"order.edit",{{"order",0},{"operation","before"},{"pattern",0}});
}
void sequenceSelect() {
  Fixture f;
  f.doc->transaction([](CSoundFile &s){
    const auto second=s.Order.AddSequence(); CHECK(second==1);
    s.Order(second).assign(1,0); s.Order(second).SetDefaultTempo(TEMPO(155.25));
    s.Order.SetSequence(0);
  });
  f.api.invoke("pattern.apply",patch()); f.api.invoke("history.undo",{{"domain","document"}});
  const auto rev=f.doc->revision, bytes=f.doc->historyBytes();
  const auto native=f.doc->native(); const auto tempo=f.doc->song().Order().GetDefaultTempo();
  CHECK(f.api.invoke("sequence.select",{{"sequence",1}})==Json::object());
  CHECK(f.doc->song().Order.GetCurrentSequenceIndex()==1);
  CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(155.25));
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==bytes); CHECK(f.doc->native()==native);
  CHECK(f.doc->canRedo()); CHECK(f.stops==1);
  f.api.invoke("sequence.select",{{"sequence",1}}); CHECK(f.stops==1);
  for(const auto &bad:std::vector<Json>{{{"sequence",2}},{{"sequence",-1}},{{"sequence",true}},
      {{"sequence",0.5}},{{"sequence",0},{"dryRun",true}},Json::object()}) rejected(f,"sequence.select",bad);
  f.api.invoke("document.patch",{{"tempo",166.75}});
  CHECK(f.doc->song().Order(0).GetDefaultTempo()==tempo);
  f.api.invoke("history.undo",{{"domain","document"}});
  CHECK(f.doc->song().Order.GetCurrentSequenceIndex()==1);
  CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(155.25));
  f.api.invoke("sequence.select",{{"sequence",0}});
  CHECK(f.doc->song().Order().GetDefaultTempo()==tempo);
}
void sampleReads() {
  Fixture f;
  f.doc->transaction([](CSoundFile &s){
    s.m_nSamples=2;
    auto &sample=s.GetSample(1); sample.Initialize(MOD_TYPE_MPT);
    sample.uFlags.set(CHN_16BIT|CHN_STEREO|CHN_LOOP|CHN_SUSTAINLOOP);
    sample.nLength=4; sample.nC5Speed=22050; sample.nVolume=192; sample.nPan=93;
    sample.nLoopStart=0; sample.nLoopEnd=4; sample.nSustainStart=1; sample.nSustainEnd=3;
    sample.nativeReverseLoops=3;
    CHECK(sample.AllocateSample());
    const std::array<int16_t,8> pcm={-32768,8192,-16384,16384,0,-8192,16384,-16384};
    std::copy(pcm.begin(),pcm.end(),sample.sample16());
    sample.PrecomputeLoops(s,false);
    s.m_szNames[1]=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,std::string("Café"));
    s.GetSample(2).Initialize(MOD_TYPE_MPT);
  });
  const auto rev=f.doc->revision, bytes=f.doc->historyBytes(), reads=f.doc->waveformReadFrames();
  const auto result=f.api.invoke("sample.get",{{"sample",1}});
  const Json expected={{"index",1},{"name","Café"},{"frames",4},{"rate",22050},{"volume",48},{"pan",93},
    {"loopStart",0},{"loopEnd",4},{"loop",true},{"pingpong",false},{"sustainStart",1},{"sustainEnd",3},
    {"sustainLoop",true},{"sustainPingpong",false},{"reverseLoop",true},{"sustainReverse",true},{"bits",16},{"channels",2}};
  CHECK(result==expected); CHECK(f.doc->waveformReadFrames()==reads);
  const auto empty=f.api.invoke("sample.get",{{"sample",2}}); CHECK(empty.at("frames")==0); CHECK(empty.at("channels")==1);
  rejected(f,"sample.get",{{"sample",0}}); rejected(f,"sample.get",{{"sample",3}});
  rejected(f,"sample.get",{{"sample",true}}); rejected(f,"sample.get",{{"sample",1},{"includeWaveform",true}});
  auto wave=f.api.invoke("sample.waveform.get",{{"sample",1},{"bins",2}});
  CHECK(wave==Json({{"sample",1},{"start",0},{"end",4},{"bins",2},{"channels","both"},
    {"peaks",Json::array({-1.0,0.5,-0.5,0.5})}}));
  for(const auto &[name,selection]:std::vector<std::pair<std::string,SampleChannels>>{
      {"both",SampleChannels::Both},{"left",SampleChannels::Left},{"right",SampleChannels::Right}}) {
    const auto actual=f.api.invoke("sample.waveform.get",{{"sample",1},{"start",1},{"end",3},{"bins",4},{"channels",name}});
    CHECK(actual.at("peaks")==Json(f.doc->waveform(1,1,3,4,selection)));
  }
  const auto blank=f.api.invoke("sample.waveform.get",{{"sample",1},{"start",2},{"end",2},{"bins",3}});
  CHECK(blank.at("peaks")==Json(std::vector<float>(6,0)));
  CHECK(f.api.invoke("sample.waveform.get",{{"sample",2}}).at("peaks")==Json(std::vector<float>(4096,0)));
  for(const auto &bad:std::vector<Json>{
      {{"sample",1},{"bins",0}},{{"sample",1},{"bins",16385}},{{"sample",1},{"bins",false}},
      {{"sample",1},{"start",4},{"end",3}},{{"sample",1},{"end",5}},{{"sample",1},{"start",1.5}},
      {{"sample",1},{"channels","middle"}},{{"sample",2},{"channels","right"}},{{"sample",1},{"extra",false}}})
    rejected(f,"sample.waveform.get",bad);
  CHECK(f.doc->revision==rev); CHECK(f.doc->historyBytes()==bytes); CHECK(f.stops==0); CHECK(f.publications==0);
}
void structuralFormatsAndInventory() {
  const std::vector<std::string> reads={"pattern.commands","sample.get","sample.waveform.get"};
  const std::vector<std::string> writes={"pattern.apply","history.undo","history.redo","document.patch","pattern.create","order.edit","sequence.select"};
  CHECK(DocumentOperations::reads()==reads); CHECK(DocumentOperations::writes()==writes);
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    Fixture f(type);
    f.api.invoke("document.patch",{{"title","Format test"},{"tempo",156.25},{"channels",6}});
    CHECK(f.doc->song().GetNumChannels()==6); CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(156.25));
    CHECK(f.api.invoke("pattern.create",{{"rows",64},{"source",0}}).at("pattern")==1);
    f.api.invoke("order.edit",{{"order",0},{"operation","before"},{"pattern",1}});
    CHECK(f.doc->song().Order()[0]==1);
    f.api.invoke("history.undo",{{"domain","document"}});
    CHECK(f.doc->song().Order().size()==2);
    f.api.invoke("history.redo",{{"domain","document"}});
    CHECK(f.doc->song().Order().size()==3); CHECK(f.doc->song().GetNumChannels()==6);
    CHECK(f.doc->song().Order().GetDefaultTempo()==TEMPO(156.25));
  }
}
void largeBatchAndNativeValidation() {
  Fixture f;
  const int pattern=f.api.invoke("pattern.create",{{"rows",1024}}).at("pattern");
  Json cells=Json::array();
  for(int row=0;row<1024;++row) for(int channel=0;channel<4;++channel)
    cells.push_back({{"pattern",pattern},{"row",row},{"channel",channel},{"note",49},{"instrument",255}});
  const auto revision=f.doc->revision; const auto stops=f.stops;
  CHECK(f.api.invoke("pattern.apply",{{"cells",cells},{"dryRun",true}}).at("changedCells")==4096);
  CHECK(f.doc->revision==revision); CHECK(f.publications==0);
  CHECK(f.api.invoke("pattern.apply",{{"cells",cells}}).at("changedCells")==4096);
  CHECK(f.doc->revision==revision+1); CHECK(f.published.size()==4096); CHECK(f.stops==stops);
  f.api.invoke("history.undo",{{"domain","document"}});
  for(int row=0;row<1024;++row) for(int channel=0;channel<4;++channel) CHECK(f.doc->cell(pattern,row,channel)==Cell{});
  Fixture capacity;
  capacity.doc->annotate([](NativeSong &n){
    for(uint32_t i=0;i<256;++i) {
      MusicalAutomationLane lane; lane.id=n.makeEntity().id; lane.pattern=n.patterns.at(0).id;
      lane.plugin="preserved-plugin-instance"; lane.parameter=i; lane.points={{0,0.5}};
      n.automation.push_back(std::move(lane));
    }
  });
  rejected(capacity,"pattern.create",{{"rows",64},{"source",0}});
  CHECK(capacity.doc->native().automation.size()==256); CHECK(capacity.stops==0);
}
// Strict regression: exercise both API transactions and the shared core directly.
int snapshotProbe() {
  Json evidence;
  Fixture title;
  title.api.invoke("document.patch",{{"title",std::string(100,'x')}});
  evidence["titleBytesBeforeUndo"]=title.doc->song().m_songName.size();
  title.api.invoke("history.undo",{{"domain","document"}});
  title.api.invoke("history.redo",{{"domain","document"}});
  evidence["titleBytesAfterRedo"]=title.doc->song().m_songName.size();
  Fixture holes(MOD_TYPE_IT);
  holes.doc->transaction([](CSoundFile &s){CHECK(s.Patterns.Insert(2,64));});
  evidence["holeAllocatedBefore"]=holes.doc->song().Patterns.IsValidPat(1);
  holes.api.invoke("pattern.create",{{"rows",64}});
  holes.api.invoke("history.undo",{{"domain","document"}});
  evidence["holeAllocatedAfterUndo"]=holes.doc->song().Patterns.IsValidPat(1);
  try { holes.doc->native().validate(holes.doc->song()); evidence["nativeValidAfterUndo"]=true; }
  catch(const std::exception &e) { evidence["nativeValidAfterUndo"]=false; evidence["nativeError"]=e.what(); }
  // Repeat without DocumentOperations to establish the shared-core boundary.
  auto direct=std::make_unique<Document>(MOD_TYPE_MPT,4);
  direct->transaction([](CSoundFile &s){s.SetTitle(std::string(100,'x'));});
  direct->undo(); direct->redo();
  evidence["coreOnlyTitleBytesAfterRedo"]=direct->song().m_songName.size();
  auto sparse=std::make_unique<Document>(MOD_TYPE_IT,4);
  sparse->transaction([](CSoundFile &s){CHECK(s.Patterns.Insert(2,64));});
  sparse->addPattern(64,false,0); sparse->undo();
  evidence["coreOnlyHoleAllocatedAfterUndo"]=sparse->song().Patterns.IsValidPat(1);
  CHECK(evidence["coreOnlyTitleBytesAfterRedo"]==evidence["titleBytesAfterRedo"]);
  CHECK(evidence["coreOnlyHoleAllocatedAfterUndo"]==evidence["holeAllocatedAfterUndo"]);
  CHECK(evidence["titleBytesAfterRedo"]==100);
  CHECK(evidence["coreOnlyTitleBytesAfterRedo"]==100);
  CHECK(evidence["holeAllocatedAfterUndo"]==false);
  CHECK(evidence["coreOnlyHoleAllocatedAfterUndo"]==false);
  CHECK(evidence["nativeValidAfterUndo"]==true);
  std::cout << evidence.dump(2) << '\n'; return 0;
}
int main(int argc,char **argv) {
  if(argc==2 && std::string(argv[1])=="--snapshot-probe") return snapshotProbe();
  unsigned failed=0;
  for(const auto &[name,test]:std::vector<std::pair<const char *,void(*)()>>{
      {"patternApply",patternApply},{"historyAndPreview",historyAndPreview},{"invalidBatches",invalidBatches},
      {"commandCatalogAndFormatLimits",commandCatalogAndFormatLimits},{"documentPatch",documentPatch},
      {"patternCreate",patternCreate},{"orderEdit",orderEdit},{"sequenceSelect",sequenceSelect},{"sampleReads",sampleReads},
      {"structuralFormatsAndInventory",structuralFormatsAndInventory},{"largeBatchAndNativeValidation",largeBatchAndNativeValidation}}) {
    try { test(); std::cout << "PASS " << name << '\n'; }
    catch(const std::exception &e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
  }
  return failed ? 1 : 0;
}
