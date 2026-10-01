// Extracted from mac/Audio/AudioUnitHost.mm; shared by all platform hosts.
#include "HostedAudio.hpp"
#include "PluginAudioLayout.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Tracker {
std::vector<PluginDescriptor> NativePlugin::builtins() {
  std::vector<PluginDescriptor> result;
  for (const auto &effect : nativeEffects()) {
    PluginDescriptor descriptor;
    descriptor.format = "Built-in"; descriptor.name = effect.name; descriptor.classID = effect.identifier;
    result.push_back(std::move(descriptor));
  }
  return result;
}
NativePlugin::NativePlugin(const PluginState &state, double rate, bool offline)
    : descriptor_(state.descriptor), instanceID_(state.instanceID), assignedInstrument_(state.instrument),
      midiChannel_(state.midiChannel), aliases_(state.aliases), rate_(rate) {
  validatePluginAssignments({&state, 1});
  auto validateBuses = [](const std::vector<uint32_t> &values) {
    std::array<bool, 64> seen{};
    for (auto bus : values) {
      if (!bus || bus >= seen.size() || seen[bus]) throw std::invalid_argument("Invalid or duplicate auxiliary bus index");
      seen[bus] = true;
    }
  };
  validateBuses(state.auxiliaryInputs); validateBuses(state.auxiliaryOutputs);
  auxiliaryInputs_ = state.auxiliaryInputs; auxiliaryOutputs_ = state.auxiliaryOutputs;
  if (descriptor_.format == "Built-in") {
    if (descriptor_.type || descriptor_.subtype || descriptor_.manufacturer || descriptor_.instrument ||
        state.instrument || !descriptor_.path.empty() || !auxiliaryOutputs_.empty())
      throw std::invalid_argument("Invalid built-in effect configuration");
    builtin_ = std::make_unique<NativeEffect>(descriptor_.classID, rate, state.state);
    const bool sidechain = builtin_->definition().sidechain;
    if(sidechain){preparedInputs_=mainInputFallback_=uint64_t(1)<<1;autoDetectorBuffer_=std::make_unique<std::array<float,maximumFrames*2>>();}
    if (!auxiliaryInputs_.empty() && (!sidechain || auxiliaryInputs_ != std::vector<uint32_t>{1}))
      throw std::invalid_argument("Built-in auxiliary input is unavailable");
    descriptor_.name = builtin_->definition().name;
    latency_ = builtin_->latency();
    tail_ = builtin_->tail();
    buses_ = {{0, 2, "Stereo input", true, true, true,0,0,2}, {0, 2, "Stereo output", false, true, true,0,0,2}};
    if (sidechain) buses_.push_back({1,2,"Detector sidechain",true,!auxiliaryInputs_.empty(),true,1,0,2});
    validatePluginAudioLayout(state.audioLayout,buses_);audioLayout_=pluginAudioLayoutSignature(buses_);
    bypassControl_.prepare(rate,uint32_t(std::llround(latency_*rate)),false,state.bypass);
    prepareBaselines();
    return;
  }
  backend_ = platformPluginBackendFactory().create(state, rate, offline);
  if (!backend_) throw std::runtime_error("Platform plugin factory returned no processor");
  latency_ = backend_->latency(); tail_ = backend_->tail(); buses_ = backend_->buses();preparedInputs_=backend_->preparedAuxiliaryInputs();preparedOutputs_=backend_->preparedAuxiliaryOutputs();
  validatePluginAudioLayout(state.audioLayout,buses_);audioLayout_=pluginAudioLayoutSignature(buses_);
  for (uint32_t bus=1;bus<64;++bus)if(preparedOutputs_&(uint64_t(1)<<bus))auxiliaryOutputBuffers_[bus] = std::make_unique<PluginAudioStorage>();
  const auto initialLatency=latency_.load(std::memory_order_relaxed);
  if(!std::isfinite(initialLatency)||initialLatency<0||initialLatency>10)throw std::invalid_argument("Plugin latency exceeds 10 seconds");
  bypassControl_.prepare(rate,uint32_t(std::llround(latency_*rate)),isInstrument(),state.bypass);
  prepareBaselines();
}
NativePlugin::~NativePlugin() = default;
bool NativePlugin::latencyChangePending() const noexcept { return backend_ && backend_->latencyChangePending(); }
std::shared_ptr<NativePlugin::LatencyUpdate> NativePlugin::prepareLatency() {
  if(!backend_)return {};
  const auto pending=backend_->pendingLatency();if(!pending)return {};
  if(!pending->serial||pending->samples>rate_*10||!std::isfinite(pending->tail)||pending->tail<0)
    throw std::invalid_argument("Plugin announced an invalid latency or tail");
  auto result=std::make_shared<LatencyUpdate>();result->snapshot=*pending;
  result->bypass=bypassControl_.prepareLatency(pending->samples);return result;
}
void NativePlugin::adoptLatency(LatencyUpdate &next) noexcept {
  bypassControl_.adoptLatency(*next.bypass);latency_.store(next.snapshot.samples/rate_,std::memory_order_release);
  tail_.store(next.snapshot.tail,std::memory_order_release);
  backend_->acknowledgeLatency(next.snapshot.serial);
}
void NativePlugin::refreshLatency() {
  if (backend_ && backend_->latencyChangePending()) {
    backend_->refreshLatency(); latency_ = backend_->latency(); tail_ = backend_->tail();
    const auto initialLatency=latency_.load(std::memory_order_relaxed);
    if(!std::isfinite(initialLatency)||initialLatency<0||initialLatency>10)throw std::invalid_argument("Plugin latency exceeds 10 seconds");
    bypassControl_.latency(uint32_t(std::llround(latency_*rate_)));
  }
}
bool NativePlugin::processBlock(float *buffer, uint32_t frames, uint64_t position, uint32_t offset) noexcept {
  if (builtin_) {
    const float *detector=inputSources_[1]?inputSources_[1]+offset*2:nullptr;
    if(autoDetectorSource_&&builtin_->value(9)==2){for(uint32_t i=0;i<frames*2;++i)(*autoDetectorBuffer_)[i]=(detector?detector[i]:0)+autoDetectorSource_[offset*2+i];detector=autoDetectorBuffer_->data();}
    return builtin_->process(buffer,frames,detector);
  }
  if (!backend_->process(buffer, frames, position, inputSources_.data(), offset, transport_)) return false;
  for (uint32_t bus=1;bus<64;++bus)if(auxiliaryOutputBuffers_[bus]) {
    const auto *source = backend_->auxiliaryOutput(bus);
    if (!source) return false;
    std::copy_n(source, frames * 2, auxiliaryOutputBuffers_[bus]->interleaved.data() + offset * 2);
  }
  return true;
}
bool NativePlugin::parameter(uint32_t id, float value, uint32_t offset) noexcept {
  return appliedParameter(id,value,renderedThrough_+offset,{},offset);
}
bool NativePlugin::appliedParameter(uint32_t id,double value,uint64_t frame,ParameterSource source,uint32_t offset) noexcept {
  bool overlaid=false;for(auto &p:baselines_)if(p.id==id){p.value=value;p.source=source;overlaid=p.overlaid;break;}
  if(overlaid || modulationParameter(id))return std::isfinite(value);
  return effectiveParameter(id,value,frame,source,offset);
}
bool NativePlugin::effectiveParameter(uint32_t id,double value,uint64_t frame,ParameterSource source,uint32_t offset) noexcept {
  const bool accepted=builtin_ ? !offset && builtin_->parameter(id,float(value)) : backend_->parameter(id,value,offset);
  if(accepted&&activity_)activity_->value(activityProcessor_,id,value,frame,source,activityAudible_&&!bypassed());
  return accepted;
}
void NativePlugin::prepareBaselines(){
  for(const auto &p:parameters())baselines_.push_back({p.id,p.value,{ParameterOrigin::Baseline}});
  std::sort(baselines_.begin(),baselines_.end(),[](const auto &a,const auto &b){return a.id<b.id;});
}
bool NativePlugin::modulationParameter(uint32_t id) const noexcept {
  if(processingModulation_)for(const auto &p:processingModulation_->targets)if(p.parameter==id)return true;
  return false;
}
double NativePlugin::baselineAt(uint32_t id,uint64_t frame) const noexcept {
  for(const auto &r:parameterRamps_)if(r.active && r.id==id)return r.ramp.value(frame);
  const auto p=std::lower_bound(baselines_.begin(),baselines_.end(),id,[](const auto &p,uint32_t id){return p.id<id;});
  return p!=baselines_.end() && p->id==id?p->value:0.;
}
void NativePlugin::editorParameter(uint32_t id,double value,uint64_t frame) noexcept {
  bool overlaid=false;for(auto &p:baselines_)if(p.id==id){p.value=value;p.source={ParameterOrigin::PluginEditor};overlaid=p.overlaid;break;}
  if(activity_ && !overlaid)activity_->value(activityProcessor_,id,value,frame,{ParameterOrigin::PluginEditor},activityAudible_&&!bypassed());
}
std::vector<PluginProgram> NativePlugin::programs() const { return builtin_ ? std::vector<PluginProgram>{} : backend_->programs(); }
void NativePlugin::loadProgram(const std::string &id) {
  if (builtin_) throw std::invalid_argument("Factory preset no longer exists or cannot be loaded");
  backend_->loadProgram(id);
  const auto catalog=parameters();for(auto &p:baselines_)for(const auto &next:catalog)if(next.id==p.id)p.value=next.value;
}
std::vector<PluginParameter> NativePlugin::parameters() const {
  if (builtin_) {
    std::vector<PluginParameter> result;
    for (const auto &p : builtin_->definition().parameters) {
      PluginParameter value{p.id, std::string(p.name), p.minimum, p.maximum, builtin_->value(p.id), 0u};
      value.step = p.step;
      value.continuous = p.step == 0 && p.choices.empty();
      if (p.unit == EffectUnit::Bits) value.unitLabel = "bits";
      else if (p.unit == EffectUnit::Decibels) { value.unit = 13u; value.unitLabel = "dB"; }
      else if (p.unit == EffectUnit::Percent) { value.unit = 3u; value.unitLabel = "%"; }
      else if (p.unit == EffectUnit::Hertz) { value.unit = 8u; value.unitLabel = "Hz"; value.logarithmic = true; }
      else if (p.unit == EffectUnit::Q) { value.unitLabel = "Q"; value.logarithmic = true; }
      else if (p.unit == EffectUnit::MidiNote) { value.unit = 11u; value.unitLabel = "MIDI"; value.step = 1; }
      else if (p.unit == EffectUnit::Semitones) value.unitLabel = "st";
      else if (p.unit == EffectUnit::Milliseconds) { value.unit = 24u; value.unitLabel = "ms"; value.logarithmic = p.minimum > 0; }
      else if (p.unit == EffectUnit::Boolean) {value.unit = 2u;value.step=1;}
      else if (p.unit == EffectUnit::Choice) {value.unit = 1u;value.step=1;}
      for (auto choice : p.choices) value.choices.emplace_back(choice);
      value.continuous = value.step == 0 && value.choices.empty() && value.unit != 2u && value.unit != 1u;
      result.push_back(std::move(value));
    }
    return result;
  }
  return backend_->parameters();
}
void NativePlugin::observedBaseline(uint32_t id,double value) noexcept {
  if(!activity_ || !activityProcessor_ || activityProcessor_>activity_->processors.size())return;
  for(auto &p:activity_->processors[activityProcessor_-1].parameters)if(p.id==id){p.value=float(value);return;}
}
PluginState NativePlugin::state() const {
  auto state = backend_ ? backend_->state() : PluginState{descriptor_};
  state.instanceID = instanceID_;
  state.bypass=bypassed();
  state.instrument = assignedInstrument_; state.midiChannel = midiChannel_; state.aliases = aliases_;
  state.auxiliaryInputs = auxiliaryInputs_; state.auxiliaryOutputs = auxiliaryOutputs_;state.audioLayout=audioLayout_;
  if (builtin_) state.state = builtin_->state();
  return state;
}
std::vector<PluginInstrumentAlias> NativePlugin::assignments() const {
  std::vector<PluginInstrumentAlias> result;
  if (assignedInstrument_) result.push_back({assignedInstrument_, midiChannel_});
  result.insert(result.end(), aliases_.begin(), aliases_.end());
  return result;
}
double NativePlugin::tail() const { return builtin_ ? builtin_->tail() : tail_.load(std::memory_order_acquire); }
uint64_t NativePlugin::tailRevision() const noexcept { return builtin_ ? builtin_->tailRevision() : 0; }
void NativePlugin::includeParameterRange(uint32_t id, float minimum, float maximum) noexcept {
  if (builtin_) builtin_->includeParameterRange(id, minimum, maximum);
}
std::vector<PluginDescriptor> NativePlugin::discover() { return platformPluginBackendFactory().discover(); }
std::vector<PluginDescriptor> NativePlugin::discoverVST3(const std::string &path) { return platformPluginBackendFactory().discoverVST3(path); }
bool NativePlugin::midi(uint8_t status, uint8_t a, uint8_t b) noexcept { return backend_ && backend_->midi(status, a, b); }
void NativePlugin::showEditor() {
  if (builtin_) throw std::runtime_error("This built-in effect uses the parameter controls in the Plugins panel.");
  backend_->showEditor();
}
void NativePlugin::closeEditor() { if (backend_) backend_->closeEditor(); }
bool NativePlugin::editorOpen() const { return backend_ && backend_->editorOpen(); }
bool NativePlugin::popEdit(uint32_t &id, float &value) noexcept { return backend_ && backend_->popEdit(id, value); }
} // namespace Tracker
