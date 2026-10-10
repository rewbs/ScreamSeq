#pragma once
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <array>
#include <iostream>
// Native wrappers supply check() and their existing callback audit boundaries.
static void playbackRegionChecks() {
  using namespace Tracker; using namespace OpenMPT;
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
    Renderer enabled(bytes,rate,0,false,{},0,{0,2,5,4,false});
    check(enabled.render(out.data(),1)==1,"Non-looping region should start");enabled.loop(true);
    for(uint32_t pos=0;pos<rowFrames*7;){
      const auto n=std::min(block,rowFrames*7-pos);uint64_t a,f,l;
      tracker_audit_begin();const auto got=enabled.render(out.data(),n);tracker_audit_end(&a,&f,&l);
      check(got==n&&!enabled.faulted()&&a+f+l==0,"Enabling a running region loop must keep rendering without callback allocation or locks");
      pos+=n;check(enabled.telemetry().row>=2&&enabled.telemetry().row<5,"Enabling loop left the captured region");
    }
    enabled.loop(false);uint32_t enabledTail=0;
    for(;;){const auto n=enabled.render(out.data(),block);enabledTail+=n;if(n<block)break;check(enabledTail<rowFrames*4,"Re-disabling a live loop failed to reach its boundary");}
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

  // Starting after one rendered frame deliberately leaves a whole tracker tick
  // pending. Short callbacks must hear the audition without advancing that tick.
  doc.transaction([](CSoundFile &s){
    s.Instruments[1]->Keyboard.fill(1);
    auto &env=s.Instruments[1]->VolEnv;
    env.push_back(0,64);env.push_back(5,32);env.dwFlags.set(ENV_ENABLED);
  });
  const auto previewBytes=doc.snapshotData();
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,128u,511u})
    for(bool paused:{false,true})for(bool rawSample:{false,true}) {
      Renderer immediate(previewBytes,rate,0,paused);
      check(immediate.render(out.data(),1)==1,"Prime preview between tracker ticks");
      const auto &state=immediate.song().m_PlayState;
      const auto remaining=state.SamplesRemainingInTick();
      const auto tick=state.m_nTickCount;
      const auto row=state.m_nRow;
      check(remaining>block,"Audition fixture must finish before the next tick");
      check(immediate.preview({61,1,127,true,uint16_t(rawSample?1:0)}),"Queue immediate audition");
      uint64_t a,f,l;tracker_audit_begin();const auto got=immediate.render(out.data(),block);tracker_audit_end(&a,&f,&l);
      check(got==block&&!immediate.faulted()&&a+f+l==0,"Immediate audition must render without callback allocation or locks");
      check(std::any_of(out.begin(),out.begin()+block*2,[](float v){return std::abs(v)>1e-7f;}),
        "Sample and instrument auditions must sound in their first callback between ticks");
      check(state.SamplesRemainingInTick()==remaining-block&&state.m_nTickCount==tick&&state.m_nRow==row,
        "Audition onset must not advance or restart the tracker tick");
      const auto &firstVoice=state.Chn[immediate.song().GetNumChannels()];
      const auto envelope=firstVoice.VolEnv.nEnvPosition;
      const auto increment=firstVoice.increment;
      check(immediate.preview({65,1,127,true,uint16_t(rawSample?1:0)}),"Queue independent second audition");
      tracker_audit_begin();const auto second=immediate.render(out.data(),17);tracker_audit_end(&a,&f,&l);
      check(second==17&&!immediate.faulted()&&a+f+l==0,"Second audition must preserve realtime safety");
      check(firstVoice.VolEnv.nEnvPosition==envelope&&firstVoice.increment==increment,
        "A new audition must not refresh an existing voice's envelope or pitch");
      check(state.SamplesRemainingInTick()==remaining-block-17&&state.m_nTickCount==tick&&state.m_nRow==row,
        "A second audition must also preserve the tracker clock");
    }
}
