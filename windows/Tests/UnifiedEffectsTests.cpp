// Engine cases mirrored from mac/Tests/UnifiedEffectsTests.mm; no AppKit bridge.
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "soundlib/mod_specifications.h"
#include <iostream>
using namespace Tracker; using namespace OpenMPT;
static void check(bool ok,const char *message){if(!ok) throw std::runtime_error(message);}
static std::unique_ptr<Document> fixture(MODTYPE type=MOD_TYPE_MPT) {
  auto d=Document::demo(type);
  d->transaction([](CSoundFile &s){
    for(auto &p:s.Patterns) if(p.IsValid()) for(auto &c:p)c.Clear();
    if(s.GetModSpecifications().patternRowsMin<=8)check(s.Patterns[0].Resize(8),"Resize");s.Order().assign(2,0);
    for(unsigned row:{0u,4u}){auto &c=*s.Patterns[0].GetpModCommand(row,0);c.note=row?68:61;c.instr=2;}
  });return d;
}
static std::vector<float> render(Document &d,uint32_t rate,uint32_t block,uint32_t cursor=0) {
  Renderer r(d.snapshotData(),rate,0,false,{},0,{UINT32_MAX,0,0,cursor,false},&d.native());
  std::vector<float> out(rate*2);
  for(uint32_t at=0;at<rate;at+=block){auto count=std::min(block,rate-at);r.render(out.data()+2*at,count);}
  return out;
}
static void movedEffects(MODTYPE type) {
  unsigned count=0;
  for(const auto &effect:patternCommands(type,false)) {
    if(!effect.command)continue;
    auto primary=fixture(type),moved=fixture(type);
    primary->transaction([&](CSoundFile &s){for(unsigned row:{0u,4u}){auto &c=*s.Patterns[0].GetpModCommand(row,0);c.command=EffectCommand(effect.command);c.param=effect.suggested;}});
    moved->annotate([&](NativeSong &n){const auto track=n.tracks.at(0).id,pattern=n.patterns.at(0).id;n.performance.columns[track]=8;
      for(unsigned row:{0u,4u}){PatternCommand c{pattern,track,row*performanceUnitsPerRow,0,7,PatternCommandKind::TrackerEffect};c.effect=effect.command;c.parameter=effect.suggested;n.performance.commands.push_back(c);}});
    auto a=render(*primary,48000,512),b=render(*moved,48000,512);
    if(a!=b){auto mismatch=std::mismatch(a.begin(),a.end(),b.begin());std::cerr<<"Moved effect "<<effect.label<<" id="<<int(effect.command)<<" value="<<int(effect.suggested)<<" mismatch frame="<<(mismatch.first-a.begin())/2<<" values "<<*mismatch.first<<'/'<<*mismatch.second<<'\n';check(false,"Moving a tracker effect to FX 8 changes audio");}
    moved->native().prepareEffects(moved->song());
    const auto aTime=primary->song().GetLength(eNoAdjust,{}),bTime=moved->song().GetLength(eNoAdjust,{});
    if(aTime.back().duration!=bTime.back().duration){std::cerr<<"Timeline effect "<<effect.label<<' '<<aTime.back().duration<<'/'<<bTime.back().duration<<'\n';check(false,"All columns must affect timeline and flow");}
    auto seekA=render(*primary,48000,512,3),seekB=render(*moved,48000,512,3);
    if(seekA!=seekB){std::cerr<<"Seek effect "<<effect.label<<"\n";check(false,"Moved effect must retain cursor-start behavior");}
    ++count;
  }
  std::cout<<"Format "<<type<<": moved "<<count<<" catalog effects; sample-exact first/last column audio, cursor-start playback and identical timelines\n";
}
static void combinations() {
  auto d=fixture();
  d->transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(0,0);c.command=CMD_CHANNELVOLUME;c.param=16;});
  d->annotate([](NativeSong &n){auto track=n.tracks.at(0).id,pattern=n.patterns.at(0).id;n.performance.columns[track]=4;
    for(auto [column,effect,param]:{std::tuple{1,CMD_CHANNELVOLUME,40},std::tuple{2,CMD_PANNING8,0},std::tuple{3,CMD_VOLUMESLIDE,1}}){PatternCommand c{pattern,track,0,0,uint8_t(column),PatternCommandKind::TrackerEffect};c.effect=effect;c.parameter=param;n.performance.commands.push_back(c);}});
  Renderer r(d->snapshotData(),48000,0,false,{},0,{},&d->native());std::array<float,2> sample{};r.render(sample.data(),1);
  check(r.song().m_PlayState.Chn[0].nGlobalVol==40&&r.song().m_PlayState.Chn[0].nPan==0,"FX runs left to right; rightmost volume wins while pan remains independent");
  check(r.song().m_PlayState.Chn[0].nativeNoteGeneration==1,"Multiple FX must trigger the note only once");
  for(uint32_t rate:{44100u,48000u,96000u}){auto expected=render(*d,rate,17);for(uint32_t block:{128u,512u,4096u})check(expected==render(*d,rate,block),"Multi-FX audio is callback-partition independent");}
  auto delay=fixture();delay->annotate([](NativeSong &n){auto t=n.tracks.at(0).id,p=n.patterns.at(0).id;n.performance.columns[t]=3;
    n.performance.commands={{p,t,0,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_S3MCMDEX,0xD3},{p,t,0,0,2,PatternCommandKind::TrackerEffect,0,0,2,CMD_PANNING8,0}};});
  Renderer delayed(delay->snapshotData(),48000,0,false,{},0,{},&delay->native());delayed.render(sample.data(),1);check(delayed.song().m_PlayState.Chn[0].nativeNoteGeneration==0,"Later-column SD delays the common onset");
  std::vector<float> ticks(6000);delayed.render(ticks.data(),3000);check(delayed.song().m_PlayState.Chn[0].nativeNoteGeneration==1,"Delayed onset fires once alongside another effect");
  auto plain=fixture(),pc=fixture();
  for(auto *doc:{plain.get(),pc.get()}) {
    doc->transaction([](CSoundFile &s){s.Patterns[0].GetpModCommand(0,0)->Clear();});
    doc->annotate([](NativeSong &n){auto t=n.tracks.at(0).id,p=n.patterns.at(0).id;n.performance.columns[t]=3;n.performance.commands={{p,t,0,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_SPEED,3},{p,t,0,0,2,PatternCommandKind::TrackerEffect,0,0,2,CMD_TEMPO,170}};});
  }
  pc->transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(0,0);c.note=NOTE_PC;c.instr=1;c.SetValueVolCol(123);c.SetValueEffectCol(789);});
  check(render(*plain,48000,512)==render(*pc,48000,512),"An unassigned imported PC note cannot reinterpret or suppress additional FX");
  plain->native().prepareEffects(plain->song());pc->native().prepareEffects(pc->song());
  check(plain->song().GetLength(eNoAdjust,{}).back().duration==pc->song().GetLength(eNoAdjust,{}).back().duration,"Timeline includes additional speed/tempo beside imported PC notes");
}
int main(){try{for(auto type:{MOD_TYPE_MPT,MOD_TYPE_IT,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_MOD})movedEffects(type);combinations();std::cout<<"PASS unified FX: PCM, timeline, cursor-start, order, onset and partitions (functional only)\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
