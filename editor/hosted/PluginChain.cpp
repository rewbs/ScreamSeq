// Extracted from mac/Audio/AudioUnitHost.mm; shared by all platform hosts.
#include "HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
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
    plugin->automate(automation, plugins_.size(), rate, uint64_t(double(startFrame) * rate / 48000));
    if (!plugin->isInstrument()) {
      latency_ += plugin->latency();
      tail_ += plugin->tail();
    }

    ParameterProcessor observed;observed.key="rack/"+state.instanceID;observed.name=state.descriptor.name;observed.plugin=state.instanceID;observed.bypass=state.bypass;observed.parameters=plugin->parameters();
    plugin->observe(activity_.get(),activity_->add(std::move(observed)));plugin->audible(true);
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
}
bool PluginChain::parameter(uint32_t slot, uint32_t id, float value) noexcept {
  const ParameterChange change{slot,id,value,0};
  return enqueueParameters({&change,1});
}
size_t PluginChain::bypassStorageBytes() const noexcept {
  size_t bytes=0;for(const auto &plugin:plugins_)bytes+=plugin->bypassStorageBytes();return bytes;
}
bool PluginChain::bypass(size_t slot,bool value) noexcept {
  if(slot>=plugins_.size())return false;
  plugins_[slot]->bypass(value);bypass_[slot]=value;
  activity_->processors[slot].bypass=value;
  return true;
}
bool PluginChain::enqueueParameters(std::span<const ParameterChange> changes) noexcept {
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(changes.size()>queue_.size() || changes.size()>queue_.size()-(w-r) || failed_.load(std::memory_order_relaxed))return false;
  for(const auto &change:changes)if(change.slot>=plugins_.size() || !std::isfinite(change.value) || change.frame)return false;
  for(size_t i=0;i<changes.size();++i)queue_[(w+uint32_t(i))%queue_.size()]={changes[i],i+1==changes.size()};
  for(const auto &change:changes)if(change.slot<musicalCatalog_.size())
    for(auto &p:musicalCatalog_[change.slot])if(p.id==change.id)p.value=change.value;
  for(const auto &change:changes)for(auto &p:activity_->processors[change.slot].parameters)if(p.id==change.id)p.value=change.value;
  write_.store(w+uint32_t(changes.size()),std::memory_order_release);
  return true;
}
void PluginChain::beginRenderBlock() noexcept {observation_->listen.begin(position_);activity_->begin(position_);consumeMusicalPlan();applyPending();parameterBlockOpen_=true;}
bool PluginChain::latencyChangePending() const noexcept {
  for (const auto &p : plugins_) if (p->latencyChangePending()) return true;
  return (signalGraph_ && signalGraph_->latencyChangePending()) ||
         (sampleSignalGraph_ && sampleSignalGraph_->latencyChangePending());
}
void PluginChain::refreshLatencies() {
  try {
    if(mixerTransition_) {
      if(!mixerTransition_->commitStopped())throw std::runtime_error("Cannot settle pending routing before refreshing latency");
      mixer_=&mixerTransition_->renderRuntime();
    }
    for (auto &p : plugins_) p->refreshLatency();
    if (signalGraph_) signalGraph_->refreshLatencies(mixerProcessors_);
    if (sampleSignalGraph_) sampleSignalGraph_->refreshLatencies(mixerProcessors_);
    if (bypassStorageBytes() + (signalGraph_ ? signalGraph_->storageBytes() : 0) +
        (sampleSignalGraph_ ? sampleSignalGraph_->storageBytes() : 0) > 256 * 1024 * 1024)
      throw std::invalid_argument("Song graph audio storage exceeds 256 MB");
    if (mixer_) {
      for (size_t i = 0; i < plugins_.size(); ++i) {
        mixerProcessors_[i].latency = uint32_t(std::llround(plugins_[i]->latency() * sampleRate_));
        mixerProcessors_[i].tail = plugins_[i]->isInstrument() ? std::max(2., plugins_[i]->tail()) : plugins_[i]->tail();
      }
      auto plan = compileMixer(mixer_->graph(), mixerTracks_, mixerProcessors_, uint32_t(sampleRate_));
      mixer_->updateLatencyPlan(std::move(plan));
      if(!mixerTransition_->withinBudget())throw std::invalid_argument("Updated mixer latency exceeds the combined audio storage budget");
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
  if(!sameSignalControlLayout(preparedSignal_,next.signal))return nullptr;
  if(!graphControlPlans_.available())throw std::runtime_error("Graph parameter updates are pending; retry the edit");
  auto plan=std::make_unique<GraphControlPlan>();
  plan->signal=next.signal;
  if(signalGraph_)signalGraph_->prepareParameters(next.signal,*plan,lastGraphControls_);
  if(sampleSignalGraph_)sampleSignalGraph_->prepareParameters(next.signal,*plan,lastGraphControls_);
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
  return plan;
}
void PluginChain::applyPending() noexcept {
  if(parameterBlockOpen_)return;
  if(const auto *plan=graphControlPlans_.consume()){
    for(const auto &[runtime,controls]:plan->runtimes)runtime->controls(*controls);
    for(const auto &change:plan->updates){
      if(change.runtime)change.runtime->parameterBase(change.node,change.parameter,change.value);
      else if(!change.appliedBaseline || *change.appliedBaseline!=change.value){
        change.plugin->cancelScheduledParameter(change.parameter);
        if(!change.plugin->appliedParameter(change.parameter,change.value,position_,{}))failed_=true;
        else if(change.appliedBaseline)*change.appliedBaseline=change.value;
      }
    }
  }
  auto r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
  // Bound ordinary UI traffic, but never split a published transaction. The
  // maximum complete batch is the preallocated queue capacity (4096 changes).
  for (unsigned n = 0; r != w;) {
    const auto entry=queue_[r % queue_.size()];const auto &change=entry.change;
    if(entry.observed)activity_->value(change.slot+1,change.id,change.value,position_,{ParameterOrigin::PluginEditor},!plugins_[change.slot]->bypassed());
    else if (!plugins_[change.slot]->appliedParameter(change.id,change.value,position_,{}))
      failed_ = true;
    ++r;++n;if(n>=128 && entry.last)break;
  }
  read_.store(r, std::memory_order_release);
}
std::vector<PluginState> PluginChain::states() {
  if(parameterBlockOpen_)throw std::logic_error("Finish the audio block before capturing plugin state");
  while (read_.load() != write_.load())
    applyPending();
  std::vector<PluginState> out;
  for (size_t i = 0; i < plugins_.size(); ++i) {
    auto state = plugins_[i]->state();
    state.bypass = bypass_[i];
    state.instrument = instruments_[i];
    state.auxiliaryInputs = explicitPorts_[i].first;
    state.auxiliaryOutputs = explicitPorts_[i].second;
    out.push_back(std::move(state));
  }
  return out;
}
std::vector<PluginFailureEntry> PluginChain::failureDiagnostics() const {
  std::vector<PluginFailureEntry> result;
  for(size_t i=0;i<plugins_.size();++i) {
    const auto failure=plugins_[i]->failure();
    if(failure.reason)result.push_back({i,instances_[i],failure});
  }
  return result;
}
std::vector<PluginParameter> PluginChain::parameters(size_t slot) const {
  return slot < plugins_.size() ? plugins_[slot]->parameters() : std::vector<PluginParameter>{};
}

void PluginChain::captureTails() {
  compiledTails_.resize(plugins_.size());
  for (size_t i = 0; i < plugins_.size(); ++i) compiledTails_[i] = plugins_[i]->tail();
}
uint64_t PluginChain::tailRevision() const noexcept {
  uint64_t revision = 0;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!plugins_[i]->isInstrument() || instruments_[i]) revision += plugins_[i]->tailRevision();
  return revision;
}
double PluginChain::tail() const {
  double result = tail_;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!plugins_[i]->isInstrument() || instruments_[i])
      result += std::max(0., plugins_[i]->tail() - compiledTails_[i]);
  return result;
}
std::vector<size_t> PluginChain::openEditors() const {
  std::vector<size_t> slots;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (plugins_[i]->editorOpen())
      slots.push_back(i);
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
  for (auto &plugin : plugins_)
    if (plugin->isInstrument())
      for (uint8_t ch = 0; ch < 16; ++ch)
        plugin->midi(0xb0 | ch, 123, 0);
}
void PluginChain::showEditor(size_t slot) {
  if (slot >= plugins_.size())
    throw std::runtime_error("Select a plugin");
  plugins_[slot]->showEditor();
}
bool PluginChain::popEdit(size_t slot, uint32_t &id, float &value) noexcept {
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(w-r==queue_.size()||slot >= plugins_.size() || !plugins_[slot]->popEdit(id, value))return false;
  queue_[w%queue_.size()]={{uint32_t(slot),id,value,0},true,true};write_.store(w+1,std::memory_order_release);
  if(slot < musicalCatalog_.size())for(auto &p:musicalCatalog_[slot])if(p.id==id)p.value=value;
  for(auto &p:activity_->processors[slot].parameters)if(p.id==id)p.value=value;
  return true;
}

bool PluginChain::graphController(uint8_t cc,uint8_t value) noexcept {if(signalGraph_)signalGraph_->controller(cc,value);if(sampleSignalGraph_)sampleSignalGraph_->controller(cc,value);return bool(signalGraph_)||bool(sampleSignalGraph_);}
std::vector<SignalActivity> PluginChain::graphActivity() const {
  auto result=signalGraph_?signalGraph_->activity():std::vector<SignalActivity>{};
  if(sampleSignalGraph_)for(auto value:sampleSignalGraph_->activity()){const auto index=value.target-NativeSong::maximumID-1;if(index<sampleRoutes_.size()&&sampleRoutes_[index].target){value.target=sampleRoutes_[index].target;value.instrument=sampleRoutes_[index].instrumentID;value.role=3;result.push_back(value);}}
  return result;
}
std::unique_ptr<MixerTransition::Plan> PluginChain::prepareMixerRouting(const NativeSong &native) {
  // Reuse existing ordinary/row/persistent and sample-instrument graph copies
  // only when their processing, audio dependencies and note membership remain
  // unchanged. Physical bus activation still cannot mutate a live vendor.
  if(!mixerTransition_ || latencyChangePending())return {};
  const auto &signal=lastGraphControls_?lastGraphControls_->signal:preparedSignal_;
  if(!sameSignalProcessing(signal,native.signal) ||
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
    const auto found=std::find_if(mixerProcessors_.begin(),mixerProcessors_.end(),[&](const auto &p){return p.instance==route.plugin;});
    if(found==mixerProcessors_.end() || route.input>=64 || !(found->activeInputs&(uint64_t(1)<<route.input)))return {};
  }
  for(const auto &route:graph.instruments)if(route.target && route.output) {
    const auto found=std::find_if(mixerProcessors_.begin(),mixerProcessors_.end(),[&](const auto &p){return p.instance==route.plugin;});
    if(found==mixerProcessors_.end() || route.output>=64 || !(found->activeOutputs&(uint64_t(1)<<route.output)))return {};
  }
  const auto compiled=compileMixer(graph,tracks,mixerProcessors_,uint32_t(sampleRate_));
  if(compiled.latency!=previous.runtime->plan().latency)return {};
  const auto reuse=reusableMixerProcessors(previous.runtime->graph(),previous.runtime->plan(),mixerProcessors_,
    graph,compiled,mixerProcessors_);
  for(size_t i=0;i<reuse.size();++i)if(reuse[i]!=i)return {};
  auto prepared=mixerTransition_->prepare(std::move(graph),mixerProcessors_);
  auto hosted=std::make_shared<HostedMixerPlan>(*static_cast<HostedMixerPlan *>(previous.processors.get()));
  hosted->busObservations.clear();
  for(size_t i=0;i<prepared->runtime->graph().buses.size();++i) {
    const auto &bus=prepared->runtime->graph().buses[i];const auto node="n"+std::to_string(bus.id);
    const auto &plan=prepared->runtime->plan().nodes[i];
    auto port=[&](bool output)->uint32_t {
      const auto key=node+(output?"/out/0":"/in/0");
      const auto found=std::find_if(observation_->ports.begin(),observation_->ports.end(),[&](const auto &p){return p.key==key;});
      if(found!=observation_->ports.end())return uint32_t(found-observation_->ports.begin()+1);
      return observation_->add({key,node,bus.name+(output?" output":" input"),output,0,2,
        output?int64_t(plan.outputLatency)-plan.inputLatency:0,output?0:plan.directDelay});
    };
    hosted->busObservations.push_back({port(false),port(true)});
  }
  prepared->processors=std::move(hosted);prepared->process=previous.process;
  prepared->processorStorage=previous.processorStorage;
  prepared->runtime->observer(HostedMixerPlan::observe,prepared.get());
  if(!mixerTransition_->accepts(*prepared))throw std::invalid_argument("Prepared routing exceeds the combined audio storage budget");
  return prepared;
}
bool PluginChain::publishMixerRouting(std::unique_ptr<MixerTransition::Plan> &plan) noexcept {
  return mixerTransition_ && mixerTransition_->publish(plan);
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
  if(plugin)return plugin->process(buffer,frames,position,inputs);
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
    const auto &observed=chain.processorObservations_[processor];
    const bool observe=&mixer==&chain.mixerTransition_->renderRuntime();
    if(observe)chain.observation_->observe(observed.input[0],buffer,frames,position);
    for(const auto &port:device.plugin->buses())if(port.input&&port.index&&port.index<64&&port.active) {
      const float *samples=nullptr;for(const auto &input:mixer.inputs(processor))if(input.bus==port.index){samples=input.samples;break;}
      if(observe)chain.observation_->observe(observed.input[port.index],samples,frames,position);
    }
    const bool okay=wrapper.process(buffer, frames, position, mixer.inputs(processor));
    if(observe)chain.observation_->observe(observed.output[0],buffer,frames,position);
    for(auto port:device.outputs) {
      if(observe && port<64)chain.observation_->observe(observed.output[port],device.output(port),frames,position);
      if(okay)mixer.instrument(processor,port,device.output(port));
    }
    return okay;
}
void PluginChain::HostedMixerPlan::observe(void *context,size_t bus,bool output,const float *samples,uint32_t frames,uint64_t position) noexcept {
  auto &plan=*static_cast<MixerTransition::Plan *>(context);
  auto &hosted=*static_cast<HostedMixerPlan *>(plan.processors.get());auto &chain=*hosted.owner;
  if(plan.runtime.get()==&chain.mixerTransition_->renderRuntime() && bus<hosted.busObservations.size())
    chain.observation_->observe(hosted.busObservations[bus][output?1:0],samples,frames,position);
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
