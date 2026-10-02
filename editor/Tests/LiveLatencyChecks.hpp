#pragma once
#include "editor/TrackerDocument.hpp"
static void enableLatencyMixer(Document &doc){doc.annotate([](NativeSong &native){const auto master=native.masterID;for(const auto &[channel,track]:native.tracks)native.mixer.buses.push_back({track.id,master,MixerBusKind::Track,"Track"});native.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});});}
template<class Announce,class Activations> static std::vector<float> latencyRender(const PluginState &effect,uint32_t rate,uint32_t block,bool recipe,
    uint32_t fixedLatency,bool live,Announce announce,Activations activations) {
  announce(live?0:fixedLatency);auto doc=Document::demo();enableLatencyMixer(*doc);
  auto native=doc->native();
  if(recipe){SignalDefinition d;d.id=native.makeEntity().id;d.number=1;d.name="Live latency copy";
    const auto input=native.makeEntity().id,fx=native.makeEntity().id,output=native.makeEntity().id;
    d.nodes={{input,SignalNodeKind::Input,"Input"},{fx,SignalNodeKind::Plugin,"Effect"},{output,SignalNodeKind::Output,"Output"}};
    const auto &p=effect.descriptor;d.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};d.audio={{input,fx},{fx,output}};
    native.signal.library.push_back(d);native.signal.assignments={{native.tracks.at(0).id,d.id,1,1}};
  }else native.mixer.buses[0].inserts={effect.instanceID};
  Renderer renderer(doc->serialize(),rate);PluginChain chain(recipe?std::vector<PluginState>{}:std::vector<PluginState>{effect},rate,true);chain.attachInstruments(renderer,&native);
  const auto initialActivations=activations();std::vector<float> output(18000*2);
  for(uint32_t at=0;at<18000;){
    if(live&&(at==1000||at==9000)){check(announce(at==1000?13:3)==1,"Notify one live vendor latency change");check(chain.refreshLatencies(native),"Live latency plan did not publish");}
    auto frames=std::min(block,18000-at);for(auto boundary:{1000u,9000u})if(at<boundary)frames=std::min(frames,boundary-at);
    uint64_t a,f,l;tracker_audit_begin();chain.beginRenderBlock();renderer.render(output.data()+at*2,frames);const auto okay=chain.process(output.data()+at*2,frames);tracker_audit_end(&a,&f,&l);
    check(okay&&!renderer.faulted()&&a+f+l==0,"Live latency transition failed or allocated on callback");at+=frames;
  }
  check(activations()==initialActivations,"Live latency changed vendor activation or reset internal state");
  check(!chain.latencyChangePending()&&std::llround(chain.latency()*rate)==(live?3:fixedLatency),"Latency notification was not acknowledged after exact accepted adoption");
  return output;
}
template<class Announce,class Activations> static void liveLatencyChecks(const PluginState &effect,Announce announce,Activations activations) {
  for(auto rate:{44100u,48000u,96000u})for(bool recipe:{false,true}){
    const auto thirteen=latencyRender(effect,rate,128,recipe,13,false,announce,activations),three=latencyRender(effect,rate,128,recipe,3,false,announce,activations);
    std::vector<float> partition;
    for(auto block:{17u,512u,4096u}){const auto result=latencyRender(effect,rate,block,recipe,0,true,announce,activations);
      for(uint32_t frame=6000;frame<9000;++frame)for(unsigned c=0;c<2;++c)check(std::abs(result[frame*2+c]-thirteen[frame*2+c])<1e-6,"Increased live PDC differs from independent fixed-latency render after settling");
      for(uint32_t frame=14000;frame<18000;++frame)for(unsigned c=0;c<2;++c)check(std::abs(result[frame*2+c]-three[frame*2+c])<1e-6,"Decreased live PDC differs from independent fixed-latency render after settling");
      if(partition.empty())partition=result;else for(size_t i=0;i<result.size();++i)check(std::abs(result[i]-partition[i])<1e-6,"Live latency transition depends on callback partition");
    }
  }
}
