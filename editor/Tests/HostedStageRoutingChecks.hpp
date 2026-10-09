#pragma once
#include "editor/TrackerDocument.hpp"

// The endpoints here deliberately name the complete active stage. This does
// not claim that a selected recipe copy has its own outer routing endpoint.
static void hostedStageRouting(const PluginDescriptor &descriptor,uint32_t rate,uint32_t block) {
  auto doc=Document::demo();doc->transaction([](OpenMPT::CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(uint32_t row=0;row<pattern.GetNumRows();++row)for(uint16_t channel=1;channel<song.GetNumChannels();++channel)*pattern.GetpModCommand(row,channel)={};});
  auto native=doc->native();native.ensureMixer();const auto source=native.tracks.at(0).id,target=native.masterID;
  auto definition=[&](bool processor){SignalDefinition d;d.id=native.makeEntity().id;d.number=processor?1:2;d.name="Outer stage fixture";
    const auto input=native.makeEntity().id,output=native.makeEntity().id;d.nodes={{input,SignalNodeKind::Input,"Input"},{output,SignalNodeKind::Output,"Output"}};
    if(processor){const auto plugin=native.makeEntity().id;d.nodes.push_back({plugin,SignalNodeKind::Plugin,"Gain"});d.nodes.back().plugin={descriptor.format,descriptor.name,descriptor.path,descriptor.classID,descriptor.type,descriptor.subtype,descriptor.manufacturer};d.audio={{input,plugin},{plugin,output},{plugin,output,0,1}};}
    else d.audio={{input,output},{input,output,1,0}};return d;};
  native.signal.library={definition(true),definition(false)};native.signal.assignments={{source,native.signal.library[0].id,1,1},{target,native.signal.library[1].id,1,1}};
  PluginState effect{descriptor};effect.instanceID="stage-rack-effect";native.mixer.detached={effect.instanceID};rootDetachedMixerPlugin(native.mixer,effect.instanceID,[&]{return native.makeEntity().id;});
  const SignalStageConnection direct{{{},source},{{},target},1,1,-6.020599913279624,true};native.signal.stageConnections={direct};
  Renderer reference(doc->snapshotData(),rate),renderer(doc->snapshotData(),rate);
  auto dry=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true),host=std::make_unique<PluginChain>(std::vector<PluginState>{effect},rate,true);
  auto dryNative=doc->native();dryNative.ensureMixer();dry->attachInstruments(reference,&dryNative);host->attachInstruments(renderer,&native);
  std::array<float,8192> plain{},actual{};uint32_t edit=0;bool sounding=false;const auto warm=uint32_t(std::ceil(rate*.035));
  for(uint32_t position=0;position<22000;) {
    if(position==4096||position==8192||position==12288||position==16384) {
      native.signal.stageConnections.clear();
      if(position==8192)native.signal.stageConnections={{{{},source},{effect.instanceID,0},1,0,0,true},{{effect.instanceID,0},{{},target},0,1,-6.020599913279624,true}};
      if(position==12288)native.signal.stageConnections={direct};
      const auto ports=host->signalObservation().ports.size();auto prepared=host->prepareMixerRouting(native);
      check(prepared&&host->signalObservation().ports.size()==ports,"Stage routing preparation exposed an unaccepted catalogue");
      check(host->publishMixerRouting(prepared),"Stage routing failed to publish a live cable edit");edit=position;
    }
    auto count=std::min(block,22000-position);for(auto boundary:{4096u,8192u,12288u,16384u})if(boundary>position)count=std::min(count,boundary-position);
    tracker_audit_begin();dry->beginRenderBlock();host->beginRenderBlock();dry->syncTransport(reference);host->syncTransport(renderer);reference.render(plain.data(),count);renderer.render(actual.data(),count);
    const bool okay=dry->process(plain.data(),count)&&host->process(actual.data(),count);uint64_t allocations,frees,locks;tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Stage routing failed or touched realtime memory/locks");
    const double scale=position<4096||position>=12288&&position<16384?.75:position>=8192&&position<12288?.625:.5;
    if(!edit||position>=edit+warm)for(uint32_t i=0;i<count*2;++i){sounding|=std::abs(plain[i])>1e-5;
      if(std::abs(actual[i]-plain[i]*scale)>=2e-6){std::cerr<<descriptor.format<<" stage rate="<<rate<<" block="<<block<<" frame="<<position+i/2<<" got="<<actual[i]<<" expected="<<plain[i]*scale<<'\n';check(false,"Stage/rack/stage routing differs from independent PCM scaling");}}
    check(reference.telemetry().frames==renderer.telemetry().frames,"Stage routing reset the sample clock");position+=count;
  }
  check(sounding&&host->mixerRoutingReady(),"Stage routing did not complete its audible handoff");
}

// Only a follower watches these auxiliary outputs: there is no audio cable to
// accidentally activate them or substitute the main channel mix for their PCM.
static void hostedStageFollower(const PluginDescriptor &descriptor,uint32_t rate,uint32_t block) {
  auto doc=Document::demo();doc->transaction([](OpenMPT::CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(uint32_t row=0;row<pattern.GetNumRows();++row)for(uint16_t channel=1;channel<song.GetNumChannels();++channel)*pattern.GetpModCommand(row,channel)={};});
  auto native=doc->native();native.ensureMixer();const auto stage=native.tracks.at(0).id;
  SignalDefinition definition;definition.id=native.makeEntity().id;definition.number=1;definition.name="Follower auxiliary fixture";const auto input=native.makeEntity().id,output=native.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{output,SignalNodeKind::Output,"Output"}};definition.audio={{input,output,0,0,.5},{input,output,0,1,.25},{input,output,0,2,.75},{input,output,1,0}};
  native.signal.library={definition};native.signal.assignments={{stage,definition.id,1,1}};
  PluginState effect{descriptor};effect.instanceID="stage-follower-target";native.mixer.buses.back().inserts={effect.instanceID};
  SignalSongSource follower;follower.node={native.makeEntity().id,SignalNodeKind::Follower,"Exact stage output"};follower.node.attack=follower.node.release=.001;follower.audioStage=stage;follower.output=1;native.signal.songSources={follower};native.signal.songModulation={{follower.node.id,effect.instanceID,7,0,1}};
  Renderer reference(doc->snapshotData(),rate),renderer(doc->snapshotData(),rate);auto dryNative=doc->native();dryNative.ensureMixer();auto dry=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true),host=std::make_unique<PluginChain>(std::vector<PluginState>{effect},rate,true);dry->attachInstruments(reference,&dryNative);host->attachInstruments(renderer,&native);
  std::array<float,8192> plain{},actual{};double envelope=0;const double pole=std::exp(-1/(rate*.001));const auto warm=uint32_t(std::ceil(rate*.035));uint32_t edit=0;bool sounding=false;
  for(uint32_t frame=0;frame<22000;){auto count=std::min(block,22000-frame);for(auto boundary:{4096u,8192u,12288u,16384u})if(boundary>frame)count=std::min(count,boundary-frame);
    if(frame==4096||frame==8192||frame==12288||frame==16384){auto &source=native.signal.songSources[0];source.audioStage=frame==4096?0:stage;source.output=frame==4096?0:frame==8192?2:1;auto prepared=host->prepareMixerRouting(native);check(prepared&&host->publishMixerRouting(prepared),"Stage follower cut/repatch/Undo must publish without stopping transport");edit=frame;}
    tracker_audit_begin();dry->beginRenderBlock();host->beginRenderBlock();dry->syncTransport(reference);host->syncTransport(renderer);reference.render(plain.data(),count);renderer.render(actual.data(),count);const bool okay=dry->process(plain.data(),count)&&host->process(actual.data(),count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);if(!okay||a+f+l){std::cerr<<descriptor.format<<" follower rate="<<rate<<" block="<<block<<" frame="<<frame<<" okay="<<okay<<" alloc="<<a<<" free="<<f<<" locks="<<l<<'\n';check(false,"Stage follower execution allocated/freed/locked or violated source order");}
    const double gain=frame<4096||frame>=12288?.25:frame>=8192?.75:0;
    for(uint32_t i=0;i<count;++i){const double value=std::min(1.,std::max(std::abs(double(plain[i*2])),std::abs(double(plain[i*2+1])))*gain);envelope=value+pole*(envelope-value);
      if(!edit||frame+i>=edit+warm)for(unsigned channel=0;channel<2;++channel){const auto expected=plain[i*2+channel]*.5*(.5+envelope);sounding|=std::abs(expected)>1e-5;if(std::abs(actual[i*2+channel]-expected)>3e-6){std::cerr<<descriptor.format<<" stage follower rate="<<rate<<" block="<<block<<" frame="<<frame+i<<" actual="<<actual[i*2+channel]<<" expected="<<expected<<'\n';check(false,"Stage follower differs from independent exact-aux AR oracle");}}
    }
    check(reference.telemetry().frames==renderer.telemetry().frames,"Stage follower edit changed transport");frame+=count;
  }
  check(sounding&&host->mixerRoutingReady(),"Stage follower fixture did not complete handoff");auto feedback=native;feedback.signal.stageConnections={{{effect.instanceID,0},{{},stage},0,1,0,true}};const auto ports=host->signalObservation().ports.size();bool rejected=false;try{host->prepareMixerRouting(feedback);}catch(const std::invalid_argument &){rejected=true;}check(rejected&&host->mixerRoutingReady()&&host->signalObservation().ports.size()==ports,"Stage follower feedback must reject before publication");
}
