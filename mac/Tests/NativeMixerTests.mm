#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "../Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/hosted/GraphPluginEndpoint.hpp"
#include "soundlib/ModInstrument.h"
#import "../Bridge/TrackerSession.h"
#include <iostream>
#include <dlfcn.h>
std::vector<Tracker::PluginDescriptor> registerFixtureAUs();
void setFixtureAUHiddenGain(float);
uint64_t fixtureAUCreatedCount();
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void enable(Document &doc) {
  doc.annotate([](NativeSong &n) {
    auto master = n.masterID;
    for (const auto &[channel, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
  });
}
static void liveRouting(const PluginState &effect,uint32_t block,bool graphCopies=false,bool newBus=false,bool changedInput=false) {
  auto doc=Document::demo();
  if(graphCopies)doc->transaction([](CSoundFile &song){
    song.m_nInstruments=4;
    for(INSTRUMENTINDEX i=1;i<=4;++i)song.Instruments[i]=new ModInstrument(i);
  });
  enable(*doc);
  auto before=doc->native();before.mixer.buses[1].gainDB=-6;
  before.mixer.buses[2].inserts={effect.instanceID};
  if(graphCopies) {
    auto &definition=before.signal.library.emplace_back();definition.id=before.makeEntity().id;definition.number=1;definition.name="Retained graph copies";
    const auto input=before.makeEntity().id,node=before.makeEntity().id,output=before.makeEntity().id,note=before.makeEntity().id;
    definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Gain"},{output,SignalNodeKind::Output,"Output"},{note,SignalNodeKind::NoteEnvelope,"Note gate"}};
    const auto &d=effect.descriptor;definition.nodes[1].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,effect.state};
    definition.nodes[1].plugin.parameters[7]=.5;definition.audio={{input,node},{node,output}};
    before.signal.assignments={{before.mixer.buses[2].id,definition.id,1,1}};
    before.signal.instrumentAssignments={{before.instruments.at(1).id,definition.id,1,1}};
    before.signal.commands={{before.patterns.begin()->second.id,before.mixer.buses[2].id,definition.id,0,0,SignalCommandKind::Start},
      {before.patterns.begin()->second.id,before.mixer.buses[2].id,definition.id,0,1,SignalCommandKind::Row}};
    NativeSignalGraph membership(before,48000,true);
    auto changedMembers=before;changedMembers.mixer.buses[0].output=before.mixer.buses[2].id;
    check(membership.sameNoteMembership(before) && !membership.sameNoteMembership(changedMembers),"Routing must preserve graph note-envelope membership independently of audio dependencies");
  }
  auto after=before;after.mixer.buses[0].output=after.mixer.buses[1].id;
  if(changedInput)after.mixer.buses[0].output=after.mixer.buses[2].id;
  uint64_t returnID=0;
  if(newBus) {
    returnID=after.makeEntity().id;
    MixerBus added{returnID,after.mixer.buses.back().id,MixerBusKind::Return,"Live return"};added.gainDB=-6;
    after.mixer.buses[0].output=returnID;
    // Inserting before existing buses also verifies source and meter identity
    // mapping, rather than accidentally relying on a stable vector index.
    after.mixer.buses.insert(after.mixer.buses.begin()+1,added);
  }
  Renderer oldRenderer(doc->serialize(),48000),newRenderer(doc->serialize(),48000),liveRenderer(doc->serialize(),48000);
  PluginChain oldChain({effect},48000,true),newChain({effect},48000,true),liveChain({effect},48000,true);
  oldChain.attachInstruments(oldRenderer,&before);newChain.attachInstruments(newRenderer,&after);liveChain.attachInstruments(liveRenderer,&before);
  std::array<float,8192> old{},changed{},actual{};
  bool changedAudio=false;
  for(uint64_t position=0;position<12000;) {
    auto frames=uint32_t(std::min<uint64_t>(block,12000-position));
    for(auto boundary:{4096u,8192u})if(boundary>position)frames=uint32_t(std::min<uint64_t>(frames,boundary-position));
    if(position==4096 || position==8192) {
      const auto observedPorts=liveChain.signalObservation().ports.size();
      auto prepared=liveChain.prepareMixerRouting(position==4096?after:before);
      check(liveChain.signalObservation().ports.size()==observedPorts,"Preparing a routing candidate publishes no bus meter identities");
      check(prepared && liveChain.publishMixerRouting(prepared),"Native host publishes a cable change/Undo without rebuilding renderer, instruments or unaffected VST");
    }
    uint64_t a,f,l;tracker_audit_begin();
    oldChain.syncTransport(oldRenderer);newChain.syncTransport(newRenderer);liveChain.syncTransport(liveRenderer);
    oldRenderer.render(old.data(),frames);newRenderer.render(changed.data(),frames);liveRenderer.render(actual.data(),frames);
    const bool okay=oldChain.process(old.data(),frames)&&newChain.process(changed.data(),frames)&&liveChain.process(actual.data(),frames);
    tracker_audit_end(&a,&f,&l);
    check(okay && a+f+l==0,"Actual hosted live routing uses no realtime allocation, free or lock");
    for(uint32_t i=0;i<frames;++i) {
      const auto at=position+i;
      const float amount=at<4096?0:at<8192?std::min(1.f,float(at-4096)/480):std::max(0.f,1-float(at-8192)/480);
      for(uint32_t channel=0;channel<2;++channel) {
        const auto sample=i*2+channel;const auto expected=old[sample]+(changed[sample]-old[sample])*amount;
        check(std::abs(actual[sample]-expected)<2e-6f,"Hosted cable change matches continuous old/new PCM and 10ms fade, including reverse/Undo");
        changedAudio|=std::abs(changed[sample]-old[sample])>1e-4f;
      }
    }
    check(oldRenderer.telemetry().frames==liveRenderer.telemetry().frames,"Routing preserves the source transport and held-note clock");
    if(newBus && position>=4608 && position<8192) {
      auto &observation=liveChain.signalObservation();const auto key="n"+std::to_string(returnID)+"/out/0";
      const auto port=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.key==key;});
      check(port!=observation.ports.end() && observation.read(uint32_t(port-observation.ports.begin()+1)).through==position+frames,
        "A bus added during playback has a real meter under its stable identity");
    }
    if(newBus && position>=8192) {
      auto &observation=liveChain.signalObservation();const auto key="n"+std::to_string(returnID)+"/out/0";
      const auto port=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.key==key;});
      check(port!=observation.ports.end() && !observation.read(uint32_t(port-observation.ports.begin()+1)).available,
        "Removing a live return retires its meter at actual plan adoption while keeping its stable catalogue identity for Undo");
    }
    position+=frames;
  }
  check(changedAudio && liveChain.mixerRoutingReady(),"Live fixture has an audible routing difference and completed handoffs");
  auto affected=after;affected.mixer.buses[0].output=before.mixer.buses[2].id;
  check(bool(liveChain.prepareMixerRouting(affected))!=graphCopies,"Retained VST input can morph while note-dependent graph membership still requires preparation");
  if(graphCopies) {
    auto controls=before;controls.signal.library[0].nodes[1].plugin.parameters[7]=.7;
    auto plan=liveChain.prepareGraphControls(controls);
    check(plan && liveChain.publishGraphControls(std::move(plan)),"Publish controls before an unrelated route edit");
    check(bool(liveChain.prepareMixerRouting(controls)),"Route preparation compares the latest published graph controls, not stale construction values");
    check(!liveChain.prepareMixerRouting(before),"A combined control/routing Undo must not silently omit changed graph parameters");
    auto topology=controls;topology.signal.instrumentAssignments.clear();
    check(!liveChain.prepareMixerRouting(topology),"Removing a sample-graph source still requires a separately prepared plan");
  }
}
static void liveRouteObservations(const PluginState &effect,uint32_t block) {
  auto doc=Document::demo();enable(*doc);auto before=doc->native();
  before.mixer.buses[0].inserts={effect.instanceID};before.mixer.buses[0].gainDB=-6;
  before.mixer.buses[0].sends={{before.mixer.buses[1].id,-12,true}};
  auto after=before;after.mixer.buses[0].sends[0].gainDB=-18;after.mixer.buses[0].sends[0].preFader=false;
  auto disconnected=before;disconnected.mixer.buses[0].sends[0].enabled=false;
  Renderer renderer(doc->serialize(),48000);PluginChain chain({effect},48000,true);chain.attachInstruments(renderer,&before);
  auto &observation=chain.signalObservation();const auto source="n"+std::to_string(before.mixer.buses[0].id),target="n"+std::to_string(before.mixer.buses[1].id);
  const auto send=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.route && p.route->kind=="send" && p.route->source==source && p.route->target==target;});
  const auto output=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.key==source+"/out/0";});
  check(send!=observation.ports.end() && output!=observation.ports.end() && send->route->tap=="post-gain","Host exposes exact immutable route descriptors beside physical ports");
  const auto token=uint32_t(send-observation.ports.begin()+1),physical=uint32_t(output-observation.ports.begin()+1);observation.scope.watch(token);
  std::array<float,8192> audio{};bool sounding=false;
  for(uint32_t position=0;position<5120;) {
    auto count=std::min(block,5120-position);for(auto boundary:{1024u,2048u,3072u,4096u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024||position==2048||position==3072||position==4096){auto plan=chain.prepareMixerRouting(position==1024?after:position==3072?disconnected:before);check(plan&&chain.publishMixerRouting(plan),"Live route gain/tap/disconnect/Undo uses prepared publication");}
    uint64_t a,f,l;tracker_audit_begin();chain.syncTransport(renderer);renderer.render(audio.data(),count);const auto okay=chain.process(audio.data(),count);tracker_audit_end(&a,&f,&l);
    check(okay&&a+f+l==0,"Exact live route capture is realtime safe");
    const auto meter=observation.read(token),out=observation.read(physical);const bool enabled=position<3072||position>=4096;
    check(meter.available==enabled,"Route membership follows actual adoption through disable and Undo");
    if(enabled){const bool pre=position<1024||position>=2048;const double gain=std::pow(10.,(pre?-12.:-18.)/20);
      check(meter.fresh && meter.preFader==pre && std::abs(meter.routeGain-gain)<1e-12 && meter.compensation==0,"Route metadata follows adopted gain/tap under a stable key");
      const auto expected=out.rmsLeft*gain/(pre?std::pow(10.,-6./20):1.);
      check(std::abs(meter.rmsLeft-expected)<1e-6,"Observed send is the actual selected pre/post-fader contribution with its own gain, not generic source output");
      sounding|=meter.rmsLeft>1e-4;
    }else check(observation.scope.snapshot().frames==0,"A disabled selected route cannot retain stale scope PCM");
    position+=count;
  }
  check(sounding && observation.read(token).fresh && observation.scope.snapshot().frames>0,"Undo restores the same route scope with current PCM");
}
static void liveRack(const PluginState &effect,uint32_t block) {
  auto doc=Document::demo();enable(*doc);auto before=doc->native();before.mixer.buses.back().inserts={effect.instanceID};
  auto added=effect;added.instanceID="live-added";auto with=before;with.mixer.buses.back().inserts={added.instanceID,effect.instanceID};
  with.mixer.buses.push_back({with.makeEntity().id,with.masterID,MixerBusKind::Return,"New rack-edit return"});
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);
  PluginChain dry({},48000,true),live({effect},48000,true);
  dry.attachInstruments(reference,&before);live.attachInstruments(renderer,&before);live.attachMusicalAutomation(renderer,before);
  check(live.parameter(0,7,.75),"Queue authoritative manual value before live add");
  std::array<float,8192> a{},b{};bool audible=false;
  for(uint32_t position=0;position<12000;){auto frames=std::min(block,12000-position);for(auto boundary:{2000u,4000u,6000u,8000u,10000u})if(boundary>position)frames=std::min(frames,boundary-position);
    if(position==2000||position==8000){auto plan=live.prepareRack({effect,added},with);check(plan&&live.publishRack(plan),"Live add and Undo restore a prepared effect without rebuilding retained processors");}
    if(position==4000){
      check(live.parameter(0,7,.25),"Queue a parameter before changing rack indexes");
      auto plan=live.prepareRack({added,effect},with);check(plan&&live.publishRack(plan),"Rack reorder publishes stable logical identities");
    }
    if(position==6000){auto plan=live.prepareRack({effect},before);check(plan&&live.publishRack(plan),"Live removal retains the uninterrupted source");}
    if(position==10000)check(live.parameter(1,7,1),"Newly inserted processor is controllable at its current rack index");
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);
    reference.render(a.data(),frames);renderer.render(b.data(),frames);const bool okay=dry.process(a.data(),frames)&&live.process(b.data(),frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Live effect add/remove/reorder callbacks allocate, free and lock nothing");
    for(uint32_t i=0;i<frames;++i){const auto at=position+i;double gain=.75;
      if(at>=2000&&at<4000)gain=.75*(1-.5*std::min(1.,(at-2000)/480.));
      else if(at>=4000&&at<6000)gain=.125;
      else if(at>=6000&&at<8000)gain=.125+.125*std::min(1.,(at-6000)/480.);
      else if(at>=8000&&at<10000)gain=.25-.125*std::min(1.,(at-8000)/480.);
      else if(at>=10000)gain=.25;
      for(size_t c=0;c<2;++c){audible|=std::abs(a[i*2+c])>1e-5;check(std::abs(b[i*2+c]-a[i*2+c]*gain)<2e-6,"Live rack PCM preserves opaque/manual state and stable queued parameter destinations");}
    }
    if(position>=2000) {
      auto &observation=live.signalObservation();const auto key="plugin:"+added.instanceID+"/out/0";
      const auto port=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.key==key;});
      check(port!=observation.ports.end(),"New processor publishes a stable observation identity");
      const auto value=observation.read(uint32_t(port-observation.ports.begin()+1));const bool active=position<6000||position>=8000;
      check(value.available==active && (!active||value.fresh),"Live Add/remove/Undo publishes only current processor meters with freshly rendered PCM");
    }
    check(reference.telemetry().frames==renderer.telemetry().frames,"Live rack edits preserve the exact sample voice clock");position+=frames;
  }
  check(audible&&live.mixerRoutingReady(),"Live rack fixture remains audible and settles");
  const auto portCount=live.signalObservation().ports.size(),activityCount=live.parameterActivity().processors.size();
  for(unsigned attempt=0;attempt<8;++attempt) {
    auto rejected=added;rejected.instanceID="rejected-"+std::to_string(attempt);auto cycle=with;cycle.mixer.buses[0].output=cycle.mixer.buses[1].id;cycle.mixer.buses[1].output=cycle.mixer.buses[0].id;
    bool refused=false;try{live.prepareRack({effect,added,rejected},cycle);}catch(const std::exception &){refused=true;}
    check(refused&&live.signalObservation().ports.size()==portCount&&live.parameterActivity().processors.size()==activityCount,"Failed live plugin candidates publish no orphan port or parameter identities");
  }
  auto automated=with;MusicalAutomationLane lane;lane.id=automated.makeEntity().id;lane.pattern=automated.patterns.begin()->second.id;lane.plugin=added.instanceID;lane.parameter=7;lane.points={{0,.2}};automated.automation.push_back(lane);
  live.updateMusicalAutomation(automated);
  auto reordered=live.prepareRack({added,effect},automated);check(reordered&&live.publishRack(reordered),"Prepare rack reorder beside a newly published automation lane");
  for(unsigned n=0;n<4;++n){uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);
    reference.render(a.data(),512);renderer.render(b.data(),512);const auto okay=dry.process(a.data(),512)&&live.process(b.data(),512);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Newly added effect envelope remains realtime safe across rack reorder");
    for(size_t i=0;i<1024;++i)check(std::abs(b[i]-a[i]*.05)<2e-6,"New effect automation targets stable processor rather than its former rack slot");}
  const auto final=live.states();check(final.size()==2&&final[1].instanceID==effect.instanceID&&final[0].instanceID==added.instanceID,"Stopped state capture follows current rack order");
}
static void liveRecipeParameters(const PluginState &effect,uint32_t block) {
  auto doc=Document::demo();enable(*doc);auto before=doc->native();
  auto &definition=before.signal.library.emplace_back();definition.id=before.makeEntity().id;definition.number=1;definition.name="Live controls";
  const auto input=before.makeEntity().id,node=before.makeEntity().id,output=before.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Gain"},{output,SignalNodeKind::Output,"Output"}};
  const auto &d=effect.descriptor;definition.nodes[1].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,effect.state};
  definition.nodes[1].plugin.parameters[7]=.3;definition.audio={{input,node},{node,output}};
  before.signal.assignments={{before.mixer.buses.back().id,definition.id,1,1}};
  auto next=before;next.signal.library[0].nodes[1].plugin.parameters[7]=.7;next.signal.library[0].audio[0].gain=.5;
  auto undo=before;undo.signal.library[0].nodes[1].plugin.parameters.clear();
  NativePlugin probe(effect,48000,true);double preset=.5;for(const auto &p:probe.parameters())if(p.id==7)preset=p.value;
  Renderer dryRenderer(doc->serialize(),48000),liveRenderer(doc->serialize(),48000);
  PluginChain dry({},48000,true),live({},48000,true);dry.attachInstruments(dryRenderer,&doc->native());live.attachInstruments(liveRenderer,&before);
  std::array<float,8192> a{},b{};bool sounding=false;
  for(uint32_t position=0;position<4096;){auto frames=std::min(block,4096-position);for(auto boundary:{1024u,2048u,3072u})if(boundary>position)frames=std::min(frames,boundary-position);
    if(position==1024){
      for(double value:{.4,.6,.7}){auto edit=next;edit.signal.library[0].nodes[1].plugin.parameters[7]=value;auto p=live.prepareGraphControls(edit);check(p&&live.publishGraphControls(std::move(p)),"Queue a complete parameter snapshot");}
      bool refused=false;try{live.prepareGraphControls(before);}catch(const std::runtime_error &){refused=true;}check(refused,"Queue pressure must reject before committing document state");
    }
    if(position==2048||position==3072){auto p=live.prepareGraphControls(position==2048?undo:next);check(p&&live.publishGraphControls(std::move(p)),"Parameter Undo/Redo is a live control publication");}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(dryRenderer);live.syncTransport(liveRenderer);
    dryRenderer.render(a.data(),frames);liveRenderer.render(b.data(),frames);const bool okay=dry.process(a.data(),frames)&&live.process(b.data(),frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Latest-wins graph parameters and Undo render without realtime allocation/free/lock");
    const auto gain=position<1024?.3:position<2048?.35:position<3072?preset:.35;
    for(uint32_t i=0;i<frames*2;++i){sounding|=std::abs(a[i])>1e-5;check(std::abs(b[i]-a[i]*gain)<2e-6,"Live graph parameter/Undo PCM differs from uninterrupted source and expected gain");}
    const double baseline=position<1024?.3:position<2048?.7:position<3072?preset:.7;
    for(const auto &processor:live.parameterActivity().processors)if(processor.graph==definition.id&&processor.node==node)
      for(const auto &parameter:processor.parameters)if(parameter.id==7)check(std::abs(parameter.value-baseline)<1e-6,"Parameter activity catalog follows live edits and Undo baselines");
    check(dryRenderer.telemetry().frames==liveRenderer.telemetry().frames,"Parameter edit preserves held-note source clock");position+=frames;
  }
  check(sounding,"Parameter fixture must contain sounding sample voices");
  if(probe.parameters().size()>512){auto excessive=next;for(uint32_t id=20001;id<=20129;++id)excessive.signal.library[0].nodes[1].plugin.parameters[id]=.8;
    bool rejected=false;try{live.prepareGraphControls(excessive);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Oversized live VST3 parameter batch is rejected before publication");
  }
  auto structural=next;structural.signal.library[0].audio[0].target=output;check(!live.prepareGraphControls(structural),"Audio routing cannot be misclassified as a parameter-only edit");
}
static void livePreparedSidechain(PluginState effect,uint32_t block,bool builtin,bool automatic=true,bool timedMode=false) {
  auto doc=Document::demo(),detectorDoc=Document::demo();enable(*doc);
  detectorDoc->transaction([](CSoundFile &song){for(ROWINDEX row=0;row<song.Patterns[0].GetNumRows();++row)for(CHANNELINDEX ch=0;ch<song.GetNumChannels();++ch)if(ch!=1)*song.Patterns[0].GetpModCommand(row,ch)={};});
  if(builtin){NativePlugin configured(effect,48000,true);check(configured.parameter(1,-40)&&configured.parameter(2,8)&&configured.parameter(9,automatic?2:1),"Configure independent detector dynamics");effect.state=configured.state().state;}
  auto native=doc->native();native.mixer.buses.back().inserts={effect.instanceID};
  std::vector<ParameterChange> modeEvents;if(timedMode)modeEvents={{0,9,1,1973},{0,9,2,2129},{0,9,1,3509},{0,9,2,3737}};
  Renderer renderer(doc->serialize(),48000),reference(doc->serialize(),48000),detector(detectorDoc->serialize(),48000);PluginChain live({effect},48000,true,modeEvents);live.attachInstruments(renderer,&native);
  auto explicitInput=effect;explicitInput.auxiliaryInputs={1};NativePlugin referencePlugin(explicitInput,48000,true);referencePlugin.automate(modeEvents,0,48000,0);
  const auto buses=live.buses(0);check(std::any_of(buses.begin(),buses.end(),[](const auto &bus){return bus.input&&bus.index==1&&!bus.active;}),"Detector starts logically inactive");
  std::array<float,8192> actual{},expected{},side{},mixed{};bool different=false;
  for(uint32_t position=0;position<6500;){auto count=std::min(block,6500-position);for(auto boundary:{1700u,3400u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1700||position==3400){native.mixer.sidechains=position==1700?std::vector<MixerSidechain>{{native.tracks.at(1).id,effect.instanceID,1,0,false,true}}:std::vector<MixerSidechain>{};auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Previously inactive but physically prepared detector input connects/disconnects during playback");}
    const bool external=position>=1700&&position<3880;
    uint64_t allocations,frees,locks;tracker_audit_begin();live.beginRenderBlock();live.syncTransport(renderer);renderer.render(actual.data(),count);reference.render(expected.data(),count);detector.render(side.data(),count);
    for(uint32_t i=0;i<count;++i){const auto at=position+i;const double mix=at<1700?0:at<3400?std::min(1.,double(at-1700)/480):1-std::min(1.,double(at-3400)/480);
      for(unsigned c=0;c<2;++c){const auto index=i*2+c;const bool autoAtFrame=automatic&&(!timedMode||!((at>=1973&&at<2129)||(at>=3509&&at<3737)));const float source=builtin&&autoAtFrame?expected[index]:0;mixed[index]=source+float((side[index]-source)*mix);}}
    MixerAudioInput input{1,mixed.data()};const bool okay=live.process(actual.data(),count)&&referencePlugin.process(expected.data(),count,position,external?std::span<const MixerAudioInput>(&input,1):std::span<const MixerAudioInput>{});tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Prepared detector input retains real DSP and callback safety");
    if(position>=2300&&position+count<3400){const auto &observation=live.signalObservation();const auto key="plugin:"+effect.instanceID+"/in/1";const auto port=std::find_if(observation.ports.begin(),observation.ports.end(),[&](const auto &p){return p.key==key;});check(port!=observation.ports.end(),"Prepared detector keeps a stable observation identity");const auto meter=observation.read(uint32_t(port-observation.ports.begin()+1));check(meter.measured&&meter.through==position+count&&(meter.peakLeft>0||meter.peakRight>0),"First live cable produces current nonzero detector input telemetry");}
    for(uint32_t i=0;i<count*2;++i){if(std::abs(actual[i]-expected[i])>=3e-6){std::cerr<<"detector mismatch "<<builtin<<" at "<<position+i/2<<" actual "<<actual[i]<<" expected "<<expected[i]<<'\n';check(false,"Self/external detector interpolation matches independently clocked processor PCM");}different|=std::abs(actual[i])>1e-6;}
    position+=count;
  }
  check(different&&live.mixerRoutingReady(),"Live detector fixture rendered audible data and settled");
}
static void liveRecipePreset(const PluginState &effect,uint32_t block,void (*hidden)(float),uint64_t (*created)(),bool sampleCopies=false) {
  hidden(.25f);PluginState quieter;{NativePlugin plugin(effect,48000,true);quieter=plugin.state();}hidden(1);
  NativePlugin catalog(effect,48000,true),replacement(quieter,48000,true);
  check(catalog.parameters()[0].value==replacement.parameters()[0].value,"Opaque preset fixture changes audible state without changing exposed parameters");
  auto doc=Document::demo();if(sampleCopies)doc->transaction([](CSoundFile &song){song.m_nInstruments=1;song.Instruments[1]=new ModInstrument(1);for(ROWINDEX row=0;row<song.Patterns[0].GetNumRows();++row)for(CHANNELINDEX ch=1;ch<song.GetNumChannels();++ch)*song.Patterns[0].GetpModCommand(row,ch)={};});enable(*doc);auto native=doc->native();
  auto &definition=native.signal.library.emplace_back();definition.id=native.makeEntity().id;definition.number=1;definition.name="Opaque shared preset";
  const auto input=native.makeEntity().id,node=native.makeEntity().id,unchanged=native.makeEntity().id,output=native.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Changed"},{unchanged,SignalNodeKind::Plugin,"Retained"},{output,SignalNodeKind::Output,"Output"}};
  const auto &d=effect.descriptor;for(unsigned i:{1u,2u})definition.nodes[i].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,effect.state};
  definition.audio={{input,node},{node,unchanged},{unchanged,output}};native.signal.assignments={{native.masterID,definition.id,1,1}};
  native.signal.commands={{native.patterns.at(0).id,native.masterID,definition.id,0,0,SignalCommandKind::Start},{native.patterns.at(0).id,native.masterID,definition.id,0,1,SignalCommandKind::Row}};
  if(sampleCopies)native.signal.instrumentAssignments={{native.instruments.at(1).id,definition.id,1,1}};
  const auto copies=3+(sampleCopies?doc->song().GetNumChannels()+1:0);
  // First row lasts 5760 frames: all three independent roles remain audible.
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double from=.5,to=.5;uint32_t changeFrame=0;
  for(uint32_t position=0;position<5000;){auto count=std::min(block,5000-position);for(auto boundary:{1024u,1100u,1536u,2048u,3072u,4096u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024||position==2048||position==3072){
      native.signal.library[0].nodes[1].plugin.state=position==2048?effect.state:quieter.state;native.signal.library[0].nodes[1].plugin.parameters.clear();
      const auto before=created();auto plan=live.prepareGraphControls(native);check(plan&&created()-before==copies,"Opaque replacement constructs only the changed node in each row/persistent/ordinary copy");
      check(live.publishGraphControls(std::move(plan)),"Full recipe state publishes to all uses without stopping");from=position==2048?.15:.5;to=position==2048?.5:.125;changeFrame=position;
      auto busy=native;busy.signal.library[0].nodes[1].plugin.state=position==2048?quieter.state:effect.state;bool rejected=false;try{live.prepareGraphControls(busy);}catch(const std::runtime_error &){rejected=true;}check(rejected,"A second opaque preset waits for adopted fade, preserving old and new vendor lifetime");
      // An ordinary snapshot can coalesce the unconsumed preset safely.
      auto carried=live.prepareGraphControls(native);check(carried&&live.publishGraphControls(std::move(carried)),"Coalesced controls retain pending replacement and predecessor owners");
    }
    if(position==1100){native.signal.library[0].nodes[1].plugin.parameters[7]=.6;auto plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"Parameter edits fan out to both fading vendor incarnations");from=.6;to=.15;}
    if(position==1536||position==4096){auto stale=live.prepareGraphControls(native);auto latest=live.prepareGraphControls(native);check(live.publishGraphControls(std::move(latest))&&!live.publishGraphControls(std::move(stale)),"Prepared graph control revision rejects a stale replacement snapshot");}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Opaque vendor adoption/crossfade has no host callback allocations/frees/locks");
    for(uint32_t i=0;i<count;++i){const double mix=changeFrame?std::min(1.,double(position+i-changeFrame+1)/480):1;const double gain=std::pow((from+(to-from)*mix)*.5,sampleCopies?4:3);for(unsigned c=0;c<2;++c)check(std::abs(b[i*2+c]-a[i*2+c]*gain)<2e-6,"Opaque state, parameter fanout, independent roles and retained neighbours produce exact smooth PCM");}position+=count;
  }
}
static void liveRecipeBypass(const PluginState &effect,uint32_t block,void (*hidden)(float),uint64_t (*created)(),bool sampleCopies=false) {
  hidden(.25f);PluginState quieter;{NativePlugin plugin(effect,48000,true);quieter=plugin.state();}hidden(1);
  auto doc=Document::demo();if(sampleCopies)doc->transaction([](CSoundFile &song){song.m_nInstruments=1;song.Instruments[1]=new ModInstrument(1);for(ROWINDEX row=0;row<song.Patterns[0].GetNumRows();++row)for(CHANNELINDEX ch=1;ch<song.GetNumChannels();++ch)*song.Patterns[0].GetpModCommand(row,ch)={};});enable(*doc);auto native=doc->native();
  auto &definition=native.signal.library.emplace_back();definition.id=native.makeEntity().id;definition.number=1;definition.name="Shared bypass";
  const auto input=native.makeEntity().id,node=native.makeEntity().id,output=native.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Effect"},{output,SignalNodeKind::Output,"Output"}};
  const auto &d=effect.descriptor;definition.nodes[1].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,effect.state};definition.nodes[1].plugin.parameters[7]=.6;
  definition.audio={{input,node},{node,output}};native.signal.assignments={{native.masterID,definition.id,1,1}};
  native.signal.commands={{native.patterns.at(0).id,native.masterID,definition.id,0,0,SignalCommandKind::Start},{native.patterns.at(0).id,native.masterID,definition.id,0,1,SignalCommandKind::Row}};
  if(sampleCopies)native.signal.instrumentAssignments={{native.instruments.at(1).id,definition.id,1,1}};
  const size_t copies=3+(sampleCopies?doc->song().GetNumChannels()+1:0);
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double wet=1,from=1,to=1;uint32_t elapsed=240;
  for(uint32_t at=0;at<5000;){auto count=std::min(block,5000-at);for(auto boundary:{1024u,1100u,1536u,2048u,3072u,4096u})if(boundary>at)count=std::min(count,boundary-at);
    if(at==1024||at==1100||at==2048||at==4096){const bool bypass=at==1024||at==2048;native.signal.library[0].nodes[1].plugin.bypass=bypass;
      const auto before=created();auto plan=live.prepareGraphControls(native);check(plan&&plan->bypasses.size()==copies&&created()==before,"Bypass prepares every independent copy without recreating a processor");
      check(live.publishGraphControls(std::move(plan)),"Recipe bypass publishes without stopping playback");
      // A subsequent snapshot may skip an unconsumed publication but must
      // carry its complete bypass state, including dormant instrument copies.
      auto carried=live.prepareGraphControls(native);check(carried&&live.publishGraphControls(std::move(carried))&&created()==before,"Coalesced controls preserve the complete bypass request");
      from=wet;to=bypass?0:1;elapsed=0;
    }
    if(at==1536){const auto before=created();for(bool bypass:{true,false}){native.signal.library[0].nodes[1].plugin.bypass=bypass;auto plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"Opposing unconsumed bypass snapshots remain publishable");}check(created()==before,"Cancelling an unconsumed bypass cannot rebuild DSP");}
    if(at==3072){native.signal.library[0].nodes[1].plugin.state=quieter.state;const auto before=created();auto plan=live.prepareGraphControls(native);
      check(plan&&created()-before==copies,"Bypassed opaque replacement prepares exactly the changed vendors");
      for(const auto &preset:plan->presets)check(preset.state->plugin->bypassed(),"Replacement must initialize from the latest requested bypass, not its original recipe flag");
      check(live.publishGraphControls(std::move(plan)),"Bypassed full preset replacement preserves the live plan");
    }
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Recipe bypass publication and all graph uses remain realtime-safe");
    for(uint32_t i=0;i<count;++i){const double enabled=at+i<3072?.6:.15,gain=std::pow(1+(enabled-1)*wet,sampleCopies?4:3);
      for(unsigned c=0;c<2;++c)check(std::abs(b[i*2+c]-a[i*2+c]*gain)<2e-6,"Shared recipe bypass misses a role/copy or changes its manual baseline");
      if(elapsed<240)++elapsed;wet=from+(to-from)*double(elapsed)/240;
    }at+=count;
  }
}
static void liveRecipeSources(const PluginState &effect,uint32_t block,SignalNodeKind kind,void (*hidden)(float),uint64_t (*created)(),bool sampleCopies=false) {
  hidden(.25f);PluginState quieter;{NativePlugin plugin(effect,48000,true);quieter=plugin.state();}hidden(1);
  auto doc=Document::demo();if(sampleCopies)doc->transaction([](CSoundFile &song){song.m_nInstruments=1;song.Instruments[1]=new ModInstrument(1);for(ROWINDEX row=0;row<song.Patterns[0].GetNumRows();++row)for(CHANNELINDEX ch=1;ch<song.GetNumChannels();++ch)*song.Patterns[0].GetpModCommand(row,ch)={};});enable(*doc);auto native=doc->native();
  auto &d=native.signal.library.emplace_back();d.id=native.makeEntity().id;d.number=1;d.name="Prepared sources";
  const auto input=native.makeEntity().id,node=native.makeEntity().id,output=native.makeEntity().id,source=native.makeEntity().id,unused=native.makeEntity().id;
  d.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Retained vendor"},{output,SignalNodeKind::Output,"Output"}};
  const auto &descriptor=effect.descriptor;d.nodes[1].plugin={descriptor.format,descriptor.name,descriptor.path,descriptor.classID,descriptor.type,descriptor.subtype,descriptor.manufacturer,effect.state};d.nodes[1].plugin.parameters[7]=.3;d.audio={{input,node},{node,output}};
  native.signal.assignments={{native.masterID,d.id,1,1}};native.signal.commands={{native.patterns.at(0).id,native.masterID,d.id,0,0,SignalCommandKind::Start},{native.patterns.at(0).id,native.masterID,d.id,0,1,SignalCommandKind::Row}};
  if(sampleCopies)native.signal.instrumentAssignments={{native.instruments.at(1).id,d.id,1,1}};
  auto addSource=[&](NativeSong &song,double depth,double inputGain){auto &definition=song.signal.library[0];definition.nodes.push_back({source,kind,"New source"});auto &n=definition.nodes.back();n.rate=13.5;n.phase=.137;n.attack=.005;n.release=.01;n.controller=74;
    if(kind==SignalNodeKind::Automation)n.envelopes={{song.patterns.at(0).id,true,{{0,.2,AutomationCurve::Linear},{256,.8}}}};
    definition.modulation={{source,node,7,0,depth,.3,true}};if(kind==SignalNodeKind::Follower)definition.audio.push_back({input,source,0,0,inputGain});
  };
  auto referenceNative=native;addSource(referenceNative,0,0);
  Renderer actualRenderer(doc->serialize(),48000),referenceRenderer(doc->serialize(),48000);PluginChain live({},48000,true),reference({},48000,true);live.attachInstruments(actualRenderer,&native);reference.attachInstruments(referenceRenderer,&referenceNative);
  const size_t copies=3+(sampleCopies?doc->song().GetNumChannels()+1:0);
  std::array<float,8192>a{},b{};bool sounding=false;
  auto publish=[&](PluginChain &chain,const NativeSong &song){auto plan=chain.prepareGraphControls(song);check(plan&&chain.publishGraphControls(std::move(plan)),"A bounded recipe source edit publishes while the same vendors keep processing");};
  for(uint32_t at=0;at<5000;){auto frames=std::min(block,5000-at);for(auto boundary:{512u,600u,1536u,2048u,2560u,3072u,4096u})if(boundary>at)frames=std::min(frames,boundary-at);
    if(at==512){native.signal.library[0].nodes[1].plugin.state=quieter.state;referenceNative.signal.library[0].nodes[1].plugin.state=quieter.state;publish(live,native);publish(reference,referenceNative);}
    if(at==600){addSource(native,.2,1);auto &r=referenceNative.signal.library[0];r.modulation[0].maximum=.2;if(kind==SignalNodeKind::Follower)r.audio.back().gain=1;
      const auto vendors=created();auto plan=live.prepareGraphControls(native);check(plan&&created()==vendors&&plan->runtimeOwners.size()==copies&&plan->scheduling.size()==copies*2,"First source stages bounded queues for every current and fading vendor without recreating plugins");check(live.publishGraphControls(std::move(plan)),"First source publishes during opaque preset fade");publish(reference,referenceNative);
    }
    if(at==1536){auto &definition=native.signal.library[0];definition.nodes.insert(definition.nodes.begin(),{unused,SignalNodeKind::MIDI,"Unconnected source"});const auto vendors=created();publish(live,native);check(created()==vendors,"Reordering/addition retains stable vendor state and follower history");}
    if(at==2048){native.signal.library[0].modulation.clear();referenceNative.signal.library[0].modulation[0].maximum=0;publish(live,native);publish(reference,referenceNative);}
    if(at==2560){native.signal.library[0].modulation={{source,node,7,0,.2,.3,true}};referenceNative.signal.library[0].modulation[0].maximum=.2;publish(live,native);publish(reference,referenceNative);}
    if(at==3072){auto removed=native;std::erase_if(removed.signal.library[0].nodes,[&](const auto &n){return n.id==source;});std::erase_if(removed.signal.library[0].audio,[&](const auto &edge){return edge.target==source;});removed.signal.library[0].modulation.clear();publish(live,removed);publish(live,native);}
    if(at==4096){std::erase_if(native.signal.library[0].nodes,[&](const auto &n){return n.id==source;});std::erase_if(native.signal.library[0].audio,[&](const auto &edge){return edge.target==source;});native.signal.library[0].modulation.clear();referenceNative.signal.library[0].modulation[0].maximum=0;publish(live,native);publish(reference,referenceNative);}
    uint64_t allocations,frees,locks;tracker_audit_begin();live.beginRenderBlock();reference.beginRenderBlock();live.syncTransport(actualRenderer);reference.syncTransport(referenceRenderer);actualRenderer.render(a.data(),frames);referenceRenderer.render(b.data(),frames);const bool okay=live.process(a.data(),frames)&&reference.process(b.data(),frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Source runtime/queue adoption and retirement stay allocation/free/lock-free");
    for(uint32_t f=0;f<frames*2;++f){sounding|=std::abs(b[f])>1e-6;check(std::abs(a[f]-b[f])<2e-6,"New/removed/reconnected sources differ from uninterrupted fixed-topology vendor/reference PCM");}
    check(actualRenderer.telemetry().frames==referenceRenderer.telemetry().frames,"Source edits preserve the held-note playback clock");at+=frames;
  }
  check(sounding,"Source fixture rendered audible sample voices");
  auto note=native;note.signal.library[0].nodes.push_back({native.nextID,SignalNodeKind::NoteEnvelope,"Unprepared scope"});check(!live.prepareGraphControls(note),"Adding a new note scope remains explicitly unsupported");
  auto external=native;external.signal.library[0].nodes.push_back({source,SignalNodeKind::Follower,"External source"});external.signal.library[0].audio.push_back({input,source,1,0});bool refused=false;try{live.prepareGraphControls(external);}catch(const std::invalid_argument &){refused=true;}check(refused,"Sources cannot activate unprepared external graph inputs");
}
static void graphTailBudget(uint32_t block) {
  PluginState comb;for(const auto &d:NativePlugin::builtins())if(d.classID=="resonance.comb-filter.v1")comb.descriptor=d;
  {NativePlugin p(comb,48000,true);check(p.parameter(1,100)&&p.parameter(3,0),"Prepare short comb state");comb.state=p.state().state;}
  auto doc=Document::demo();enable(*doc);auto native=doc->native();auto &definition=native.signal.library.emplace_back();definition.id=native.makeEntity().id;definition.number=1;definition.name="Live decay";
  const auto input=native.makeEntity().id,a=native.makeEntity().id,b=native.makeEntity().id,output=native.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{a,SignalNodeKind::Plugin,"First comb"},{b,SignalNodeKind::Plugin,"Second comb"},{output,SignalNodeKind::Output,"Output"}};
  const auto &d=comb.descriptor;for(unsigned i:{1u,2u})definition.nodes[i].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,comb.state};
  definition.audio={{input,a},{a,b},{b,output}};native.signal.assignments={{native.masterID,definition.id,1,1}};
  Renderer renderer(doc->serialize(),48000);PluginChain chain({},48000,true);chain.attachInstruments(renderer,&native);const auto shortTail=chain.tail();check(shortTail<.1,"Short-state fixture really has a brief tail");
  NativePlugin oracle(comb,48000,true);check(oracle.parameter(1,36)&&oracle.parameter(3,90),"Independent processor exposes changed decay budget");const auto expected=std::min(60.,oracle.tail()*2);
  std::array<float,8192> audio{};bool lateAudio=false;uint64_t previousRevision=chain.tailRevision();
  for(uint32_t position=0;position<24000;){auto count=std::min(block,24000-position);for(auto boundary:{1024u,4096u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024){for(unsigned i:{1u,2u}){native.signal.library[0].nodes[i].plugin.parameters[1]=36;native.signal.library[0].nodes[i].plugin.parameters[3]=90;}auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Live decay controls publish before tail rendering");}
    audio.fill(0);uint64_t allocations,frees,locks;tracker_audit_begin();chain.beginRenderBlock();if(position<4096){chain.syncTransport(renderer);renderer.render(audio.data(),count);}const bool okay=chain.process(audio.data(),count);const auto budget=chain.tail();const auto revision=chain.tailRevision();tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Dynamic graph tail budget/revision remain callback-safe");
    if(position>=1024)check(budget+1e-5>=expected&&revision>previousRevision,"Changed live recipe decay extends full serial path and reports a new tail revision");
    for(uint32_t i=0;i<count;++i)if(position+i>4096+uint32_t(shortTail*48000)+1000)lateAudio|=std::abs(audio[i*2])+std::abs(audio[i*2+1])>1e-7;
    position+=count;
  }
  check(lateAudio,"Rendered recipe still has audible stored energy after its original short tail budget");
  NativePlugin preset(comb,48000,true);check(preset.parameter(1,12)&&preset.parameter(2,-12)&&preset.parameter(3,95),"Prepare long opaque decay");const auto state=preset.state().state;
  for(unsigned i:{1u,2u}){native.signal.library[0].nodes[i].plugin.state=state;native.signal.library[0].nodes[i].plugin.parameters.clear();}
  auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan))&&chain.tail()==60,"Opaque serial preset replacement publishes the bounded full-path export budget before adoption");
  for(unsigned pass=0;pass<2;++pass){audio.fill(0);chain.beginRenderBlock();check(chain.process(audio.data(),512),"Long preset adoption remains renderable");}
  auto routed=native;routed.mixer.buses[0].gainDB=-3;auto routing=chain.prepareMixerRouting(routed);check(routing&&chain.publishMixerRouting(routing)&&chain.tail()==60,"Subsequent routing publication retains changed recipe tail budgets");
}
static void combinedRecipeAutomation(const PluginState &effect,uint32_t block) {
  auto doc=Document::demo();enable(*doc);auto native=doc->native();
  auto &definition=native.signal.library.emplace_back();definition.id=native.makeEntity().id;definition.number=1;definition.name="Combined recipe";
  const auto input=native.makeEntity().id,node=native.makeEntity().id,output=native.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Gain"},{output,SignalNodeKind::Output,"Output"}};
  const auto &d=effect.descriptor;definition.nodes[1].plugin={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,effect.state};definition.nodes[1].plugin.parameters[7]=.3;
  definition.audio={{input,node},{node,output}};native.signal.assignments={{native.masterID,definition.id,1,1}};
  MusicalAutomationLane lane;lane.id=native.makeEntity().id;lane.pattern=native.patterns.at(0).id;lane.plugin=effect.instanceID;lane.parameter=7;lane.points={{0,.1}};native.automation={lane};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({effect},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double expected=.03;
  for(uint32_t position=0;position<7500;){auto count=std::min(block,7500-position);for(auto boundary:{1024u,2048u,3072u,4096u,5120u,6144u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024){native.signal.library[0].nodes[1].plugin.parameters[7]=.6;native.automation[0].points[0].value=.2;auto plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"Recipe controls and rack lanes share one prepared boundary");native.automation[0].points[0].value=.25;live.updateMusicalAutomation(native);expected=.15;}
    if(position==2048){native.signal.library[0].nodes[1].plugin.parameters[7]=.5;native.automation[0].points[0].value=.3;auto plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"Repeated combined recipe/parameter edit publishes atomically");native.signal.library[0].nodes[1].plugin.parameters[7]=.4;plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"A coalesced later recipe snapshot carries the unconsumed lane publication");expected=.12;}
    if(position==3072){auto stale=native;stale.signal.library[0].nodes[1].plugin.parameters[7]=.2;stale.automation[0].points[0].value=.6;auto plan=live.prepareGraphControls(stale);native.automation[0].points[0].value=.4;live.updateMusicalAutomation(native);check(plan&&!live.publishGraphControls(std::move(plan)),"A stale combined recipe plan rejects without changing either recipe or lane");expected=.16;}
    if(position==4096||position==6144){native.signal.library[0].nodes[1].plugin.parameters[7]=position==4096?.7:.8;auto plan=live.prepareGraphControls(native);check(plan&&live.publishGraphControls(std::move(plan)),"Later graph snapshots retain their currently audible musical-plan lifetime");expected=position==4096?.28:.4;}
    if(position==5120){native.automation.clear();live.updateMusicalAutomation(native);expected=.35;}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Combined recipe/lane adoption remains realtime safe");for(uint32_t i=0;i<count*2;++i)check(std::abs(b[i]-a[i]*expected)<2e-6,"No half-applied recipe/lane template, stale overwrite or reset baseline leaks into PCM");position+=count;
  }
}
static void songModulationHost(const PluginState &effect,uint32_t block) {
  auto doc=Document::demo();enable(*doc);auto native=doc->native();native.mixer.buses.back().inserts={effect.instanceID};
  SignalSongSource amount;amount.node={native.makeEntity().id,SignalNodeKind::Amount,"Song amount"};amount.amount=.2;
  native.signal.songSources={amount};native.signal.songModulation={{amount.node.id,effect.instanceID,7,0,1}};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({effect},48000,true);
  dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192> a{},b{};
  for(uint32_t position=0;position<6000;){auto frames=std::min(block,6000-position);for(auto boundary:{1024u,2048u,3072u,4096u})if(boundary>position)frames=std::min(frames,boundary-position);
    if(position==1024){check(live.parameter(0,7,.1),"Manual baseline remains writable under modulation");const auto pending=live.parameters(0);check(pending[0].manualValue&&std::abs(*pending[0].manualValue-.1)<1e-6,"Pending manual edits immediately update the safe control-owned baseline snapshot");}
    if(position==2048 || position==3072 || position==4096){auto next=native;if(position==3072)next.signal.songModulation.clear();else next.signal.songSources[0].amount=.4;
      auto prepared=live.prepareMixerRouting(next);check(prepared&&live.publishMixerRouting(prepared),"Root modulation changes and cable cuts publish during uninterrupted playback");}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);
    reference.render(a.data(),frames);renderer.render(b.data(),frames);const auto okay=dry.process(a.data(),frames)&&live.process(b.data(),frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Root modulation renders with no callback allocation/free/lock");
    const double expected=position<1024?.7:position<2048?.3:position<3072?.5:position<4096?.1:.5;
    const auto values=live.parameters(0);check(values[0].manualValue&&std::abs(*values[0].manualValue-(position<1024?.5:.1))<1e-6&&std::abs(values[0].value-expected)<1e-6,"Editable manual base stays stable while effective root modulation changes");
    for(uint32_t i=0;i<frames*2;++i)check(std::abs(b[i]-a[i]*expected)<2e-6,"Root modulation adds to unmodulated baseline, edits immediately, and cut restores exact manual value");
    position+=frames;
  }
  // Effect-output followers require actual PCM from the same render range.
  // The second processor must see the first processor's output before render.
  auto precursor=effect;precursor.instanceID="follower-input";auto following=doc->native();following.mixer.buses.back().inserts={precursor.instanceID,effect.instanceID};
  SignalSongSource follower;follower.node={following.makeEntity().id,SignalNodeKind::Follower,"Actual audio"};follower.node.attack=follower.node.release=.00001;follower.audioPlugin=precursor.instanceID;
  following.signal.songSources={follower};following.signal.songModulation={{follower.node.id,effect.instanceID,7,0,.5}};
  Renderer followerDry(doc->serialize(),48000),followerRenderer(doc->serialize(),48000);PluginChain dryFollower({},48000,true),hosted({effect,precursor},48000,true);
  dryFollower.attachInstruments(followerDry,&doc->native());hosted.attachInstruments(followerRenderer,&following);
  double envelope=0;const double coefficient=std::exp(-1./(48000*.00001));
  for(uint32_t position=0;position<4096;){const auto frames=std::min(block,4096-position);uint64_t allocations,frees,locks;tracker_audit_begin();
    dryFollower.beginRenderBlock();hosted.beginRenderBlock();dryFollower.syncTransport(followerDry);hosted.syncTransport(followerRenderer);followerDry.render(a.data(),frames);followerRenderer.render(b.data(),frames);
    const bool okay=dryFollower.process(a.data(),frames)&&hosted.process(b.data(),frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Follower schedule and dense VST3 sample-offset queues remain realtime safe");
    for(uint32_t i=0;i<frames;++i){const double source=std::min(1.,std::max(std::abs(a[i*2]),std::abs(a[i*2+1]))*.5);envelope=source+coefficient*(envelope-source);for(unsigned c=0;c<2;++c)check(std::abs(b[i*2+c]-a[i*2+c]*.5*(.5+.5*envelope))<3e-6,"Follower reads this block's actual upstream stereo PCM");}
    position+=frames;
  }
  auto feedback=following;feedback.signal.songSources[0].audioPlugin=effect.instanceID;bool rejected=false;
  feedback.mixer.buses.push_back({feedback.makeEntity().id,feedback.masterID,MixerBusKind::Return,"Rejected bus"});
  const auto portCount=hosted.signalObservation().ports.size();
  try{hosted.prepareMixerRouting(feedback);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected&&hosted.mixerRoutingReady()&&hosted.signalObservation().ports.size()==portCount,"Follower feedback rejects before changing the playing schedule or publishing orphan bus meters");
  // A missing saved target is dormant, not a reason for song playback to fail.
  auto missing=following;missing.mixer.buses.back().inserts.clear();PluginChain absent({},48000,true);Renderer missingRenderer(doc->serialize(),48000);absent.attachInstruments(missingRenderer,&missing);
  missingRenderer.render(a.data(),128);check(absent.process(a.data(),128),"Unresolved saved modulation targets and taps do not prevent playback");
}
static void songNoteScope(const PluginState &effect,uint32_t block,unsigned scope) {
  auto doc=Document::demo();doc->transaction([](CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);song.m_nInstruments=1;song.Instruments[1]=new ModInstrument(1);});enable(*doc);
  auto native=doc->native();native.mixer.buses.back().inserts={effect.instanceID};
  native.preciseNotes={{native.patterns.at(0).id,native.tracks.at(0).id,0,1,61,127},{native.patterns.at(0).id,native.tracks.at(0).id,12000,1,61,127}};
  SignalSongSource note;note.node={native.makeEntity().id,SignalNodeKind::NoteEnvelope,"Scoped note"};note.node.attack=.01;note.node.release=.1;
  if(scope<2)note.noteTarget=native.tracks.at(scope).id;else if(scope==2)note.noteTarget=native.masterID;
  else if(scope==3)note.noteInstrument=native.instruments.at(1).id;
  else {const auto group=native.makeEntity().id;native.mixer.buses.push_back({group,native.masterID,MixerBusKind::Group,"Note scope group"});native.mixer.buses[0].output=group;note.noteTarget=group;}
  native.signal.songSources={note};native.signal.songModulation={{note.node.id,effect.instanceID,7,0,.5}};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);reference.preparePreciseNotes(native);renderer.preparePreciseNotes(native);
  PluginChain dry({},48000,true),hosted({effect},48000,true);dry.attachInstruments(reference,&native);hosted.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double envelope=0;bool sounding=false;
  const uint32_t retrigger=uint32_t(std::ceil(12000.*5760/65536));
  for(uint32_t position=0;position<3000;){auto count=std::min(block,3000-position);if(position<2000)count=std::min(count,2000-position);
    if(position==2000){auto updated=native;updated.signal.songSources[0].node.attack=.02;auto plan=hosted.prepareMixerRouting(updated);check(plan&&hosted.publishMixerRouting(plan),"Live AR edits preserve the held scoped note's envelope/generation");}
    const double coefficient=std::exp(-1./(position<2000?480.:960.));uint64_t allocations,frees,locks;tracker_audit_begin();
    dry.beginRenderBlock();hosted.beginRenderBlock();dry.syncTransport(reference);hosted.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&hosted.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Scoped note controls preserve precise event rendering and realtime safety");
    for(uint32_t frame=0;frame<count;++frame){if(position+frame==retrigger)envelope=0;envelope=scope==1?0:1+coefficient*(envelope-1);
      for(unsigned c=0;c<2;++c){sounding|=std::abs(a[frame*2+c])>1e-5;if(std::abs(b[frame*2+c]-a[frame*2+c]*(.5+.5*envelope))>=3e-6){std::cerr<<"scope="<<scope<<" block="<<block<<" frame="<<position+frame<<" dry="<<a[frame*2+c]<<" actual="<<b[frame*2+c]<<" expected="<<a[frame*2+c]*(.5+.5*envelope)<<" envelope="<<envelope<<" retrigger="<<retrigger<<'\n';check(false,"Note envelopes honor channel/master/group/instrument scope and precise retriggers");}}}
    position+=count;
  }
  check(sounding,"Scoped note fixture must sound");
}
static void songBusFollower(const PluginState &effect,uint32_t block){
  auto doc=Document::demo();doc->transaction([](CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(uint32_t row=0;row<pattern.GetNumRows();++row)for(uint32_t channel=1;channel<song.GetNumChannels();++channel)pattern.GetpModCommand(row,channel)->Clear();});enable(*doc);
  auto precursor=effect;precursor.instanceID="bus-follower-insert";auto native=doc->native();native.mixer.buses[0].inserts={precursor.instanceID};native.mixer.buses[0].gainDB=-12;native.mixer.buses.back().inserts={effect.instanceID};
  SignalSongSource source;source.node={native.makeEntity().id,SignalNodeKind::Follower,"Bus tap"};source.node.attack=.002;source.node.release=.003;source.audioBus=native.tracks.at(0).id;source.preFader=true;native.signal.songSources={source};native.signal.songModulation={{source.node.id,effect.instanceID,7,0,.5}};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({effect,precursor},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double envelope=0;const double fader=std::pow(10.,-12./20.);
  for(uint32_t position=0;position<4096;){auto count=std::min(block,4096-position);for(auto boundary:{1024u,2048u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024||position==2048){native.signal.songSources[0].preFader=position==2048;auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Bus follower tap switches without resetting its running AR state");}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Bus follower tap rendering has no realtime allocation/free/lock");
    for(uint32_t i=0;i<count;++i){const auto peak=std::max(std::abs(a[i*2]),std::abs(a[i*2+1]))*.5*(native.signal.songSources[0].preFader?1:fader);const double coefficient=std::exp(-1./(48000*(peak>envelope?.002:.003)));envelope=peak+coefficient*(envelope-peak);
      for(unsigned channel=0;channel<2;++channel)if(std::abs(b[i*2+channel]-a[i*2+channel]*.5*fader*(.5+.5*envelope))>=3e-6){std::cerr<<"bus follower block="<<block<<" frame="<<position+i<<" pre="<<native.signal.songSources[0].preFader<<" dry="<<a[i*2+channel]<<" actual="<<b[i*2+channel]<<" expected="<<a[i*2+channel]*.5*fader*(.5+.5*envelope)<<" envelope="<<envelope<<std::endl;check(false,"Bus pre-fader tap is post-insert; post-fader tap and live changes use current audio with retained envelope");}}
    position+=count;
  }
}
static void detachedHost(const PluginState &effect,uint32_t block){
  auto doc=Document::demo();auto native=doc->native();native.mixer.detached={effect.instanceID};
  SignalSongSource amount;amount.node={native.makeEntity().id,SignalNodeKind::Amount,"Detached control"};amount.amount=.2;native.signal.songSources={amount};native.signal.songModulation={{amount.node.id,effect.instanceID,7,0,1}};
  auto connected=native;connected.ensureMixer();connected.mixer.detached.clear();connected.mixer.buses.back().inserts={effect.instanceID};auto detached=connected;detached.mixer.buses.back().inserts.clear();detached.mixer.detached={effect.instanceID};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({effect},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  check(!doc->native().mixer.active(),"Detached playback does not materialize document routing");
  check(live.parameter(0,7,.25),"Manual edits reach a silently clocked processor");
  std::array<float,8192>a{},b{};
  for(uint32_t position=0;position<6000;){auto count=std::min(block,6000-position);for(auto boundary:{1500u,3000u,4500u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1500||position==3000||position==4500){auto plan=live.prepareMixerRouting(position==3000?detached:connected);check(plan&&live.publishMixerRouting(plan),"Detached effect inserts/Undo using its existing parameter/editor state");}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Silent-host scheduling and attachment allocate, free and lock nothing");
    for(uint32_t i=0;i<count;++i){const auto at=position+i;double gain=1;
      if(at>=1500&&at<3000)gain=1-.55*std::min(1.,(at-1500)/480.);
      else if(at>=3000&&at<4500)gain=.45+.55*std::min(1.,(at-3000)/480.);
      else if(at>=4500)gain=1-.55*std::min(1.,(at-4500)/480.);
      for(unsigned channel=0;channel<2;++channel)check(std::abs(b[i*2+channel]-a[i*2+channel]*gain)<2e-6,"Detached effect never falls onto Master; reinsertion retains both manual baseline and song modulation");
    }
    const auto &observation=live.signalObservation();bool observed=false;
    for(size_t i=0;i<observation.ports.size();++i)if(observation.ports[i].node=="plugin:"+effect.instanceID && observation.ports[i].output){auto meter=observation.read(uint32_t(i+1));observed|=meter.measured&&meter.through==position+count;}
    check(observed,"Detached processor output is measured on every block, including silence");position+=count;
  }
}
static void combinedSongAutomation(const PluginState &effect,uint32_t block){
  auto doc=Document::demo();enable(*doc);auto native=doc->native();native.mixer.buses.back().inserts={effect.instanceID};
  SignalSongSource amount;amount.node={native.makeEntity().id,SignalNodeKind::Amount,"Combined template"};amount.amount=.2;native.signal.songSources={amount};native.signal.songModulation={{amount.node.id,effect.instanceID,7,0,1}};
  MusicalAutomationLane lane;lane.id=native.makeEntity().id;lane.pattern=native.patterns.at(0).id;lane.plugin=effect.instanceID;lane.parameter=7;lane.points={{0,.1}};native.automation={lane};
  Renderer reference(doc->serialize(),48000),renderer(doc->serialize(),48000);PluginChain dry({},48000,true),live({effect},48000,true);dry.attachInstruments(reference,&doc->native());live.attachInstruments(renderer,&native);
  std::array<float,8192>a{},b{};double expected=.3;
  for(uint32_t position=0;position<7000;){auto count=std::min(block,7000-position);for(auto boundary:{1024u,2048u,3072u,4096u,5120u,6000u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024){native.signal.songSources[0].amount=.4;native.automation[0].points[0].value=.3;auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Publish source and parameter lane as one prepared musical edit");native.automation[0].points[0].value=.2;live.updateMusicalAutomation(native);expected=.6;}
    if(position==2048){native.signal.songSources[0].amount=.1;native.automation[0].points[0].value=.25;auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Second compound musical edit prepares without a stopped transport");expected=.35;}
    if(position==3072){auto stale=native;stale.signal.songSources[0].amount=.2;stale.automation[0].points[0].value=.4;auto plan=live.prepareMixerRouting(stale);native.automation[0].points[0].value=.1;live.updateMusicalAutomation(native);check(plan&&!live.publishMixerRouting(plan),"An intervening lane edit rejects a stale compound plan before routing publication");expected=.2;}
    if(position==4096){native.signal.songSources[0].amount=.3;native.automation[0].points[0].value=.2;auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Retry combined edit with a fresh musical version");expected=.5;}
    if(position==5120){native.signal.songSources[0].amount=.35;auto plan=live.prepareMixerRouting(native);check(plan&&live.publishMixerRouting(plan),"Unrelated later routing retains compound envelope ownership");expected=.55;}
    if(position==6000){native.automation.clear();live.updateMusicalAutomation(native);expected=.85;}
    uint64_t allocations,frees,locks;tracker_audit_begin();dry.beginRenderBlock();live.beginRenderBlock();dry.syncTransport(reference);live.syncTransport(renderer);reference.render(a.data(),count);renderer.render(b.data(),count);const bool okay=dry.process(a.data(),count)&&live.process(b.data(),count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&allocations+frees+locks==0,"Combined publication and retained automation lifetime are realtime safe");for(uint32_t sample=0;sample<count*2;++sample)check(std::abs(b[sample]-a[sample]*expected)<2e-6,"Combined controls never expose half a template or overwrite a newer lane publication");position+=count;
  }
}
static std::vector<float> render(Document &doc, const std::vector<PluginState> &plugins, uint32_t rate, uint32_t block,
                                 bool offline = true, uint32_t order = 0) {
  Renderer renderer(doc.serialize(), rate, order);
  PluginChain chain(plugins, rate, offline, {}, uint64_t(double(renderer.telemetry().frames) * 48000 / rate));
  chain.attachInstruments(renderer, &doc.native()); chain.attachMusicalAutomation(renderer, doc.native());
  const uint32_t total = rate * 3;
  std::vector<float> result(total * 2);
  bool ended = false;
  for (uint32_t pos = 0; pos < total; pos += block) {
    const auto count = std::min(block, total - pos);
    uint64_t a, f, l; tracker_audit_begin();
    chain.syncTransport(renderer);
    if (!ended && renderer.render(result.data() + pos * 2, count) < count) { chain.endNotes(); ended = true; }
    bool ok = chain.process(result.data() + pos * 2, count);
    tracker_audit_end(&a, &f, &l);
    check(ok && !renderer.faulted(), "Native mixer rendering succeeds");
    check(a + f + l == 0, "Native mixer performs no callback allocation, free or lock");
  }
  if(chain.hasMixer()) {
    const auto &observed=chain.signalObservation();
    for(const auto &bus:doc.native().mixer.buses)for(const char *direction:{"/in/0","/out/0"}) {
      const auto key="n"+std::to_string(bus.id)+direction;
      const auto found=std::find_if(observed.ports.begin(),observed.ports.end(),[&](const auto &p){return p.key==key;});
      check(found!=observed.ports.end(),"Mixer audio observation keeps the document's stable bus identity");
      const auto meter=observed.read(uint32_t(found-observed.ports.begin()+1));
      check(meter.measured && std::isfinite(meter.peakLeft) && std::isfinite(meter.rmsRight),"Every rendered bus has finite input and output measurements, including silent buses");
    }
  }
  return result;
}
static double error(const std::vector<float> &a, const std::vector<float> &b) {
  double result = 0; check(a.size() == b.size(), "Render length matches");
  for (size_t i = 0; i < a.size(); ++i) result = std::max(result, std::abs(double(a[i]) - b[i]));
  return result;
}
static void monitorWhileRendering(bool testBypass=false) {
  auto doc=Document::demo();enable(*doc);
  PluginState compressor;compressor.descriptor.format="Built-in";compressor.descriptor.classID="resonance.compressor.v1";compressor.instanceID="monitor-downstream";
  {NativePlugin prepare(compressor,48000);check(prepare.parameter(1,-48),"Prepare an audible compressor comparison");compressor.state=prepare.state().state;}
  doc->annotate([&](NativeSong &n){n.mixer.buses.back().inserts={compressor.instanceID};n.mixer.sidechains={{n.tracks.at(1).id,compressor.instanceID,1,0,false,true}};});
  for(const auto block:{17u,512u,4096u}) {
    Renderer reference(doc->serialize(),48000),audition(doc->serialize(),48000);
    PluginChain dry({compressor},48000,true),listening({compressor},48000,true);
    dry.attachInstruments(reference,&doc->native());listening.attachInstruments(audition,&doc->native());
    const auto key="n"+std::to_string(doc->native().tracks.at(0).id)+"/out/0";
    auto &observed=listening.signalObservation();const auto found=std::find_if(observed.ports.begin(),observed.ports.end(),[&](const auto &p){return p.key==key;});
    check(found!=observed.ports.end(),"Prepared channel monitor port exists");const auto token=uint32_t(found-observed.ports.begin()+1);
    std::array<float,8192> a{},b{};bool different=false,signal=false;
    for(uint32_t position=0;position<96000;) {
      auto count=std::min(block,96000-position);
      if(position<24000)count=std::min(count,24000-position);
      if(position<48000)count=std::min(count,48000-position);
      if(position==24000){if(testBypass)check(listening.bypass(0,true),"Accept live bypass");else observed.listen.select(token);}
      if(position==48000){if(testBypass)check(listening.bypass(0,false),"Accept live enable");else observed.listen.select(0);}
      a.fill(0);b.fill(0);uint64_t alloc,free,locks;tracker_audit_begin();
      dry.beginRenderBlock();listening.beginRenderBlock();dry.syncTransport(reference);listening.syncTransport(audition);
      reference.render(a.data(),count);audition.render(b.data(),count);
      const auto okay=dry.process(a.data(),count)&&listening.process(b.data(),count);tracker_audit_end(&alloc,&free,&locks);
      check(okay&&alloc+free+locks==0,"Listen during native playback keeps the complete graph realtime safe");
      for(uint32_t i=0;i<count*2;++i) {
        const auto frame=position+i/2;
        if(frame>=24240&&frame<48000){different|=std::abs(a[i]-b[i])>1e-5;signal|=std::abs(b[i])>1e-5;}
        if(frame<24000||frame>=48240)check(a[i]==b[i],"Restoring monitor gives identical downstream state/audio; sidechain and all processors kept running");
      }
      check(reference.telemetry().frames==audition.telemetry().frames,"Listening never changes transport advancement");
      position+=count;
    }
    check(different&&signal&&!observed.listen.pending(),"Channel listen isolates real rendered audio and restores the original mix");
  }
}
int main(int argc, char **argv) { trustFixtureArguments(argc, argv);
  @autoreleasepool { try {
    check(argc == 2, "Fixture bundle path required");
    auto descriptions = NativePlugin::discoverVST3(argv[1]);
    PluginState gain{descriptions.at(0)}, synth{descriptions.at(1)}, delayed{descriptions.at(2)};
    gain.instanceID = "gain"; synth.instanceID = "synth"; delayed.instanceID = "delayed";
    for(auto block:{17u,512u,4096u}){songModulationHost(gain,block);liveRack(gain,block);liveRouteObservations(gain,block);liveRouting(gain,block);liveRouting(gain,block,false,false,true);liveRouting(gain,block,true);liveRouting(gain,block,false,true);liveRouting(gain,block,true,true);liveRecipeParameters(gain,block);}
    for(auto block:{17u,512u,4096u})for(unsigned scope=0;scope<5;++scope)songNoteScope(gain,block,scope);
    for(auto block:{17u,512u,4096u}){combinedSongAutomation(gain,block);combinedRecipeAutomation(gain,block);detachedHost(gain,block);songBusFollower(gain,block);}
    {auto bundle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
      auto large=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureLargeCatalog"));check(large,"Large parameter-catalog fixture is available");large(true);
      liveRecipeParameters(gain,512);large(false);
      auto hidden=reinterpret_cast<void(*)(float)>(dlsym(bundle,"ResonanceFixtureHiddenGain"));auto created=reinterpret_cast<uint64_t(*)()>(dlsym(bundle,"ResonanceFixtureCreated"));check(hidden&&created,"Opaque recipe state fixture is available");for(auto block:{17u,512u,4096u}){liveRecipePreset(gain,block,hidden,created);liveRecipeBypass(gain,block,hidden,created);}liveRecipePreset(gain,512,hidden,created,true);liveRecipeBypass(gain,512,hidden,created,true);
      for(auto kind:{SignalNodeKind::Follower,SignalNodeKind::LFO,SignalNodeKind::Random,SignalNodeKind::MIDI,SignalNodeKind::Amount,SignalNodeKind::Automation})for(auto block:{17u,512u,4096u})liveRecipeSources(gain,block,kind,hidden,created);
      liveRecipeSources(gain,512,SignalNodeKind::Follower,hidden,created,true);dlclose(bundle);
    }
    for(auto block:{17u,512u,4096u})graphTailBudget(block);
    PluginState au{registerFixtureAUs().at(0)};au.instanceID="prepared-au";
    PluginState compressor;for(const auto &d:NativePlugin::builtins())if(d.classID=="resonance.compressor.v1")compressor.descriptor=d;compressor.instanceID="prepared-compressor";
    for(auto block:{17u,512u,4096u}){liveRecipePreset(au,block,setFixtureAUHiddenGain,fixtureAUCreatedCount);liveRecipeBypass(au,block,setFixtureAUHiddenGain,fixtureAUCreatedCount);livePreparedSidechain(au,block,false);livePreparedSidechain(compressor,block,true);livePreparedSidechain(compressor,block,true,false);livePreparedSidechain(compressor,block,true,true,true);}
    for(auto kind:{SignalNodeKind::Follower,SignalNodeKind::LFO,SignalNodeKind::Random,SignalNodeKind::MIDI,SignalNodeKind::Amount,SignalNodeKind::Automation})for(auto block:{17u,512u,4096u})liveRecipeSources(au,block,kind,setFixtureAUHiddenGain,fixtureAUCreatedCount);
    synth.instrument = delayed.instrument = 1;
    {
      auto plain=Document::demo(),modulated=Document::demo();
      for(auto *document:{plain.get(),modulated.get()}){document->transaction([](CSoundFile &song){song.m_nInstruments=4;for(INSTRUMENTINDEX i=1;i<=4;++i)song.Instruments[i]=new ModInstrument(i);});enable(*document);}
      modulated->annotate([&](NativeSong &native){SignalSongSource amount;amount.node={native.makeEntity().id,SignalNodeKind::Amount,"Instrument expression"};amount.amount=.2;native.signal.songSources={amount};native.signal.songModulation={{amount.node.id,synth.instanceID,7,0,1}};});
      auto louder=synth;NativePlugin prepared(louder,48000,true);check(prepared.parameter(7,.7),"Prepare independent instrument expression reference");louder.state=prepared.state().state;
      for(auto block:{17u,512u,4096u})check(error(render(*plain,{louder},48000,block),render(*modulated,{synth},48000,block))<2e-7,"Native instrument adapters receive root control overlays before MIDI/source audio rendering");
    }
    {
      auto doc=Document::demo();enable(*doc);
      doc->annotate([&](NativeSong &native){native.mixer.buses.back().inserts={gain.instanceID};
        for(auto kind:{SignalNodeKind::LFO,SignalNodeKind::Random,SignalNodeKind::Automation}) {
          SignalSongSource source;source.node={native.makeEntity().id,kind,"Partitioned song source"};source.node.rate=kind==SignalNodeKind::Random?17.25:3.75;source.node.phase=.137;
          if(kind==SignalNodeKind::Automation)source.node.envelopes={{native.patterns.at(0).id,true,{{0,.2,AutomationCurve::Scripted,CurveFormula("mix(start,end,t*t)+sin(beat)*0.02")},{137,.8,AutomationCurve::Step},{281,.1,AutomationCurve::Smooth},{1024,.7}}}};
          native.signal.songModulation.push_back({source.node.id,gain.instanceID,7,-.08,.08});native.signal.songSources.push_back(source);
        }
      });
      for(auto rate:{44100u,48000u,96000u}){const auto reference=render(*doc,{gain},rate,17);for(auto block:{512u,4096u})check(error(reference,render(*doc,{gain},rate,block))<2e-6,"Hosted LFO/random/scripted pattern sources share the real musical clock and do not depend on callback partition");}
    }
    monitorWhileRendering();
    monitorWhileRendering(true);
    for (uint32_t rate : {44100, 48000, 96000}) {
      auto detached=Document::demo();enable(*detached);
      detached->annotate([](NativeSong &n){for(auto &bus:n.mixer.buses)bus.output=0;});
      auto disconnected=render(*detached,{},rate,128);
      check(std::all_of(disconnected.begin(),disconnected.end(),[](float value){return value==0;}),"Disconnected main outputs render silence");
      detached->annotate([](NativeSong &n){n.mixer.buses[0].sends.push_back({n.mixer.buses.back().id,0,false,true});});
      auto sent=render(*detached,{},rate,128);
      check(std::any_of(sent.begin(),sent.end(),[](float value){return std::abs(value)>1e-5;}),"A disconnected main output preserves independently audible sends");
      auto synthOnly=Document::demo();enable(*synthOnly);
      synthOnly->transaction([](CSoundFile &s){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);});
      synthOnly->annotate([](NativeSong &n){for(auto &bus:n.mixer.buses)bus.output=0;});
      auto defaultOutput=render(*synthOnly,{synth},rate,128);
      check(std::any_of(defaultOutput.begin(),defaultOutput.end(),[](float value){return std::abs(value)>1e-5;}),"An unrouted plugin instrument has its default master output");
      synthOnly->annotate([](NativeSong &n){n.mixer.instruments={{"synth",0,0}};});
      auto silentPlugin=render(*synthOnly,{synth},rate,128);
      check(std::all_of(silentPlugin.begin(),silentPlugin.end(),[](float value){return value==0;}),"Explicitly disconnecting a plugin output suppresses its default master route");
      auto doc = Document::demo();
      doc->transaction([](CSoundFile &song) {
        song.Order().assign(2, 0); song.Patterns[0].Resize(4);
        song.m_nInstruments = 4;
        for (int n = 1; n <= 4; ++n) { song.Instruments[n] = new ModInstrument(SAMPLEINDEX(n)); song.Instruments[n]->nNNA = NewNoteAction::Continue; }
      });
      auto legacy = render(*doc, {}, rate, 128); enable(*doc);
      auto routed = render(*doc, {}, rate, 128);
      auto chainDoc=Document::demo();enable(*chainDoc);
      PluginState secondGain=gain;secondGain.instanceID="second-gain";
      const std::vector<PluginState> movePlugins={gain,secondGain};
      const auto masterAudio=render(*chainDoc,movePlugins,rate,128);
      chainDoc->annotate([](NativeSong &n){moveMixerInserts(n.mixer,{"gain","second-gain"},{"gain","second-gain"},n.tracks.at(0).id);});
      const auto movedAudio=render(*chainDoc,movePlugins,rate,17);
      check(error(masterAudio,movedAudio)>1e-5,"Moving a master chain to one track must stop processing its neighboring tracks");
      chainDoc->undo();check(error(masterAudio,render(*chainDoc,movePlugins,rate,17))<2e-7,"Undo restores the original master-chain audio");
      chainDoc->annotate([](NativeSong &n){n.mixer.buses[0].inserts={"gain","second-gain"};});
      check(error(movedAudio,render(*chainDoc,movePlugins,rate,128))<2e-7,"Atomic chain movement renders exactly like explicit target-track ownership across buffer sizes");
      PluginState compressor;compressor.descriptor.format="Built-in";compressor.descriptor.classID="resonance.compressor.v1";compressor.instanceID="compressor";
      auto duck=Document::demo();enable(*duck);duck->annotate([](NativeSong &n){n.mixer.buses[0].inserts={"compressor"};n.mixer.sidechains={{n.tracks.at(1).id,"compressor",1,0,false,true}};});
      const auto inferred=render(*duck,{compressor},rate,17);compressor.auxiliaryInputs={1};
      check(error(inferred,render(*duck,{compressor},rate,128))<2e-7,"Wiring activates the detector bus and matches explicitly enabled sidechain audio");
      compressor.auxiliaryInputs.clear();Renderer portRenderer(duck->serialize(),rate);PluginChain routedPorts({compressor},rate,true);routedPorts.attachInstruments(portRenderer,&duck->native());
      check(routedPorts.states()[0].auxiliaryInputs.empty(),"Inferred routing ports cannot leak into plugin state or plugin Undo");
      std::cout << "Native mixer unity at " << rate << " Hz: " << error(legacy, routed) << '\n';
      check(error(legacy, routed) < 2e-7, "Unity mixer retains sample/NNA playback and song ending");
      for (auto block : {17u, 4096u}) check(error(routed, render(*doc, {}, rate, block)) < 2e-7, "Core graph rendering is callback-size independent");
      doc->annotate([](NativeSong &n) {
        auto group = n.makeEntity().id, master = n.mixer.buses.back().id;
        for (auto &bus : n.mixer.buses) if (bus.kind == MixerBusKind::Track) bus.output = group;
        n.mixer.buses.push_back({group, master, MixerBusKind::Group, "Group"});
      });
      check(error(routed, render(*doc, {}, rate, 17)) < 2e-7, "Group topology preserves sample output even when master is not last in stored order");
      doc->annotate([](NativeSong &n) { n.mixer.buses[0].mute = true; });
      auto muted = render(*doc, {}, rate, 128);
      doc->transaction([](CSoundFile &s) { for (auto &pattern : s.Patterns) if (pattern.IsValid()) for (ROWINDEX row = 0; row < pattern.GetNumRows(); ++row) *pattern.GetpModCommand(row, 0) = {}; });
      check(error(muted, render(*doc, {}, rate, 17)) < 2e-7, "Track mute removes only that track's samples and NNA tails");
      doc->undo(); doc->undo();
      doc->annotate([](NativeSong &n) { n.mixer.buses[0].inserts = {"gain"}; });
      auto effected = render(*doc, {gain}, rate, 128);
      check(error(effected, routed) > 1e-5 && error(effected, render(*doc, {gain}, rate, 17)) < 2e-7, "Track insert changes audio once and remains callback independent");
      doc->annotate([](NativeSong &n) {
        n.mixer.instruments = {{"synth", n.tracks.at(1).id, 0}};
        n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "gain", 7, true,
          {{0, .2, AutomationCurve::Linear}, {768, .8, AutomationCurve::Step}}});
      });
      auto instruments = render(*doc, {gain, synth}, rate, 128, false);
      check(error(instruments, render(*doc, {gain, synth}, rate, 17)) < 2e-7 && error(instruments, render(*doc, {gain, synth}, rate, 4096)) < 2e-7,
            "Routed plugin instruments, musical effect automation and tails agree live/offline across callback sizes");
      check(error(render(*doc, {gain, synth}, rate, 17, true, 1), render(*doc, {gain, synth}, rate, 4096, true, 1)) < 2e-7,
            "Order seek initializes mixer and automation on the same timeline");
      doc->annotate([](NativeSong &n) { n.automation.clear(); n.mixer = {}; });
      auto delayedLegacy = render(*doc, {delayed}, rate, 128);
      enable(*doc);
      auto delayedMixer = render(*doc, {delayed}, rate, 17);
      check(error(delayedLegacy, delayedMixer) < 2e-7, "Graph compensates delayed instrument and samples with the same result as the legacy host");
      doc->annotate([](NativeSong &n) { for (auto &bus : n.mixer.buses) if (bus.kind == MixerBusKind::Track) bus.timingMS = 500; });
      doc->transaction([](CSoundFile &s) { s.m_nDefaultGlobalVolume = 0; });
      auto silent = render(*doc, {}, rate, 4096);
      check(std::all_of(silent.begin(), silent.end(), [](float sample) { return sample == 0; }),
            "Delayed audio remains muted by song global volume after the core ends");
    }
    TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
    auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
      auto p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
      auto result = [session automationMethod:method params:p error:&problem];
      if (!result) throw std::runtime_error(problem.localizedDescription.UTF8String);
      return result;
    };
    auto rejects = [&](NSString *method, NSDictionary *params) {
      NSString *revision = session.automationRevision;
      auto p = [params mutableCopy]; p[@"expectedRevision"] = revision;
      check(![session automationMethod:method params:p error:&problem], "Invalid mixer edit is rejected");
      check([revision isEqual:session.automationRevision], "Invalid edit preserves the revision");
    };
    check(![call(@"mixer.get", @{})[@"data"][@"active"] boolValue], "Existing projects retain the legacy mixer until enabled");
    check(![call(@"mixer.enable", @{@"dryRun": @YES}, true)[@"changed"] boolValue], "Mixer enable preview is read-only");
    call(@"mixer.enable", @{}, true);
    NSDictionary *initial = call(@"mixer.get", @{})[@"data"];
    NSDictionary *oldMetadata = [NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:&problem][@"native"];
    check([oldMetadata[@"version"] intValue] == 17 && [oldMetadata[@"mixer"][@"buses"][0][@"prePan"] doubleValue] == 0 &&
      [initial[@"buses"][0][@"prePan"] doubleValue] == 0, "Neutral input balance is explicit in the current format and API");
    NSString *first = initial[@"buses"][0][@"id"], *master = [initial[@"buses"] lastObject][@"id"];
    NSString *revision = session.automationRevision;
    check(![call(@"mixer.enable", @{}, true)[@"changed"] boolValue] && [revision isEqual:session.automationRevision], "Enable is idempotent");
    auto group = call(@"mixer.bus.add", @{@"kind": @"group", @"name": @"Rhythm"}, true)[@"data"][@"bus"];
    auto space = call(@"mixer.bus.add", @{@"kind": @"return", @"name": @"Space"}, true)[@"data"][@"bus"];
    call(@"mixer.bus.set", @{@"bus": first, @"output": group}, true);
    call(@"mixer.sends.set", @{@"bus": first, @"sends": @[@{@"target": space, @"gainDB": @(-6), @"preFader": @YES}]}, true);
    rejects(@"mixer.bus.set", @{@"bus": group, @"output": first});
    rejects(@"mixer.sends.set", @{@"bus": group, @"sends": @[@{@"target": group}]});
    rejects(@"mixer.bus.set", @{@"bus": first, @"pan": @2});
    rejects(@"mixer.bus.set", @{@"bus": first, @"prePan": @(-1.01)});
    rejects(@"mixer.bus.set", @{@"bus": first, @"prePan": @YES});
    rejects(@"mixer.bus.set", @{@"bus": first, @"gainDB": @YES});
    rejects(@"mixer.bus.remove", @{@"bus": master});
    rejects(@"mixer.bus.set", @{@"bus": first, @"inserts": @[@"missing-plugin"]});
    rejects(@"mixer.bus.set", @{@"bus": first, @"output": master, @"preview": @YES});
    revision = session.automationRevision;
    call(@"mixer.bus.set", @{@"bus": first, @"gainDB": @(-12), @"prePan": @(-0.5), @"preview": @YES}, true);
    check([revision isEqual:session.automationRevision] && [call(@"mixer.get", @{})[@"data"][@"buses"][0][@"gainDB"] doubleValue] == 0,
          "Live control preview does not mutate song or consume undo");
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"prePan"] doubleValue] == 0, "Input balance preview does not persist");
    call(@"mixer.bus.set", @{@"bus": first, @"gainDB": @(-12), @"prePan": @(-0.5), @"pan": @0.25, @"width": @0.75, @"mute": @YES}, true);
    call(@"history.undo", @{@"domain": @"document"}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"gainDB"] doubleValue] == 0, "One undo restores all controls in a gesture");
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"prePan"] doubleValue] == 0, "Input balance shares the gesture's document Undo");
    call(@"history.redo", @{@"domain": @"document"}, true);
    NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
    NSDictionary *saved = call(@"mixer.get", @{})[@"data"];
    check([session savePath:path error:&problem], problem.localizedDescription.UTF8String ?: "Mixer project saves");
    NSMutableDictionary *encoded = [NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path] options:NSPropertyListMutableContainers format:nil error:&problem];
    check([encoded[@"native"][@"version"] intValue] == 17 && [encoded[@"native"][@"mixer"][@"buses"][0][@"prePan"] doubleValue] == -.5,
      "Non-neutral input balance requires native metadata v6");
    encoded[@"native"][@"version"] = @5;
    NSString *badPath = [path stringByAppendingString:@".invalid.resonance"];
    [[NSPropertyListSerialization dataWithPropertyList:encoded format:NSPropertyListBinaryFormat_v1_0 options:0 error:&problem] writeToFile:badPath atomically:YES];
    revision = session.automationRevision;
    check(![session openPath:badPath error:&problem] && [revision isEqual:session.automationRevision], "Legacy metadata with input balance is rejected without replacing the song");
    encoded[@"native"][@"version"] = @17; [encoded[@"native"][@"mixer"][@"buses"][0] removeObjectForKey:@"prePan"];
    [[NSPropertyListSerialization dataWithPropertyList:encoded format:NSPropertyListBinaryFormat_v1_0 options:0 error:&problem] writeToFile:badPath atomically:YES];
    check(![session openPath:badPath error:&problem] && [revision isEqual:session.automationRevision], "Version 6 requires an explicit valid input balance on every bus");
    [[NSFileManager defaultManager] removeItemAtPath:badPath error:nil];
    check([session openPath:path error:&problem], "Mixer project reopens");
    auto loaded = call(@"mixer.get", @{})[@"data"];
    check([saved[@"buses"] isEqual:loaded[@"buses"]] && [saved[@"instruments"] isEqual:loaded[@"instruments"]], "Routing and control values persist exactly");
    call(@"mixer.bus.remove", @{@"bus": group}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"][0][@"output"] isEqual:master], "Deleting group reconnects its child to its output");
    call(@"history.undo", @{@"domain": @"document"}, true);
    check([call(@"mixer.get", @{})[@"data"][@"buses"] isEqual:loaded[@"buses"]], "Undo restores group identity and routing");
    call(@"mixer.enable", @{@"enabled": @NO}, true);
    NSData *legacyProject = [session serializedData];
    call(@"mixer.enable", @{}, true);
    NSData *plainProject = [session serializedData];
    NSString *newMaster = [call(@"mixer.get", @{})[@"data"][@"buses"] lastObject][@"id"];
    call(@"mixer.bus.set", @{@"bus": newMaster, @"gainDB": @(-6), @"prePan": @(-0.5)}, true);
    NSString *plainPath = [path stringByAppendingString:@".plain.wav"], *mixedPath = [path stringByAppendingString:@".mixed.wav"],
      *legacyPath = [path stringByAppendingString:@".legacy.wav"];
    check([TrackerSession exportData:legacyProject path:legacyPath error:&problem], "Legacy sample-mode project exports");
    check([TrackerSession exportData:plainProject path:plainPath error:&problem], "Baseline project exports");
    check([TrackerSession exportData:[session serializedData] path:mixedPath error:&problem], "Persisted mixer project exports");
    NSData *plainAudio = [NSData dataWithContentsOfFile:plainPath], *mixedAudio = [NSData dataWithContentsOfFile:mixedPath],
      *legacyAudio = [NSData dataWithContentsOfFile:legacyPath];
    check(plainAudio.length == mixedAudio.length && plainAudio.length > 44, "Mixer export preserves song duration");
    check(legacyAudio.length == plainAudio.length, "Native and legacy sample-mode export durations agree");
    double signal = 0, maximum = 0, legacyDifference = 0;
    for (size_t byte = 44; byte + sizeof(float) <= plainAudio.length; byte += sizeof(float)) {
      float a = 0, b = 0, legacy = 0;
      std::memcpy(&a, static_cast<const char *>(plainAudio.bytes) + byte, sizeof(float));
      std::memcpy(&b, static_cast<const char *>(mixedAudio.bytes) + byte, sizeof(float));
      std::memcpy(&legacy, static_cast<const char *>(legacyAudio.bytes) + byte, sizeof(float));
      legacyDifference = std::max(legacyDifference, std::abs(double(a) - legacy));
      const double balance = ((byte - 44) / sizeof(float)) % 2 ? .5 : 1;
      signal += std::abs(a); maximum = std::max(maximum, std::abs(b - a * std::pow(10.0, -.3) * balance));
    }
    std::cout << "Mixer WAV fader check: signal " << signal << ", maximum difference " << maximum << ", legacy sample-mode difference " << legacyDifference << '\n';
    check(signal > 1 && maximum < 2e-7, "Actual WAV export decodes the saved graph and applies independent input balance and master fader");
    // Separately decaying integer channel click-removal offsets cannot be
    // bit-identical to decaying their already-summed integer offset. Retain a
    // strict sub -114 dB bound for that sample-mode routing difference.
    check(legacyDifference < 2e-6, "Sample-mode routing retains the legacy signal within bounded click-removal rounding");
    [[NSFileManager defaultManager] removeItemAtPath:plainPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:mixedPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:legacyPath error:nil];
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    // Exercise the same atomic operation used by the graph's cable gesture.
    for(NSString *classID in @[@"resonance.distortion.v1",@"resonance.compressor.v1"]) {
      NSDictionary *descriptor=nil;for(NSDictionary *d in session.builtInPlugins)if([d[@"classID"] isEqual:classID])descriptor=d;
      check(descriptor!=nil,"Chain fixture effect exists");call(@"plugin.add",@{@"descriptor":descriptor},true);
    }
    auto inventory=call(@"mixer.get",@{})[@"data"];
    NSArray *chain=@[inventory[@"plugins"][0][@"id"],inventory[@"plugins"][1][@"id"]];
    first=inventory[@"buses"][0][@"id"];newMaster=[inventory[@"buses"] lastObject][@"id"];
    NSDictionary *state0=call(@"plugin.state.get",@{@"slot":@0})[@"data"],*state1=call(@"plugin.state.get",@{@"slot":@1})[@"data"];
    revision=session.automationRevision;
    auto preview=call(@"mixer.inserts.move",@{@"plugins":chain,@"target":first,@"dryRun":@YES},true);
    check([preview[@"data"][@"wouldChange"] boolValue] && [revision isEqual:session.automationRevision],"Chain dry-run is read-only");
    call(@"mixer.inserts.move",@{@"plugins":chain,@"target":first},true);
    auto chainBuses=call(@"mixer.get",@{})[@"data"][@"buses"];
    check([chainBuses[0][@"inserts"] isEqual:chain] && [[chainBuses lastObject][@"inserts"] count]==0,"Entire chain moved to the chosen track");
    check([call(@"plugin.state.get",@{@"slot":@0})[@"data"] isEqual:state0] && [call(@"plugin.state.get",@{@"slot":@1})[@"data"] isEqual:state1],"Moving a chain preserves both plugin states and identities");
    revision=session.automationRevision;call(@"mixer.inserts.move",@{@"plugins":chain,@"target":first},true);
    check([revision isEqual:session.automationRevision],"No-op chain move creates no revision");
    rejects(@"mixer.inserts.move",@{@"plugins":@[chain[1],chain[0]],@"target":newMaster});
    rejects(@"mixer.inserts.move",@{@"plugins":chain,@"target":newMaster,@"before":@"missing"});
    check([call(@"mixer.get",@{})[@"data"][@"buses"] isEqual:chainBuses],"Invalid chain movements leave all routing unchanged");
    call(@"history.undo",@{@"domain":@"document"},true);
    check([call(@"mixer.get",@{})[@"data"][@"buses"] isEqual:inventory[@"buses"]],"One Undo restores both owners including implicit master membership");
    call(@"history.redo",@{@"domain":@"document"},true);
    check([call(@"mixer.get",@{})[@"data"][@"buses"] isEqual:chainBuses],"Redo restores the moved chain");
    check([session savePath:path error:&problem] && [session openPath:path error:&problem],"Moved chain project roundtrip");
    check([call(@"mixer.get",@{})[@"data"][@"buses"] isEqual:chainBuses],"Moved chain persists without changing plugin identity");
    auto sideBefore=call(@"mixer.get",@{})[@"data"];
    auto second=sideBefore[@"buses"][1][@"id"];
    call(@"mixer.sidechains.set",@{@"plugin":chain[1],@"input":@1,@"sources":@[@{@"source":second}],@"dryRun":@YES},true);
    call(@"mixer.sidechains.set",@{@"plugin":chain[1],@"input":@1,@"sources":@[@{@"source":second}]},true);
    check([call(@"mixer.get",@{})[@"data"][@"sidechains"] count]==1,"An inactive but supported detector can be connected directly");
    rejects(@"mixer.sidechains.set",@{@"plugin":chain[1],@"input":@63,@"sources":@[@{@"source":second}]});
    call(@"history.undo",@{@"domain":@"document"},true);
    check([call(@"mixer.get",@{})[@"data"][@"sidechains"] isEqual:sideBefore[@"sidechains"]],"Sidechain routing uses one document Undo");
    call(@"mixer.sidechains.set",@{@"plugin":chain[1],@"input":@0,@"sources":@[@{@"source":second,@"gainDB":@-6,@"preFader":@YES}]},true);
    auto mainInput=call(@"mixer.get",@{})[@"data"][@"sidechains"];
    check([mainInput count]==1 && [mainInput[0][@"input"] unsignedIntValue]==0,"Main-input fan-in is an explicit saved route");
    call(@"history.undo",@{@"domain":@"document"},true);
    check([call(@"mixer.get",@{})[@"data"][@"sidechains"] isEqual:sideBefore[@"sidechains"]],"One Undo removes main-input fan-in");
    call(@"history.redo",@{@"domain":@"document"},true);
    check([call(@"mixer.get",@{})[@"data"][@"sidechains"] isEqual:mainInput],"Redo restores exact main-input fan-in");
    check([session savePath:path error:&problem] && [session openPath:path error:&problem],"Main-input fan-in project roundtrip");
    check([call(@"mixer.get",@{})[@"data"][@"sidechains"] isEqual:mainInput],"Main-input fan-in survives native save/reopen");
    call(@"mixer.sidechains.set",@{@"plugin":chain[1],@"input":@0,@"sources":@[]},true);
    call(@"mixer.plugin.route",@{@"plugin":chain[0],@"output":@0,@"targets":@[newMaster,second]},true);
    check([call(@"mixer.get",@{})[@"data"][@"instruments"] count]==2,"One plugin output can feed two destinations");
    rejects(@"mixer.plugin.route",@{@"plugin":chain[0],@"targets":@[first]});
    rejects(@"mixer.plugin.route",@{@"plugin":chain[0],@"targets":@[newMaster,newMaster]});
    rejects(@"mixer.plugin.route",@{@"plugin":chain[0],@"target":newMaster,@"targets":@[second]});
    check([session savePath:path error:&problem] && [session openPath:path error:&problem],"Fan-out project roundtrip");
    check([call(@"mixer.get",@{})[@"data"][@"instruments"] count]==2,"Fan-out destinations persist");
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    std::cout << "PASS native mixer: sample/NNA fidelity, groups, track effects, instruments, automation, seeks, PDC, tails, realtime safety, API validation, undo and persistence\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
}
