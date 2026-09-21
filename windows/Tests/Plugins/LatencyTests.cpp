#include "editor/hosted/HostedAudio.hpp"
#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <windows.h>
#include <filesystem>

#include <cmath>
#include <iostream>

using namespace Tracker;
using namespace OpenMPT;
static void check(bool b, const char *why) { if (!b) throw std::runtime_error(why); }
static void enable(Document &doc) {
  doc.annotate([](NativeSong &n) {
    auto master = n.makeEntity().id;
    for (const auto &[ch, t] : n.tracks) n.mixer.buses.push_back({t.id,master,MixerBusKind::Track,"Track"});
    n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});
  });
}
int main(int argc, char **argv) { try {
  check(argc==4,"scanner fixture cache");
  WindowsVST3::configure(argv[1],argv[3]);
  auto descriptors=WindowsVST3::rescan(argv[2]);
  auto module=GetModuleHandleW(std::filesystem::u8path(descriptors[0].path).c_str());
  if(!module){auto pin=platformPluginBackendFactory().create(PluginState{descriptors[0]},48000,true);module=LoadLibraryExW(std::filesystem::u8path(descriptors[0].path).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);}
  auto latencyFn=reinterpret_cast<int(*)(uint32_t)>(GetProcAddress(module,"ResonanceFixtureLatency"));
  auto gestureFn=reinterpret_cast<int(*)(double)>(GetProcAddress(module,"ResonanceFixtureGesture"));
  auto lifecycle=reinterpret_cast<int(*)(int)>(GetProcAddress(module,"FixtureCount"));
  check(latencyFn&&gestureFn&&lifecycle,"Fixture hooks");
  auto latency=[&](uint32_t n){int result=0;WindowsVST3::pluginMainCall([&]{result=latencyFn(n);});return result;};
  auto gesture=[&](double n){int result=0;WindowsVST3::pluginMainCall([&]{result=gestureFn(n);});return result;};
  PluginState effect{descriptors.at(0)}; effect.instanceID = "latency-effect";
  for (uint32_t rate : {44100,48000,96000}) {
    latency(0);
    {
      NativePlugin p(effect,rate);
      check(latency(7)==1 && p.latencyChangePending(), "Latency request is accepted, not a failure");
      check(gesture(.75)==1, "Pending editor value before save");
      auto saved=p.state(); check(!saved.state.empty(), "Latency notification does not poison state capture");
      p.refreshLatency(); check(!p.latencyChangePending() && std::llround(p.latency()*rate)==7, "Reactivation updates latency");
      std::array<float,256> audio{};audio[0]=audio[1]=1;
      const bool ok=p.process(audio.data(),128,0);
      check(ok && audio[0]==0 && audio[14]==.75f, "Updated processor produces delayed audio ");
    }
    for (bool mixer : {false,true}) {
      latency(0);
      auto doc=Document::demo(); if(mixer) enable(*doc);
      if(mixer)doc->annotate([&](NativeSong &n){n.mixer.buses[0].inserts={effect.instanceID};});
      Renderer renderer(doc->serialize(),rate);
      PluginChain chain({effect},rate);chain.attachInstruments(renderer,&doc->native());
      std::array<float,256> audio{};
      renderer.render(audio.data(),128);check(chain.process(audio.data(),128),"Initial render");
      const auto before=renderer.telemetry().frames;
      check(latency(7)==1 && chain.latencyChangePending(),"Chain sees latency request");
      chain.refreshLatencies();
      check(!chain.failed() && !chain.latencyChangePending() && std::llround(chain.latency()*rate)==7,"Rack/mixer delay compensation refreshes");
      check(renderer.telemetry().frames==before,"Latency update preserves musical position");
      renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);
      check(ok && renderer.telemetry().frames>before,"Playback continues after delay rebuild ");
      check(latency(0)==1,"Latency may decrease");chain.refreshLatencies();check(chain.latency()==0,"Decreased latency applied");
    }
    latency(0);
    {
      auto doc=Document::demo();enable(*doc);
      doc->annotate([&](NativeSong &n){
        SignalDefinition d;d.id=n.makeEntity().id;d.number=1;d.name="Latency graph";
        const auto input=n.makeEntity().id,fx=n.makeEntity().id,output=n.makeEntity().id;
        d.nodes={{input,SignalNodeKind::Input,"Input"},{fx,SignalNodeKind::Plugin,"Effect"},{output,SignalNodeKind::Output,"Output"}};
        const auto &p=effect.descriptor;d.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};
        d.audio={{input,fx},{fx,output}};n.signal.library.push_back(d);n.signal.assignments={{n.tracks.at(0).id,d.id,1,1}};
      });
      Renderer renderer(doc->serialize(),rate);PluginChain chain({},rate);chain.attachInstruments(renderer,&doc->native());
      std::array<float,256> audio{};renderer.render(audio.data(),128);check(chain.process(audio.data(),128),"Graph initial render");
      const auto activity=chain.graphActivity();check(!activity.empty(),"Active ordinary graph");
      check(latency(7)==1 && chain.latencyChangePending(),"Graph copy latency request");chain.refreshLatencies();
      check(std::llround(chain.latency()*rate)==7 && !chain.failed(),"Subgraph and mixer delay plans update together");
      const auto after=chain.graphActivity();check(after.size()==activity.size() && after[0].order==activity[0].order,"Graph activity retained");
      renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);
      check(ok,"Reconfigured graph renders correctly");
    }
  }
  latency(0);
  {
    NativePlugin p(effect,48000);latency(96001);bool rejected=false;
    try{p.refreshLatency();}catch(const std::exception&){rejected=true;}
    check(rejected,"Excessive plugin latency remains rejected");
  }
  latency(0);check(lifecycle(0)==0 && lifecycle(3)==0,"Balanced plugin reactivation and destruction");
  FreeLibrary(module);
  std::cout<<"PASS dynamic VST3 latency: notification, pending-edit save, reactivation, audio, rack/mixer/subgraph compensation, retained transport, 44.1/48/96 kHz\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
