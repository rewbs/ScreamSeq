#include "windows/Samples/Preview.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include <windows.h>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
using namespace ScreamSeq;
using namespace ScreamSeq::Samples;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string text(const fs::path &p){auto v=p.u8string();return {reinterpret_cast<const char *>(v.data()),v.size()};}
static void wav(const fs::path &path,unsigned frames,unsigned rate,unsigned channels,unsigned bits=16,int16_t amplitude=12000){
  std::ofstream out(path,std::ios::binary);auto u16=[&](unsigned v){out.put(char(v));out.put(char(v>>8));};auto u32=[&](unsigned v){u16(v);u16(v>>16);};
  out.write("RIFF",4);u32(36+frames*channels*bits/8);out.write("WAVEfmt ",8);u32(16);u16(1);u16(channels);u32(rate);u32(rate*channels*bits/8);u16(channels*bits/8);u16(bits);out.write("data",4);u32(frames*channels*bits/8);
  for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<channels;++c){const auto value=int16_t(std::sin(f*.031)*amplitude*(c?-1:1));if(bits==16)u16(uint16_t(value));else out.put(char((value/256)+128));}
  check(bool(out),"write owned WAV");
}
struct Folder{fs::path parent,path;explicit Folder(fs::path p):parent(fs::canonical(p)),path(parent/("sample-preview-"+std::to_string(GetCurrentProcessId()))){check(path.parent_path()==parent&&fs::create_directory(path),"create owned preview directory");}~Folder(){if(path.parent_path()==parent&&path.filename()=="sample-preview-"+std::to_string(GetCurrentProcessId())){std::error_code e;fs::remove_all(path,e);}}};
static void voiceTests(){
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned channels:{1u,2u}){
    PreviewAudio audio;audio.rate=rate;audio.channels=channels;audio.frames=rate/10;audio.totalFrames=audio.frames;audio.pcm.resize(size_t(audio.frames)*channels);
    for(unsigned i=0;i<audio.frames;++i)for(unsigned c=0;c<channels;++c)audio.pcm[size_t(i)*channels+c]=(c?-.8f:.4f);
    std::vector<float> reference;
    for(unsigned block:{1u,17u,128u,4096u}){
      PreviewVoice voice;voice.prepare(audio,32);voice.gain(.5f);std::vector<float> output(size_t(audio.frames+64)*2);
      for(unsigned at=0;at<audio.frames+64;){const auto frames=std::min(block,audio.frames+64-at);{AudioAudit::Scope audit;voice.render(output.data()+size_t(at)*2,frames);}at+=frames;}
      check(voice.finished()&&voice.rendered()==audio.frames,"preview source end/tail accounting");
      if(reference.empty())reference=output;else check(output==reference,"preview depends on callback partition");
      check(std::abs(output[2*(audio.frames/2)]-.2f)<1e-6,"preview gain");
      check(std::abs(output[2*(audio.frames/2)+1]-(channels==1?.2f:-.4f))<1e-6,"mono/stereo channel identity");
      check(output[0]>0&&output[0]<.003&&output[2*(audio.frames-1)]<.003&&output[2*audio.frames]==0,"2ms edge fades or end silence");
    }
    PreviewVoice voice;voice.prepare(audio,0,true);std::vector<float> silent(64,1);{AudioAudit::Scope audit;voice.render(silent.data(),32);}check(std::all_of(silent.begin(),silent.end(),[](float v){return v==0;}),"qualification silence applied after DSP");
    audio.pcm[0]=std::numeric_limits<float>::quiet_NaN();audio.pcm[1]=10;voice.prepare(audio,0);voice.gain(1);voice.render(silent.data(),32);check(silent[0]==0&&std::all_of(silent.begin(),silent.end(),[](float v){return std::isfinite(v)&&std::abs(v)<=1;}),"nonfinite/clamped PCM");
  }
  check(AudioAudit::allocations.load()==0&&AudioAudit::deallocations.load()==0,"preview callback allocated/freed C++ storage");
  std::cout<<"PASS mono/stereo, gain, 2ms fades, end silence, nonfinite clamp; partition exact at 44.1/48/96 kHz with blocks 1/17/128/4096; callback C++ allocations/frees = 0\n";
}
static void decoderTests(const fs::path &parent){
  Folder folder(parent);PreviewDecoder decoder;
  for(unsigned channels:{1u,2u})for(unsigned bits:{8u,16u}){
    const auto path=folder.path/(std::to_string(channels)+"-"+std::to_string(bits)+".wav");wav(path,4410,44100,channels,bits);
    auto a=decoder.decode(text(path)).get();const auto data=a->dictionary();
    check(a->frames==4410&&a->totalFrames==4410&&a->rate==44100&&a->channels==channels&&a->pcm.size()==4410*channels,"shared decoder metadata");
    check(a->peaks.size()==512&&!data.contains("pcm"),"bounded waveform / private PCM");
    check(decoder.decode(text(path)).get()==a,"unchanged file cache miss");
    wav(path,4500,48000,channels,bits,20000);auto b=decoder.decode(text(path)).get();check(a!=b&&b->rate==48000&&b->frames==4500,"changed file reused old preview cache");
  }
  const auto longPath=folder.path/"long.wav";wav(longPath,44100*31,44100,1);auto longAudio=decoder.decode(text(longPath)).get();check(longAudio->totalFrames==44100*31&&longAudio->frames==44100*30,"30 second preview bound");
  const auto highPath=folder.path/"high.wav";wav(highPath,2097200,96000,1);auto high=decoder.decode(text(highPath)).get();check(high->frames==2097152&&high->totalFrames==2097200,"preview frame capacity");
  bool rejected=false;try{decoder.decode(text(folder.path/"missing.wav")).get();}catch(const std::exception &){rejected=true;}check(rejected,"missing file accepted");
  std::promise<void> entered,release;auto gate=release.get_future().share();std::atomic<bool> cancelled=false;
  {PreviewDecoder gated([&]{entered.set_value();gate.wait();});
    auto pending=gated.decode(text(longPath),[&]{return cancelled.load();});check(entered.get_future().wait_for(2s)==std::future_status::ready,"decoder gate");cancelled=true;release.set_value();
    rejected=false;try{pending.get();}catch(const Api::ApiError &e){rejected=e.code==-32002;}check(rejected,"late decode escaped cancellation generation");}
  std::cout<<"PASS real 8/16-bit mono/stereo decode, private PCM, waveform, cache invalidation, preview limits and cancellation\n";
}
static void deviceTests(){
  WasapiDevice song;std::atomic<unsigned> songFrames=0;
  auto render=[](void *context,float *out,uint32_t frames)noexcept{std::fill_n(out,size_t(frames)*2,0.f);static_cast<std::atomic<unsigned> *>(context)->fetch_add(frames);};
  check(song.open(render,&songFrames)&&song.start(),"open silent song stream");
  for(unsigned rate:{44100u,48000u,96000u}){
    auto audio=std::make_shared<PreviewAudio>();audio->rate=rate;audio->channels=2;audio->frames=rate/5;audio->totalFrames=audio->frames;audio->pcm.resize(size_t(audio->frames)*2,.1f);
    PreviewPlayer preview;const auto before=songFrames.load();preview.play(audio,.25f,true);
    const auto deadline=std::chrono::steady_clock::now()+3s;while(preview.status()["deviceOpen"].get<bool>()&&std::chrono::steady_clock::now()<deadline){preview.service();std::this_thread::sleep_for(5ms);}
    auto state=preview.status();check(!state["deviceOpen"].get<bool>()&&state["renderedFrames"]==audio->frames&&state["callbacks"]>0,"preview completion/retirement");
    check(state["deviceErrors"]==0&&state["lastError"]==0&&state["mmcssError"]==0,"preview device fault");
    check(song.running()&&songFrames.load()>before,"preview disrupted independent song stream");std::cout<<"DEVICE "<<rate<<' '<<state.dump()<<'\n';
    preview.play(audio,.25f,true);preview.stop();check(!preview.status()["deviceOpen"].get<bool>()&&song.running(),"explicit preview Stop affected song");
  }
  song.stop();std::cout<<"PASS separate quality-converted WASAPI streams, automatic completion and Stop, silent after preview DSP, song remains active\n";
}
int main(int argc,char **argv){try{check(argc==2,"fixture parent or --device required");if(std::string(argv[1])=="--device")deviceTests();else{voiceTests();decoderTests(fs::u8path(argv[1]));}return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
