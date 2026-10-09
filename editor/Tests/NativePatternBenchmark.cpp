#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Tracker;
using namespace OpenMPT;
#if defined(TRACKER_REALTIME_AUDIT) && !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
static constexpr bool audited=true;
#else
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
static constexpr bool audited=false;
#endif
namespace {
constexpr uint32_t rate=48000,voices=32;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::unique_ptr<Document> fixture(bool vibrato){
  auto document=std::make_unique<Document>();
  document->transaction([](CSoundFile &song){
    Document::resizeChannels(song,voices);
    for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
    check(song.Patterns[0].Resize(128),"Cannot resize benchmark pattern");
    song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);
    song.m_nSamples=1;auto &sample=song.GetSample(1);sample.Initialize();sample.nLength=4096;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);
    check(sample.AllocateSample()!=0,"Cannot allocate benchmark sample");
    for(uint32_t i=0;i<sample.nLength;++i)sample.sample16()[i]=int16_t(1200*std::sin(6.2831853071795864769*8*i/sample.nLength));
    sample.SetLoop(0,sample.nLength,true,false,song);sample.PrecomputeLoops(song,false);
    for(uint32_t channel=0;channel<voices;++channel){
      song.ChnSettings[channel].nPan=128;song.ChnSettings[channel].dwFlags.reset(CHN_MUTE);
      auto &cell=*song.Patterns[0].GetpModCommand(0,channel);cell.note=uint8_t(49+channel%12);cell.instr=1;cell.volcmd=VOLCMD_VOLUME;cell.vol=32;
    }
  });
  document->annotate([&](NativeSong &native){
    for(uint32_t channel=0;channel<voices;++channel){
      const auto track=native.tracks.at(channel).id;native.performance.columns[track]=vibrato?3:2;
      auto add=[&](NativePatternOp op,uint8_t column){PatternCommand command;command.pattern=native.patterns.at(0).id;command.track=track;command.column=column;command.kind=PatternCommandKind::Native;command.native=op;command.arguments=nativePatternDefaults(op);return command;};
      auto gain=add(NativePatternOp::GainSet,0);gain.arguments[0]=.35+.25*channel/(voices-1);native.performance.commands.push_back(gain);
      auto pan=add(NativePatternOp::PanSet,1);pan.arguments[0]=-.8+1.6*channel/(voices-1);native.performance.commands.push_back(pan);
      if(vibrato){auto command=add(NativePatternOp::Vibrato,2);command.arguments[0]=.375+.025*(channel%7);command.arguments[1]=4.125+.125*(channel%9);command.arguments[2]=1;command.arguments[4]=double(channel)/voices;native.performance.commands.push_back(command);}
    }
  });
  return document;
}
void verifyVoices(Renderer &renderer,bool vibrato){
  const auto &song=renderer.song();
  for(uint32_t channel=0;channel<voices;++channel){const auto &voice=song.m_PlayState.Chn[channel];
    check(voice.nLength&&voice.pCurrentSample,"Benchmark lost a looping sample voice");
    check(voice.nativePatternVoice.gain.active&&voice.nativePatternVoice.pan.active,"Benchmark native gain/pan workload is inactive");
    check(voice.nativePatternVoice.vibrato.active==vibrato,"Benchmark vibrato workload differs from its label");
  }
}
struct Result {
  bool vibrato=false;
  uint32_t block=0,callbacks=0,frames=0,warmupFrames=0;
  uint64_t allocations=0,frees=0,locks=0,misses=0;
  double maximum=0,p99=0,mean=0,deadline=0,energy=0,peak=0;
};
Result run(bool vibrato,uint32_t block){
  auto document=fixture(vibrato);
  auto renderer=std::make_unique<Renderer>(document->snapshotData(),rate);
  renderer->preparePreciseNotes(document->native());
  Result result;result.vibrato=vibrato;result.block=block;result.callbacks=(5*rate+block-1)/block;result.frames=result.callbacks*block;result.warmupFrames=((rate+block-1)/block)*block;result.deadline=1e6*block/rate;
  std::array<float,1024> pcm{};
  std::vector<double> elapsed(result.callbacks); // Allocate all timing storage before rendering.
  for(uint32_t frame=0;frame<result.warmupFrames;frame+=block)check(renderer->render(pcm.data(),block)==block&&!renderer->faulted(),"Benchmark warmup failed");
  verifyVoices(*renderer,vibrato);
  for(uint32_t callback=0;callback<result.callbacks;++callback){
    uint64_t a=0,f=0,l=0;
    const auto begin=std::chrono::steady_clock::now();
    tracker_audit_begin();
    const auto rendered=renderer->render(pcm.data(),block);
    tracker_audit_end(&a,&f,&l);
    const auto end=std::chrono::steady_clock::now();
    // Clock conversion, vector writes, validation and reporting are outside the
    // audited callback. Elapsed timing includes the audit guard when enabled.
    const double microseconds=std::chrono::duration<double,std::micro>(end-begin).count();
    elapsed[callback]=microseconds;result.mean+=microseconds;result.misses+=microseconds>result.deadline;
    result.allocations+=a;result.frees+=f;result.locks+=l;
    check(rendered==block&&!renderer->faulted(),"Benchmark render failed");
    for(uint32_t i=0;i<block*2;++i){check(std::isfinite(pcm[i]),"Benchmark emitted non-finite PCM");result.energy+=double(pcm[i])*pcm[i];result.peak=std::max(result.peak,std::abs(double(pcm[i])));}
  }
  verifyVoices(*renderer,vibrato);
  check(result.energy>0,"Benchmark workload rendered silence");
  check(!result.allocations&&!result.frees&&!result.locks,"Benchmark callback audit detected allocation, free or lock");
  std::sort(elapsed.begin(),elapsed.end());result.maximum=elapsed.back();result.p99=elapsed[size_t(std::ceil(.99*elapsed.size()))-1];result.mean/=elapsed.size();
  return result;
}
}
int main(){try{
  std::vector<Result> results;results.reserve(4);
  for(bool vibrato:{false,true})for(uint32_t block:{128u,512u})results.push_back(run(vibrato,block));
  std::cout<<std::setprecision(10)<<"{\"benchmark\":\"native-pattern\",\"sampleRate\":"<<rate<<",\"voices\":"<<voices<<",\"auditEnabled\":"<<(audited?"true":"false")
    <<",\"timingIncludesAuditGuard\":"<<(audited?"true":"false")<<",\"p99Method\":\"nearest-rank\",\"scope\":\"Offline renderer; no device or UI. Run with competing compilers stopped. Observed timing, not a universal voice-count guarantee.\",\"cases\":[";
  for(size_t i=0;i<results.size();++i){const auto &r=results[i];if(i)std::cout<<',';
    std::cout<<"{\"workload\":\""<<(r.vibrato?"gain-pan-vibrato":"gain-pan")<<"\",\"bufferFrames\":"<<r.block<<",\"callbacks\":"<<r.callbacks
      <<",\"renderedFrames\":"<<r.frames<<",\"audioSeconds\":"<<double(r.frames)/rate<<",\"warmupSeconds\":"<<double(r.warmupFrames)/rate
      <<",\"deadlineUs\":"<<r.deadline<<",\"maxUs\":"<<r.maximum<<",\"p99Us\":"<<r.p99<<",\"meanUs\":"<<r.mean<<",\"deadlineMisses\":"<<r.misses
      <<",\"allocations\":"<<r.allocations<<",\"frees\":"<<r.frees<<",\"locks\":"<<r.locks<<",\"peak\":"<<r.peak<<",\"energy\":"<<r.energy<<'}';
  }
  std::cout<<"]}\n";return 0;
}catch(const std::exception &e){std::cerr<<"Native pattern benchmark failed: "<<e.what()<<'\n';return 1;}}
