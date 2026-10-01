// Shared voice snapshot checks from mac/Tests/PlaybackDisplayTests.mm.
// Functional coverage only; the Darwin realtime interposer is not available.
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <iostream>
using namespace Tracker;using namespace OpenMPT;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  for(uint32_t rate:{44100u,48000u,96000u}) {
    auto doc=Document::demo();
    doc->transaction([](CSoundFile &s){
      s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);
      s.Instruments[1]->VolEnv.push_back(0,64);s.Instruments[1]->VolEnv.push_back(30,32);s.Instruments[1]->VolEnv.dwFlags.set(ENV_ENABLED);
      auto &sample=s.GetSample(1);sample.nLoopStart=100;sample.nLoopEnd=500;sample.uFlags.set(CHN_LOOP);sample.PrecomputeLoops(s);
    });
    Renderer renderer(doc->serialize(),rate,0,true);std::array<float,256> block{};
    check(renderer.preview({61,1,100,true}),"First preview note accepted");renderer.render(block.data(),128);
    check(renderer.preview({65,1,100,true}),"Overlapping preview note accepted");
    for(int i=0;i<50;++i)renderer.render(block.data(),128);
    const auto positions=renderer.voicePositions();
    check(positions.size()==2,"Both overlapping voices have independent markers");
    check(positions[0].channel!=positions[1].channel && positions[0].sample==1 && positions[0].instrument==1,"Markers identify sample, instrument and channel");
    check(positions[0].sampleFrame<500 && positions[1].sampleFrame<500,"Waveform position follows the sample loop");
    check(positions[0].envelopeTicks[0]>0,"Envelope cursor follows actual envelope ticks");
    renderer.panic();renderer.render(block.data(),128);check(renderer.voicePositions().empty(),"Panic retires playback markers");
  }
  std::cout<<"PASS playback display: overlap, sample loops, envelope ticks and panic at three rates (functional only)\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
