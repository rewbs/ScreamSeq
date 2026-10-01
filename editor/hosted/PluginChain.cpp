// Extracted from mac/Audio/AudioUnitHost.mm; shared by all platform hosts.
#include "HostedAudio.hpp"
#include "GraphPluginEndpoint.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
namespace Tracker {
void validatePluginCapacity(const std::vector<PluginState> &states, size_t mixerBuses) {
  validatePluginAssignments(states);
  static_assert(maximumNativeAdapters == OpenMPT::MAX_MIXPLUGINS);
  if (states.size() > maximumNativePlugins) throw std::invalid_argument("Use at most 64 native devices.");
  const auto assigned = std::count_if(states.begin(), states.end(), [](const auto &state) { return state.instrument != 0; });
  if (mixerBuses > maximumNativeAdapters || size_t(assigned) > maximumNativeAdapters - mixerBuses)
    throw std::invalid_argument("Mixer buses and assigned plugin instruments together exceed 250. Remove a bus or unassign an instrument.");
}
PluginChain::PluginChain(const std::vector<PluginState> &states, double rate, bool offline,
                         const std::vector<ParameterChange> &automation, uint64_t startFrame)
    : sampleRate_(rate), offline_(offline), automation_(automation) {
  recordedActive_.store(!automation.empty(),std::memory_order_relaxed);
  activity_=std::make_unique<ParameterActivity>(rate);
  observation_=std::make_unique<SignalObservation>(rate);
  validatePluginCapacity(states);
  size_t bypassBytes=0;
  for (auto &state : states) {
    auto plugin = std::make_shared<NativePlugin>(state, rate, offline);
    bypassBytes+=plugin->bypassStorageBytes();
    if(bypassBytes>256u*1024u*1024u)throw std::invalid_argument("Plugin bypass audio storage exceeds 256 MB");
    ObservedProcessor observedPorts;
    for(const auto &port:plugin->buses())if(port.index<64 && port.supported && port.channels>=1 && port.channels<=2) {
      const auto node="plugin:"+state.instanceID;
      auto &token=port.input?observedPorts.input[port.index]:observedPorts.output[port.index];
      token=observation_->add({node+(port.input?"/in/":"/out/")+std::to_string(port.index),node,port.name,!port.input,port.index,port.channels,int64_t(std::llround(plugin->latency()*rate)),0});
    }
    processorObservations_.push_back(observedPorts);
    const auto baselineParameters=plugin->parameters();
    plugin->automate(automation, plugins_.size(), rate, uint64_t(double(startFrame) * rate / 48000));
    if (!plugin->isInstrument()) {
      latency_ += plugin->latency();
      tail_ += plugin->tail();
    }

    ParameterProcessor observed;observed.key="rack/"+state.instanceID;observed.name=state.descriptor.name;observed.plugin=state.instanceID;observed.bypass=state.bypass;observed.parameters=plugin->parameters();
    plugin->observe(activity_.get(),activity_->add(std::move(observed)));plugin->audible(true);
    auto entry=std::make_shared<RackEntry>();entry->plugin=plugin;entry->baseline=state;
    entry->parameters=baselineParameters;entry->ports=observedPorts;entry->activity=uint32_t(activity_->processors.size());
    rack_.push_back(entry);retainedRack_.push_back(entry);
    plugins_.push_back(std::move(plugin));
    instances_.push_back(state.instanceID);
    explicitPorts_.emplace_back(state.auxiliaryInputs,state.auxiliaryOutputs);
    instruments_.push_back(state.instrument);
    bypass_.push_back(state.bypass);
  }
  double instrumentLatency = 0, instrumentTail = 0;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (plugins_[i]->isInstrument() && instruments_[i]) {
      instrumentLatency = std::max(instrumentLatency, plugins_[i]->latency());
      instrumentTail = std::max(instrumentTail, std::max(2.0, plugins_[i]->tail()));
    }
  latency_ += instrumentLatency;
  tail_ += instrumentTail;
  dryDelay_.assign(size_t(std::llround(instrumentLatency * rate)) * 2, 0);
  for (auto &plugin : plugins_)
    if (plugin->isInstrument())
      plugin->compensateLatency(uint32_t(std::max(0.0, std::round((instrumentLatency - plugin->latency()) * rate))));
  for (auto &point : automation_) {
    if (point.slot >= states.size() || !std::isfinite(point.value) || point.frame > uint64_t(48000) * 604800)
      throw std::runtime_error("Invalid Audio Unit automation point");
    point.frame = uint64_t(double(point.frame) * rate / 48000);
  }
  position_ = uint64_t(double(startFrame) * rate / 48000);
  dryThrough_ = position_;
  captureTails();
  for(size_t i=0;i<retainedRack_.size();++i)publishedPlugins_[i].store(retainedRack_[i]->plugin.get(),std::memory_order_relaxed);
  publishedPluginCount_.store(retainedRack_.size(),std::memory_order_release);
}
// Routing is document-owned: connected ports are activated on a playback copy,
// without creating plugin history or leaking inferred enables into saved state.
void PluginChain::prepareRoutingPorts(const MixerGraph &graph) {
  const auto original=states();
  auto prepared=plugins_;
  for(size_t i=0;i<plugins_.size();++i) {
    auto state=original[i];const auto catalog=plugins_[i]->buses();
    auto enable=[&](uint32_t port,bool input) {
      if(!port)return;
      auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &b){return b.input==input&&b.index==port;});
      if(found==catalog.end()||!found->supported)return; // Retain unresolved saved routes.
      auto &ports=input?state.auxiliaryInputs:state.auxiliaryOutputs;
      if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);
    };
    for(const auto &r:graph.sidechains)if(r.plugin==state.instanceID&&r.enabled)enable(r.input,true);
    for(const auto &r:graph.instruments)if(r.plugin==state.instanceID&&r.target)enable(r.output,false);
    const auto active=plugins_[i]->state();
    if(state.auxiliaryInputs!=active.auxiliaryInputs||state.auxiliaryOutputs!=active.auxiliaryOutputs) {
      prepared[i]=std::make_shared<NativePlugin>(state,sampleRate_,offline_);
      // automation_ has already been converted to device-rate frames.
      prepared[i]->automate(automation_,i,48000,position_);
      prepared[i]->observe(activity_.get(),uint32_t(i+1));prepared[i]->audible(true);
    }
  }
  plugins_=std::move(prepared);
  for(size_t i=0;i<plugins_.size();++i){rack_[i]->plugin=plugins_[i];publishedPlugins_[i].store(plugins_[i].get(),std::memory_order_relaxed);}
}
bool PluginChain::parameter(uint32_t slot, uint32_t id, float value) noexcept {
  const ParameterChange change{slot,id,value,0};
  return enqueueParameters({&change,1});
}
size_t PluginChain::bypassStorageBytes() const noexcept {
  size_t bytes=0;for(const auto &plugin:plugins_)bytes+=plugin->bypassStorageBytes();return bytes;
}
bool PluginChain::bypass(size_t slot,bool value) noexcept {
  if(slot>=rack_.size())return false;
  auto &entry=*rack_[slot];entry.plugin->bypass(value);entry.baseline.bypass=value;
  activity_->processors[entry.activity-1].bypass=value;
  return true;
}
bool PluginChain::enqueueParameters(std::span<const ParameterChange> changes) noexcept {
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(changes.size()>queue_.size() || changes.size()>queue_.size()-(w-r) || failed_.load(std::memory_order_relaxed))return false;
  for(const auto &change:changes)if(change.slot>=rack_.size() || !std::isfinite(change.value) || change.frame)return false;
  for(size_t i=0;i<changes.size();++i) {
    const auto &change=changes[i];auto &entry=*rack_[change.slot];
    queue_[(w+uint32_t(i))%queue_.size()]={change,i+1==changes.size(),false,entry.plugin.get(),entry.activity};
    for(auto &p:entry.parameters)if(p.id==change.id)p.value=change.value;
    for(auto &p:activity_->processors[entry.activity-1].parameters)if(p.id==change.id)p.value=change.value;
  }
  write_.store(w+uint32_t(changes.size()),std::memory_order_release);
  return true;
}
void PluginChain::beginRenderBlock() noexcept {observation_->listen.begin(position_);activity_->begin(position_);consumeMusicalPlan();applyPending();parameterBlockOpen_=true;}
bool PluginChain::latencyChangePending() const noexcept {
  // Also queried by Windows rendering: never traverse the control rack while it changes.
  const auto count=publishedPluginCount_.load(std::memory_order_acquire);
  for(size_t i=0;i<count;++i){const auto *p=publishedPlugins_[i].load(std::memory_order_relaxed);if(p->musicalActive()&&p->latencyChangePending())return true;}
  return (signalGraph_ && signalGraph_->latencyChangePending()) ||
         (sampleSignalGraph_ && sampleSignalGraph_->latencyChangePending());
}
void PluginChain::refreshLatencies() {
  try {
    if(parameterBlockOpen_)throw std::logic_error("Finish the audio block before refreshing latency");
    applyPending(); // A queued preset must adopt before its latency is refreshed.
    ++graphControlRevision_; // Reject control/source plans prepared against the old compensation.
    if(mixerTransition_) {
      if(!mixerTransition_->commitStopped())throw std::runtime_error("Cannot settle pending routing before refreshing latency");
      mixer_=&mixerTransition_->renderRuntime();
    }
    for (auto &p : retainedRack_) p->plugin->refreshLatency();
    if (signalGraph_) signalGraph_->refreshLatencies(mixerProcessors_);
    if (sampleSignalGraph_) sampleSignalGraph_->refreshLatencies(mixerProcessors_);
    if (bypassStorageBytes() + (signalGraph_ ? signalGraph_->storageBytes() : 0) +
        (sampleSignalGraph_ ? sampleSignalGraph_->storageBytes() : 0) > 256 * 1024 * 1024)
      throw std::invalid_argument("Song graph audio storage exceeds 256 MB");
    if (mixer_) {
      auto catalog=mixerTransition_->controlPlan().catalog;
      for(auto &info:catalog)for(const auto &entry:rack_)if(info.instance==entry->baseline.instanceID) {
        info.latency=uint32_t(std::llround(entry->plugin->latency()*sampleRate_));
        info.tail=entry->plugin->isInstrument()?std::max(2.,entry->plugin->tail()):entry->plugin->tail();
      }
      // Graph copies report through their synthetic processor identities too.
      // Keep the transition's catalogue coherent, otherwise future repatches
      // compare against pre-refresh latency while the runtime uses new delays.
      for(auto &info:catalog)for(size_t i=plugins_.size();i<mixerProcessors_.size();++i)if(info.instance==mixerProcessors_[i].instance){info.latency=mixerProcessors_[i].latency;info.tail=mixerProcessors_[i].tail;}
      mixerTransition_->refreshStopped(std::move(catalog));
      const auto &plan=mixerTransition_->controlPlan();auto &hosted=*static_cast<HostedMixerPlan *>(plan.processors.get());prepareObservations(plan,hosted);observation_->activate(hosted.observationPlan);
      latency_ = mixer_->plan().latency / sampleRate_; tail_ = mixer_->plan().tail;
    } else {
      latency_ = 0; tail_ = 0;
      double instrumentLatency = 0, instrumentTail = 0;
      for (size_t i = 0; i < plugins_.size(); ++i) {
        const auto &p = *plugins_[i];
        if (!p.isInstrument()) { latency_ += p.latency(); tail_ += p.tail(); }
        else if (instruments_[i]) {
          instrumentLatency = std::max(instrumentLatency, p.latency());
          instrumentTail = std::max(instrumentTail, std::max(2., p.tail()));
        }
      }
      latency_ += instrumentLatency; tail_ += instrumentTail;
      const auto delay = size_t(std::llround(instrumentLatency * sampleRate_)) * 2;
      if (dryDelay_.size() != delay) { dryDelay_.assign(delay, 0); dryDelayPosition_ = 0; dryThrough_ = position_; }
      for (auto &p : plugins_) if (p->isInstrument())
        p->compensateLatency(uint32_t(std::max(0., std::round((instrumentLatency - p->latency()) * sampleRate_))));
    }
    if(!mixer_){std::vector<SignalPortConfiguration> ports;for(size_t i=0;i<plugins_.size();++i)for(const auto &bus:plugins_[i]->buses())if(bus.index<64&&bus.supported){const auto token=bus.input?processorObservations_[i].input[bus.index]:processorObservations_[i].output[bus.index];if(token)ports.push_back({token,int64_t(std::llround(plugins_[i]->latency()*sampleRate_)),bus.input?-1:0});}observation_->activate(ports);}
    captureTails();
  } catch (...) { failed_ = true; throw; }
}
bool PluginChain::process(float *buffer, uint32_t frames) noexcept {
  struct EndBlock {bool &open;~EndBlock(){open=false;}} endBlock{parameterBlockOpen_};
  if(!parameterBlockOpen_)beginRenderBlock();
  if (frames > 4096) {
    failed_ = true;
    std::fill(buffer, buffer + frames * 2, 0.f);
    return false;
  }
  if (mixer_) {
    const bool okay=finishMixer(buffer, frames);
    if(okay)observation_->listen.apply(buffer,frames);
    return okay;
  }
  if (!dryDelay_.empty() && dryThrough_ < position_ + frames) {
    uint32_t offset = uint32_t(std::min<uint64_t>(frames, dryThrough_ > position_ ? dryThrough_ - position_ : 0));
    for (uint32_t n = offset * 2; n < frames * 2; ++n) {
      buffer[n] += dryDelay_[dryDelayPosition_];
      dryDelay_[dryDelayPosition_] = 0;
      dryDelayPosition_ = (dryDelayPosition_ + 1) % dryDelay_.size();
    }
    dryThrough_ = position_ + frames;
  }
  for (size_t i = 0; i < plugins_.size(); ++i) {
    auto &plugin = *plugins_[i];
    if (plugin.isInstrument()) {
      // The engine renders instrument blocks. Complete the final partial block
      // and release tails after the song ends, without rendering a block twice.
      auto rendered = plugin.renderedThrough();
      if (rendered < position_ + frames) {
        uint32_t offset = uint32_t(std::min<uint64_t>(frames, rendered > position_ ? rendered - position_ : 0));
        auto count = frames - offset;
        tailBuffer_.fill(0);
        if (!plugin.process(tailBuffer_.data(), count, position_ + offset))
          failed_ = true;
        routeInstrument(i,tailBuffer_.data(),count,position_+offset);
        if (instruments_[i])
          for (uint32_t n = 0; n < count * 2; ++n)
            buffer[offset * 2 + n] += tailBuffer_[n];
      }
    }
  }
  for (size_t i = 0; i < plugins_.size(); ++i) if (!plugins_[i]->isInstrument()) {
    observation_->observe(processorObservations_[i].input[0],buffer,frames,position_);
    if(!plugins_[i]->process(buffer, frames, position_))failed_=true;
    observation_->observe(processorObservations_[i].output[0],buffer,frames,position_);
  }
  position_ += frames;
  if (failed_) {
    std::fill(buffer, buffer + frames * 2, 0.f);
    return false;
  }
  observation_->listen.apply(buffer,frames);
  return true;
}
std::unique_ptr<GraphControlPlan> PluginChain::prepareGraphControls(const NativeSong &next) {
  if(mixerTransition_){const auto &hosted=*static_cast<HostedMixerPlan *>(mixerTransition_->controlPlan().processors.get());
    if(hosted.songSpec.songSources!=next.signal.songSources || hosted.songSpec.songModulation!=next.signal.songModulation)return nullptr;
  }
  // This path retains copy/role/assignment and outer-port ownership. Recipe
  // internals are compiled independently and transition through aligned dry.
  auto layout=[](SignalGraph graph){graph.songSources.clear();graph.songModulation.clear();for(auto &d:graph.library){d.nodes.clear();d.audio.clear();d.modulation.clear();}return graph;};
  if(!sameSignalControlLayout(layout(preparedSignal_),layout(next.signal)))return nullptr;
  if(!graphControlPlans_.available())throw std::runtime_error("Graph parameter updates are pending; retry the edit");
  auto plan=std::make_unique<GraphControlPlan>();
  plan->signal=next.signal;plan->sourceRevision=graphControlRevision_;plan->activityBase=activity_->processors.size();
  if(noteLedger_)plan->noteRouting=prepareNoteRouting(next);
  size_t preparedBytes=(signalGraph_?signalGraph_->storageBytes():0)+(sampleSignalGraph_?sampleSignalGraph_->storageBytes():0)+bypassStorageBytes();
  std::set<const GraphPluginState *> states;std::set<const SignalRuntime *> runtimes;std::set<const PluginParameterQueue *> queues;std::set<const GraphProcessorSet *> processorSets;std::set<const GraphPluginEndpoint *> endpoints;
  for(const auto &entry:retainedRack_)preparedBytes+=entry->plugin->musicalMIDIStorageBytes();
  auto budget=[&](size_t bytes){preparedBytes+=bytes;if(preparedBytes>256u*1024u*1024u)throw std::invalid_argument("Live graph controls exceed the 256 MB prepared storage budget");};
  auto account=[&](const GraphControlPlan &snapshot){
    // Initial endpoints are already included in NativeSignalGraph::storageBytes.
    for(const auto &owner:snapshot.processorOwners)for(const auto &p:owner.initial->entries){endpoints.insert(p.endpoint.get());states.insert(p.endpoint->initial().get());}
    if(snapshot.noteRouting)budget(snapshot.noteRouting->storageBytes());
    budget(snapshot.observations.capacity()*sizeof(GraphControlPlan::Observation));
    for(const auto &entry:snapshot.observations){const auto &d=entry.descriptor;budget(d.key.capacity()+d.name.capacity()+d.plugin.capacity()+d.parameters.capacity()*sizeof(PluginParameter));for(const auto &p:d.parameters){budget(p.name.capacity()+p.unitLabel.capacity()+p.choices.capacity()*sizeof(std::string));for(const auto &choice:p.choices)budget(choice.capacity());}}
    for(const auto &p:snapshot.presets)for(const auto &state:{p.state,p.previous})if(state&&state!=p.endpoint->initial()&&states.insert(state.get()).second)budget(state->storageBytes());
    for(const auto &owner:snapshot.runtimeOwners){if(owner.state!=owner.initial&&runtimes.insert(owner.state.get()).second)budget(owner.state->storageBytes());for(const auto &old:owner.predecessors)if(old!=owner.initial&&runtimes.insert(old.get()).second)budget(old->storageBytes());}
    for(const auto &owner:snapshot.processorOwners){
      const auto accountSet=[&](const std::shared_ptr<GraphProcessorSet> &set){
        if(set!=owner.initial&&processorSets.insert(set.get()).second)budget(sizeof(*set)+(set->entries.capacity()+set->retired.capacity())*sizeof(GraphProcessorSet::Entry));
        for(const auto *entries:{&set->entries,&set->retired})for(const auto &p:*entries)if(endpoints.insert(p.endpoint.get()).second){budget(p.endpoint->storageBytes());if(states.insert(p.endpoint->initial().get()).second)budget(p.endpoint->initial()->storageBytes());}
      };
      accountSet(owner.state);for(const auto &old:owner.predecessors)accountSet(old);
      budget(owner.predecessors.capacity()*sizeof(std::shared_ptr<GraphProcessorSet>));
    }
    budget(snapshot.processorOwners.capacity()*sizeof(GraphControlPlan::Processors));
    for(const auto &s:snapshot.scheduling)if(queues.insert(s.queue.get()).second)budget(sizeof(*s.queue));
    budget(sizeof(GraphControlPlan)+snapshot.signal.bytes()+snapshot.updates.capacity()*sizeof(GraphParameterUpdate)+snapshot.presets.capacity()*sizeof(GraphControlPlan::Preset)+snapshot.ranges.capacity()*sizeof(GraphControlPlan::Range)+snapshot.scheduling.capacity()*sizeof(GraphControlPlan::Scheduling)+snapshot.runtimeOwners.capacity()*sizeof(GraphControlPlan::Runtime));
    for(const auto &owner:snapshot.runtimeOwners)budget(owner.predecessors.capacity()*sizeof(std::shared_ptr<SignalRuntime>));
    for(const auto &controls:snapshot.controls)budget(sizeof(SignalControls)+controls->definition.bytes()+controls->envelopeCoefficients.capacity()*sizeof(std::array<double,2>));
  };
  // All slots still own memory, including consumed/overwritten candidates.
  // Reserve first-use event storage before allocating one queue per vendor.
  budget(0);graphControlPlans_.forEachRetained(account);plan->preparationHeadroom=256u*1024u*1024u-preparedBytes;
  if(signalGraph_)signalGraph_->prepareParameters(next.signal,*plan,lastGraphControls_);
  if(sampleSignalGraph_)sampleSignalGraph_->prepareParameters(next.signal,*plan,lastGraphControls_);
  // consume() acknowledges before adoption. Retain every possible old runtime
  // through that boundary even if the producer immediately reuses old slots.
  graphControlPlans_.forEachRetained([&](const GraphControlPlan &snapshot){
    for(auto &owner:plan->processorOwners)for(const auto &old:snapshot.processorOwners)if(old.target==owner.target&&old.state!=owner.state&&old.state!=owner.initial)
      if(std::find(owner.predecessors.begin(),owner.predecessors.end(),old.state)==owner.predecessors.end())owner.predecessors.push_back(old.state);
    for(auto &owner:plan->runtimeOwners)for(const auto &old:snapshot.runtimeOwners)if(old.target==owner.target&&old.state!=owner.state&&old.state!=owner.initial)
      if(std::find(owner.predecessors.begin(),owner.predecessors.end(),old.state)==owner.predecessors.end())owner.predecessors.push_back(old.state);
  });
  size_t processorCount=0;for(const auto &owner:plan->processorOwners)processorCount+=owner.state->entries.size();
  if(processorCount>256)throw std::invalid_argument("Active song graph exceeds 256 prepared plugin copies");
  account(*plan);
  if(mixerTransition_){const auto &routing=mixerTransition_->controlPlan();auto catalog=routing.catalog;
    for(auto &processor:catalog)for(const auto &[id,tail]:plan->graphTails)if(processor.instance==id)processor.tail=tail;
    plan->renderedTail=compileMixer(routing.runtime->graph(),mixerTracks_,catalog,uint32_t(sampleRate_)).tail;
  }
  std::map<NativePlugin *,size_t> changed;
  // Three unconsumed publications can accumulate at most 384 changed IDs per
  // processor. Reserve the remaining VST3 queue capacity for graph ramps.
  // Compare producer-owned snapshots, never read audio-owned baseline cells.
  std::map<std::pair<NativePlugin *,uint32_t>,double> prior;
  if(lastGraphControls_)for(const auto &p:lastGraphControls_->updates)if(p.plugin)prior[{p.plugin,p.parameter}]=p.value;
  for(const auto &edit:plan->updates){if(!edit.plugin)continue;
    const auto found=prior.find({edit.plugin,edit.parameter});
    const double previous=found==prior.end()?edit.initialBaseline:found->second;
    if(previous!=edit.value&&++changed[edit.plugin]>128)throw std::invalid_argument("Change at most 128 parameters per graph processor in one live transaction");
  }
  // A bank template can update reusable-graph controls and ordinary rack
  // lanes in one edit. Keep both in this single queue publication, with the
  // same musical revision ordering used by standalone and routing updates.
  auto musical=std::make_shared<HostedMixerPlan>();
  if(lastGraphControls_ && lastGraphControls_->musicalPublication)
    musical->musical=static_cast<const HostedMixerPlan *>(lastGraphControls_->musicalPublication.get())->musical;
  MixerTransition::Plan storage;prepareRoutingMusical(next,*musical,storage);
  if(storage.processorStorage>64u*1024u*1024u)throw std::invalid_argument("Combined graph automation exceeds the 64 MB control budget");
  if(musical->musical)plan->musicalPublication=std::move(musical);
  size_t observations=plan->activityBase;for(const auto &p:plan->observations)observations=std::max(observations,size_t(p.token));
  activity_->processors.reserve(observations);
  return plan;
}
bool PluginChain::publishGraphControls(std::unique_ptr<GraphControlPlan> plan) {
  if(!plan || plan->sourceRevision!=graphControlRevision_ || plan->activityBase!=activity_->processors.size())return false;
  auto *musical=static_cast<HostedMixerPlan *>(plan->musicalPublication.get());
  if((musical && !acceptsRoutingMusical(*musical))||!acceptsNoteRouting(plan->noteRouting))return false;
  auto *published=plan.get();
  if(!graphControlPlans_.publish(std::move(plan)))return false;
  if(musical)commitRoutingMusical(*musical);
  commitNoteRouting(published->noteRouting);
  static_assert(std::is_nothrow_move_constructible_v<ParameterProcessor> && std::is_nothrow_move_assignable_v<ParameterProcessor>);
  // Catalogue vectors are control-only; capacity was reserved before the
  // no-fail document commit. No rejected candidate leaves phantom processors.
  for(auto &entry:published->observations){
    if(entry.token<=activity_->processors.size())activity_->processors[entry.token-1]=std::move(entry.descriptor);
    else activity_->processors.push_back(std::move(entry.descriptor));
  }
  for(const auto &s:published->scheduling)s.state->scheduling=s.queue;
  lastGraphControls_=published;++graphControlRevision_;
  liveGraphTail_.store(published->renderedTail,std::memory_order_relaxed);liveTailRevision_.fetch_add(1,std::memory_order_relaxed);
  for(const auto &r:published->readings)r.plugin->observedBaseline(r.parameter,r.value);
  return true;
}
void PluginChain::applyPending() noexcept {
  if(parameterBlockOpen_)return;
  const auto *recorded=recordedPlans_.consume();
  if(const auto *plan=graphControlPlans_.consume()){
    if(plan->noteRouting)adoptNoteRouting(*plan->noteRouting);
    for(const auto &s:plan->scheduling)s.state->plugin->adoptScheduling(s.queue.get());
    for(const auto &range:plan->ranges)range.plugin->includeParameterRange(range.parameter,range.minimum,range.maximum);
    for(const auto &owner:plan->runtimeOwners){
      if(owner.adopt)owner.adopt(owner.context,owner.state.get(),owner.processors,owner.structural);
      else if(*owner.target!=owner.state.get()){owner.state->inheritState(**owner.target);*owner.target=owner.state.get();}
    }
    for(const auto &preset:plan->presets)preset.endpoint->adopt(*preset.state);
    for(const auto &[endpoint,bypass]:plan->bypasses)endpoint->bypass(bypass);
    for(const auto &[target,value]:plan->tails)*target=value;
    if(sampleSignalGraph_)for(size_t i=0;i<sampleRoutes_.size();++i)sampleRoutes_[i].previewTail=sampleSignalGraph_->tailFrames(i)+uint64_t(std::ceil(.02*sampleRate_));
    for(const auto &[runtime,controls]:plan->runtimes)runtime->controls(*controls);
    for(const auto &change:plan->updates){
      if(change.runtime)change.runtime->parameterBase(change.node,change.parameter,change.value);
      else if(change.resetModulation || !change.appliedBaseline || *change.appliedBaseline!=change.value){
        bool accepted;
        if(change.endpoint)accepted=change.endpoint->parameter(change.parameter,change.value,position_);
        else {change.plugin->cancelScheduledParameter(change.parameter);accepted=change.plugin->appliedParameter(change.parameter,change.value,position_,{});}
        if(!accepted)failed_=true;
        else if(change.appliedBaseline)*change.appliedBaseline=change.value;
      }
    }
    if(plan->musicalPublication){const auto &musical=*static_cast<const HostedMixerPlan *>(plan->musicalPublication.get());if(musical.musical)activateMusicalPlan(*musical.musical);}
  }
  auto r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
  bool recordedApplied=false;
  auto adoptRecorded=[&]{for(const auto &lane:recorded->lanes)if(!lane.plugin->adoptRecordedAutomation(lane.timeline,position_))failed_=true;recordedApplied=true;};
  if(recorded&&int32_t(recorded->parameterFence-r)<=0)adoptRecorded();
  // Bound ordinary UI traffic, but never split a published transaction. The
  // maximum complete batch is the preallocated queue capacity (4096 changes).
  for (unsigned n = 0; r != w;) {
    const auto entry=queue_[r % queue_.size()];const auto &change=entry.change;
    if(entry.observed)entry.plugin->editorParameter(change.id,change.value,position_);
    else if (!entry.plugin->appliedParameter(change.id,change.value,position_,{}))
      failed_ = true;
    ++r;++n;
    if(recorded&&!recordedApplied&&r==recorded->parameterFence)adoptRecorded();
    // A timeline follows every complete manual batch preceding its publication,
    // even if that prefix exceeds the ordinary per-block UI traffic budget.
    if(n>=128 && entry.last && (!recorded||recordedApplied))break;
  }
  read_.store(r, std::memory_order_release);
}
std::vector<PluginState> PluginChain::states() {
  if(parameterBlockOpen_)throw std::logic_error("Finish the audio block before capturing plugin state");
  while (read_.load() != write_.load())
    applyPending();
  std::vector<PluginState> out;
  for(const auto &entry:rack_) {
    auto state=entry->plugin->state();const auto &baseline=entry->baseline;
    state.bypass=entry->plugin->bypassed();state.instrument=baseline.instrument;
    state.auxiliaryInputs=baseline.auxiliaryInputs;state.auxiliaryOutputs=baseline.auxiliaryOutputs;
    out.push_back(std::move(state));
  }
  return out;
}
std::vector<PluginFailureEntry> PluginChain::failureDiagnostics() const {
  std::vector<PluginFailureEntry> result;
  for(size_t i=0;i<rack_.size();++i) {
    const auto failure=rack_[i]->plugin->failure();
    if(failure.reason)result.push_back({i,rack_[i]->baseline.instanceID,failure});
  }
  return result;
}
std::vector<PluginParameter> PluginChain::parameters(size_t slot) const {
  if(slot>=rack_.size())return {};auto result=rack_[slot]->plugin->parameters();
  for(auto &p:result){const auto manual=std::find_if(rack_[slot]->parameters.begin(),rack_[slot]->parameters.end(),[&](const auto &v){return v.id==p.id;});if(manual!=rack_[slot]->parameters.end())p.manualValue=manual->value;}
  return result;
}

std::vector<PluginAudioBus> PluginChain::buses(size_t slot) const {
  if(slot>=rack_.size())return {};auto result=rack_[slot]->plugin->buses();
  // Rendering capacity is immutable; the UI's hollow/connected state follows
  // the accepted routing document instead of mutating a vendor mid-process.
  if(mixerTransition_){const auto &graph=mixerTransition_->controlPlan().runtime->graph();for(auto &bus:result)if(bus.input&&bus.index&&bus.index<64&&bus.supported&&
    (rack_[slot]->plugin->preparedAuxiliaryInputs()&(uint64_t(1)<<bus.index)))for(const auto &route:graph.sidechains)if(route.enabled&&route.plugin==rack_[slot]->baseline.instanceID&&route.input==bus.index)bus.active=true;}
  return result;
}
void PluginChain::captureTails() {
  compiledTails_.resize(plugins_.size());
  for (size_t i = 0; i < plugins_.size(); ++i) compiledTails_[i] = plugins_[i]->tail();
}
uint64_t PluginChain::tailRevision() const noexcept {
  uint64_t revision = liveTailRevision_.load(std::memory_order_relaxed);
  if(signalGraph_)revision+=signalGraph_->tailRevision();
  if(sampleSignalGraph_)revision+=sampleSignalGraph_->tailRevision();
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!plugins_[i]->isInstrument() || instruments_[i]) revision += plugins_[i]->tailRevision();
  return revision;
}
double PluginChain::tail() const {
  double result = std::max({tail_,liveTail_.load(std::memory_order_relaxed),liveGraphTail_.load(std::memory_order_relaxed)});
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!plugins_[i]->isInstrument() || instruments_[i])
      result += std::max(0., plugins_[i]->tail() - compiledTails_[i]);
  if(signalGraph_)result+=signalGraph_->tailGrowth();
  if(sampleSignalGraph_)result+=sampleSignalGraph_->tailGrowth();
  return std::min(60.,result);
}
std::vector<size_t> PluginChain::openEditors() const {
  std::vector<size_t> slots;
  for(size_t i=0;i<rack_.size();++i)if(rack_[i]->plugin->editorOpen())slots.push_back(i);
  return slots;
}
void PluginChain::delayDry(float *left, float *right, uint32_t frames, uint64_t position) noexcept {
  if (!dryDelay_.empty())
    for (uint32_t n = 0; n < frames; ++n) {
      std::swap(left[n], dryDelay_[dryDelayPosition_]);
      std::swap(right[n], dryDelay_[dryDelayPosition_ + 1]);
      dryDelayPosition_ = (dryDelayPosition_ + 2) % dryDelay_.size();
    }
  dryThrough_ = position + frames;
}
void PluginChain::endNotes() noexcept {
  if(noteLedger_)releaseInstrumentNotes(nullptr,position_);
  for (auto &plugin : plugins_)
    if (plugin->isInstrument())
      for (uint8_t ch = 0; ch < 16; ++ch)
        plugin->midi(0xb0 | ch, 123, 0);
}
void PluginChain::showEditor(size_t slot) {
  if (slot >= rack_.size())
    throw std::runtime_error("Select a plugin");
  rack_[slot]->plugin->showEditor();
}
bool PluginChain::popEdit(size_t slot, uint32_t &id, float &value) noexcept {
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(w-r==queue_.size()||slot >= rack_.size() || !rack_[slot]->plugin->popEdit(id, value))return false;
  auto &entry=*rack_[slot];
  queue_[w%queue_.size()]={{uint32_t(slot),id,value,0},true,true,entry.plugin.get(),entry.activity};write_.store(w+1,std::memory_order_release);
  for(auto &p:entry.parameters)if(p.id==id)p.value=value;
  for(auto &p:activity_->processors[entry.activity-1].parameters)if(p.id==id)p.value=value;
  return true;
}

bool PluginChain::graphController(uint8_t cc,uint8_t value) noexcept {if(cc>=128||value>=128)return false;songControllers_[cc].store(value,std::memory_order_relaxed);if(signalGraph_)signalGraph_->controller(cc,value);if(sampleSignalGraph_)sampleSignalGraph_->controller(cc,value);return bool(mixerTransition_)||bool(signalGraph_)||bool(sampleSignalGraph_);}
std::vector<SignalActivity> PluginChain::graphActivity() const {
  auto result=signalGraph_?signalGraph_->activity():std::vector<SignalActivity>{};
  if(sampleSignalGraph_)for(auto value:sampleSignalGraph_->activity()){const auto index=value.target-NativeSong::maximumID-1;if(index<sampleRoutes_.size()&&sampleRoutes_[index].target){value.target=sampleRoutes_[index].target;value.instrument=sampleRoutes_[index].instrumentID;value.role=3;result.push_back(value);}}
  return result;
}
void PluginChain::prepareRouteObservations(const MixerTransition::Plan &plan,HostedMixerPlan &hosted,std::vector<SignalPortIdentity> &pending) {
  const auto &graph=plan.runtime->graph();const auto &mix=plan.runtime->plan();
  auto busID=[&](size_t index){return "n"+std::to_string(graph.buses[index].id);};
  auto graphBus=[&](size_t processor)->size_t{for(size_t i=0;i<graph.buses.size();++i)if(plan.catalog[processor].instance==signalBusIdentity(graph.buses[i].id))return i;return SIZE_MAX;};
  auto token=[&](SignalRouteIdentity route,std::string name)->uint32_t {
    auto component=[](const std::string &s){return std::to_string(s.size())+":"+s;};
    const auto key="route/"+route.kind+"/"+component(route.source)+component(route.target)+component(route.plugin)+"/"+std::to_string(route.input)+"/"+std::to_string(route.output);
    const auto old=std::find_if(observation_->ports.begin(),observation_->ports.end(),[&](const auto &p){return p.key==key;});
    if(old!=observation_->ports.end())return uint32_t(old-observation_->ports.begin()+1);
    const auto staged=std::find_if(pending.begin(),pending.end(),[&](const auto &p){return p.key==key;});
    if(staged!=pending.end())return uint32_t(observation_->ports.size()+size_t(staged-pending.begin())+1);
    SignalPortIdentity port{key,!route.source.empty()?route.source:"plugin:"+route.plugin,std::move(name),true,route.output,2,0,0};port.route=std::move(route);pending.push_back(std::move(port));
    return uint32_t(observation_->ports.size()+pending.size());
  };
  auto &connections=hosted.routeObservations[0];connections.assign(mix.connections.size(),0);
  for(size_t i=0;i<mix.connections.size();++i){const auto &edge=mix.connections[i];SignalRouteIdentity route;route.kind=edge.send?"send":"output";route.source=busID(edge.source);route.target=busID(edge.target);
    connections[i]=token(std::move(route),graph.buses[edge.source].name+(edge.send?" send → ":" output → ")+graph.buses[edge.target].name);}
  auto &sidechains=hosted.routeObservations[1];sidechains.assign(mix.sidechains.size(),0);
  for(size_t i=0;i<mix.sidechains.size();++i){const auto &edge=mix.sidechains[i];SignalRouteIdentity route;route.source=busID(edge.source);route.input=edge.input;const auto target=graphBus(edge.processor);
    if(target!=SIZE_MAX){route.kind="graph-input";route.target=busID(target);}else {route.kind="plugin-input";route.plugin=plan.catalog[edge.processor].instance;}
    sidechains[i]=token(std::move(route),graph.buses[edge.source].name+" → input "+std::to_string(edge.input));}
  auto &instruments=hosted.routeObservations[2];instruments.assign(mix.instruments.size(),0);
  for(size_t i=0;i<mix.instruments.size();++i){const auto &edge=mix.instruments[i];SignalRouteIdentity route;route.target=busID(edge.target);route.output=edge.output;const auto source=graphBus(edge.processor);
    if(source!=SIZE_MAX){route.kind="graph-output";route.source=busID(source);}else {
      // Sample-instrument graph adapters have independent per-channel copies;
      // their internal observation identity belongs to the later recipe slice.
      if(!hosted.processors[edge.processor]->processor().plugin)continue;
      route.kind="plugin-output";route.plugin=plan.catalog[edge.processor].instance;
    }
    instruments[i]=token(std::move(route),"Output "+std::to_string(edge.output)+" → "+graph.buses[edge.target].name);}
  auto &inserts=hosted.routeObservations[3];inserts.assign(plan.catalog.size(),0);
  for(size_t bus=0;bus<mix.nodes.size();++bus)for(auto processor:mix.nodes[bus].processors)if(hosted.processors[processor]->processor().plugin){SignalRouteIdentity route;route.kind="insert";route.source=busID(bus);route.plugin=plan.catalog[processor].instance;route.tap="main-path";
    inserts[processor]=token(std::move(route),graph.buses[bus].name+" serial main path");}
  auto &master=hosted.routeObservations[4];master.assign(mix.nodes.size(),0);
  if(!mix.nodes[mix.master].processors.empty()){SignalRouteIdentity route;route.kind="master-output";route.source=busID(mix.master);route.tap="pre-master-fader";
    master[mix.master]=token(std::move(route),graph.buses[mix.master].name+" before final fader");}
}
void PluginChain::prepareObservations(const MixerTransition::Plan &plan,HostedMixerPlan &hosted) {
  hosted.observationPlan.clear();
  for(size_t i=0;i<hosted.busObservations.size();++i){const auto &node=plan.runtime->plan().nodes[i];const auto &ports=hosted.busObservations[i];
    hosted.observationPlan.push_back({ports[0],0,node.directDelay});hosted.observationPlan.push_back({ports[1],int64_t(node.outputLatency)-node.inputLatency,0});}
  for(size_t i=0;i<hosted.processors.size();++i)if(const auto &plugin=hosted.processors[i]->processor().plugin){const auto &ports=hosted.processorObservations[i];
    for(const auto &bus:plugin->buses())if(bus.index<64&&bus.supported){const auto token=bus.input?ports.input[bus.index]:ports.output[bus.index];
      if(token)hosted.observationPlan.push_back({token,plan.catalog[i].latency,bus.input?-1:0});}
  }
  const auto &mix=plan.runtime->plan();
  for(size_t kind=0;kind<hosted.routeObservations.size();++kind)for(size_t i=0;i<hosted.routeObservations[kind].size();++i)if(auto token=hosted.routeObservations[kind][i]){
    SignalPortConfiguration value{token,0,0};
    if(kind==0){const auto &edge=mix.connections[i];value.compensation=edge.delay;value.routeGain=edge.gain;value.preFader=edge.preFader;}
    else if(kind==1){const auto &edge=mix.sidechains[i];value.compensation=edge.delay;value.routeGain=edge.gain;value.preFader=edge.preFader;}
    else if(kind==2)value.compensation=mix.instruments[i].delay;
    else if(kind==4)value.preFader=true;
    hosted.observationPlan.push_back(value);
  }
}
std::unique_ptr<MixerTransition::Plan> PluginChain::prepareMixerRouting(const NativeSong &native) {
  if(!mixerTransition_)return {};
  const auto &previous=mixerTransition_->controlPlan();
  return prepareMixerRouting(native,previous.catalog,std::make_shared<HostedMixerPlan>(*static_cast<HostedMixerPlan *>(previous.processors.get())));
}
std::unique_ptr<MixerTransition::Plan> PluginChain::prepareMixerRouting(const NativeSong &native,const std::vector<MixerProcessorInfo> &inputCatalog,std::shared_ptr<HostedMixerPlan> hosted) {
  auto catalog=inputCatalog;
  if(lastGraphControls_)for(auto &processor:catalog)for(const auto &[id,tail]:lastGraphControls_->graphTails)if(processor.instance==id)processor.tail=tail;
  // Reuse existing ordinary/row/persistent and sample-instrument graph copies
  // only when their processing, audio dependencies and note membership remain
  // unchanged. Physical bus activation still cannot mutate a live vendor.
  if(!mixerTransition_ || latencyChangePending())return {};
  const auto &signal=lastGraphControls_?lastGraphControls_->signal:preparedSignal_;
  auto recipeBefore=signal,recipeAfter=native.signal;recipeBefore.songSources.clear();recipeBefore.songModulation.clear();recipeAfter.songSources.clear();recipeAfter.songModulation.clear();
  if(!sameSignalProcessing(recipeBefore,recipeAfter) ||
     (signalGraph_ && !signalGraph_->sameNoteMembership(native)))return {};
  if(!mixerTransition_->ready())throw std::runtime_error("A routing transition is still preparing");
  const auto &previous=mixerTransition_->controlPlan();
  const auto &oldBuses=previous.runtime->graph().buses;
  for(const auto &after:native.mixer.buses) {
    const auto before=std::find_if(oldBuses.begin(),oldBuses.end(),[&](const auto &b){return b.id==after.id;});
    if(before!=oldBuses.end()) {if(before->kind!=after.kind)return {};}
    else if(after.kind!=MixerBusKind::Group && after.kind!=MixerBusKind::Return)return {};
  }
  std::vector<uint64_t> tracks;for(const auto &[index,track]:native.tracks)tracks.push_back(track.id);
  if(tracks!=mixerTracks_)return {};
  auto graph=signalRoutingGraph(native.mixer,native.signal);
  for(size_t i=0;i<sampleRoutes_.size();++i)
    graph.instruments.push_back({signalBusIdentity(NativeSong::maximumID+1+i),sampleRoutes_[i].target,0});
  for(const auto &route:graph.sidechains)if(route.enabled && route.input) {
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.instance==route.plugin;});
    if(found==catalog.end() || route.input>=64 || !(found->activeInputs&(uint64_t(1)<<route.input)))return {};
  }
  for(const auto &route:graph.instruments)if(route.target && route.output) {
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.instance==route.plugin;});
    if(found==catalog.end() || route.output>=64 || !(found->activeOutputs&(uint64_t(1)<<route.output)))return {};
  }
  const auto compiled=compileMixer(graph,tracks,catalog,uint32_t(sampleRate_));
  if(compiled.latency!=previous.runtime->plan().latency)return {};
  auto prepared=mixerTransition_->prepareRetained(std::move(graph),catalog);
  hosted->busObservations.clear();
  std::vector<SignalPortIdentity> routingPorts;const auto routingPortBase=observation_->ports.size();
  for(size_t i=0;i<prepared->runtime->graph().buses.size();++i) {
    const auto &bus=prepared->runtime->graph().buses[i];const auto node="n"+std::to_string(bus.id);
    const auto &plan=prepared->runtime->plan().nodes[i];
    auto port=[&](bool output)->uint32_t {
      const auto key=node+(output?"/out/0":"/in/0");
      const auto found=std::find_if(observation_->ports.begin(),observation_->ports.end(),[&](const auto &p){return p.key==key;});
      if(found!=observation_->ports.end())return uint32_t(found-observation_->ports.begin()+1);
      routingPorts.push_back({key,node,bus.name+(output?" output":" input"),output,0,2,
        output?int64_t(plan.outputLatency)-plan.inputLatency:0,output?0:plan.directDelay});
      return uint32_t(routingPortBase+routingPorts.size());
    };
    hosted->busObservations.push_back({port(false),port(true)});
  }
  prepared->processorStorage=(signalGraph_?signalGraph_->storageBytes():0)+(sampleSignalGraph_?sampleSignalGraph_->storageBytes():0)+
    hosted->processors.size()*(sizeof(MixerProcessor)+RenderOnce<MixerProcessor>::storageBytes());
  for(const auto &entry:hosted->rack)prepared->processorStorage+=entry->plugin->bypassStorageBytes()+entry->plugin->musicalMIDIStorageBytes();
  if(noteLedger_){hosted->noteRouting=prepareNoteRouting(native,hosted->rack);prepared->processorStorage+=hosted->noteRouting->storageBytes();}
  hosted->song=prepareSongControls(native,*prepared,*hosted);
  hosted->songSpec.songSources=native.signal.songSources;hosted->songSpec.songModulation=native.signal.songModulation;
  prepared->processorStorage+=hosted->songSpec.bytes();
  if(!hosted->musical)hosted->musical=static_cast<HostedMixerPlan *>(previous.processors.get())->musical;
  prepareRoutingMusical(native,*hosted,*prepared);
  mixerTransition_->prepareDependencies(*prepared);
  prepareRouteObservations(*prepared,*hosted,routingPorts);
  for(const auto &tokens:hosted->routeObservations)prepared->processorStorage+=tokens.capacity()*sizeof(uint32_t);
  hosted->pendingPorts=std::make_shared<SignalObservation::PreparedPorts>(observation_->preparePorts(std::move(routingPorts)));
  prepared->processorStorage+=hosted->pendingPorts->storageBytes();
  prepareObservations(*prepared,*hosted);prepared->processorStorage+=hosted->observationPlan.capacity()*sizeof(SignalPortConfiguration);
  prepared->processors=std::move(hosted);prepared->process=previous.process;prepared->output=HostedMixerPlan::output;
  prepared->begin=HostedMixerPlan::begin;prepared->adopt=HostedMixerPlan::adopt;prepared->source=HostedMixerPlan::source;
  prepared->runtime->observer(HostedMixerPlan::observe,prepared.get());prepared->runtime->routeObserver(HostedMixerPlan::observeRoute,prepared.get());
  if(!mixerTransition_->accepts(*prepared))throw std::invalid_argument("Prepared routing exceeds the combined audio storage budget");
  return prepared;
}
std::shared_ptr<PluginChain::RackEntry> PluginChain::prepareRackEntry(const PluginState &state) {
  auto entry=std::make_shared<RackEntry>();entry->baseline=state;
  entry->plugin=std::make_shared<NativePlugin>(state,sampleRate_,offline_);entry->plugin->prepareMusicalAutomation();
  entry->parameters=entry->plugin->parameters();
  return entry;
}
std::unique_ptr<PluginChain::RackPlan> PluginChain::prepareRack(const std::vector<PluginState> &states,const NativeSong &native) {
  if(!mixerTransition_||!mixerRoutingReady())throw std::runtime_error("A routing transition is still preparing; retry the edit");
  validatePluginCapacity(states,native.mixer.buses.size());
  auto result=std::make_unique<RackPlan>();result->retained=retainedRack_;
  for(const auto &state:states) {
    auto found=std::find_if(result->retained.begin(),result->retained.end(),[&](const auto &p){return p->baseline.instanceID==state.instanceID;});
    std::shared_ptr<RackEntry> entry;
    if(found!=result->retained.end()) {
      entry=*found;const auto &base=entry->baseline;
      // This API edits topology, not presets: retained identity means retain
      // current opaque vendor/controller state, including custom-editor edits.
      // Saved state is used only to construct a genuinely new identity.

      if(state.descriptor!=base.descriptor||state.instrument!=base.instrument||state.midiChannel!=base.midiChannel||state.aliases!=base.aliases||state.auxiliaryInputs!=base.auxiliaryInputs||state.auxiliaryOutputs!=base.auxiliaryOutputs)
        throw std::runtime_error("Changing a live plugin source or physical ports requires a stopped transport");
    } else {
      if(state.descriptor.instrument||state.descriptor.type==audioUnitMusicDeviceType)throw std::runtime_error("Adding an instrument source requires a stopped transport");
      if(result->retained.size()>=256)throw std::runtime_error("Live processor retention is full; stop playback before adding more effects");
      entry=prepareRackEntry(state);result->retained.push_back(entry);
    }
    result->rack.push_back(entry);
  }
  for(const auto &entry:rack_)if(entry->plugin->isInstrument()&&std::none_of(result->rack.begin(),result->rack.end(),[&](const auto &p){return p==entry;}))
    throw std::runtime_error("Removing an instrument source requires a stopped transport");
  size_t retainedBytes=0;for(const auto &entry:result->retained)retainedBytes+=entry->plugin->preparedStorageBytes()+sizeof(RackEntry);
  if(retainedBytes>256u*1024u*1024u)throw std::runtime_error("Live processor retention exceeds the prepared audio budget; stop playback before adding effects");
  const auto &previous=mixerTransition_->controlPlan();auto &old=*static_cast<HostedMixerPlan *>(previous.processors.get());
  auto hosted=std::make_shared<HostedMixerPlan>();hosted->owner=this;hosted->rack=result->rack;
  std::vector<MixerProcessorInfo> catalog;
  for(const auto &entry:result->rack) {
    const auto &plugin=*entry->plugin;uint32_t count=1;uint64_t outputs=1,inputs=plugin.preparedAuxiliaryInputs();
    for(const auto &bus:plugin.buses())if(bus.index<64) {if(bus.input&&bus.index&&bus.active)inputs|=uint64_t(1)<<bus.index;if(!bus.input){count=std::max(count,bus.index+1);if(bus.active)outputs|=uint64_t(1)<<bus.index;}}
    catalog.push_back({entry->baseline.instanceID,uint32_t(std::llround(plugin.latency()*sampleRate_)),plugin.isInstrument()?std::max(2.,plugin.tail()):plugin.tail(),plugin.isInstrument(),plugin.isInstrument()&&!entry->baseline.instrument,count,outputs,inputs,plugin.mainInputFallback()});
    const auto prior=std::find_if(previous.catalog.begin(),previous.catalog.end(),[&](const auto &p){return p.instance==entry->baseline.instanceID;});
    if(prior!=previous.catalog.end())hosted->processors.push_back(old.processors[size_t(prior-previous.catalog.begin())]);
    else {auto processor=std::make_shared<MixerProcessor>();processor->plugin=entry->plugin;
      for(const auto &port:plugin.buses())if(!port.input&&port.index&&port.active)processor->outputs.push_back(port.index);
      hosted->processors.push_back(std::make_shared<RenderOnce<MixerProcessor>>(std::move(processor)));}
    hosted->processorObservations.push_back(entry->ports);
  }
  // Recipe and sample graph processors retain their identity and state too.
  for(size_t i=0;i<previous.catalog.size();++i)if(!old.processors[i]->processor().plugin) {
    catalog.push_back(previous.catalog[i]);hosted->processors.push_back(old.processors[i]);hosted->processorObservations.push_back({});
  }
  result->routing=prepareMixerRouting(native,catalog,std::move(hosted));
  if(!result->routing)throw std::runtime_error("This live rack change has unsupported source, port or latency changes; playback was preserved");
  // Telemetry metadata is prepared only after topology, ports and memory
  // validation succeeds; rejected candidates publish no orphan identities.
  auto &preparedHosted=*static_cast<HostedMixerPlan *>(result->routing->processors.get());
  result->routing->processorStorage-=preparedHosted.pendingPorts->storageBytes();
  auto ports=std::move(preparedHosted.pendingPorts->identities);const auto portBase=preparedHosted.pendingPorts->base;
  result->activityBase=activity_->processors.size();
  for(const auto &entry:result->rack)if(!entry->activity){
    for(const auto &port:entry->plugin->buses())if(port.index<64&&port.supported&&port.channels>=1&&port.channels<=2){
      const auto node="plugin:"+entry->baseline.instanceID;
      ports.push_back({node+(port.input?"/in/":"/out/")+std::to_string(port.index),node,port.name,!port.input,port.index,port.channels,int64_t(std::llround(entry->plugin->latency()*sampleRate_)),0});
      (port.input?entry->ports.input[port.index]:entry->ports.output[port.index])=uint32_t(portBase+ports.size());
    }
    ParameterProcessor observed;observed.key="rack/"+entry->baseline.instanceID;observed.name=entry->baseline.descriptor.name;observed.plugin=entry->baseline.instanceID;observed.bypass=entry->baseline.bypass;observed.parameters=entry->parameters;
    result->activity.push_back(std::move(observed));entry->activity=uint32_t(result->activityBase+result->activity.size());
    entry->plugin->observe(activity_.get(),entry->activity);
  }
  if(result->activityBase+result->activity.size()>4096)throw std::invalid_argument("Parameter observation capacity exceeded");
  preparedHosted.pendingPorts=std::make_shared<SignalObservation::PreparedPorts>(observation_->preparePorts(std::move(ports)));
  result->routing->processorStorage+=preparedHosted.pendingPorts->storageBytes();
  for(size_t i=0;i<result->rack.size();++i)preparedHosted.processorObservations[i]=result->rack[i]->ports;
  result->routing->processorStorage-=preparedHosted.observationPlan.capacity()*sizeof(SignalPortConfiguration);
  prepareObservations(*result->routing,preparedHosted);result->routing->processorStorage+=preparedHosted.observationPlan.capacity()*sizeof(SignalPortConfiguration);
  if(!mixerTransition_->accepts(*result->routing))throw std::invalid_argument("Prepared rack metadata exceeds the combined audio storage budget");
  activity_->processors.reserve(result->activityBase+result->activity.size());
  return result;
}
bool PluginChain::publishRack(std::unique_ptr<RackPlan> &plan) noexcept {
  if(!plan || !plan->routing || activity_->processors.size()!=plan->activityBase)return false;
  const auto tail=plan->routing->runtime->plan().tail;
  if(!publishMixerRouting(plan->routing))return false;
  static_assert(std::is_nothrow_move_constructible_v<ParameterProcessor>);
  for(auto &entry:plan->activity)activity_->processors.push_back(std::move(entry));
  // Stable pointers in already queued edits keep their original destination.
  // Retired effects ignore musical events until restored, so no orphan queue fills.
  for(const auto &entry:rack_)if(std::find(plan->rack.begin(),plan->rack.end(),entry)==plan->rack.end())entry->plugin->musicalActive(false);
  for(const auto &entry:plan->rack)entry->plugin->musicalActive(true);
  for(size_t i=retainedRack_.size();i<plan->retained.size();++i)publishedPlugins_[i].store(plan->retained[i]->plugin.get(),std::memory_order_relaxed);
  publishedPluginCount_.store(plan->retained.size(),std::memory_order_release);
  rack_.swap(plan->rack);retainedRack_.swap(plan->retained);
  liveTail_.store(tail,std::memory_order_relaxed);liveTailRevision_.fetch_add(1,std::memory_order_relaxed);return true;
}
bool PluginChain::publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &plan) noexcept {
  if(!plan || !plan->processors)return false;
  auto &hosted=*static_cast<HostedMixerPlan *>(plan->processors.get());
  if(!acceptsNoteRouting(hosted.noteRouting) || !acceptsRoutingMusical(hosted) || (hosted.pendingPorts && !observation_->canPublishPorts(*hosted.pendingPorts)))return false;
  const bool modulated=!hosted.songSpec.songModulation.empty();
  const bool published=mixerTransition_ && mixerTransition_->publish(plan);
  if(published){if(hosted.pendingPorts)observation_->publishPorts(*hosted.pendingPorts);commitRoutingMusical(hosted);commitNoteRouting(hosted.noteRouting);}
  if(published && modulated)hasMusicalControls_.store(true,std::memory_order_relaxed);
  return published;
}
void PluginChain::beginMixer(uint32_t frames) noexcept {
  if (!mixer_) return;
  applyPending();if(!mixerTransition_->begin(frames,mixer_->through())){failed_=true;return;}
  mixer_=&mixerTransition_->renderRuntime();
  for(auto &input:mixerDirect_)input->captured=false;
}
void PluginChain::routeInstrument(size_t processor, const float *buffer,uint32_t frames,uint64_t position) noexcept {
  if(processor<processorObservations_.size()) {
    if(frames) {
      observation_->observe(processorObservations_[processor].output[0],buffer,frames,position);
      for(const auto &bus:plugins_[processor]->buses())if(!bus.input&&bus.index&&bus.index<64&&bus.active)
        observation_->observe(processorObservations_[processor].output[bus.index],plugins_[processor]->auxiliaryOutput(bus.index),frames,position);
    }
  }
  if (mixer_) {
    mixerTransition_->instrument(processor, 0, buffer);
    for (const auto &bus : plugins_[processor]->buses()) if (!bus.input && bus.index && bus.active)
      mixerTransition_->instrument(processor, bus.index, plugins_[processor]->auxiliaryOutput(bus.index));
  }
}
void PluginChain::processSampleGraph(size_t index,const float *left,const float *right,uint32_t frames,float *outputLeft,float *outputRight) noexcept {
  if(!sampleSignalGraph_||!mixer_||index>=sampleRoutes_.size()||frames>4096){failed_=true;return;}
  auto &route=sampleRoutes_[index];
  if(!route.target){const auto budget=sampleSignalGraph_->tailFrames(index)+uint64_t(std::ceil(.02*sampleRate_));if(budget>route.previewTail){if(route.previewRemaining)route.previewRemaining+=budget-route.previewTail;route.previewTail=budget;}}
  for(uint32_t f=0;f<frames;++f){sampleGraphBuffer_[f*2]=left?left[f]:0;sampleGraphBuffer_[f*2+1]=right?right[f]:0;if(!route.target&&(sampleGraphBuffer_[f*2]!=0||sampleGraphBuffer_[f*2+1]!=0))route.previewRemaining=frames+route.previewTail;}
  if(!sampleSignalGraph_->process(index,sampleGraphBuffer_.data(),frames,route.target||!outputLeft?mixer_->through():mixer_->through()-frames,{})){failed_=true;return;}
  if(sampleRoutes_[index].target)mixerTransition_->instrument(sampleRoutes_[index].processor,0,sampleGraphBuffer_.data());
  else {const auto audible=std::min<uint64_t>(frames,route.previewRemaining);route.previewRemaining-=audible;
    if(outputLeft&&outputRight)for(uint32_t f=0;f<audible;++f){outputLeft[f]+=sampleGraphBuffer_[2*f];outputRight[f]+=sampleGraphBuffer_[2*f+1];}}
}
const float *PluginChain::captureMixerBus(size_t bus,const float *left,const float *right,uint32_t frames,bool finish) noexcept {
  if(!mixer_ || frames>MixerRuntime::maximumFrames || bus>=mixerDirect_.size()){failed_=true;return nullptr;}
  auto &input=*mixerDirect_[bus];
  if(left)std::copy_n(left,frames,input.left.data());else std::fill_n(input.left.data(),frames,0);
  if(right)std::copy_n(right,frames,input.right.data());else std::fill_n(input.right.data(),frames,0);
  input.captured=true;
  if(!finish)return nullptr;
  for(size_t index=0;index<mixerDirect_.size();++index) {
    const auto &source=*mixerDirect_[index];
    mixerInputs_[index]={source.captured?source.left.data():nullptr,source.captured?source.right.data():nullptr};
  }
  const auto *output=mixerTransition_->render(mixerInputs_);
  if(!output || mixerTransition_->failed())failed_=true;
  mixer_=&mixerTransition_->renderRuntime();
  return output;
}
bool PluginChain::MixerProcessor::process(float *buffer,uint32_t frames,uint64_t position,std::span<const PluginAudioInput> inputs) noexcept {
  if(plugin)return plugin->process(buffer,frames,position,inputs,modulation);
  return graph && graph->process(graphIndex,buffer,frames,position,inputs);
}
const float *PluginChain::MixerProcessor::output(uint32_t port) const noexcept {
  return plugin?plugin->auxiliaryOutput(port):graph?graph->output(graphIndex,port):nullptr;
}
bool PluginChain::HostedMixerPlan::process(void *context,MixerRuntime &mixer,size_t processor,float *buffer,uint32_t frames,uint64_t position) noexcept {
    auto &hosted=*static_cast<HostedMixerPlan *>(context);auto &chain=*hosted.owner;
    if(processor>=hosted.processors.size())return false;
    auto &wrapper=*hosted.processors[processor];auto &device=wrapper.processor();
    if(!device.plugin) {
      if(!wrapper.process(buffer,frames,position,mixer.inputs(processor)))return false;
      for(auto port:device.outputs)mixer.instrument(processor,port,device.output(port));return true;
    }
    const auto &observed=hosted.processorObservations[processor];
    device.modulation=chain.modulationFor(hosted,processor);
    device.plugin->transport(chain.currentTransport_);
    const bool observe=&mixer==&chain.mixerTransition_->renderRuntime();
    if(observe)chain.observation_->observe(observed.input[0],buffer,frames,position);
    for(const auto &port:device.plugin->buses())if(port.input&&port.index&&port.index<64&&(device.plugin->preparedAuxiliaryInputs()&(uint64_t(1)<<port.index))) {
      const float *samples=nullptr;for(const auto &input:mixer.inputs(processor))if(input.bus==port.index){samples=input.samples;break;}
      if(observe)chain.observation_->observe(observed.input[port.index],samples,frames,position);
    }
    const bool okay=wrapper.process(buffer, frames, position, mixer.inputs(processor));
    chain.songFollower(hosted,SIZE_MAX,processor,0,buffer,frames,position);
    if(observe)chain.observation_->observe(observed.output[0],buffer,frames,position);
    for(auto port:device.outputs) {
      if(observe && port<64)chain.observation_->observe(observed.output[port],device.output(port),frames,position);
      chain.songFollower(hosted,SIZE_MAX,processor,port,device.output(port),frames,position);
      if(okay)mixer.instrument(processor,port,device.output(port));
    }
    return okay;
}
const float *PluginChain::HostedMixerPlan::output(void *context,size_t processor,uint32_t port) noexcept {
  auto &hosted=*static_cast<HostedMixerPlan *>(context);
  return processor<hosted.processors.size()?hosted.processors[processor]->processor().output(port):nullptr;
}
void PluginChain::HostedMixerPlan::observe(void *context,size_t bus,bool output,const float *samples,uint32_t frames,uint64_t position) noexcept {
  auto &plan=*static_cast<MixerTransition::Plan *>(context);
  auto &hosted=*static_cast<HostedMixerPlan *>(plan.processors.get());auto &chain=*hosted.owner;
  if(output){chain.songFollower(hosted,bus,SIZE_MAX,0,plan.runtime->busPreFader(bus),frames,position);chain.songFollower(hosted,bus,SIZE_MAX,1,samples,frames,position);}
  if(plan.runtime.get()==&chain.mixerTransition_->renderRuntime() && bus<hosted.busObservations.size())
    chain.observation_->observe(hosted.busObservations[bus][output?1:0],samples,frames,position);
}
void PluginChain::HostedMixerPlan::observeRoute(void *context,MixerRuntime::RouteKind kind,size_t index,const float *samples,uint32_t frames,uint64_t position) noexcept {
  auto &plan=*static_cast<MixerTransition::Plan *>(context);auto &hosted=*static_cast<HostedMixerPlan *>(plan.processors.get());auto &chain=*hosted.owner;
  const auto &tokens=hosted.routeObservations[size_t(kind)];
  if(plan.runtime.get()==&chain.mixerTransition_->renderRuntime() && index<tokens.size() && tokens[index])chain.observation_->observe(tokens[index],samples,frames,position);
}
bool PluginChain::finishMixer(float *buffer, uint32_t frames) noexcept {
  const uint64_t end = position_ + frames;
  if (mixer_->through() < end) {
    const uint32_t offset = uint32_t(mixer_->through() > position_ ? mixer_->through() - position_ : 0);
    const auto count = frames - offset;
    beginMixer(count);
    if(signalGraph_)signalGraph_->tail();
    if(sampleSignalGraph_){sampleSignalGraph_->tail();for(size_t i=0;i<sampleRoutes_.size();++i)processSampleGraph(i,nullptr,nullptr,count);}
    for (size_t i = 0; i < plugins_.size(); ++i) if (plugins_[i]->isInstrument() && instruments_[i]) {
      tailBuffer_.fill(0);
      if (!plugins_[i]->process(tailBuffer_.data(), count, position_ + offset)) failed_ = true;
      routeInstrument(i, tailBuffer_.data(),count,position_+offset);
    }
    std::fill(mixerInputs_.begin(),mixerInputs_.end(),MixerTransition::DirectInput{});
    if(const auto *result=mixerTransition_->render(mixerInputs_))std::copy_n(result,count*2,buffer+offset*2);else failed_=true;
    mixer_=&mixerTransition_->renderRuntime();
    if (mixerRenderer_) mixerRenderer_->processNativeTail(buffer + offset * 2, count);
  }
  position_ = end;
  if (failed_) { std::fill_n(buffer, frames * 2, 0.f); return false; }
  return true;
}
} // namespace Tracker
