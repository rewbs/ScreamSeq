#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <cmath>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
static std::unique_ptr<Document> fixture(VolumeCommand volume,EffectCommand effect=CMD_NONE,uint8_t parameter=0,bool extra=false){
  auto d=std::make_unique<Document>();d->transaction([&](CSoundFile &s){
    for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();
    s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
    s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=65536;sample.nC5Speed=8192;sample.nVolume=128;sample.uFlags.set(CHN_16BIT);sample.cues[0]=2048;
    check(sample.AllocateSample()!=0,"Allocate ND fixture");std::fill_n(sample.sample16(),sample.nLength,int16_t(1200));sample.PrecomputeLoops(s,false);
    auto &first=*s.Patterns[0].GetpModCommand(0,0);first.note=61;first.instr=1;
    auto &delayed=*s.Patterns[0].GetpModCommand(1,0);delayed.note=65;delayed.instr=1;delayed.volcmd=volume;delayed.vol=volume==VOLCMD_OFFSET?1:3;delayed.command=effect;delayed.param=parameter;
  });
  d->annotate([&](NativeSong &n){PatternCommand c;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::NoteDelay;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.position=65536+16384;c.column=1;n.performance.columns[c.track]=extra?3:2;n.performance.commands={c};
    if(extra){auto f=c;f.position=65536;f.column=2;f.kind=PatternCommandKind::TrackerEffect;f.native=NativePatternOp::None;f.effect=CMD_VOLUMESLIDE;f.parameter=0x3f;n.performance.commands.push_back(f);}
  });return d;
}
int main(){try{
  for(auto volume:{VOLCMD_NONE,VOLCMD_FINEVOLUP,VOLCMD_VOLSLIDEUP,VOLCMD_PORTAUP,VOLCMD_VIBRATODEPTH,VOLCMD_OFFSET})for(bool extra:{false,true}) {
    auto d=fixture(volume,CMD_NONE,0,extra);
    auto render=[&](uint32_t block,bool inspect){Renderer r(d->snapshotData(),48000);r.preparePreciseNotes(d->native());std::vector<float> pcm(14000*2);
      for(uint32_t frame=0;frame<14000;frame+=block){r.render(pcm.data()+frame*2,std::min(block,14000-frame));check(!r.faulted(),"Delay render stays healthy");if(!inspect)continue;const auto &v=r.song().m_PlayState.Chn[0];
        if(frame>=5760&&frame<7200){check(v.nativeNoteGeneration==1,"Original note continues until delayed onset");check(v.nVolume==128,"Delayed local effects do not modify the earlier note");}
        if(frame==7200){check(v.nativeNoteGeneration==2,"ND produces one onset at fractional row position");check(v.nVolume==128+(volume==VOLCMD_FINEVOLUP?12:0)+(extra?12:0),"ND applies sample default, fine volume and extra local FX exactly once");
          if(volume==VOLCMD_OFFSET)check(v.position.ToDouble()>2048&&v.position.ToDouble()<2049,"Delayed volume-column cue seeks at onset");
          if(volume==VOLCMD_VIBRATODEPTH)check(v.nVibratoDepth!=0,"Delayed volume-column vibrato initializes on onset");
        }
        if(frame==7680&&volume==VOLCMD_VOLSLIDEUP)check(v.nVolume==140+(extra?12:0),"Delayed volume slide continues on subsequent real tracker ticks");
      }return pcm;};
    const auto reference=render(1,true);for(auto block:{17u,128u,4096u}){const auto actual=render(block,false);double peak=0;for(size_t i=0;i<actual.size();++i)peak=std::max(peak,std::abs(double(actual[i])-reference[i]));check(peak<1e-6,"ND local/volume effects are callback-partition independent");}
  }
  auto d=fixture(VOLCMD_NONE,CMD_VOLUMESLIDE,0x3f);Renderer r(d->snapshotData(),48000);r.preparePreciseNotes(d->native());std::array<float,2> pcm{};
  for(uint32_t frame=0;frame<9000;++frame){r.render(pcm.data(),1);if(frame>=5760&&frame<7200)check(r.song().m_PlayState.Chn[0].nVolume==128,"Primary fine slide waits for delayed onset");if(frame>=7200)check(r.song().m_PlayState.Chn[0].nVolume==140,"Primary fine slide applies once on delayed note");}
  std::cout<<"PASS native delay: sample volume, volume-column fine/continuing slides, portamento/vibrato/cue, primary and extra local effects, original-note isolation, exact onset and four callback partitions\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
