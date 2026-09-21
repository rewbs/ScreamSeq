#include "editor/hosted/HostedAudio.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  check(NativePlugin::discover().empty(),"Default Windows provider does not advertise unavailable formats");
  for(const char *format:{"AU","VST3"}){
    PluginState missing;missing.descriptor.format=format;missing.descriptor.name="Unavailable fixture";
    missing.instanceID="retained-missing";missing.state={std::byte{0x34},std::byte{0x56}};
    bool rejected=false;try{NativePlugin plugin(missing,48000,true);}catch(const std::runtime_error &e){rejected=std::string(e.what()).find("unavailable")!=std::string::npos;}
    check(rejected&&missing.state==std::vector<std::byte>{std::byte{0x34},std::byte{0x56}}&&missing.instanceID=="retained-missing","Missing foreign plugins fail explicitly, never render pretend dry parity or mutate source state");
  }
  const auto builtins=NativePlugin::builtins();auto gain=std::find_if(builtins.begin(),builtins.end(),[](const auto &p){return p.classID=="resonance.gainer.v1";});
  check(gain!=builtins.end(),"Persisted gainer identity retained");
  for(uint32_t rate:{44100u,48000u,96000u}){
    auto doc=Document::demo();
    NativeEffect effect(gain->classID,rate);check(effect.parameter(1,-12),"Native dB parameter");
    PluginState recipe{*gain};recipe.state=effect.state();recipe.instanceID="gain-state";
    NativePlugin roundtrip(recipe,rate,true);auto state=roundtrip.state();
    check(state.state==recipe.state&&state.instanceID==recipe.instanceID&&state.descriptor.classID==recipe.descriptor.classID,"Built-in opaque state and identity roundtrip");
    const auto parameters=roundtrip.parameters();const auto parameter=std::find_if(parameters.begin(),parameters.end(),[](auto p){return p.id==1;});
    check(parameter!=parameters.end()&&parameter->value==-12&&parameter->min==-96&&parameter->max==24&&parameter->unit==13&&parameter->unitLabel=="dB","Built-in parameter values and units stay native, not normalized");
    auto render=[&](uint32_t block){
      auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);
      auto renderer=std::make_unique<Renderer>(doc->snapshotData(),rate);
      chain->attachInstruments(*renderer,&doc->native());chain->attachMusicalAutomation(*renderer,doc->native());
      check(chain->hasMixer(),"Empty rack still prepares graph and mixer");
      std::vector<float> audio(rate*2);
      for(uint32_t pos=0;pos<rate;pos+=block){const auto count=std::min(block,rate-pos);chain->syncTransport(*renderer);renderer->render(audio.data()+pos*2,count);check(chain->process(audio.data()+pos*2,count)&&!renderer->faulted(),"Built-in graph render");}
      renderer.reset();return audio;
    };
    uint64_t master=0;
    doc->annotate([&](NativeSong &n){master=n.makeEntity().id;for(const auto &[channel,track]:n.tracks)n.mixer.buses.push_back({track.id,master,MixerBusKind::Track,"Track"});n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});});
    auto dry=render(128);
    doc->annotate([&](NativeSong &n){const auto id=n.makeEntity().id,in=n.makeEntity().id,fx=n.makeEntity().id,out=n.makeEntity().id;
      SignalDefinition d;d.id=id;d.number=1;d.name="Built-in master";d.nodes={{in,SignalNodeKind::Input,"Input"},{fx,SignalNodeKind::Plugin,"Gain"},{out,SignalNodeKind::Output,"Output"}};
      d.nodes[1].plugin={gain->format,gain->name,gain->path,gain->classID,0,0,0,recipe.state};d.audio={{in,fx},{fx,out}};n.signal.library={d};n.signal.assignments={{master,id,1,1}};});
    auto wet=render(128);double energy=0,error=0,delta=0;
    for(size_t i=0;i<wet.size();++i){check(std::isfinite(wet[i]),"Graph PCM finite");energy+=std::abs(wet[i]);error=std::max(error,std::abs(double(wet[i])-dry[i]*std::pow(10.,-12./20)));}
    check(energy>1&&error<2e-6,"Empty rack graph audibly applies built-in native gain");
    for(uint32_t block:{17u,4096u}){auto other=render(block);for(size_t i=0;i<wet.size();++i)delta=std::max(delta,std::abs(double(wet[i])-other[i]));}
    check(delta<1e-6,"Empty-rack built-in graph partition bound");
    std::cout<<"empty-rack graph rate="<<rate<<" energy="<<energy<<" gain-error="<<error<<" partition-max="<<delta<<'\n';
  }
  std::cout<<"PASS builtin state/native parameters, explicit unavailable formats and empty-rack graph PCM; functional-only\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
