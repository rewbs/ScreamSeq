#include "editor/PatternTimeline.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Tracker;
using namespace OpenMPT;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void close(std::optional<double> value,double expected,const char *message){
  check(value && std::abs(*value-expected)<1e-6,message);
}
auto fixture() {
  auto doc=Document::demo();
  doc->transaction([](CSoundFile &song){
    for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
    song.Patterns[0].Resize(8);song.Order().assign(2,0);
    auto timing=songTiming(song);timing.mode=TempoMode::Classic;timing.rowsPerBeat=4;timing.rowsPerMeasure=16;
    timing.groove.clear();timing.sequences[0]={TEMPO(125.0).GetRaw(),6};applySongTiming(song,timing);
  });
  return doc;
}
void occurrenceAndPurity() {
  auto doc=fixture();
  doc->transaction([](CSoundFile &song){
    auto &pattern=song.Patterns[0];pattern.GetpModCommand(0,0)->command=CMD_TEMPO;pattern.GetpModCommand(0,0)->param=125;
    pattern.GetpModCommand(4,0)->command=CMD_TEMPO;pattern.GetpModCommand(4,0)->param=250;
  });
  const auto bytes=doc->snapshotData();const auto native=doc->native();const auto revision=doc->revision;
  const auto undo=doc->canUndo(),redo=doc->canRedo();
  const auto first=patternTimeline(*doc,0),second=patternTimeline(*doc,0,1);
  check(first.order==0&&second.order==1&&second.positions.size()==8,"Wrong occurrence or row count");
  close(first.positions[0].songSeconds,0,"First occurrence origin changed");
  close(second.positions[0].songSeconds,.72,"Repeated occurrence lost absolute song time");
  close(second.positions[4].patternSeconds,.48,"Tempo change not reflected in relative time");
  close(second.positions[7].patternSeconds,.66,"Fast rows not reflected in relative time");
  check(second.positions[7].beat==1.75,"Beat positions changed");
  for(const auto args:{std::pair<uint16_t,uint32_t>{0,UINT32_MAX},{UINT16_MAX,0}}){
    bool rejected=false;try{patternTimeline(*doc,args.first,args.second);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected,"Invalid pattern/order accepted");
  }
  check(doc->snapshotData()==bytes&&doc->native()==native&&doc->revision==revision&&doc->canUndo()==undo&&doc->canRedo()==redo,
    "Timeline read or invalid request changed song, identities or history");
  doc->transaction([](CSoundFile &song){check(song.Patterns.Insert(1,8),"Unarranged pattern fixture");});
  const auto unarranged=patternTimeline(*doc,1);
  check(!unarranged.order&&unarranged.positions.size()==8,"Unarranged pattern lost rows");
  for(const auto &entry:unarranged.positions)check(!entry.songSeconds&&!entry.patternSeconds,"Unarranged row invented time");
  bool mismatch=false;try{patternTimeline(*doc,1,0);}catch(const std::invalid_argument &){mismatch=true;}
  check(mismatch,"Explicit order containing another pattern accepted");
  doc->transaction([](CSoundFile &song){
    check(song.Order.AddSequence()==1,"Second sequence fixture");song.Order.SetSequence(1);
    song.Order().assign(2,0);song.Order().SetDefaultSpeed(3);song.Order().SetDefaultTempo(TEMPO(125.0));
  });
  close(patternTimeline(*doc,0,1).positions[0].songSeconds,.36,"Timeline did not use the current sequence's speed");
  check(doc->song().Order.GetCurrentSequenceIndex()==1,"Timeline changed current sequence");
}
void flowAndNativeTiming() {
  auto doc=fixture();
  doc->transaction([](CSoundFile &song){
    song.Order().assign(1,0);
    song.Patterns[0].GetpModCommand(2,0)->command=CMD_POSITIONJUMP;
    song.Patterns[0].GetpModCommand(2,0)->param=0;
  });
  const auto loop=patternTimeline(*doc,0);
  close(loop.positions[2].songSeconds,.24,"First loop visit changed");
  for(unsigned row=3;row<8;++row)check(!loop.positions[row].songSeconds&&!loop.positions[row].patternSeconds,"Jumped rows must stay unreachable");
  doc=fixture();
  doc->annotate([](NativeSong &native){
    PatternCommand command;command.pattern=native.patterns.at(0).id;command.track=native.tracks.at(0).id;
    command.kind=PatternCommandKind::Native;command.native=NativePatternOp::TempoSet;
    command.arguments=nativePatternDefaults(command.native);command.arguments[0]=250;
    native.performance.commands={command};
  });
  close(patternTimeline(*doc,0).positions[4].songSeconds,.24,"Shared native tempo was not prepared for the engine walk");
  doc=fixture();
  doc->annotate([](NativeSong &native){
    PatternCommand command;command.pattern=native.patterns.at(0).id;command.track=native.tracks.at(0).id;
    native.performance.columns[command.track]=2;command.column=1;command.position=4*performanceUnitsPerRow;
    command.kind=PatternCommandKind::TrackerEffect;command.effect=CMD_TEMPO;command.parameter=250;
    native.performance.commands={command};
  });
  close(patternTimeline(*doc,0).positions[7].songSeconds,.66,"Extra FX column timing was not prepared for the engine walk");
}
void grooveAndSignature() {
  auto doc=fixture();
  doc->transaction([](CSoundFile &song){
    auto timing=songTiming(song);timing.mode=TempoMode::Modern;
    const std::array<double,4> weights{1.12,.88,1.12,.88};timing.groove=normalizedGroove(weights);
    applySongTiming(song,timing);
  });
  const auto grooved=patternTimeline(*doc,0);
  check(grooved.positions[1].songSeconds&&grooved.positions[2].songSeconds,"Groove rows unreachable");
  const auto longRow=*grooved.positions[1].songSeconds,shortRow=*grooved.positions[2].songSeconds-longRow;
  check(longRow>shortRow&&std::abs(longRow/shortRow-1.12/.88)<.002,"Engine groove did not affect row durations");
  doc->transaction([](CSoundFile &song){song.Patterns[0].SetSignature(3,12);});
  check(patternTimeline(*doc,0).positions[3].beat==1,"Pattern signature override lost");
}
}
int main(){try{
  occurrenceAndPurity();flowAndNativeTiming();grooveAndSignature();
  std::cout<<"PASS shared first-visit timing, repeated orders, flow, native tempo, groove, signatures and read purity\n";
  return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
