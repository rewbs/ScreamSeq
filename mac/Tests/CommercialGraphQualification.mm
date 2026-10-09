// Opt-in commercial-plugin qualification. Never enumerates/loads unrelated
// plugins or opens an audio device, and never writes a vendor/user preset.
#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import <AppKit/AppKit.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static uint32_t fourcc(NSString *text){check(text.length==4,"Component needs a four-character identifier");uint32_t out=0;for(NSUInteger i=0;i<4;++i)out=(out<<8)|[text characterAtIndex:i];return out;}
static PluginDescriptor descriptor(const std::string &path){
  if(path.ends_with(".vst3")){auto found=NativePlugin::discoverVST3(path);check(!found.empty(),"No VST3 class found");return found.front();}
  check(path.ends_with(".component"),"Pass one installed .vst3 or .component bundle");
  auto info=[NSDictionary dictionaryWithContentsOfFile:[@(path.c_str()) stringByAppendingPathComponent:@"Contents/Info.plist"]];
  NSDictionary *component=[info[@"AudioComponents"] firstObject];check(component!=nil,"AU component description unavailable");
  PluginDescriptor d;d.type=fourcc(component[@"type"]);d.subtype=fourcc(component[@"subtype"]);d.manufacturer=fourcc(component[@"manufacturer"]);d.name=[component[@"name"] UTF8String];d.instrument=d.type==kAudioUnitType_MusicDevice;return d;
}
static void qualify(const PluginDescriptor &descriptor,bool allowSilent){
  constexpr uint32_t rate=48000,block=512,total=rate*10;
  auto document=Document::demo();document->transaction([&](CSoundFile &s){
    s.Order().assign(16,0);s.m_nInstruments=4;for(INSTRUMENTINDEX i=1;i<=4;++i)s.Instruments[i]=new ModInstrument(i);
    if(descriptor.instrument){
      for(auto &pattern:s.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
      for(ROWINDEX row=0;row<s.Patterns[0].GetNumRows();row+=8){auto &note=*s.Patterns[0].GetpModCommand(row,0);note.note=61;note.instr=1;
        if(row+4<s.Patterns[0].GetNumRows())s.Patterns[0].GetpModCommand(row+4,0)->note=NOTE_KEYOFF;}
    }
  });
  auto native=document->native();native.ensureMixer();
  PluginState state{descriptor};state.instanceID="commercial-qualification";if(descriptor.instrument)state.instrument=1;else native.mixer.buses[0].inserts={state.instanceID};
  Renderer renderer(document->snapshotData(),rate);
  // Match the application: the large prepared host belongs on the heap, not
  // in a default-size worker stack shared with vendor render calls.
  auto owner=std::make_unique<PluginChain>(std::vector<PluginState>{state},rate,true);auto &chain=*owner;chain.attachInstruments(renderer,&native);
  std::array<float,block*2> pcm{};std::array<double,10> energy{};std::array<float,10> peak{};std::array<double,10> maxRenderMicros{};
  const auto instrument=native.instruments.at(1).id,track=native.tracks.at(0).id,route=native.makeEntity().id;
  auto controls=[&]{auto plan=chain.prepareGraphControls(native);check(bool(plan)&&chain.publishGraphControls(std::move(plan)),"Commercial note routing publication failed");};
  for(uint32_t frame=0;frame<total;){
    const auto stage=frame/rate;
    if(frame%rate==0){
      if(descriptor.instrument){
        if(stage==1){native.signal.noteRouting.suppressedAssignments={instrument};controls();}
        if(stage==2){native.signal.noteRouting.suppressedAssignments.clear();controls();}
        if(stage==3){native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,state.instanceID,2,true}};controls();}
        if(stage==4){native.signal.noteRouting.routes.clear();controls();}
      }
      if(stage==5){
        const auto before=chain.noteActivity();auto invalid=native;invalid.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"missing-plugin",0,true}};
        bool rejected=false;try{chain.prepareGraphControls(invalid);}catch(const std::exception &){rejected=true;}
        check(rejected&&!chain.failed()&&chain.noteActivity().requestedGeneration==before.requestedGeneration,"Preparation failure changed the accepted plan");
      }
      if(stage==6&&!descriptor.instrument){auto detached=native;detached.removePluginRoutes(state.instanceID);auto plan=chain.prepareRack({},detached);check(bool(plan)&&chain.publishRack(plan),"Commercial effect removal failed");}
      if(stage==7&&!descriptor.instrument){auto plan=chain.prepareRack({state},native);check(bool(plan)&&chain.publishRack(plan),"Commercial effect restoration failed");}
      if(stage==8)check(chain.bypass(0,true),"Commercial bypass rejected");
      if(stage==9)check(chain.bypass(0,false),"Commercial unbypass rejected");
    }
    const auto frames=std::min({block,total-frame,rate-frame%rate});
    const auto began=std::chrono::steady_clock::now();chain.beginRenderBlock();chain.syncTransport(renderer);
    check(renderer.render(pcm.data(),frames)==frames,"Commercial test transport ended");check(chain.process(pcm.data(),frames)&&!renderer.faulted()&&!chain.failed(),"Commercial audio processing failed");
    const auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count();maxRenderMicros[stage]=std::max(maxRenderMicros[stage],elapsed);
    for(size_t i=0;i<frames*2;++i){check(std::isfinite(pcm[i]),"Commercial plugin produced non-finite audio");energy[stage]+=double(pcm[i])*pcm[i];peak[stage]=std::max(peak[stage],std::abs(pcm[i]));}
    frame+=frames;
  }
  if(!allowSilent)check(energy[0]>1e-8&&energy[9]>1e-8,"Expected nonzero musical output before/after transitions");
  const auto activity=chain.noteActivity();check(activity.fresh&&!chain.failed(),"Commercial engine ended with failed or stale routing");
  for(const auto &r:activity.routes)check(!r.failures,"Commercial MIDI destination rejected events");
  for(size_t i=0;i<energy.size();++i)std::cout<<"stage="<<i<<" rms="<<std::sqrt(energy[i]/(rate*2))<<" peak="<<peak[i]<<" max_render_us="<<maxRenderMicros[i]<<'\n';
  std::cout<<"PASS "<<descriptor.format<<' '<<descriptor.name<<" at "<<rate<<" Hz / "<<block<<" frames: 10 seconds, route edits, failed-preparation retention, "<<(descriptor.instrument?"note ownership":"rack removal/restoration")<<", bypass; no device opened. Vendor-private realtime behavior and speakers not qualified.\n";
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc>=2&&argc<=3,"usage: commercial-graph-qualification <bundle> [--allow-silent]");
  const bool silent=argc==3&&std::string(argv[2])=="--allow-silent";check(argc==2||silent,"Unknown option");
  NSApplication.sharedApplication.activationPolicy=NSApplicationActivationPolicyProhibited;
  trustFixtureArguments(argc,argv);const auto selected=descriptor(argv[1]);
  std::atomic<bool> done{false};std::exception_ptr failure;
  std::thread worker([&]{@autoreleasepool{try{qualify(selected,silent);}catch(...){failure=std::current_exception();}done.store(true,std::memory_order_release);}});
  auto deadline=[NSDate dateWithTimeIntervalSinceNow:180];
  while(!done.load(std::memory_order_acquire)&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
  if(!done.load(std::memory_order_acquire)){std::cerr<<"FAIL commercial plugin preparation/render timeout\n";std::_Exit(1);}worker.join();if(failure)std::rethrow_exception(failure);return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
