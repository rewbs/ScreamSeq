#include "editor/InstrumentEnvelopeTools.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/SampleArchive.hpp"
#include "soundlib/mod_specifications.h"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool caught=false;try { f(); } catch(const std::invalid_argument &) { caught=true; }check(caught,"Invalid envelope edit rejects"); }
template<class F> static void malformed(F f) { bool caught=false;try { f(); } catch(const std::exception &) { caught=true; }check(caught,"Malformed envelope archive rejects"); }
static InstrumentEnvelope fixture() {
  InstrumentEnvelope e;e.push_back(0,64);e.push_back(4,48);e.push_back(8,32);e.push_back(12,0);
  e.dwFlags.set(ENV_ENABLED|ENV_LOOP|ENV_SUSTAIN|ENV_CARRY);e.nLoopStart=e.nSustainStart=1;e.nLoopEnd=e.nSustainEnd=e.nReleaseNode=2;return e;
}
int main(){try{
  const auto original=fixture();AutomationTool t;t.operation="flip-time";t.end=13;
  const auto flipped=transformInstrumentEnvelope(original,25,t);
  check(flipped.envelope[0]==EnvelopeNode(0,0)&&flipped.envelope[1]==EnvelopeNode(4,32)&&flipped.envelope[2]==EnvelopeNode(8,48)&&flipped.envelope[3]==EnvelopeNode(12,64),"Independent reversed vertices");
  check(flipped.envelope.nLoopStart==1&&flipped.envelope.nLoopEnd==2&&flipped.envelope.nSustainStart==1&&flipped.envelope.nSustainEnd==2&&flipped.envelope.nReleaseNode==1&&flipped.envelope.dwFlags==original.dwFlags,"Reversal follows node ownership and orders sustain/loop boundaries");
  check(sameInstrumentEnvelope(transformInstrumentEnvelope(flipped.envelope,25,t).envelope,original),"Time flip reverses exact points and every marker");
  t.operation="flip-values";auto inverted=transformInstrumentEnvelope(original,25,t).envelope;
  check(inverted[0].value==0&&inverted[1].value==16&&inverted[2].value==32&&inverted[3].value==64,"Value inversion uses native 0..64 values");
  t.operation="shift";t.start=4;t.end=9;t.shift=1;auto moved=transformInstrumentEnvelope(original,25,t);
  check(moved.envelope[1].tick==5&&moved.envelope[2].tick==9&&moved.envelope.nReleaseNode==2&&moved.reanchored==0,"Shift keeps anchors attached to their original nodes");
  t.shift=4;rejects([&]{transformInstrumentEnvelope(original,25,t);});
  t.start=0;t.end=13;t.shift=1;rejects([&]{transformInstrumentEnvelope(original,25,t);});
  const auto clip=copyInstrumentEnvelope(original,0,5);check(clip.span==5&&clip.points.size()==2&&clip.points[1].value==.75,"Clipboard preserves exact tick span and normalized values");
  t={};t.operation="insert";t.start=4;t.end=65536;t.clip=clip;t.repeats=2;
  auto inserted=transformInstrumentEnvelope(original,25,t);
  const uint16_t ticks[]{0,4,8,9,13,14,18,22};
  for(size_t i=0;i<8;++i)check(inserted.envelope[i].tick==ticks[i],"Repeated insertion matches independently positioned nodes");
  check(inserted.envelope.nLoopStart==5&&inserted.envelope.nLoopEnd==6&&inserted.envelope.nSustainStart==5&&inserted.envelope.nSustainEnd==6&&inserted.envelope.nReleaseNode==6,"Insert keeps markers on original nodes, not pasted copies");
  t.operation="paste";t.start=8;t.repeats=1;auto pasted=transformInstrumentEnvelope(original,25,t);
  check(pasted.reanchored==3&&pasted.envelope.nReleaseNode==2,"Replaced active anchors reattach and are reported");
  t={};t.operation="ramp";t.end=13;t.from=0;t.to=1;auto ramp=transformInstrumentEnvelope(original,25,t);
  check(ramp.envelope.size()==2&&ramp.envelope[0]==EnvelopeNode(0,0)&&ramp.envelope[1]==EnvelopeNode(12,64)&&ramp.reanchored==5,"Ramp preview reports replaced sustain, loop and release anchors");
  check(ramp.envelope.nLoopStart==0&&ramp.envelope.nLoopEnd==1&&ramp.envelope.nReleaseNode==1,"Replaced markers select nearest generated tick");
  t={};t.operation="sine";t.end=17;t.spacing=4;t.amount=.5;t.offset=.5;t.cycles=1;
  const auto sine=transformInstrumentEnvelope({},12,t);const uint8_t values[]{32,64,32,0,32};
  for(int i=0;i<5;++i)check(sine.envelope[i]==EnvelopeNode(uint16_t(i*4),values[i]),"Empty envelope sine matches quadrature reference");
  check(!sine.envelope.dwFlags[ENV_ENABLED],"Tools never enable an envelope implicitly");
  t={};t.operation="scale";t.end=13;t.amount=2;auto scaled=transformInstrumentEnvelope(original,25,t);
  check(scaled.clipped==2&&scaled.envelope[2].value==64&&scaled.envelope[3].value==0,"Clipped scaling is bounded and counted");
  t.amount=1.01;check(transformInstrumentEnvelope(original,25,t).rounded==2,"Integer quantization is visible in previews");
  t={};t.operation="humanize";t.start=1;t.end=13;t.amount=.125;t.jitter=1;t.seed=123;
  auto human=transformInstrumentEnvelope(original,25,t);check(sameInstrumentEnvelope(human.envelope,transformInstrumentEnvelope(original,25,t).envelope),"Seeded value/tick jitter repeats exactly");
  check(human.envelope[human.envelope.nReleaseNode].tick==human.envelope[2].tick,"Jitter keeps release-node identity");
  t={};t.operation="sine";t.end=65536;t.spacing=1;t.amount=.5;t.offset=.5;rejects([&]{transformInstrumentEnvelope(original,25,t);});
  t.operation="ramp";t.end=13;t.curve=AutomationCurve::Smooth;rejects([&]{transformInstrumentEnvelope(original,25,t);});
  t.curve=AutomationCurve::Linear;rejects([&]{transformInstrumentEnvelope(original,1,t);});
  auto corrupt=original;corrupt[1].tick=0;rejects([&]{transformInstrumentEnvelope(corrupt,25,t);});
  for(auto type:{MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    Document doc(type);doc.transaction([&](CSoundFile &song){song.m_nInstruments=1;song.Instruments[1]=new ModInstrument;
      song.Instruments[1]->VolEnv=original;song.Instruments[1]->PanEnv.push_back(0,64);song.Instruments[1]->PanEnv.push_back(10,64);song.Instruments[1]->PanEnv.dwFlags.set(ENV_ENABLED);
      if(type==MOD_TYPE_XM){auto &e=song.Instruments[1]->VolEnv;e.nSustainEnd=e.nSustainStart;e.nReleaseNode=ENV_RELEASE_NODE_UNSET;e.dwFlags.reset(ENV_CARRY);}});
    auto before=doc.song().Instruments[1]->VolEnv;t={};t.operation="insert";t.start=4;t.end=65536;t.clip=clip;t.repeats=2;
    auto after=transformInstrumentEnvelope(before,doc.song().GetModSpecifications().envelopePointsMax,t).envelope;
    doc.transaction([&](CSoundFile &song){song.Instruments[1]->VolEnv=after;});
    Document reopened(doc.snapshotData());const auto &actual=reopened.song().Instruments[1]->VolEnv;
    if(!sameInstrumentEnvelope(actual,after)) { std::cerr<<"Format "<<int(type)<<" points "<<actual.size()<<'/'<<after.size()<<" flags "<<actual.dwFlags.GetRaw()<<'/'<<after.dwFlags.GetRaw()<<" release "<<int(actual.nReleaseNode)<<'/'<<int(after.nReleaseNode)<<" sustain "<<int(actual.nSustainStart)<<','<<int(actual.nSustainEnd)<<'/'<<int(after.nSustainStart)<<','<<int(after.nSustainEnd)<<" loop "<<int(actual.nLoopStart)<<','<<int(actual.nLoopEnd)<<'/'<<int(after.nLoopStart)<<','<<int(after.nLoopEnd)<<'\n'; }
    check(sameInstrumentEnvelope(actual,after),"XM/IT/MPT snapshot retains transformed nodes and anchors");
    check(sameInstrumentEnvelope(reopened.song().Instruments[1]->PanEnv,doc.song().Instruments[1]->PanEnv),"Full-right pan 64 survives native snapshots");
    if(type==MOD_TYPE_XM) {
      auto snapshot=doc.snapshotData();auto parts=splitSongSnapshot(snapshot);
      std::vector<std::byte> archive(parts.samples.begin(),parts.samples.end());
      const auto text=std::string(reinterpret_cast<const char *>(archive.data()),archive.size());const auto at=text.find("RSENVS1");
      check(at!=std::string::npos,"Source-incompatible envelope requires explicit correction record");
      for(size_t length=at+1;length<archive.size();++length) malformed([&]{Document bad(packSongSnapshot(parts.module,std::span(archive).first(length),parts.timing));});
      for(auto [offset,value]:std::vector<std::pair<size_t,uint8_t>>{{6,'2'},{8,0},{10,0},{12,3},{13,255},{14,128},{15,239},{19,240},{20,1},{22,65}}){
        auto bytes=archive;bytes[at+offset]=std::byte(value);malformed([&]{Document bad(packSongSnapshot(parts.module,bytes,parts.timing));});
      }
      auto duplicate=archive;duplicate.insert(duplicate.end(),archive.begin()+at,archive.end());malformed([&]{Document bad(packSongSnapshot(parts.module,duplicate,parts.timing));});
      auto legacy=doc.serialize();Document base(legacy);malformed([&]{validateSampleExport(doc.song(),base.song());});
      doc.transaction([](CSoundFile &s){s.m_nSamples=std::max<SAMPLEINDEX>(1,s.m_nSamples);s.GetSample(1).nativeReverseLoops=1;});
      Document combined(doc.snapshotData());check(sameInstrumentEnvelope(combined.song().Instruments[1]->VolEnv,after)&&combined.song().GetSample(1).nativeReverseLoops==1,"Reverse-loop and envelope corrections coexist in canonical order");
      doc.undo();
    }
    doc.undo();check(sameInstrumentEnvelope(doc.song().Instruments[1]->VolEnv,before),"Instrument transform Undo restores exact envelope");
    doc.redo();check(sameInstrumentEnvelope(doc.song().Instruments[1]->VolEnv,after),"Instrument transform Redo restores exact envelope");
  }
  std::cout<<"PASS instrument envelopes: shared deterministic transforms, native integer quantization, retained/replaced anchors, clipboard/repeat/insert, empty generators, limits and three-format persistence/history\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
