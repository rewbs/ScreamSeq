// Shared musical cases: no device, UI or platform plugin is constructed.
#include "editor/ArrangementTools.hpp"
#include "editor/EnvelopeBank.hpp"
#include "soundlib/mod_specifications.h"
#ifdef _WIN32
#include "windows/Project/NativeProject.hpp"
#endif
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
using namespace OpenMPT;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,"Expected rejection");}
std::unique_ptr<Document> fixture(uint16_t sourceRows=8,uint16_t targetRows=8) {
  auto d=std::make_unique<Document>(MOD_TYPE_MPT,4);
  d->transaction([&](CSoundFile &s){
    if(s.Patterns[0].GetNumRows()!=sourceRows)check(s.Patterns[0].Resize(sourceRows),"Resize source");
    check(s.Patterns.Insert(1,targetRows),"Create target");s.Order().assign(3,1);s.Order()[0]=0;
    check(s.Order.AddSequence()==1,"Create second sequence");s.Order(1).assign(1,1);s.Order.SetSequence(0);
  });return d;
}
void setCell(Document &d,uint16_t p,uint16_t r,uint16_t c,Cell value){d.edit({{p,r,c,{},value}});}
PreciseNote note(const Document &d,uint16_t p,uint16_t c,uint32_t at,uint8_t pitch,uint8_t velocity=127) {
  return {d.native().patterns.at(p).id,d.native().tracks.at(c).id,at,0,pitch,velocity};
}
const PreciseNote *findNote(const Document &d,uint16_t p,uint16_t c,uint32_t at,bool on=true) {
  for(const auto &n:d.native().preciseNotes)if(n.pattern==d.native().patterns.at(p).id&&n.track==d.native().tracks.at(c).id&&n.position==at&&(n.note>=1&&n.note<=120)==on)return &n;
  return nullptr;
}
const PatternCommand *findFx(const Document &d,uint16_t p,uint16_t c,uint32_t row,uint8_t column) {
  for(const auto &v:d.native().performance.commands)if(v.pattern==d.native().patterns.at(p).id&&v.track==d.native().tracks.at(c).id&&v.position/performanceUnitsPerRow==row&&v.column==column)return &v;
  return nullptr;
}
void roundtrip(Document &d) {
  const auto native=d.native();const auto before=d.snapshotData();
  Document snapshot(before);snapshot.restoreNative(native);check(snapshot.snapshotData()==before&&snapshot.native()==native,"Exact shared snapshot lost content");
#ifdef _WIN32
  auto state=ScreamSeq::Project::newProjectState(d);
  const auto project=ScreamSeq::Project::serializeNativeProject(d,state);
  auto restored=ScreamSeq::Project::openNativeProjectBytes(project);
  check(restored.document->native()==native&&restored.document->snapshotData()==before,"Native project codec lost copied content");
#endif
}
void preciseModes() {
  for(const std::string mode:{"overwrite","merge","mix"}) {
    auto d=fixture();
    d->annotate([&](NativeSong &n){n.preciseNotes={note(*d,0,0,17,60,90),note(*d,0,0,17,254),note(*d,0,0,100,62),
      note(*d,1,1,17,70,80),note(*d,1,1,17,255),note(*d,1,1,50,65),note(*d,1,2,17,75)};});
    const auto before=d->native();const auto bytes=d->snapshotData();const auto revision=d->revision;
    ArrangementCopy copy{0,1,0,1,1,mode,false,false};auto plan=prepareArrangementCopy(*d,copy);
    check(plan.changed()&&plan.edits.empty()&&!plan.clone,"Precise-only source was reported unchanged");
    check(d->native()==before&&d->revision==revision&&d->snapshotData()==bytes,"Preparation changed source");
    applyArrangementCopy(*d,plan);
    check(findNote(*d,1,1,17)->note==(mode=="mix"?70:60),"Precise onset collision policy");
    check(findNote(*d,1,1,17,false)->note==(mode=="mix"?255:254),"On/off partition was conflated");
    check(findNote(*d,1,1,100)&&bool(findNote(*d,1,1,50))==(mode!="overwrite"),"Precise merge range policy");
    check(findNote(*d,1,2,17)->note==75&&findNote(*d,0,0,17)->note==60,"Untouched/source notes changed");
    const auto after=d->native();roundtrip(*d);d->undo();check(d->native()==before&&d->snapshotData()==bytes,"Precise copy Undo");
    d->redo();check(d->native()==after&&d->revision==revision+3,"Precise copy Redo");
  }
}
void fxModes() {
  for(const std::string mode:{"overwrite","merge","mix"})for(const bool sourceNative:{false,true})for(const bool targetNative:{false,true}) {
    auto d=fixture();
    if(!sourceNative)setCell(*d,0,0,0,{0,0,0,0,0,19}); // Parameter-only legacy field is occupied.
    if(!targetNative)setCell(*d,1,0,1,{0,0,0,0,CMD_VIBRATO,37});
    d->annotate([&](NativeSong &n){
      n.performance.bindings[1]={"unresolved-stable-plugin",7,"Parameter"};
      if(sourceNative)n.performance.commands.push_back({n.patterns.at(0).id,n.tracks.at(0).id,17,0,0,PatternCommandKind::ParameterSet,1,.25});
      if(targetNative)n.performance.commands.push_back({n.patterns.at(1).id,n.tracks.at(1).id,31,0,0,PatternCommandKind::ParameterSet,1,.75});
      n.performance.columns[n.tracks.at(0).id]=4;
      n.performance.commands.push_back({n.patterns.at(0).id,n.tracks.at(0).id,performanceUnitsPerRow,0,3,PatternCommandKind::TrackerEffect,0,0,2,CMD_VIBRATO,12});
    });
    ArrangementCopy copy{0,1,0,1,1,mode,false,false};auto plan=prepareArrangementCopy(*d,copy);applyArrangementCopy(*d,plan);
    const bool expectedNative=mode=="mix"?targetNative:sourceNative;const auto fx=findFx(*d,1,1,0,0);
    check(bool(fx)==expectedNative,"Unified primary FX occupancy");
    if(expectedNative)check(fx->value==(mode=="mix"?.75:.25)&&d->cell(1,0,1).effect==0&&d->cell(1,0,1).parameter==0,"Native FX1 left hidden tracker effect");
    else check(d->cell(1,0,1).parameter==(mode=="mix"?37:19),"Tracker FX1 merge/mix was redirected");
    check(findFx(*d,1,1,1,3)&&d->native().performance.columns.at(d->native().tracks.at(1).id)==4,"Occupied extra column was not copied/grown");
    const auto before=d->native();setCell(*d,1,0,1,{0,0,0,0,CMD_PANNING8,32});
    check(!findFx(*d,1,1,0,0)&&findFx(*d,1,1,1,3),"Shared primary invalidation removed extra FX");
    d->undo();check(d->native()==before,"Primary edit Undo did not restore native FX");
  }
}
void overlapAndAliases() {
  for(const bool unique:{false,true}) {
    auto d=fixture();d->transaction([](CSoundFile &s){s.Order().assign(2,0);});
    setCell(*d,0,0,0,{61,1});setCell(*d,0,0,1,{62,2});
    d->annotate([&](NativeSong &n){n.preciseNotes={note(*d,0,0,17,71),note(*d,0,1,17,72)};});
    const auto slot=d->native().sequences[0].orders[1].id,originalID=d->native().patterns.at(0).id;
    ArrangementCopy copy{0,1,0,1,2,"overwrite",unique,false};auto plan=prepareArrangementCopy(*d,copy);applyArrangementCopy(*d,plan);
    const auto target=plan.targetPattern;
    check(plan.clone==unique&&d->cell(target,0,1).note==61&&d->cell(target,0,2).note==62,"Overlapping cells read modified source");
    check(findNote(*d,target,1,17)->note==71&&findNote(*d,target,2,17)->note==72,"Overlapping precise events read modified source");
    check(d->native().sequences[0].orders[1].id==slot&&d->native().patterns.at(0).id==originalID,"Copy changed existing alias identity");
    if(unique)check(d->cell(0,0,1).note==62&&!findNote(*d,0,2,17),"Independent clone changed old alias");
  }
  auto d=fixture();d->transaction([](CSoundFile &s){s.Order().assign(2,1);s.Order()[0]=0;});setCell(*d,0,0,0,{61,1});
  auto plan=prepareArrangementCopy(*d,{0,1,0,0});check(plan.clone,"Inactive-sequence alias was missed");
  applyArrangementCopy(*d,plan);check(d->song().Order(1)[0]==1&&d->cell(1,0,0)==Cell{},"Inactive alias changed");
}
void clippingAndSentinels() {
  auto d=fixture(16,8);
  d->annotate([&](NativeSong &n){
    n.preciseNotes={note(*d,0,0,8*performanceUnitsPerRow-1,60),note(*d,0,0,8*performanceUnitsPerRow,254)};
    n.performance.columns[n.tracks.at(0).id]=2;
    n.performance.commands.push_back({n.patterns.at(0).id,n.tracks.at(0).id,7*performanceUnitsPerRow,3*performanceUnitsPerRow,1,PatternCommandKind::PitchSlide,0,2});
  });
  ArrangementCopy copy{0,1,0,1,1,"overwrite",false,false};rejects([&]{prepareArrangementCopy(*d,copy);});copy.clip=true;
  auto plan=prepareArrangementCopy(*d,copy);applyArrangementCopy(*d,plan);
  check(findNote(*d,1,1,8*performanceUnitsPerRow-1)&&!findNote(*d,1,1,8*performanceUnitsPerRow,false),"Exact-end clipping synthesized/carried an event");
  check(findFx(*d,1,1,7,1)->duration==performanceUnitsPerRow,"Copied slide exceeded overlap");
  auto longer=fixture(8,16);longer->annotate([&](NativeSong &n){n.preciseNotes={note(*longer,1,1,10*performanceUnitsPerRow,70)};});
  auto clipped=prepareArrangementCopy(*longer,copy);check(!clipped.changed(),"Clipped overwrite erased destination tail");
  for(const auto sentinel:{PATTERNINDEX_INVALID,PATTERNINDEX_SKIP}) {
    d->transaction([&](CSoundFile &s){s.Order()[0]=sentinel;});rejects([&]{prepareArrangementCopy(*d,copy);});
  }
}
void noOpAndAdmission() {
  auto d=fixture();setCell(*d,1,0,1,{0,0,0,0,CMD_VIBRATO,37});d->annotate([&](NativeSong &n){
    n.preciseNotes={note(*d,1,1,100,64),note(*d,1,1,17,61)};
    n.performance.commands={{n.patterns.at(1).id,n.tracks.at(1).id,2*performanceUnitsPerRow,0,0,PatternCommandKind::NoteCut},
      {n.patterns.at(1).id,n.tracks.at(1).id,17,0,0,PatternCommandKind::PitchSet,0,1}};
  });
  setCell(*d,0,1,0,{61});d->undo();const auto before=d->native();const auto bytes=d->snapshotData();
  const auto revision=d->revision;const auto history=d->historyBytes();unsigned validations=0;
  auto noop=prepareArrangementCopy(*d,{1,2,1,1},[&](Document &){++validations;});applyArrangementCopy(*d,noop);
  for(const std::string mode:{"overwrite","merge","mix"})for(const uint16_t target:{uint16_t(1),uint16_t(2)}) {
    auto same=prepareArrangementCopy(*d,{1,target,1,1,1,mode},[&](Document &){++validations;});
    check(!same.changed()&&!same.clone,"Exact self/alias span normalized imported FX 1 coexistence");applyArrangementCopy(*d,same);
  }
  rejects([&]{prepareArrangementCopy(*d,{1,2,1,1,1,"invalid"});});
  check(!noop.changed()&&!noop.clone&&!validations&&d->native()==before&&d->snapshotData()==bytes,"Musical no-op reordered or cloned metadata");
  check(d->revision==revision&&d->historyBytes()==history&&d->canRedo(),"No-op consumed revision/history/Redo");
  auto other=fixture();rejects([&]{applyArrangementCopy(*other,noop);});
  d->redo();auto copy=ArrangementCopy{0,1,0,1};const auto native=d->native();const auto original=d->snapshotData();const auto rev=d->revision;const auto hist=d->historyBytes();
  bool completeCandidate=false;
  rejects([&]{prepareArrangementCopy(*d,copy,[&](Document &candidate){
    ++validations;check(candidate.song().Order()[1]!=1&&candidate.cell(candidate.song().Order()[1],1,1).note==61,"Admission missed complete cloned cells");
    check(candidate.native().patterns.size()==native.patterns.size()+1,"Admission missed cloned native identity");
    check(candidate.revision==rev+1&&!findFx(candidate,candidate.song().Order()[1],1,0,0)&&!findFx(candidate,candidate.song().Order()[1],1,2,0),"Admission missed native overwrite result");
    completeCandidate=true;
    throw std::runtime_error("Injected view-cache rejection");
  });});
  check(completeCandidate&&validations==1&&d->native()==native&&d->snapshotData()==original&&d->revision==rev&&d->historyBytes()==hist,"Rejected preparation changed live state or IDs");
  auto changed=prepareArrangementCopy(*d,copy);d->song().Order.SetSequence(1);rejects([&]{applyArrangementCopy(*d,changed);});
  d->song().Order.SetSequence(0);d->annotate([](NativeSong &n){n.tracks.at(0).name="New revision";});rejects([&]{applyArrangementCopy(*d,changed);});
}
void clonePropertiesAndLimits() {
  auto d=fixture(64,64);setCell(*d,0,0,0,{61,1});
  d->transaction([](CSoundFile &s){auto &p=s.Patterns[1];check(p.SetSignature(16,64),"Signature");TempoSwing swing;swing.assign(16,TempoSwing::Unity/4);swing[0]=TempoSwing::Unity*4;p.SetTempoSwing(swing);p.SetName("Exact destination pattern");p.SetColor(0x123456);});
  d->annotate([&](NativeSong &n){
    n.patterns.at(1).name="Native target";n.patterns.at(1).annotation="Retain full clone metadata";
    const auto lane=n.makeEntity().id,bank=n.makeEntity().id;
    n.automation.push_back({lane,n.patterns.at(1).id,"unresolved",7,true,{{0,.25},{256,.75}}});
    EnvelopeShape shape;shape.points={{0,.25},{256,.75}};n.envelopeBank.push_back({bank,"Linked master",shape});
    n.envelopeLinks.push_back({{EnvelopeTargetKind::Parameter,lane,0},bank,64*256});
  });
  const auto before=d->snapshotData();const auto native=d->native();const auto swing=d->song().Patterns[1].GetTempoSwing();
  auto renormalized=swing;renormalized.Normalize();check(renormalized!=swing,"Fixture must detect a second groove normalization");
  auto plan=prepareArrangementCopy(*d,{0,1,0,0});applyArrangementCopy(*d,plan);const auto target=plan.targetPattern;
  const auto &p=d->song().Patterns[target];
  check(p.GetTempoSwing()==swing&&p.GetRowsPerBeat()==16&&p.GetRowsPerMeasure()==64&&p.GetName()==d->song().Patterns[1].GetName()&&p.GetColor()==0x123456,"Independent clone lost exact engine properties");
  check(d->native().patterns.at(target).annotation==native.patterns.at(1).annotation&&d->native().automation.size()==2&&d->native().envelopeLinks.size()==2&&d->native().envelopeBank==native.envelopeBank,"Independent clone lost native metadata/links");
  roundtrip(*d);const auto after=d->native();d->undo();auto undone=native;undone.nextID=after.nextID;
  check(d->snapshotData()==before&&d->native()==undone,"One Undo did not restore full clone");d->redo();check(d->native()==after,"Redo lost clone identity");
  auto full=fixture(64,64);setCell(*full,0,0,0,{61,1});
  full->annotate([&](NativeSong &n){for(uint32_t i=0;i<maximumPreciseNotes;++i)n.preciseNotes.push_back(note(*full,1,3,i,60));});
  const auto fullNative=full->native();const auto rev=full->revision;const auto hist=full->historyBytes();
  rejects([&]{prepareArrangementCopy(*full,{0,1,0,0});});
  check(full->native()==fullNative&&full->revision==rev&&full->historyBytes()==hist,"Clone capacity failure consumed IDs/history");
}
void renderedNativeCopy() {
  auto d=Document::demo();d->transaction([](CSoundFile &s){
    for(auto &c:s.Patterns[0])c.Clear();check(s.Patterns.Insert(1,64),"Audio target");s.Order().assign(3,1);s.Order()[0]=0;s.m_nTempoMode=TempoMode::Modern;
    for(const auto p:{0,1}){check(s.Patterns[p].SetSignature(16,64),"Audio signature");TempoSwing swing;swing.assign(16,TempoSwing::Unity/4);swing[0]=TempoSwing::Unity*4;s.Patterns[p].SetTempoSwing(swing);}
  });
  d->annotate([&](NativeSong &n){auto on=note(*d,0,0,17,61,100);on.instrument=1;n.preciseNotes={on,note(*d,0,0,2*performanceUnitsPerRow,255)};n.performance.columns[n.tracks.at(0).id]=2;n.performance.commands={{n.patterns.at(0).id,n.tracks.at(0).id,performanceUnitsPerRow,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_PANNING8,32}};});
  auto plan=prepareArrangementCopy(*d,{0,1,0,0});check(plan.changed()&&plan.edits.empty(),"Audio native-only copy skipped");applyArrangementCopy(*d,plan);
  check(d->song().Patterns[plan.targetPattern].GetTempoSwing()==d->song().Patterns[0].GetTempoSwing(),"Audio clone changed exact groove");
  auto withoutFx=d->native();withoutFx.performance.commands.clear();
  std::vector<float> reference;
  for(const uint32_t block:{128u,511u}) {
    auto render=[&](uint16_t p,const NativeSong &native){Renderer r(d->snapshotData(),48000,0,false,{},0,{p,0,32,0,false},&native);r.preparePreciseNotes(native);std::vector<float> pcm(48000);
      for(uint32_t at=0;at<24000;at+=block){const auto count=std::min(block,24000-at);check(r.render(pcm.data()+at*2,count)==count,"Render short read");}check(!r.faulted(),"Renderer fault");return pcm;};
    const auto source=render(0,d->native()),target=render(plan.targetPattern,d->native()),control=render(0,withoutFx);
    float peak=0,fxDifference=0,maxDifference=0;for(size_t i=0;i<source.size();++i){check(std::isfinite(source[i])&&std::isfinite(control[i]),"Nonfinite audio");peak=std::max(peak,std::abs(source[i]));fxDifference=std::max(fxDifference,std::abs(source[i]-control[i]));maxDifference=std::max(maxDifference,std::abs(source[i]-target[i]));}
    std::cout<<"Matrix native PCM sampleRate=48000 frames=24000 buffer="<<block<<" exactEqual="<<(source==target)<<" peak="<<peak<<" maxDifference="<<maxDifference<<" fxDifference="<<fxDifference<<'\n';
    check(source==target&&peak>.001f,"Native-only copied block differs audibly");
    check(fxDifference>.001f,"Copied extra FX did not affect rendered audio");
    if(reference.empty())reference=source;else check(source==reference,"Native copy audio changed with callback partition");
  }
}
void densitySummaries() {
  auto d=fixture(32,64);d->transaction([](CSoundFile &s){
    s.Order().assign(6,1);s.Order()[0]=s.Order()[2]=0;s.Order()[3]=PATTERNINDEX_INVALID;s.Order()[4]=PATTERNINDEX_SKIP;
  });
  setCell(*d,0,0,0,{61,1});setCell(*d,0,31,0,{0,2});setCell(*d,0,1,1,{0,0,0,0,CMD_VIBRATO,32});
  d->annotate([&](NativeSong &n){
    n.preciseNotes={note(*d,0,0,0,60),note(*d,0,0,2*performanceUnitsPerRow-1,254),
      note(*d,0,0,2*performanceUnitsPerRow,62),note(*d,0,0,32*performanceUnitsPerRow-1,255),note(*d,0,2,17,72)};
    n.performance.bindings[1]={"unresolved-stable-plugin",7,"Parameter"};n.performance.columns[n.tracks.at(1).id]=4;n.performance.columns[n.tracks.at(2).id]=2;
    n.performance.commands={
      {n.patterns.at(0).id,n.tracks.at(1).id,performanceUnitsPerRow,0,0,PatternCommandKind::ParameterSet,1,.25},
      {n.patterns.at(0).id,n.tracks.at(1).id,2*performanceUnitsPerRow,0,3,PatternCommandKind::TrackerEffect,0,0,2,CMD_VIBRATO,12},
      {n.patterns.at(1).id,n.tracks.at(2).id,0,0,1,PatternCommandKind::NoteCut}};
  });
  setCell(*d,1,7,3,{63});d->undo();const auto native=d->native();const auto bytes=d->snapshotData();
  const auto revision=d->revision;const auto history=d->historyBytes();
  const auto summary=summarizeArrangement(*d);
  check(summary.sequence==0&&summary.totalOrders==6&&summary.totalChannels==4&&summary.channelCount==4,"Default matrix page totals");
  check(summary.orders.size()==6&&summary.patterns.size()==2,"Repeated orders duplicated pattern summaries");
  check(summary.orders[0].summaryIndex==summary.orders[2].summaryIndex&&summary.orders[1].summaryIndex==summary.orders[5].summaryIndex,"Repeated occurrence lost summary identity");
  check(summary.orders[3].pattern==PATTERNINDEX_INVALID&&summary.orders[4].pattern==PATTERNINDEX_SKIP&&
    summary.orders[3].summaryIndex==noArrangementPatternSummary&&summary.orders[4].summaryIndex==noArrangementPatternSummary,"Sentinels received matrix blocks");
  const auto sentinels=summarizeArrangement(*d,{3,2,0,1});
  check(sentinels.orders.size()==2&&sentinels.patterns.empty(),"Sentinel-only page exposed unrelated patterns");
  const auto &pattern=summary.patterns[summary.orders[0].summaryIndex];const auto &mixed=pattern.blocks[0];
  check(pattern.pattern==0&&pattern.patternID==native.patterns.at(0).id&&pattern.rows==32,"Pattern summary identity/length");
  check(mixed.trackID==native.tracks.at(0).id&&mixed.events==6&&mixed.notes==3&&mixed.trackerEvents==2&&mixed.preciseEvents==4&&mixed.nativeFxEvents==0,"Precise on/off and tracker counts");
  std::array<uint32_t,16> expected{};expected[0]=3;expected[1]=1;expected[15]=2;
  check(mixed.bins==expected,"Precise positions crossed the wrong density boundary");
  const auto &fx=pattern.blocks[1];expected={};expected[0]=2;expected[1]=1;
  check(fx.events==3&&fx.notes==0&&fx.trackerEvents==1&&fx.nativeFxEvents==2&&fx.bins==expected,"Native FX1 and tracker effect coexistence was collapsed");
  const auto &preciseOnly=pattern.blocks[2];
  check(preciseOnly.events==1&&preciseOnly.notes==1&&preciseOnly.preciseEvents==1&&!preciseOnly.trackerEvents&&!preciseOnly.nativeFxEvents,"Precise-only block appeared empty");
  const auto &fxOnly=summary.patterns[summary.orders[1].summaryIndex].blocks[2];
  check(fxOnly.events==1&&fxOnly.nativeFxEvents==1&&!fxOnly.notes&&!fxOnly.trackerEvents&&!fxOnly.preciseEvents,"FX-only block appeared empty");
  const auto narrow=summarizeArrangement(*d,{2,1,1,2});
  check(narrow.orders.size()==1&&narrow.orders[0].order==2&&narrow.patterns.size()==1&&narrow.patterns[0].blocks.size()==2&&
    narrow.patterns[0].blocks[0].channel==1&&narrow.patterns[0].blocks[0].events==3&&narrow.patterns[0].blocks[1].events==1,"Paged tracks/patterns leaked or lost native events");
  check(d->native()==native&&d->snapshotData()==bytes&&d->revision==revision&&d->historyBytes()==history&&d->canRedo(),"Matrix query consumed state, IDs, history or Redo");
  d->song().Order.SetSequence(1);const auto inactive=summarizeArrangement(*d);
  check(inactive.sequence==1&&inactive.totalOrders==1&&inactive.patterns.size()==1&&inactive.patterns[0].pattern==1&&inactive.patterns[0].blocks[2].events==1,"Matrix read the wrong sequence");
  check(d->song().Order.GetCurrentSequenceIndex()==1&&d->revision==revision&&d->historyBytes()==history&&d->canRedo(),"Matrix changed selected sequence or history");
}
void densityBounds() {
  Document d(MOD_TYPE_MPT,40);d.transaction([](CSoundFile &s){s.Order().assign(130,0);});
  const auto defaults=summarizeArrangement(d);
  check(defaults.orders.size()==64&&defaults.channelCount==16&&defaults.patterns.size()==1,"Mac default matrix page changed");
  const auto maximum=summarizeArrangement(d,{0,128,8,32});
  check(maximum.orders.size()==128&&maximum.patterns.size()==1&&maximum.patterns[0].blocks.size()==32,"Maximum matrix page exceeded fixed bounds");
  const auto tail=summarizeArrangement(d,{129,128,35});
  check(tail.orders.size()==1&&tail.orders[0].order==129&&tail.channelCount==5,"Tail/default channel page was not clipped");
  const auto end=summarizeArrangement(d,{130,64,39});
  check(end.orders.empty()&&end.patterns.empty()&&end.channelCount==1&&end.totalOrders==130,"Order-at-size should be a valid empty page");
  for(const auto &range:std::vector<ArrangementMatrixRange>{{131,1,0},{0,0,0},{0,129,0},{0,UINT32_MAX,0},
      {0,1,40},{0,1,0,0},{0,1,0,33},{0,1,39,2},{0,1,0,UINT32_MAX},{130,0,0},{130,1,40},{130,1,0,0}})
    rejects([&]{summarizeArrangement(d,range);});
  auto dense=fixture(64,64);setCell(*dense,0,0,0,{60,1});dense->annotate([&](NativeSong &n){
    for(uint32_t i=0;i<maximumPreciseNotes;++i)n.preciseNotes.push_back(note(*dense,0,0,i,60));
    n.performance.columns[n.tracks.at(0).id]=2;
    n.performance.commands={{n.patterns.at(0).id,n.tracks.at(0).id,17,0,0,PatternCommandKind::PitchSet,0,1},
      {n.patterns.at(0).id,n.tracks.at(0).id,0,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_PANNING8,32}};
  });
  const auto result=summarizeArrangement(*dense,{0,1,0,1});const auto &block=result.patterns[0].blocks[0];
  std::array<uint32_t,16> expected{};expected[0]=65539;
  check(block.preciseEvents==65536&&block.events==65539&&block.notes==65537&&block.trackerEvents==1&&block.nativeFxEvents==2&&block.bins==expected,"Dense native events overflowed/truncated a uint16 count");
}
}
int main(){unsigned failed=0;for(const auto &[name,test]:std::vector<std::pair<const char*,void(*)()>>{{"preciseModes",preciseModes},{"fxModes",fxModes},{"overlapAndAliases",overlapAndAliases},{"clippingAndSentinels",clippingAndSentinels},{"noOpAndAdmission",noOpAndAdmission},{"clonePropertiesAndLimits",clonePropertiesAndLimits},{"renderedNativeCopy",renderedNativeCopy},{"densitySummaries",densitySummaries},{"densityBounds",densityBounds}}){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return failed?1:0;}
