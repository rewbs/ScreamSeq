#pragma once
#include <algorithm>
#include <cmath>
#include <iostream>
// Included by provider tests with check() and the host realtime audit available.
static Tracker::PluginState rackPresetState(Tracker::PluginState state,double rate,float gain) {
  Tracker::NativePlugin prepared(state,rate,true);check(prepared.parameter(7,gain),"Preset fixture gain rejected");
  std::array<float,64> silence{};check(prepared.process(silence.data(),32,0),"Preset fixture preparation failed");
  return prepared.state();
}
static void hostedRackPreset(const Tracker::PluginDescriptor &descriptor,uint32_t rate,uint32_t block,bool laterManual=false) {
  using namespace Tracker;
  auto doc=Document::demo();auto native=doc->native();native.ensureMixer();
  PluginState initial{descriptor};initial.instanceID="live-preset";initial=rackPresetState(initial,rate,.5f);
  auto changed=rackPresetState(initial,rate,.25f);
  for(auto &bus:native.mixer.buses)if(bus.kind==MixerBusKind::Master)bus.inserts={initial.instanceID};
  Renderer reference(doc->serialize(),rate),renderer(doc->serialize(),rate);
  auto referenceChain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);
  auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{initial},rate,true);
  auto referenceNative=native;for(auto &bus:referenceNative.mixer.buses)bus.inserts.clear();
  referenceChain->attachInstruments(reference,&referenceNative);chain->attachInstruments(renderer,&native);
  std::array<float,8192> dry{},actual{};float expected=.5f;uint32_t edit=0;
  for(uint32_t position=0;position<24000;){
    if(position==4096||position==16384){
      const auto target=position==4096?changed:initial;
      const std::array<std::string,1> ids{initial.instanceID};
      auto prepared=chain->prepareRack({target},native,ids);
      check(prepared&&chain->publishRack(prepared),"Compatible rack preset publication rejected");
      expected=position==4096?.25f:.5f;edit=position;
      if(laterManual){check(chain->parameter(0,7,.7f),"Manual edit after preset publication rejected");expected=.7f;}
    }
    if(position==12288){auto routing=chain->prepareMixerRouting(native);check(routing&&chain->publishMixerRouting(routing),"Unrelated routing after a preset must preserve the adopted vendor and manual baseline");}
    auto count=std::min(block,24000-position);for(auto boundary:{4096u,12288u,16384u})if(position<boundary)count=std::min(count,boundary-position);
    tracker_audit_begin();referenceChain->beginRenderBlock();chain->beginRenderBlock();reference.render(dry.data(),count);renderer.render(actual.data(),count);
    const bool okay=referenceChain->process(dry.data(),count)&&chain->process(actual.data(),count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
    check(okay&&a+f+l==0,"Live rack preset caused a render failure or realtime allocation/free/lock");
    for(uint32_t i=0;i<count;++i)if(!edit||position+i>=edit+uint32_t(std::ceil(rate*.035)))for(size_t c=0;c<2;++c)
      if(std::abs(actual[i*2+c]-dry[i*2+c]*expected)>2e-6){std::cerr<<descriptor.format<<" rate="<<rate<<" block="<<block<<" laterManual="<<laterManual<<" frame="<<position+i<<" got="<<actual[i*2+c]<<" expected="<<dry[i*2+c]*expected<<'\n';check(false,"Live rack preset settled PCM disagrees with reference");}
    check(renderer.telemetry().frames==reference.telemetry().frames,"Rack preset reset the musical transport");position+=count;
  }
}
static void hostedPresetRamp(const Tracker::PluginDescriptor &descriptor,uint32_t rate,uint32_t block) {
  using namespace Tracker;
  PluginState initial{descriptor};initial.instanceID="preset-ramp";initial=rackPresetState(initial,rate,.5f);
  auto changed=rackPresetState(initial,rate,.25f);
  NativePlugin plugin(initial,rate,true),reference(initial,rate,true);
  plugin.prepareMusicalAutomation();reference.prepareMusicalAutomation();
  check(plugin.scheduleRamp(7,.2,.8,0,12000,{ParameterOrigin::PatternSlide}),"Fixture ramp rejected");
  check(reference.scheduleRamp(7,.2,.8,0,12000,{ParameterOrigin::PatternSlide}),"Reference ramp rejected");
  std::shared_ptr<NativePlugin::Preset> preset;
  std::array<float,8192> actual{},expected{};
  for(uint32_t position=0;position<16000;){
    if(position==4096){preset=plugin.preparePreset(changed,true,{});check(plugin.presetReady(*preset),"Fresh preset not ready");
      tracker_audit_begin();plugin.adoptPreset(*preset);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Preset adoption touches realtime memory/locks");
      bool rejected=false;try{plugin.preparePreset(initial,true,preset);}catch(const std::exception &){rejected=true;}check(rejected,"Overlapping preset fade accepted");
    }
    auto count=std::min(block,16000-position);if(position<4096)count=std::min(count,4096-position);
    std::fill_n(actual.data(),count*2,.2f);std::fill_n(expected.data(),count*2,.2f);
    tracker_audit_begin();const bool okay=plugin.process(actual.data(),count,position)&&reference.process(expected.data(),count,position);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);
    check(okay&&a+f+l==0,"Preset/ramp render failed or touched realtime memory/locks");
    for(uint32_t i=0;i<count*2;++i)check(std::abs(actual[i]-expected[i])<2e-6,"Preset discarded an active slide or held automation baseline");
    position+=count;
  }
}
