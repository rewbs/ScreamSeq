#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include "GraphRealtimeAudit.hpp"
#include <iostream>
using namespace Tracker; using namespace OpenMPT;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try {
  Document doc;
  doc.transaction([](CSoundFile &s){s.Patterns[0].Resize(8);s.Order().assign(2,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);});
  auto bytes=doc.snapshotData();
  for(uint32_t rate:{44100u,48000u,96000u}) for(uint32_t block:{1u,17u,128u,4096u}) {
    const auto rowFrames=rate*12/100;
    for(auto range:{PlaybackRegion{0,2,5,2,false},PlaybackRegion{0,2,5,4,false},PlaybackRegion{0,0,8,0,false},PlaybackRegion{0,0,8,7,false}}) {
      Renderer r(bytes,rate,0,false,{},0,range);std::array<float,8192> out{};uint64_t total=0;
      while(total<rowFrames*12) {
        uint64_t a,f,l;tracker_audit_begin();auto got=r.render(out.data(),block);tracker_audit_end(&a,&f,&l);
        check(!r.faulted() && a+f+l==0,"Range rendering must not allocate or lock");total+=got;if(got<block)break;
      }
      if(total!=(range.endRow-range.cursorRow)*rowFrames) {std::cerr<<"range "<<range.startRow<<":"<<range.endRow<<" cursor "<<range.cursorRow<<" rate "<<rate<<" block "<<block<<" got "<<total<<'\n';throw std::runtime_error("Range ends exactly at the selected row boundary");}
    }
    Renderer r(bytes,rate,0,false,{},0,{0,2,5,4,true});std::array<float,8192> out{};
    for(uint32_t pos=0;pos<rowFrames*8;){auto n=std::min(block,rowFrames*8-pos);check(r.render(out.data(),n)==n,"Loop should keep rendering");pos+=n;check(r.telemetry().row>=2&&r.telemetry().row<5,"Loop stays inside selection");}
    r.loop(false);uint32_t tail=0;for(;;){auto n=r.render(out.data(),block);tail+=n;if(n<block)break;check(tail<rowFrames*4,"Loop disable takes effect at next boundary");}
    Renderer song(bytes,rate,0,false,{},0,{UINT32_MAX,0,0,5,false});check(song.render(out.data(),1)==1&&song.telemetry().row==5,"Song starts at cursor");
  }
  {
    Renderer songLoop(bytes,48000,0,false,{},0,{UINT32_MAX,0,0,0,true});std::array<float,8192> out{};
    for(unsigned i=0;i<60;++i) check(songLoop.render(out.data(),4096)==4096,"Whole-song loop survives several arrangement passes");
    songLoop.loop(false);unsigned remaining=0;for(;;){auto n=songLoop.render(out.data(),4096);remaining+=n;if(n<4096)break;check(remaining<48000*3,"Whole-song loop disable finishes current pass");}
    Document timed(bytes);timed.transaction([](CSoundFile &s){auto &cell=*s.Patterns[0].GetpModCommand(3,0);cell.command=CMD_TEMPO;cell.param=250;});
    Renderer selection(timed.snapshotData(),48000,0,false,{},0,{0,2,5,2,false});unsigned total=0;for(;;){auto n=selection.render(out.data(),4096);total+=n;if(n<4096)break;}
    check(total==11520,"Selection endpoints follow tempo changes, not a precomputed timer");
  }
  // Direct sample keys must work even if an unrelated instrument occupies slot 1.
  doc.transaction([](CSoundFile &s){s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=1024;sample.nC5Speed=8363;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"Sample storage");for(int i=0;i<1024;++i)sample.sample16()[i]=int16_t(std::sin(i*.1)*10000);sample.SetLoop(0,1024,true,false,s);s.m_nInstruments=1;s.Instruments[1]=new ModInstrument();});
  Renderer audition(doc.snapshotData(),48000,0,true);std::array<float,8192> out{};check(audition.preview({61,0,127,true,1}),"Queue sample note");audition.render(out.data(),4096);
  check(std::any_of(out.begin(),out.end(),[](float v){return std::abs(v)>.001f;}),"Sample inspector actually produces audio");
  check(audition.preview({61,0,0,false,1}),"Queue sample release");audition.render(out.data(),4096);audition.render(out.data(),4096);check(std::all_of(out.begin(),out.end(),[](float v){return std::abs(v)<1e-6f;}),"Sample release stops a loop");
  std::cout<<"Playback ranges, live loop switching and direct sample audition passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
