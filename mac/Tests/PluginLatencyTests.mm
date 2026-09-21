#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include <cmath>
#include <iostream>
#include "GraphRealtimeAudit.hpp"
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
int main(int argc, char **argv) { @autoreleasepool { try {
  check(argc == 2, "Pass fixture bundle");
  auto descriptors = NativePlugin::discoverVST3(argv[1]);
  void *module = dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(), RTLD_NOW|RTLD_LOCAL);
  auto latency = reinterpret_cast<int(*)(uint32_t)>(dlsym(module,"ResonanceFixtureLatency"));
  auto gesture = reinterpret_cast<int(*)(double)>(dlsym(module,"ResonanceFixtureGesture"));
  auto lifecycle = reinterpret_cast<int(*)(int)>(dlsym(module,"ResonanceFixtureLifecycle"));
  check(latency && gesture && lifecycle, "Fixture hooks");
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
      tracker_audit_begin();const bool ok=p.process(audio.data(),128,0);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(ok && a+f+l==0 && audio[0]==0 && audio[14]==.75f, "Updated processor produces delayed audio without callback allocation/free/lock");
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
      tracker_audit_begin();renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(ok && a+f+l==0 && renderer.telemetry().frames>before,"Playback continues after delay rebuild with no realtime allocations");
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
      tracker_audit_begin();renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(ok && a+f+l==0,"Reconfigured graph remains realtime safe");
    }
  }
  latency(0);
  {
    NativePlugin p(effect,48000);latency(96001);bool rejected=false;
    try{p.refreshLatency();}catch(const std::exception&){rejected=true;}
    check(rejected,"Excessive plugin latency remains rejected");
  }
  latency(0);check(lifecycle(0)==0 && lifecycle(3)==0,"Balanced plugin reactivation and destruction");
  dlclose(module);
  std::cout<<"PASS dynamic VST3 latency: notification, pending-edit save, reactivation, audio, rack/mixer/subgraph compensation, retained transport, 44.1/48/96 kHz and realtime audit\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }}
