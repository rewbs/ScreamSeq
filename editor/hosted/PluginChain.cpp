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
  validatePluginCapacity(states);
  for (auto &state : states) {
    auto plugin = std::make_shared<NativePlugin>(state, rate, offline);
    plugin->automate(automation, plugins_.size(), rate, uint64_t(double(startFrame) * rate / 48000));
    if (!state.bypass && !plugin->isInstrument()) {
      latency_ += plugin->latency();
      tail_ += plugin->tail();
    }

    plugins_.push_back(std::move(plugin));
    instances_.push_back(state.instanceID);
    instruments_.push_back(state.instrument);
    bypass_.push_back(state.bypass);
  }
  double instrumentLatency = 0, instrumentTail = 0;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (plugins_[i]->isInstrument() && !bypass_[i] && instruments_[i]) {
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
bool PluginChain::parameter(uint32_t slot, uint32_t id, float value) noexcept {
  const ParameterChange change{slot,id,value,0};
  return enqueueParameters({&change,1});
}
bool PluginChain::enqueueParameters(std::span<const ParameterChange> changes) noexcept {
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(changes.size()>queue_.size() || changes.size()>queue_.size()-(w-r) || failed_.load(std::memory_order_relaxed))return false;
  for(const auto &change:changes)if(change.slot>=plugins_.size() || !std::isfinite(change.value) || change.frame)return false;
  for(size_t i=0;i<changes.size();++i)queue_[(w+uint32_t(i))%queue_.size()]={changes[i],i+1==changes.size()};
  write_.store(w+uint32_t(changes.size()),std::memory_order_release);
  return true;
}
void PluginChain::beginRenderBlock() noexcept {applyPending();parameterBlockOpen_=true;}
bool PluginChain::latencyChangePending() const noexcept {
  for (const auto &p : plugins_) if (p->latencyChangePending()) return true;
  return (signalGraph_ && signalGraph_->latencyChangePending()) ||
         (sampleSignalGraph_ && sampleSignalGraph_->latencyChangePending());
}
void PluginChain::refreshLatencies() {
  try {
    for (auto &p : plugins_) p->refreshLatency();
    if (signalGraph_) signalGraph_->refreshLatencies(mixerProcessors_);
    if (sampleSignalGraph_) sampleSignalGraph_->refreshLatencies(mixerProcessors_);
    if ((signalGraph_ ? signalGraph_->storageBytes() : 0) +
        (sampleSignalGraph_ ? sampleSignalGraph_->storageBytes() : 0) > 256 * 1024 * 1024)
      throw std::invalid_argument("Song graph audio storage exceeds 256 MB");
    if (mixer_) {
      for (size_t i = 0; i < plugins_.size(); ++i) {
        mixerProcessors_[i].latency = uint32_t(std::llround(plugins_[i]->latency() * sampleRate_));
        mixerProcessors_[i].tail = plugins_[i]->isInstrument() ? std::max(2., plugins_[i]->tail()) : plugins_[i]->tail();
      }
      auto plan = compileMixer(mixer_->graph(), mixerTracks_, mixerProcessors_, uint32_t(sampleRate_));
      mixer_->updateLatencyPlan(std::move(plan));
      latency_ = mixer_->plan().latency / sampleRate_; tail_ = mixer_->plan().tail;
    } else {
      latency_ = 0; tail_ = 0;
      double instrumentLatency = 0, instrumentTail = 0;
      for (size_t i = 0; i < plugins_.size(); ++i) if (!bypass_[i]) {
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
  applyPending();
  if (frames > 4096) {
    failed_ = true;
    std::fill(buffer, buffer + frames * 2, 0.f);
    return false;
  }
  if (mixer_) return finishMixer(buffer, frames);
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
        if (!bypass_[i] && instruments_[i])
          for (uint32_t n = 0; n < count * 2; ++n)
            buffer[offset * 2 + n] += tailBuffer_[n];
      }
    }
  }
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!bypass_[i] && !plugins_[i]->isInstrument() && !plugins_[i]->process(buffer, frames, position_))
      failed_ = true;
  position_ += frames;
  if (failed_) {
    std::fill(buffer, buffer + frames * 2, 0.f);
    return false;
  }
  return true;
}
void PluginChain::applyPending() noexcept {
  if(parameterBlockOpen_)return;
  auto r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
  // Bound ordinary UI traffic, but never split a published transaction. The
  // maximum complete batch is the preallocated queue capacity (4096 changes).
  for (unsigned n = 0; r != w;) {
    const auto entry=queue_[r % queue_.size()];const auto &change=entry.change;
    if (!plugins_[change.slot]->parameter(change.id, change.value))
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
    if (!bypass_[i] && (!plugins_[i]->isInstrument() || instruments_[i])) revision += plugins_[i]->tailRevision();
  return revision;
}
double PluginChain::tail() const {
  double result = tail_;
  for (size_t i = 0; i < plugins_.size(); ++i)
    if (!bypass_[i] && (!plugins_[i]->isInstrument() || instruments_[i]))
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
  return slot < plugins_.size() && plugins_[slot]->popEdit(id, value);
}

bool PluginChain::graphController(uint8_t cc,uint8_t value) noexcept {if(signalGraph_)signalGraph_->controller(cc,value);if(sampleSignalGraph_)sampleSignalGraph_->controller(cc,value);return bool(signalGraph_)||bool(sampleSignalGraph_);}
std::vector<SignalActivity> PluginChain::graphActivity() const {
  auto result=signalGraph_?signalGraph_->activity():std::vector<SignalActivity>{};
  if(sampleSignalGraph_)for(auto value:sampleSignalGraph_->activity()){const auto index=value.target-NativeSong::maximumID-1;if(index<sampleRoutes_.size()){value.target=sampleRoutes_[index].target;value.instrument=sampleRoutes_[index].instrumentID;value.role=3;result.push_back(value);}}
  return result;
}
void PluginChain::beginMixer(uint32_t frames) noexcept {
  if (!mixer_) return;
  applyPending(); mixer_->begin(frames, mixer_->through());
}
void PluginChain::routeInstrument(size_t processor, const float *buffer) noexcept {
  if (mixer_) {
    mixer_->instrument(processor, 0, buffer);
    for (const auto &bus : plugins_[processor]->buses()) if (!bus.input && bus.index && bus.active)
      mixer_->instrument(processor, bus.index, plugins_[processor]->auxiliaryOutput(bus.index));
  }
}
void PluginChain::processSampleGraph(size_t index,const float *left,const float *right,uint32_t frames) noexcept {
  if(!sampleSignalGraph_||!mixer_||index>=sampleRoutes_.size()||frames>4096){failed_=true;return;}
  for(uint32_t f=0;f<frames;++f){sampleGraphBuffer_[f*2]=left?left[f]:0;sampleGraphBuffer_[f*2+1]=right?right[f]:0;}
  if(!sampleSignalGraph_->process(index,sampleGraphBuffer_.data(),frames,mixer_->through(),{})){failed_=true;return;}
  mixer_->instrument(sampleRoutes_[index].processor,0,sampleGraphBuffer_.data());
}
const float *PluginChain::processMixerBus(size_t bus, const float *left, const float *right) noexcept {
  if (!mixer_) return nullptr;
  auto process = [](void *context, size_t processor, float *buffer, uint32_t frames, uint64_t position) noexcept {
    auto &chain = *static_cast<PluginChain *>(context);
    if(processor >= chain.plugins_.size()) {
      const auto index=processor-chain.plugins_.size();
      if(!chain.signalGraph_||!chain.signalGraph_->process(index,buffer,frames,position,chain.mixer_->inputs(processor)))return false;
      for(auto port:chain.signalGraph_->outputs(index))chain.mixer_->instrument(processor,port,chain.signalGraph_->output(index,port));
      return true;
    }
    const bool okay=chain.plugins_[processor]->process(buffer, frames, position, chain.mixer_->inputs(processor));
    if(okay)for(const auto &bus:chain.plugins_[processor]->buses())if(!bus.input&&bus.index&&bus.active)chain.mixer_->instrument(processor,bus.index,chain.plugins_[processor]->auxiliaryOutput(bus.index));
    return okay;
  };
  const auto *result = mixer_->process(bus, left, right, process, this);
  if (bus == mixer_->plan().master) mixer_->complete();
  if (mixer_->failed()) failed_ = true;
  return result;
}
bool PluginChain::finishMixer(float *buffer, uint32_t frames) noexcept {
  const uint64_t end = position_ + frames;
  if (mixer_->through() < end) {
    const uint32_t offset = uint32_t(mixer_->through() > position_ ? mixer_->through() - position_ : 0);
    const auto count = frames - offset;
    mixer_->begin(count, position_ + offset);
    if(signalGraph_)signalGraph_->tail();
    if(sampleSignalGraph_){sampleSignalGraph_->tail();for(size_t i=0;i<sampleRoutes_.size();++i)processSampleGraph(i,nullptr,nullptr,count);}
    for (size_t i = 0; i < plugins_.size(); ++i) if (plugins_[i]->isInstrument() && !bypass_[i] && instruments_[i]) {
      tailBuffer_.fill(0);
      if (!plugins_[i]->process(tailBuffer_.data(), count, position_ + offset)) failed_ = true;
      routeInstrument(i, tailBuffer_.data());
    }
    for (auto bus : mixer_->plan().order) {
      auto result = processMixerBus(bus, nullptr, nullptr);
      if (bus == mixer_->plan().master && result)
        std::copy_n(result, count * 2, buffer + offset * 2);
    }
    if (mixerRenderer_) mixerRenderer_->processNativeTail(buffer + offset * 2, count);
  }
  position_ = end;
  if (failed_) { std::fill_n(buffer, frames * 2, 0.f); return false; }
  return true;
}
} // namespace Tracker
