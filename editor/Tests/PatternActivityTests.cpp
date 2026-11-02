#include "editor/PatternActivityCatalog.hpp"
#include "editor/TrackLayout.hpp"
#include <iostream>

int main(){try{
  using namespace Tracker;using namespace OpenMPT;
  auto doc=Document::demo();doc->transaction([](CSoundFile &song){
    for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
    song.Patterns[0].Resize(4);if(!song.Patterns.Insert(1,4))throw std::runtime_error("Cannot create activity fixture pattern");
    song.Order().assign(3,0);song.Order()[1]=1;auto &sample=song.GetSample(1);sample.nLoopStart=0;sample.nLoopEnd=sample.nLength;sample.uFlags.set(CHN_LOOP);
    auto *note=song.Patterns[1].GetpModCommand(0,0);note->note=61;note->instr=1;
  });
  const auto before=doc->native();
  const auto first=patternActivityCatalog(*doc,0,0,1,0,{}),second=patternActivityCatalog(*doc,0,2,1,0,{});
  if(!first.playback.complete||!second.playback.complete||!first.playback.samples.empty()||!second.playback.samples.contains(1))throw std::runtime_error("Repeated pattern did not inherit distinct start-to-cursor sample state");
  if(!patternActivityCatalog(*doc,0,2,1,1,{}).playback.samples.empty())throw std::runtime_error("Context leaked a different channel's voices");
  if(doc->native()!=before)throw std::runtime_error("Activity read changed native music");
  doc->annotate([](NativeSong &native){
    native.ensureMixer();native.mixer.buses.back().inserts={"gain","filter"};
    PatternCommand gate;gate.pattern=native.patterns.at(1).id;gate.track=native.tracks.at(0).id;gate.kind=PatternCommandKind::Native;gate.native=NativePatternOp::Gate;gate.arguments=nativePatternDefaults(gate.native);native.performance.commands.push_back(gate);
  });
  const auto routed=patternActivityCatalog(*doc,0,2,1,0,{{"gain","Gain",false,false,{}},{"filter","Filter",false,true,{}}});
  bool gain=false,filter=false,gate=false;for(const auto &item:routed.items){gain|=item.plugin=="gain";filter|=item.plugin=="filter"&&item.label.find("bypassed")!=std::string::npos;gate|=item.kind=="effect"&&item.pattern==1&&item.label.find("inherited")!=std::string::npos;}
  if(!gain||!filter||!gate)throw std::runtime_error("Activity omitted routed processors, bypass or inherited GT");
  const auto original=doc->native();SetNoteColumnMutes batch;for(const auto &[channel,track]:original.tracks)batch.columns.push_back({track.id,channel!=0});
  const auto muted=prepareNoteTrackEdit(original,doc->song(),batch);if(!muted.changed||!effectiveColumnMute(muted.native,doc->song(),1))throw std::runtime_error("Atomic solo candidate missing mutes");
  if(prepareNoteTrackEdit(muted.native,doc->song(),batch).changed)throw std::runtime_error("Mute batch no-op changed history");
  std::cout<<"PASS replay from start, repeated occurrences, channel isolation, routed/bypassed processors, inherited GT and atomic mute candidate\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
