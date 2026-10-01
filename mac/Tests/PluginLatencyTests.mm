#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "../Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/hosted/RenderOnce.hpp"
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
    auto master = n.masterID;
    for (const auto &[ch, t] : n.tracks) n.mixer.buses.push_back({t.id,master,MixerBusKind::Track,"Track"});
    n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});
  });
}
int main(int argc, char **argv) { trustFixtureArguments(argc, argv); @autoreleasepool { try {
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
    for(uint32_t block:{17u,512u,4096u}) {
      latency(13);NativePlugin plugin(effect,rate,true);check(plugin.parameter(7,.5),"Set bypass comparison gain");
      std::array<float,8192> audio{};
      for(uint32_t position=0;position<6000;) {
        auto frames=std::min(block,6000-position);for(auto boundary:{1000u,1100u,3000u})if(position<boundary)frames=std::min(frames,boundary-position);
        if(position==1000)plugin.bypass(true);if(position==1100)plugin.bypass(false);if(position==3000)plugin.bypass(true);
        audio.fill(1);uint64_t a,f,l;tracker_audit_begin();const auto ok=plugin.process(audio.data(),frames,position);tracker_audit_end(&a,&f,&l);
        check(ok&&a+f+l==0,"VST3 bypass continues processing without callback allocation/free/lock");
        const double fade=std::round(rate*.005),reverse=1-100/fade;
        for(uint32_t i=0;i<frames;++i){const auto frame=position+i;
          const double wet=frame<1000?1:frame<1100?1-(frame-1000)/fade:frame<3000?std::min(1.,reverse+(1-reverse)*(frame-1100)/fade):std::max(0.,1-(frame-3000)/fade);
          const double expected=frame<13?0:1-.5*wet;
          check(std::abs(audio[i*2]-expected)<1e-6&&std::abs(audio[i*2+1]-expected)<1e-6,"VST3 delayed wet/dry and interrupted fade have exact continuous gain");
        }
        position+=frames;
      }
      check(std::llround(plugin.latency()*rate)==13,"Bypass retains vendor latency");
    }
    for(uint32_t block:{17u,512u,4096u}) {
      latency(13);
      NativePlugin reference(effect,rate,true);
      auto processor=std::make_shared<NativePlugin>(effect,rate,true);
      auto shared=std::make_shared<RenderOnce<NativePlugin>>(processor);
      // These represent two prepared plans owning the same unchanged stage.
      auto incoming=shared;
      check(reference.parameter(7,.5) && processor->parameter(7,.5),"Set shared processor reference gain");
      std::array<float,8192> expected{},outgoing{},next{};
      for(uint32_t position=0;position<9000;) {
        const auto frames=std::min(block,9000-position);
        for(uint32_t i=0;i<frames*2;++i) expected[i]=outgoing[i]=next[i]=float(std::sin((position+i/2)*.027)*.5);
        uint64_t a,f,l;tracker_audit_begin();
        bool okay=reference.process(expected.data(),frames,position);
        okay=shared->process(outgoing.data(),frames,position) && okay;
        if(position<6000)okay=incoming->process(next.data(),frames,position) && okay;
        tracker_audit_end(&a,&f,&l);
        check(okay && a+f+l==0,"Shared VST3 output reuse has no callback allocation/free/lock");
        check(std::equal(expected.begin(),expected.begin()+frames*2,outgoing.begin()),"An unchanged latency-bearing processor retains exact state across shared and single-plan rendering");
        if(position<6000)check(std::equal(expected.begin(),expected.begin()+frames*2,next.begin()),"Both plans receive the same current audio without advancing the processor twice");
        if(frames>1)check(!incoming->process(next.data(),frames-1,position),"Mismatched plan chunk boundaries are rejected without reprocessing");
        if(position)check(!incoming->process(next.data(),frames,position-1),"An expired render chunk cannot be replayed from an unrelated cache interval");
        position+=frames;
      }
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
      auto &observations=chain.signalObservation();
      auto token=[&](const std::string &key){const auto p=std::find_if(observations.ports.begin(),observations.ports.end(),[&](const auto &v){return v.key==key;});check(p!=observations.ports.end(),"Latency fixture has a stable observed port");return uint32_t(p-observations.ports.begin()+1);};
      const auto pluginOutput=token("plugin:"+effect.instanceID+"/out/0");
      const auto trackOutput=mixer?token("n"+std::to_string(doc->native().tracks.at(0).id)+"/out/0"):0;
      check(observations.read(pluginOutput).fresh && observations.read(pluginOutput).processorLatency==0,"Initial latency telemetry is measured at the actual processor output");
      check(latency(7)==1 && chain.latencyChangePending(),"Chain sees latency request");
      chain.refreshLatencies();
      check(!chain.failed() && !chain.latencyChangePending() && std::llround(chain.latency()*rate)==7,"Rack/mixer delay compensation refreshes");
      check(renderer.telemetry().frames==before,"Latency update preserves musical position");
      check(observations.read(pluginOutput).available && !observations.read(pluginOutput).fresh && observations.read(pluginOutput).processorLatency==7,
        "Stopped latency refresh updates existing observation metadata and invalidates pre-refresh PCM");
      if(mixer)check(observations.read(trackOutput).processorLatency==7,"Bus telemetry updates aggregate insert latency with the rebuilt mixer");
      tracker_audit_begin();renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(ok && a+f+l==0 && renderer.telemetry().frames>before,"Playback continues after delay rebuild with no realtime allocations");
      check(observations.read(pluginOutput).fresh,"Latency-refreshed processor telemetry becomes current after the next audio block");
      check(latency(0)==1,"Latency may decrease");chain.refreshLatencies();check(chain.latency()==0,"Decreased latency applied");
      check(observations.read(pluginOutput).processorLatency==0 && !observations.read(pluginOutput).fresh,"Decreased latency replaces prior catalogue metadata without reusing old samples");
      if(mixer)check(observations.read(trackOutput).processorLatency==0,"Bus telemetry follows decreased insert latency");
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
      auto withSource=doc->native();withSource.signal.library[0].nodes.push_back({withSource.makeEntity().id,SignalNodeKind::Amount,"Prepared source"});
      auto sourcePlan=chain.prepareGraphControls(withSource);check(sourcePlan&&chain.publishGraphControls(std::move(sourcePlan)),"Prepare a replacement control runtime before latency refresh");
      renderer.render(audio.data(),128);check(chain.process(audio.data(),128),"New source runtime adopts before latency changes");
      auto staleSource=chain.prepareGraphControls(withSource);
      check(latency(7)==1 && chain.latencyChangePending(),"Graph copy latency request");chain.refreshLatencies();
      check(std::llround(chain.latency()*rate)==7 && !chain.failed(),"Subgraph and mixer delay plans update together");
      check(!chain.publishGraphControls(std::move(staleSource)),"Stopped compensation refresh invalidates plans prepared against the old delays");
      withSource.signal.library[0].nodes.push_back({withSource.makeEntity().id,SignalNodeKind::Random,"Second prepared source"});
      auto refreshedSource=chain.prepareGraphControls(withSource);check(refreshedSource&&chain.publishGraphControls(std::move(refreshedSource)),"Further source edits use the adopted runtime's refreshed latency rather than its retired initial plan");
      const auto after=chain.graphActivity();check(after.size()==activity.size() && after[0].order==activity[0].order,"Graph activity retained");
      tracker_audit_begin();renderer.render(audio.data(),128);const bool ok=chain.process(audio.data(),128);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
      check(ok && a+f+l==0,"Reconfigured graph remains realtime safe");
      auto next=doc->native();NativePlugin fresh(effect,rate);next.signal.library[0].nodes[1].plugin.state=fresh.state().state;
      auto preset=chain.prepareGraphControls(next);check(preset&&chain.publishGraphControls(std::move(preset)),"Same actual latency preset remains compatible after stopped graph latency refresh");
      chain.refreshLatencies(); // The opaque preset has not been consumed by audio yet.
      renderer.render(audio.data(),128);check(chain.process(audio.data(),128),"A pending preset adopts before stopped latency refresh and then renders");
      auto route=chain.prepareMixerRouting(next);check(route&&chain.publishMixerRouting(route),"Transition catalogue remains coherent after stopped graph latency refresh");
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
