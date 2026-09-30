#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import "../Bridge/TrackerSession.h"
#include <iostream>
#include <dlfcn.h>
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
    auto master = n.makeEntity().id;
    for (const auto &[channel, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
  });
}
static void liveRouting(const PluginState &effect,uint32_t block,bool graphCopies=false,bool newBus=false) {
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
      auto prepared=liveChain.prepareMixerRouting(position==4096?after:before);
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
    position+=frames;
  }
  check(changedAudio && liveChain.mixerRoutingReady(),"Live fixture has an audible routing difference and completed handoffs");
  auto affected=after;affected.mixer.buses[0].output=before.mixer.buses[2].id;
  check(!liveChain.prepareMixerRouting(affected),"Changed VST input requires an independently prepared copy, never unsafe sharing");
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
int main(int argc, char **argv) {
  @autoreleasepool { try {
    check(argc == 2, "Fixture bundle path required");
    auto descriptions = NativePlugin::discoverVST3(argv[1]);
    PluginState gain{descriptions.at(0)}, synth{descriptions.at(1)}, delayed{descriptions.at(2)};
    gain.instanceID = "gain"; synth.instanceID = "synth"; delayed.instanceID = "delayed";
    for(auto block:{17u,512u,4096u}){liveRouting(gain,block);liveRouting(gain,block,true);liveRouting(gain,block,false,true);liveRouting(gain,block,true,true);liveRecipeParameters(gain,block);}
    {auto bundle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);
      auto large=reinterpret_cast<void(*)(bool)>(dlsym(bundle,"ResonanceFixtureLargeCatalog"));check(large,"Large parameter-catalog fixture is available");large(true);
      liveRecipeParameters(gain,512);large(false);dlclose(bundle);
    }
    synth.instrument = delayed.instrument = 1;
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
