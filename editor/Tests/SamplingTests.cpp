#include "editor/Sampling.hpp"
#include "soundlib/ModInstrument.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace Tracker; using namespace OpenMPT;
static void check(bool ok, const char *why) { if(!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool failed=false; try { f(); } catch(const std::invalid_argument &) { failed=true; } check(failed,"Invalid sampling input rejects"); }
int main() { try {
  for(uint32_t channels : {1u,2u}) for(bool instrument : {false,true}) {
    Document d; const auto previousSamples=d.song().GetNumSamples();
    d.transaction([](CSoundFile &s) { s.Patterns[0].GetpModCommand(0,0)->instr=1; });
    const auto before=d.snapshotData(); const auto metadata=d.native(); const auto revision=d.revision;
    const std::vector<float> pcm=channels==1 ? std::vector<float>{-2,-1,-.25f,0,.25f,1,2} : std::vector<float>{-2,2,-1,1,-.25f,.25f,0,0};
    const auto dry=importRecordedAudio(d,pcm,44100,channels,"Recorded voice",instrument,true);
    check(d.revision==revision && d.snapshotData()==before && d.native()==metadata,"Dry run preserves every source field/history");
    auto actual=importRecordedAudio(d,pcm,44100,channels,"Recorded voice",instrument);
    check(actual.sample==dry.sample && actual.sample==previousSamples+1 && actual.frames==pcm.size()/channels && actual.clippedValues==2,"Import reports exact dimensions and clipping");
    const auto &sample=d.song().GetSample(SAMPLEINDEX(actual.sample));
    check(sample.nC5Speed==44100 && sample.GetNumChannels()==channels && sample.GetElementarySampleSize()==2,"Native PCM keeps source rate/channels");
    for(size_t i=0;i<pcm.size();++i) check(sample.sample16()[i]==std::clamp<long>(std::lround(double(std::clamp(pcm[i],-1.f,1.f))*32768.),-32768,32767),"Independent saturation oracle");
    if(instrument) {
      check(d.song().GetNumInstruments()==actual.instrument && d.song().Instruments[actual.instrument]->Keyboard[60]==actual.sample,"New instrument maps the recording");
      for(SAMPLEINDEX i=1;i<=previousSamples;++i) check(d.song().Instruments[i]->Keyboard[60]==i,"First-instrument conversion preserves old sample notes");
    }
    const auto after=d.snapshotData(); const auto afterNative=d.native(); const auto history=d.historyHead(false);
    check(d.revision==revision+1,"Sample and optional instrument are one edit");
    d.undo(); auto undoMetadata=metadata; undoMetadata.nextID=afterNative.nextID;
    check(d.snapshotData()==before && d.native()==undoMetadata,"One Undo restores complete source, retaining the monotonic identity allocator");
    d.redo(); check(d.snapshotData()==after && d.native()==afterNative && d.historyHead(false)==history,"Redo restores exact PCM and stable IDs");
    Document reopened(after); reopened.restoreNative(afterNative);
    check(reopened.snapshotData()==after && reopened.native()==afterNative,"Native sample/instrument snapshot persists exactly");
    const auto unchanged=d.snapshotData(); const auto rev=d.revision;
    for(float invalid : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) { auto bad=pcm; bad.back()=invalid; rejects([&]{importRecordedAudio(d,bad,44100,channels,"Invalid",instrument);}); }
    rejects([&]{importRecordedAudio(d,{},44100,channels,"Empty",instrument);});
    rejects([&]{importRecordedAudio(d,pcm,44100,0,"Invalid",instrument);});
    rejects([&]{importRecordedAudio(d,pcm,7999,channels,"Invalid",instrument);});
    rejects([&]{validateRecordedAudioImport(d,maximumRecordedSampleFrames+1,48000,2,"Oversized",false);});
    rejects([&]{importRecordedAudio(d,pcm,44100,channels,"",instrument);});
    rejects([&]{importRecordedAudio(d,pcm,44100,channels,std::string("\xc0\x80",2),instrument);});
    check(d.snapshotData()==unchanged && d.revision==rev,"All rejected imports preserve PCM/history/revision");
  }
  { Document longName; const std::string name(128,'x'); const std::vector<float> audio{.1f,.2f};
    const auto result=importRecordedAudio(longName,audio,48000,1,name,true);
    check(longName.native().samples.at(uint16_t(result.sample)).name==name && longName.native().instruments.at(uint16_t(result.instrument)).name==name,"Full native names survive the legacy header limit");
    Document reopen(longName.snapshotData());reopen.restoreNative(longName.native());check(reopen.native().samples.at(uint16_t(result.sample)).name==name,"Full name persists"); }
  Document d; const auto old=d.snapshotData(); const auto rev=d.revision;
  rejects([&]{prepareSamplingSelection(d,{65535,0,0,0,0});});
  rejects([&]{prepareSamplingSelection(d,{0,5,4,0,0});});
  rejects([&]{prepareSamplingSelection(d,{0,0,0,1,0});});
  rejects([&]{prepareSamplingSelection(d,{0,0,0,0,65535});});
  check(d.snapshotData()==old&&d.revision==rev,"Selection validation does not mutate");
  d.transaction([](CSoundFile &s) { auto &p=s.Patterns[0];p.GetpModCommand(0,0)->command=CMD_POSITIONJUMP;p.GetpModCommand(1,0)->command=CMD_PATTERNBREAK;p.GetpModCommand(2,0)->command=CMD_MODCMDEX;p.GetpModCommand(2,0)->param=0x63;p.GetpModCommand(3,0)->command=CMD_S3MCMDEX;p.GetpModCommand(3,0)->param=0xb3;p.GetpModCommand(4,0)->command=CMD_TEMPO;p.GetpModCommand(4,0)->param=100; });
  d.annotate([](NativeSong &n) { PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.position=5*65536;c.column=1;n.performance.columns[c.track]=2;c.kind=PatternCommandKind::TrackerEffect;c.effect=CMD_POSITIONJUMP;n.performance.commands.push_back(c); });
  const auto source=d.snapshotData(); const auto meta=d.native();
  auto prepared=prepareSamplingSelection(d,{0,2,5,0,0}); Document filtered(prepared.module);
  for(int row=0;row<4;++row) check(filtered.song().Patterns[0].GetpModCommand(ROWINDEX(row),0)->command==CMD_NONE,"Transport flow is isolated in copy");
  check(filtered.song().Patterns[0].GetpModCommand(4,0)->command==CMD_TEMPO&&prepared.native.performance.commands.empty(),"Tempo retained, extra-column transport loop removed");
  check(source==d.snapshotData()&&meta==d.native(),"Preparing a bounded selection preserves the song");
  std::cout<<"PASS atomic recorded PCM, first-instrument conversion, validation, Undo/Redo, persistence and linear selection snapshot\n"; return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; } }
