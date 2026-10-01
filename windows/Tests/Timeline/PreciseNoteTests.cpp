#include "windows/Session/TimelineOperations.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "soundlib/NativeNoteEffects.h"
#include "soundlib/mod_specifications.h"
#include "windows/Api/SessionAdapter.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using Json=nlohmann::json;
namespace {
void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
using namespace Tracker;
using namespace OpenMPT;
struct Fixture {
  std::unique_ptr<Document> doc;
  unsigned stops=0;
  ScreamSeq::TimelineOperations api;
  explicit Fixture(MODTYPE type=MOD_TYPE_MPT):doc(Document::demo(type)),api(*doc,[this]{++stops;}){}
  Json get(uint16_t pattern=0){return api.invoke("pattern.notes.get",{{"pattern",pattern}}).at("events");}
  Json set(const Json &events,bool dry=false){return api.invoke("pattern.notes.set",{{"pattern",0},{"events",events},{"dryRun",dry}});}
  void unordered(){
    auto n=doc->native();const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;
    n.preciseNotes={{p,t,65536,1,61,127},{p,t,0,1,62,127}};doc->restoreNative(n);
  }
  void pendingRedo(){
    doc->annotate([](NativeSong &n){n.patterns.at(0).annotation="Pending redo";});doc->undo();
    check(doc->canRedo(),"fixture must have a pending redo");
  }
};
// Explicit test encoding of every stored field; not the project wire format or
// C++ struct padding. Order is significant.
std::vector<uint8_t> eventBytes(const std::vector<PreciseNote> &events){
  std::vector<uint8_t> bytes;
  auto put=[&](uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes.push_back(uint8_t(value>>(i*8)));};
  for(const auto &n:events){put(n.pattern,8);put(n.track,8);put(n.position,4);put(n.instrument,2);
    put(n.note,1);put(n.velocity,1);put(n.effect,1);put(n.parameter,1);}
  return bytes;
}
std::vector<PreciseNote> unrelated(const NativeSong &n,uint64_t target){
  auto result=n.preciseNotes;std::erase_if(result,[&](const auto &e){return e.pattern==target;});return result;
}
void rejected(Fixture &f,const Json &request){
  const auto native=f.doc->native(),redo=f.doc->historyNative(true);
  const auto core=f.doc->snapshotData();
  const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  const auto canUndo=f.doc->canUndo(),canRedo=f.doc->canRedo();bool invalid=false;
  try{f.api.invoke("pattern.notes.set",request);}catch(const ScreamSeq::Api::ApiError &e){invalid=e.code==-32602;}
  check(invalid,"invalid precise-note request must report -32602");
  check(f.doc->native()==native&&f.doc->snapshotData()==core&&f.doc->revision==revision&&f.doc->historyBytes()==history&&f.stops==stops,
    "invalid request preserves all metadata/core, revision, history bytes and playback");
  check(f.doc->canUndo()==canUndo&&f.doc->canRedo()==canRedo&&f.doc->historyNative(true)==redo,
    "invalid request preserves exact pending redo");
}
void noncanonicalRoundTrip(){
  // Generated valid native metadata, not the external Mac reference project.
  auto doc=Tracker::Document::demo();unsigned stops=0;
  ScreamSeq::TimelineOperations api(*doc,[&]{++stops;});
  auto native=doc->native();const auto pattern=native.patterns.at(0).id,track=native.tracks.at(0).id;
  native.preciseNotes={{pattern,track,65536,1,61,127},{pattern,track,0,1,62,127}};
  doc->restoreNative(native); // The real model accepts order-independent events.
  check(doc->native()==native,"restoreNative must not normalize stored event order");
  doc->annotate([](Tracker::NativeSong &n){n.patterns.at(0).annotation="Pending redo";});
  const auto redo=doc->native();doc->undo();
  check(!doc->canUndo()&&doc->canRedo(),"noncanonical fixture has exactly one pending redo");
  const auto revision=doc->revision;
  const auto events=api.invoke("pattern.notes.get",{{"pattern",0}}).at("events");
  check(events[0]["position"]==65536&&events[1]["position"]==0,"get preserves native order");
  const auto result=api.invoke("pattern.notes.set",{{"pattern",0},{"events",events}});
  std::cout<<"noncanonical get/set: wouldChange="<<result.at("wouldChange")
    <<" stops="<<stops<<" revisionUnchanged="<<(doc->revision==revision)
    <<" canUndo="<<doc->canUndo()<<" canRedo="<<doc->canRedo()<<'\n';
  check(result.at("wouldChange")==false,"unchanged noncanonical events must be a no-op");
  check(doc->revision==revision&&stops==0&&doc->native()==native,"no-op preserves revision, playback and all metadata");
  check(!doc->canUndo()&&doc->canRedo()&&doc->historyNative(true)==redo,"no-op preserves exact pending redo without an undo entry");
  doc->redo();check(doc->native()==redo,"pending redo still applies exactly");
  doc->undo();check(doc->native()==native&&!doc->canUndo(),"redo/undo retains noncanonical event bytes and history depth");
}
void pendingRedoAndRequestPermutation(){
  Fixture f;f.unordered();f.pendingRedo();const auto native=f.doc->native(),redo=f.doc->historyNative(true);
  const auto revision=f.doc->revision,history=f.doc->historyBytes();auto events=f.get();
  std::reverse(events.begin(),events.end());
  check(f.set(events).at("wouldChange")==false,"a request permutation is also a no-op");
  check(f.set(events,true).at("wouldChange")==false,"dry no-op reports no change");
  auto changed=events;changed[0]["velocity"]=93;
  check(f.set(changed,true).at("wouldChange")==true,"dry real change is reported");
  check(f.doc->native()==native&&f.doc->revision==revision&&f.doc->historyBytes()==history&&f.stops==0,
    "permuted no-op and both dry-run kinds retain original bytes/revision/history/playback");
  check(!f.doc->canUndo()&&f.doc->canRedo()&&f.doc->historyNative(true)==redo,"dry runs preserve pending redo");
  auto duplicates=events;duplicates.push_back(events[0]);
  rejected(f,{{"pattern",0},{"events",duplicates}});
  auto invalid=events;invalid[0]["velocity"]=0;rejected(f,{{"pattern",0},{"events",invalid},{"dryRun",true}});
  rejected(f,{{"pattern",0},{"events",events},{"clearRows",Json::array({{{"row",64},{"channel",0}}})}});
  check(f.doc->historyBytes()==history,"invalid batches retain pending history byte count");
  f.doc->redo();check(f.doc->native()==redo&&!f.doc->canRedo(),"pending redo remains executable after no-op/dry/invalid");
}
void unrelatedOrderOnReplacement(){
  Fixture f;auto &song=f.doc->song();
  const auto p1=song.Patterns.InsertAny(64),p2=song.Patterns.InsertAny(64);
  check(p1==1&&p2==2,"generated unrelated pattern fixtures allocated");
  auto native=f.doc->native();native.reconcile(song);
  const auto p=native.patterns.at(0).id,a=native.patterns.at(p1).id,b=native.patterns.at(p2).id;
  const auto t=native.tracks.at(0).id,u=native.tracks.at(1).id;
  native.patterns.at(p1).annotation="Preserve identity and all metadata";
  native.preciseNotes={{b,u,65536,2,67,87,CMD_VIBRATO,0x23},{p,t,65536,1,61,127},
    {a,t,131072,1,62,90},{b,t,0,0,255,127},{p,t,0,1,62,127},{a,u,0,3,69,101}};
  f.doc->restoreNative(native);const auto otherBytes=eventBytes(unrelated(native,p));
  const auto aRead=f.get(p1),bRead=f.get(p2),initial=f.get();
  check(f.set(initial).at("wouldChange")==false&&f.doc->native()==native&&f.stops==0,
    "noncanonical unrelated and interleaved target events do not cause no-op writes");
  auto events=initial;events[0]["velocity"]=91;
  check(f.set(events).at("wouldChange")==true&&f.stops==1,"targeted real change stops exactly once");
  check(eventBytes(unrelated(f.doc->native(),p))==otherBytes&&f.get(p1)==aRead&&f.get(p2)==bRead,
    "real target replacement preserves unrelated event order and every field byte");
  auto expected=native;expected.preciseNotes=unrelated(native,p);
  expected.preciseNotes.push_back({p,t,0,1,62,127});expected.preciseNotes.push_back({p,t,65536,1,61,91});
  check(f.doc->native()==expected,"only changed target canonicalized; all IDs and other metadata unchanged");
  f.doc->undo();check(f.doc->native()==native&&!f.doc->canUndo(),"one Undo restores exact original global order");
  f.doc->redo();check(f.doc->native()==expected,"Redo restores target replacement without unrelated normalization");
  check(f.set(Json::array()).at("wouldChange")==true,"empty replacement deletes only target");
  expected.preciseNotes=unrelated(native,p);check(f.doc->native()==expected,"deletion preserves all unrelated fields and order");
  const auto revision=f.doc->revision;check(f.set(Json::array()).at("wouldChange")==false&&f.doc->revision==revision&&f.stops==2,
    "empty target no-op never normalizes other patterns");
}
void everyPayloadField(){
  // Both comparator equality and the real binding must include each payload field.
  const std::vector<std::pair<const char *,int>> changes={{"channel",1},{"position",1234},{"instrument",2},
    {"note",65},{"velocity",73},{"effect",CMD_TREMOLO},{"parameter",0x34}};
  for(const auto &[field,value]:changes){
    Fixture f;auto native=f.doc->native();const auto p=native.patterns.at(0).id,t=native.tracks.at(0).id;
    native.preciseNotes={{p,t,65536,1,62,127},{p,t,0,1,61,93,CMD_VIBRATO,0x24}};f.doc->restoreNative(native);
    auto events=f.get();events[1][field]=value;const auto revision=f.doc->revision;
    check(f.set(events,true).at("wouldChange")==true&&f.doc->native()==native&&f.stops==0,
      "each payload-only change is visible in a nonmutating preview");
    check(f.set(events).at("wouldChange")==true&&f.doc->revision==revision+1&&f.stops==1,
      "each payload field commits exactly one change");
    auto expected=native;auto &n=expected.preciseNotes[1];
    const std::string name=field;
    if(name=="channel")n.track=expected.tracks.at(value).id;
    else if(name=="position")n.position=value;else if(name=="instrument")n.instrument=uint16_t(value);
    else if(name=="note")n.note=uint8_t(value);else if(name=="velocity")n.velocity=uint8_t(value);
    else if(name=="effect")n.effect=uint8_t(value);else n.parameter=uint8_t(value);
    std::reverse(expected.preciseNotes.begin(),expected.preciseNotes.end());
    check(f.doc->native()==expected,"changed payload retained exactly and all other metadata untouched");
    f.doc->undo();check(f.doc->native()==native&&!f.doc->canUndo(),"payload-only Undo restores raw input order");
    f.doc->redo();check(f.doc->native()==expected,"payload-only Redo restores exact replacement");
  }
}
void releasesAndCanonicalOrder(){
  Fixture f;
  Json events=Json::array({{{"channel",0},{"position",65536},{"note",62},{"instrument",1}},
    {{"channel",0},{"position",0},{"note",61},{"instrument",1}},
    {{"channel",0},{"position",65536},{"note",254}},{{"channel",0},{"position",0},{"note",255}}});
  check(f.set(events).at("wouldChange")==true&&f.stops==1,"off and cut accepted through real native validation");
  const auto native=f.doc->native();const auto &notes=native.preciseNotes;
  check(notes.size()==4&&notes[0].note==255&&notes[1].note==61&&notes[2].note==254&&notes[3].note==62,
    "changed target uses same-track release-before-onset order as PreciseNoteRuntime");
  check(notes[0].position==0&&notes[1].position==0&&notes[2].position==65536&&notes[3].position==65536,
    "canonical release pairs retain positions");
  check(notes[0].instrument==0&&notes[0].velocity==127&&notes[2].instrument==0&&notes[2].velocity==127,
    "off/cut default fields preserved");
  const auto history=f.doc->historyBytes(),revision=f.doc->revision;
  check(f.set(events).at("wouldChange")==false&&f.stops==1&&f.doc->historyBytes()==history&&f.doc->revision==revision,
    "unordered same-position release/onset request is a no-op");
  events[3]["note"]=254;check(f.set(events).at("wouldChange")==true,"off-to-cut payload difference is not a no-op");
  check(f.doc->native().preciseNotes[0].note==254,"off-to-cut change retained");
  f.doc->undo();check(f.doc->native()==native&&f.doc->canRedo(),"off-to-cut Undo preserves original notes and pending redo");
  for(const auto &extra:Json::array({
      {{"channel",0},{"position",0},{"note",255}}, // duplicate release (off + cut)
      {{"channel",0},{"position",0},{"note",65}}})){ // duplicate onset (different pitch)
    auto duplicate=events;duplicate.push_back(extra);rejected(f,{{"pattern",0},{"events",duplicate}});
  }
  for(const auto &[field,value]:std::vector<std::pair<const char *,int>>{
      {"instrument",1},{"velocity",126},{"effect",CMD_VIBRATO},{"parameter",1}}){
    auto bad=events;bad[2][field]=value;rejected(f,{{"pattern",0},{"events",bad}});
  }
}
void explicitRowEffectClearing(){
  for(bool clearLegacy:{false,true})for(bool clearEffects:{false,true}){
    Fixture f;f.unordered();
    const Cell row0{61,1,VOLCMD_VOLUME,42,CMD_VIBRATO,0x23};
    const Cell row1{62,2,VOLCMD_PANNING,32,CMD_PANNING8,0x80};
    const Cell row4{65,3,VOLCMD_VOLUME,31,CMD_TREMOLO,0x12};
    Document::put(f.doc->song(),{0,0,0,{},row0});Document::put(f.doc->song(),{0,1,0,{},row1});
    Document::put(f.doc->song(),{0,4,0,{},row4});
    const auto native=f.doc->native();const auto neighbor=f.doc->cell(0,0,1);auto events=f.get();
    auto noSelection=f.api.invoke("pattern.notes.set",{{"pattern",0},{"events",events},{"clearRowEffects",true}});
    check(noSelection.at("wouldChange")==false&&f.stops==0&&f.doc->cell(0,0,0)==row0,
      "clearRowEffects alone does not select cells or normalize unordered events");
    Json request={{"pattern",0},{"events",events},{"clearLegacy",clearLegacy},{"clearRowEffects",clearEffects},
      {"clearRows",Json::array({{{"row",0},{"channel",0}},{{"row",0},{"channel",0}},{{"row",4},{"channel",0}}})}};
    const auto revision=f.doc->revision;request["dryRun"]=true;
    check(f.api.invoke("pattern.notes.set",request).at("wouldChange")==true&&f.stops==0&&f.doc->revision==revision,
      "clear-row preview does not stop or mutate");
    check(f.doc->cell(0,0,0)==row0&&f.doc->cell(0,1,0)==row1&&f.doc->cell(0,4,0)==row4,"clear-row preview preserves all cells");
    request["dryRun"]=false;const auto result=f.api.invoke("pattern.notes.set",request);
    check(result.at("clearedRows")==unsigned(clearLegacy?3:2)&&f.stops==1&&f.doc->revision==revision+1,
      "overlapping clearLegacy/clearRows are deduplicated in one mutation");
    auto cleared=[&](Cell c){c.note=c.instrument=0;if(c.volumeCommand==VOLCMD_VOLUME)c.volumeCommand=c.volume=0;
      if(clearEffects)c.effect=c.parameter=0;return c;};
    check(f.doc->cell(0,0,0)==cleared(row0)&&f.doc->cell(0,4,0)==cleared(row4),"explicit row clearing obeys effect opt-in");
    check(f.doc->cell(0,1,0)==(clearLegacy?cleared(row1):row1),"clearLegacy selects event rows and preserves non-volume commands");
    check(f.doc->native()==native&&f.doc->cell(0,0,1)==neighbor,"cell-only change preserves raw native order and unrelated channel");
    f.doc->undo();check(f.doc->cell(0,0,0)==row0&&f.doc->cell(0,1,0)==row1&&f.doc->cell(0,4,0)==row4&&
      f.doc->native()==native&&!f.doc->canUndo(),"single Undo restores row fields and original event order");
    f.doc->redo();check(f.doc->cell(0,0,0)==cleared(row0)&&f.doc->native()==native,"Redo restores row clear without rewriting native order");
  }
  Fixture f;f.unordered();const auto before=f.doc->cell(0,4,0);
  check(f.api.invoke("pattern.notes.set",{{"pattern",0},{"events",Json::array()},
    {"clearRows",Json::array({{{"row",4},{"channel",0}}})},{"clearRowEffects",true}}).at("wouldChange")==true,
    "clearRows also works while deleting the last precise events");
  check(f.doc->native().preciseNotes.empty()&&f.doc->cell(0,4,0).note==0,"deletion and explicit clear commit together");
  f.doc->undo();check(f.doc->native().preciseNotes.size()==2&&f.doc->cell(0,4,0)==before,"deletion/clear Undo is atomic");
}
void formatSpecificEffects(){
  Json event={{"channel",0},{"position",0},{"note",61},{"instrument",1},{"effect",CMD_S3MCMDEX},{"parameter",0x91}};
  check(NativeNoteEffectSupported(CMD_S3MCMDEX,0x91),"fixture effect supported by precise scheduler semantics");
  for(const auto type:{MOD_TYPE_MOD,MOD_TYPE_XM}){
    Fixture f(type);f.pendingRedo();
    check(!f.doc->song().GetModSpecifications().HasCommand(CMD_S3MCMDEX),"fixture format forbids S3M effect");
    rejected(f,{{"pattern",0},{"events",Json::array({event})}});
    rejected(f,{{"pattern",0},{"events",Json::array({event})},{"dryRun",true}});
    check(f.doc->canRedo()&&f.stops==0,"format-specific rejection retains redo before stop");
  }
  Fixture f;check(f.doc->song().GetModSpecifications().HasCommand(CMD_S3MCMDEX),"MPTM accepts the same effect");
  check(f.set(Json::array({event})).at("wouldChange")==true,"same note-local effect accepted in supporting format");
  auto read=f.get();check(read[0]["effect"]==CMD_S3MCMDEX&&read[0]["parameter"]==0x91,"effect readback is exact");
  f.doc->undo();
  for(const auto &[effect,param]:std::vector<std::pair<int,int>>{{CMD_S3MCMDEX,0x9A},{CMD_TEMPO,125},{CMD_NONE,1}}){
    event["effect"]=effect;event["parameter"]=param;rejected(f,{{"pattern",0},{"events",Json::array({event})}});
  }
}
void selectedSequenceIsolation(){
  Fixture f;auto &song=f.doc->song();const auto secondPattern=song.Patterns.InsertAny(64);
  check(secondPattern==1,"sequence-isolation pattern fixture");
  song.Order(0).assign(1,0);song.Order(0).SetDefaultTempo(TEMPO(124.5));song.Order(0).SetDefaultSpeed(6);
  check(song.Order.AddSequence()==1,"sequence-isolation second sequence fixture");
  song.Order(1).assign(1,secondPattern);song.Order(1).SetDefaultTempo(TEMPO(155.25));song.Order(1).SetDefaultSpeed(4);
  song.Order.SetSequence(1);
  auto native=f.doc->native();native.reconcile(song);
  const auto p=native.patterns.at(0).id,q=native.patterns.at(1).id,t=native.tracks.at(0).id;
  native.preciseNotes={{q,t,65536,2,65,90},{p,t,65536,1,61,127},{q,t,0,2,67,90},{p,t,0,1,62,127}};
  f.doc->restoreNative(native);f.pendingRedo();
  const auto before=songTiming(f.doc->song());const auto redo=f.doc->historyNative(true);
  const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto other=f.get(1);
  auto events=f.get(0);check(events[0]["note"]==61&&other[0]["note"]==65,"explicit pattern reads do not follow selected sequence");
  check(f.set(events).at("wouldChange")==false,"precise no-op while second sequence selected");
  events[0]["velocity"]=93;check(f.set(events,true).at("wouldChange")==true,"precise preview while second sequence selected");
  Json timing={{"tempo",167.375},{"speed",7},{"dryRun",true}};
  check(f.api.invoke("document.timing.get",Json::object()).at("sequence")==1,"timing read addresses selected sequence");
  check(f.api.invoke("document.timing.set",timing).at("wouldChange")==true,"selected-sequence timing preview");
  bool invalid=false;try{f.api.invoke("document.timing.set",{{"tempo",513}});}catch(const ScreamSeq::Api::ApiError &e){invalid=e.code==-32602;}
  check(invalid&&f.doc->revision==revision&&f.doc->historyBytes()==history&&f.doc->native()==native&&f.stops==0,
    "sequence previews/no-op/invalid preserve revision, history, metadata and transport");
  check(f.doc->canRedo()&&f.doc->historyNative(true)==redo&&songTiming(f.doc->song())==before&&
    f.doc->song().Order.GetCurrentSequenceIndex()==1,"sequence previews retain timing, selection and exact pending redo");
  timing["dryRun"]=false;check(f.api.invoke("document.timing.set",timing).at("wouldChange")==true,"selected-sequence tempo edit commits");
  auto expected=before;expected.sequences.at(1)={TEMPO(167.375).GetRaw(),7};
  check(songTiming(f.doc->song())==expected&&f.doc->native()==native&&f.stops==1,"tempo/speed edit isolates other sequence and native event order");
  f.doc->undo();check(songTiming(f.doc->song())==before&&f.doc->song().Order.GetCurrentSequenceIndex()==1,"timing Undo restores exact values and selected sequence");
  f.doc->redo();check(songTiming(f.doc->song())==expected&&f.doc->song().Order.GetCurrentSequenceIndex()==1,"timing Redo retains selected sequence");
  check(f.set(events).at("wouldChange")==true&&f.get(1)==other&&songTiming(f.doc->song())==expected&&
    f.doc->song().Order.GetCurrentSequenceIndex()==1&&f.stops==2,"precise edit addresses requested pattern, not selected sequence pattern");
  f.doc->undo();check(f.doc->native()==native&&f.doc->song().Order.GetCurrentSequenceIndex()==1,"precise Undo preserves selected sequence");
}
void generatedPermutationCorpus(){
  // Exhaustive generated permutations, NOT actual-reference project coverage.
  Fixture f;auto native=f.doc->native();const auto p=native.patterns.at(0).id,t=native.tracks.at(0).id,u=native.tracks.at(1).id;
  native.preciseNotes={{p,t,65536,1,62,93,CMD_VIBRATO,0x24},{p,u,0,2,69,101},
    {p,t,0,1,61,127},{p,t,0,0,255,127}};f.doc->restoreNative(native);f.pendingRedo();
  const auto source=f.get();const auto bytes=eventBytes(native.preciseNotes);const auto history=f.doc->historyBytes();
  const auto revision=f.doc->revision;std::vector<size_t> order={0,1,2,3};unsigned count=0;
  do{
    auto events=Json::array();for(auto i:order)events.push_back(source[i]);
    check(f.set(events).at("wouldChange")==false&&f.set(events,true).at("wouldChange")==false,"each generated permutation is a no-op");
    check(f.doc->native()==native&&eventBytes(f.doc->native().preciseNotes)==bytes&&f.doc->revision==revision&&
      f.doc->historyBytes()==history&&f.doc->canRedo()&&!f.doc->canUndo()&&f.stops==0,"permutation corpus preserves original metadata/history/redo/transport");
    ++count;
  }while(std::next_permutation(order.begin(),order.end()));
  check(count==24,"all four-event permutations were exercised");
  std::cout<<"Generated permutation corpus: "<<count<<" permutations, each real and dry-run; no actual Mac fixture used\n";
}
}
void runPreciseNoteRegressions(){
  for(const auto &[name,run]:std::vector<std::pair<const char *,void(*)()>>{
      {"noncanonical get/set",noncanonicalRoundTrip},{"pending redo and permutation",pendingRedoAndRequestPermutation},
      {"unrelated event bytes/order",unrelatedOrderOnReplacement},{"every payload field",everyPayloadField},
      {"off/cut and release-before-onset",releasesAndCanonicalOrder},{"explicit row effect clearing",explicitRowEffectClearing},
      {"format-specific effect rejection",formatSpecificEffects},{"selected-sequence isolation",selectedSequenceIsolation},
      {"permutation corpus",generatedPermutationCorpus}}){
    run();std::cout<<"PASS generated actual-model regression: "<<name<<'\n';
  }
}
