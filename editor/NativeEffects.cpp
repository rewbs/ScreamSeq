#include "NativeEffects.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace Tracker {
namespace {
constexpr std::string_view onOff[]{"Off", "On"};
constexpr std::string_view polarity[]{"Normal", "Inverted"};
constexpr std::string_view mono[]{"Average L + R", "Left", "Right"};
constexpr EffectParameter gainer[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Gain", -96, 24, 0, EffectUnit::Decibels},
  {2, "Balance", -100, 100, 0, EffectUnit::Percent},
  {3, "Left polarity", 0, 1, 0, EffectUnit::Boolean, polarity},
  {4, "Right polarity", 0, 1, 0, EffectUnit::Boolean, polarity},
};
constexpr EffectParameter dc[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Offset", -100, 100, 0, EffectUnit::Percent},
  {2, "Auto DC", 0, 1, 1, EffectUnit::Boolean, onOff},
};
constexpr EffectParameter stereo[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Width", 0, 200, 100, EffectUnit::Percent},
  {2, "Phase spread", 0, 100, 0, EffectUnit::Percent},
  {3, "Mono source", 0, 2, 0, EffectUnit::Choice, mono},
};
constexpr std::string_view channels[]{"Stereo", "Left", "Right", "Mid", "Side"};
constexpr std::string_view digitalShapes[]{"Low-pass", "High-pass", "Band-pass", "Notch", "All-pass"};
constexpr FilterShape digitalTypes[]{FilterShape::LowPass, FilterShape::HighPass, FilterShape::BandPass, FilterShape::Notch, FilterShape::AllPass};
constexpr std::string_view eqShapes[]{"Bell", "Low shelf", "High shelf", "Low-pass", "High-pass", "Notch"};
constexpr FilterShape eqTypes[]{FilterShape::Bell, FilterShape::LowShelf, FilterShape::HighShelf, FilterShape::LowPass, FilterShape::HighPass, FilterShape::Notch};
constexpr EffectParameter digital[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Shape", 0, 4, 0, EffectUnit::Choice, digitalShapes},
  {2, "Frequency", 20, 20000, 1000, EffectUnit::Hertz},
  {3, "Q", .1f, 12, .70710678f, EffectUnit::Q},
  {4, "Channels", 0, 4, 0, EffectUnit::Choice, channels},
  {5, "Output gain", -24, 24, 0, EffectUnit::Decibels},
};
constexpr EffectParameter comb[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Note", 12, 127, 69, EffectUnit::MidiNote},
  {2, "Transpose", -12, 12, 0, EffectUnit::Semitones},
  {3, "Feedback", -95, 95, 50, EffectUnit::Percent},
  {4, "Dry / wet", 0, 100, 50, EffectUnit::Percent},
  {5, "Inertia", 5, 1000, 20, EffectUnit::Milliseconds},
  {6, "Channels", 0, 4, 0, EffectUnit::Choice, channels},
  {7, "Output gain", -24, 24, 0, EffectUnit::Decibels},
};
constexpr double minimumCombFrequency = 8.175798915643707; // MIDI note 0: minimum Note + Transpose.
constexpr std::string_view distortionModes[]{"Soft clip", "Hard clip", "Fold", "Wrap"};
constexpr EffectParameter distortion[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Drive", 0, 36, 6, EffectUnit::Decibels},
  {2, "Mode", 0, 3, 0, EffectUnit::Choice, distortionModes},
  {3, "Tone", -100, 100, 0, EffectUnit::Percent},
  {4, "Dry mix", 0, 100, 0, EffectUnit::Percent},
  {5, "Wet mix", 0, 100, 100, EffectUnit::Percent},
  {6, "Output gain", -24, 24, -6, EffectUnit::Decibels},
};
constexpr EffectParameter lofi[]{
  {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff},
  {1, "Bit depth", 1, 24, 16, EffectUnit::Bits},
  {2, "Rate", 20, 384000, 48000, EffectUnit::Hertz},
  {3, "Noise", 0, 100, 0, EffectUnit::Percent},
  {4, "Smooth", 0, 1, 0, EffectUnit::Boolean, onOff},
  {5, "Dry mix", 0, 100, 0, EffectUnit::Percent},
  {6, "Wet mix", 0, 100, 100, EffectUnit::Percent},
  {7, "Output gain", -24, 24, 0, EffectUnit::Decibels},
  {8, "Noise seed", 1, 16777215, 1, EffectUnit::Generic, {}, 1},
};
double combDelay(double note, double rate) noexcept {
  return rate / std::min(rate * .25, 440 * std::exp2((note - 69) / 12));
}
constexpr std::string_view frequencyNames[]{"Band 1 frequency", "Band 2 frequency", "Band 3 frequency", "Band 4 frequency", "Band 5 frequency", "Band 6 frequency", "Band 7 frequency", "Band 8 frequency", "Band 9 frequency", "Band 10 frequency"};
constexpr std::string_view gainNames[]{"Band 1 gain", "Band 2 gain", "Band 3 gain", "Band 4 gain", "Band 5 gain", "Band 6 gain", "Band 7 gain", "Band 8 gain", "Band 9 gain", "Band 10 gain"};
constexpr std::string_view qNames[]{"Band 1 Q", "Band 2 Q", "Band 3 Q", "Band 4 Q", "Band 5 Q", "Band 6 Q", "Band 7 Q", "Band 8 Q", "Band 9 Q", "Band 10 Q"};
constexpr std::string_view shapeNames[]{"Band 1 shape", "Band 2 shape", "Band 3 shape", "Band 4 shape", "Band 5 shape", "Band 6 shape", "Band 7 shape", "Band 8 shape", "Band 9 shape", "Band 10 shape"};
template<size_t N> constexpr auto eqParameters(const std::array<float, N> &frequencies, bool shelves = false) {
  std::array<EffectParameter, 3 + 4 * N> result{};
  result[0] = {0, "Enabled", 0, 1, 1, EffectUnit::Boolean, onOff};
  result[1] = {1, "Channels", 0, 4, 0, EffectUnit::Choice, channels};
  result[2] = {2, "Output gain", -24, 24, 0, EffectUnit::Decibels};
  for (size_t i = 0; i < N; ++i) {
    const auto id = uint32_t(10 + 4 * i); const auto at = 3 + 4 * i;
    result[at] = {id, frequencyNames[i], 20, 20000, frequencies[i], EffectUnit::Hertz};
    result[at + 1] = {id + 1, gainNames[i], -18, 18, 0, EffectUnit::Decibels};
    result[at + 2] = {id + 2, qNames[i], .1f, 12, .70710678f, EffectUnit::Q};
    result[at + 3] = {id + 3, shapeNames[i], 0, 5, float(shelves ? (i == 0 ? 1 : i == N - 1 ? 2 : 0) : 0), EffectUnit::Choice, eqShapes};
  }
  return result;
}
constexpr auto eq5 = eqParameters(std::array<float, 5>{80, 250, 1000, 4000, 12000});
constexpr auto eq10 = eqParameters(std::array<float, 10>{31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000});
constexpr auto mixerEQ = eqParameters(std::array<float, 3>{120, 1000, 6000}, true);
const auto cabinetNames = [] { std::array<std::string_view,18> result; size_t i=0; for (const auto &model : cabinetModels()) result[i++]=model.name; return result; }();
constexpr std::string_view cabinetRoutes[]{"Preamp → Cabinet → EQ", "Preamp → EQ → Cabinet", "Cabinet → Preamp → EQ", "Cabinet → EQ → Preamp", "EQ → Preamp → Cabinet", "EQ → Cabinet → Preamp"};
constexpr std::string_view cabinetChannels[]{"Stereo", "Mono"};
constexpr auto cabinet = [] {
  std::array<EffectParameter,30> p{};
  p[0]={0,"Enabled",0,1,1,EffectUnit::Boolean,onOff};
  p[1]={1,"Cabinet model",0,17,2,EffectUnit::Choice,cabinetNames};
  p[2]={2,"Routing",0,5,0,EffectUnit::Choice,cabinetRoutes};
  p[3]={3,"Preamp gain",0,36,6,EffectUnit::Decibels};
  p[4]={4,"Channels",0,1,0,EffectUnit::Choice,cabinetChannels};
  p[5]={5,"Dry mix",0,100,0,EffectUnit::Percent};
  p[6]={6,"Wet mix",0,100,100,EffectUnit::Percent};
  p[7]={7,"Output gain",-24,24,-6,EffectUnit::Decibels};
  p[8]={8,"Preamp enabled",0,1,1,EffectUnit::Boolean,onOff};
  p[9]={9,"Cabinet enabled",0,1,1,EffectUnit::Boolean,onOff};
  for (size_t i=0;i<20;++i) p[i+10]=eq5[i+3];
  return p;
}();
constexpr std::string_view detectorKinds[]{"Peak", "RMS"};
constexpr std::string_view detectorSources[]{"Internal", "External sidechain"};
constexpr std::string_view gateKinds[]{"Gate", "Duck"};
constexpr EffectParameter compressor[]{
  {0,"Enabled",0,1,1,EffectUnit::Boolean,onOff},
  {1,"Threshold",-96,0,-18,EffectUnit::Decibels},
  {2,"Ratio",1,40,4},
  {3,"Attack",.1f,200,10,EffectUnit::Milliseconds},
  {4,"Release",5,5000,100,EffectUnit::Milliseconds},
  {5,"Makeup",-24,24,0,EffectUnit::Decibels},
  {6,"Knee",0,24,6,EffectUnit::Decibels},
  {7,"Detector",0,1,0,EffectUnit::Choice,detectorKinds},
  {8,"Stereo link",0,100,100,EffectUnit::Percent},
  {9,"Detector source",0,1,0,EffectUnit::Choice,detectorSources},
  {10,"Detector high-pass",20,20000,20,EffectUnit::Hertz},
  {11,"Detector low-pass",20,20000,20000,EffectUnit::Hertz},
  {12,"Detector filters",0,1,0,EffectUnit::Boolean,onOff},
  {13,"Listen to detector",0,1,0,EffectUnit::Boolean,onOff},
  {14,"Dry / wet",0,100,100,EffectUnit::Percent},
  {15,"RMS window",1,100,10,EffectUnit::Milliseconds},
};
constexpr auto gate = [] {
  std::array<EffectParameter,18> result;size_t at=0;
  for(const auto &p:compressor) if(p.id!=2 && p.id!=6) result[at++]=p;
  result[1].initial=-36;result[2].initial=1;result[4].name="Output gain";
  result[at++]={16,"Hold",0,2000,50,EffectUnit::Milliseconds};
  result[at++]={17,"Floor",-96,0,-96,EffectUnit::Decibels};
  result[at++]={18,"Hysteresis",0,24,3,EffectUnit::Decibels};
  result[at++]={19,"Mode",0,1,0,EffectUnit::Choice,gateKinds};
  return result;
}();
constexpr std::string_view busResponses[]{"Adaptive", "Feedback", "Feedforward"};
constexpr auto busCompressor = [] {
  auto result = std::to_array(compressor);
  result[7] = {7,"Response",0,2,0,EffectUnit::Choice,busResponses};
  return result;
}();
constexpr EffectParameter maximizer[]{
  {0,"Enabled",0,1,1,EffectUnit::Boolean,onOff},
  {1,"Boost",0,36,0,EffectUnit::Decibels},
  {2,"Threshold",-36,0,-.3f,EffectUnit::Decibels},
  {3,"Peak release",1,200,20,EffectUnit::Milliseconds},
  {4,"Slow release",20,5000,200,EffectUnit::Milliseconds},
  {5,"Ceiling",-36,0,-.3f,EffectUnit::Decibels},
};
constexpr EffectDefinition definitions[]{
  {"resonance.gainer.v1", "Gainer", "Gain, stereo balance and independent channel polarity.", gainer, 0, EffectKind::Gainer},
  {"resonance.dc-offset.v1", "DC Offset", "5 Hz automatic DC removal followed by manual offset correction.", dc, 1, EffectKind::DCOffset},
  {"resonance.stereo-expander.v1", "Stereo Expander", "Mid/side width, selectable mono fold-down and left-channel phase spread.", stereo, .1, EffectKind::StereoExpander},
  {"resonance.digital-filter.v1", "Digital Filter", "Resonant 12 dB low/high-pass, band-pass, notch or all-pass with channel and mid/side selection.", digital, 0, EffectKind::DigitalFilter, 1},
  {"resonance.eq5.v1", "EQ5", "Five parametric bands with selectable bell, shelf, cut and notch responses.", eq5, 0, EffectKind::Equalizer, 5},
  {"resonance.eq10.v1", "EQ10", "Ten parametric bands with selectable bell, shelf, cut and notch responses.", eq10, 0, EffectKind::Equalizer, 10},
  {"resonance.mixer-eq.v1", "Mixer EQ", "Three flexible bands, initially low shelf, bell and high shelf.", mixerEQ, 0, EffectKind::Equalizer, 3},
  {"resonance.comb-filter.v1", "Comb Filter", "Musically tuned delay with signed feedback, inertia, wet/dry mix and channel selection.", comb, 0, EffectKind::Comb},
  {"resonance.distortion.v1", "Distortion", "Soft/hard clipping, folding and wrapping with tone, separate dry/wet levels and fixed 16x oversampling.", distortion, 1.02, EffectKind::Distortion},
  {"resonance.lofimat.v1", "LofiMat", "Bit and sample-rate reduction with reproducible stereo noise, smoothing and separate dry/wet levels.", lofi, 0, EffectKind::LofiMat},
  {"resonance.cabinet-simulator.v1", "Cabinet Simulator", "Eighteen original cabinet voicings, oversampled asymmetric preamp, five-band EQ and six stage orders.", cabinet, 0, EffectKind::Cabinet, 5},
  {"resonance.compressor.v1", "Compressor", "Feedforward peak/RMS compression with soft knee, stereo linking and filtered internal/external detector.", compressor, 0, EffectKind::Compressor, 0, true},
  {"resonance.gate.v1", "Gate", "Hysteretic stereo gate/ducker with hold, floor and filtered internal/external peak/RMS detector.", gate, 0, EffectKind::Gate, 0, true},
  {"resonance.maximizer.v1", "Maximizer", "Stereo-linked sample-peak limiter with fixed 5 ms lookahead, boost, output ceiling and adaptive peak/slow release.", maximizer, 0, EffectKind::Maximizer},
  {"resonance.bus-compressor.v1", "Bus Compressor", "Adaptive peak-feedback/RMS-feedforward compression with fixed 5 ms lookahead and a filtered internal/external detector.", busCompressor, 0, EffectKind::BusCompressor, 0, true},
};
static_assert(std::atomic<double>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
double flush(double value) noexcept { return std::abs(value) < 1e-30 ? 0 : value; }
float quantized(const EffectParameter &p, float value) noexcept {
  if (!p.choices.empty() || p.unit == EffectUnit::MidiNote) return std::round(value);
  if (p.step > 0) return std::clamp(p.minimum + std::round((value - p.minimum) / p.step) * p.step, p.minimum, p.maximum);
  return value;
}
void append(std::vector<std::byte> &out, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(std::byte((value >> shift) & 255));
}
uint32_t read(std::span<const std::byte> data, size_t &at) {
  if (at > data.size() || data.size() - at < 4) throw std::invalid_argument("Truncated built-in effect state");
  uint32_t value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(data[at++]) << shift;
  return value;
}
}
std::span<const EffectDefinition> nativeEffects() { return definitions; }
const EffectDefinition &nativeEffect(std::string_view identifier) {
  for (const auto &definition : definitions) if (definition.identifier == identifier) return definition;
  throw std::invalid_argument("Unknown built-in effect identifier");
}
void NativeEffect::Ramp::set(double value, uint32_t frames, bool restart) noexcept {
  if (value == target && frames && !restart) return;
  target = value; remaining = frames;
  if (!frames) { current = value; increment = 0; }
  else increment = (target - current) / frames;
}
double NativeEffect::Ramp::next() noexcept {
  if (remaining) { current += increment; if (!--remaining) current = target; }
  return current;
}
NativeEffect::NativeEffect(std::string_view identifier, double rate, std::span<const std::byte> saved)
    : definition_(nativeEffect(identifier)), kind_(definition_.kind), rate_(rate) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Unsupported built-in effect sample rate");
  if (kind_ == EffectKind::Comb) comb_ = std::make_unique<FractionalDelay>(rate / minimumCombFrequency);
  if (kind_ == EffectKind::Distortion) distortion_ = std::make_unique<Distortion>(rate);
  if (kind_ == EffectKind::LofiMat) lofi_ = std::make_unique<LofiMat>(rate);
  if (kind_ == EffectKind::Cabinet) cabinet_ = std::make_unique<CabinetSimulator>(rate);
  if (kind_ == EffectKind::Compressor || kind_ == EffectKind::Gate || kind_ == EffectKind::BusCompressor)
    dynamics_ = std::make_unique<Dynamics>(rate, kind_ == EffectKind::Gate ? DynamicsKind::Gate : kind_ == EffectKind::BusCompressor ? DynamicsKind::BusCompressor : DynamicsKind::Compressor);
  if (kind_ == EffectKind::Maximizer) maximizer_ = std::make_unique<Maximizer>(rate);
  smoothingFrames_ = uint32_t(std::ceil(rate * .005));
  dcPole_ = std::exp(-2 * std::numbers::pi * 5 / rate);
  const double tangent = std::tan(std::numbers::pi * 700 / rate);
  phaseCoefficient_ = (tangent - 1) / (tangent + 1);
  for (const auto &p : definition_.parameters) values_[p.id].store(p.initial, std::memory_order_relaxed);
  if (!saved.empty()) {
    size_t at = 0;
    if (read(saved, at) != 0x53584652 || read(saved, at) != 1) throw std::invalid_argument("Unsupported built-in effect state");
    const auto length = read(saved, at);
    if (length != identifier.size() || length > saved.size() - at) throw std::invalid_argument("Built-in effect state identifier mismatch");
    for (char c : identifier) if (saved[at++] != std::byte(c)) throw std::invalid_argument("Built-in effect state identifier mismatch");
    if (read(saved, at) != definition_.parameters.size()) throw std::invalid_argument("Incomplete built-in effect state");
    std::array<bool, 64> seen{};
    for (size_t i = 0; i < definition_.parameters.size(); ++i) {
      const auto id = read(saved, at); const float value = std::bit_cast<float>(read(saved, at));
      if (id >= seen.size() || seen[id] || !parameter(id, value)) throw std::invalid_argument("Invalid built-in effect parameter state");
      seen[id] = true;
    }
    if (at != saved.size()) throw std::invalid_argument("Trailing built-in effect state data");
  }
  for (const auto &p : definition_.parameters) minima_[p.id] = maxima_[p.id] = value(p.id);
  rangesReady_ = true;
  tail_.store(definition_.tail, std::memory_order_relaxed);
  update(); updateTail();
}
bool NativeEffect::parameter(uint32_t id, float value) noexcept {
  auto parameters = definition_.parameters;
  const auto found = std::find_if(parameters.begin(), parameters.end(), [id](const auto &p) { return p.id == id; });
  if (found == parameters.end() || !std::isfinite(value) || value < found->minimum || value > found->maximum) return false;
  // Continuous automation can cross discrete controls: quantize at the processor
  // boundary and expose the actual selected value to the UI/state reader.
  value = quantized(*found, value);
  if (this->value(id) == value) {
    // Repeated automation must not restart an in-progress inertia transition.
    // Retain the exact finite target bits (including signed zero) for state.
    values_[id].store(value, std::memory_order_relaxed);
    return true;
  }
  if ((definition_.bands || kind_ == EffectKind::Comb || distortion_ || lofi_ || dynamics_) && rendered_) tailRevision_.fetch_add(1, std::memory_order_relaxed);
  values_[id].store(value, std::memory_order_relaxed); update(id);
  if (rangesReady_) includeParameterRange(id, value, value);
  return true;
}
float NativeEffect::value(uint32_t id) const noexcept { return id < values_.size() ? values_[id].load(std::memory_order_relaxed) : 0; }
bool NativeEffect::includeParameterRange(uint32_t id, float minimum, float maximum) noexcept {
  const auto ps = definition_.parameters;
  const auto p = std::find_if(ps.begin(), ps.end(), [id](const auto &p) { return p.id == id; });
  if (p == ps.end() || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum || minimum < p->minimum || maximum > p->maximum) return false;
  minimum = quantized(*p, minimum); maximum = quantized(*p, maximum);
  if (minimum >= minima_[id] && maximum <= maxima_[id]) return true;
  minima_[id] = std::min(minima_[id], minimum); maxima_[id] = std::max(maxima_[id], maximum);
  updateTail(id); return true;
}
void NativeEffect::updateTail(uint32_t id) noexcept {
  if (dynamics_) {
    // Only detector listening can emit stored audio after program silence.
    const double total = maxima_[13] > 0 && maxima_[12] > 0 ? .005 +
      StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::HighPass),minima_[10],maxima_[10],std::sqrt(.5),std::sqrt(.5),0,0,rate_) +
      StateVariableFilter::decaySecondsRange(1u<<unsigned(FilterShape::LowPass),minima_[11],maxima_[11],std::sqrt(.5),std::sqrt(.5),0,0,rate_) : 0;
    tail_.store(std::max(tail(),std::min(60.,total)),std::memory_order_relaxed);
    return;
  }
  if (cabinet_) {
    for (uint32_t band=0;band<5;++band) {
      if (id!=UINT32_MAX && (id<10 || (id-10)/4!=band)) continue;
      const auto f=10+4*band,q=f+2,shape=f+3;
      uint32_t mask=0;
      for (int s=int(minima_[shape]);s<=int(maxima_[shape]);++s) mask|=1u<<unsigned(eqTypes[s]);
      bandTails_[band]=StateVariableFilter::decaySecondsRange(mask,minima_[f],maxima_[f],minima_[q],maxima_[q],minima_[f+1],maxima_[f+1],rate_);
    }
    double total=(maxima_[8]>0?1.02:0)+(maxima_[9]>0?cabinet_->cabinetTail():0);
    for (size_t b=0;b<5;++b) total+=bandTails_[b];
    if (maxima_[6]==0) total=0;
    if (total>0) total+=.005;
    tail_.store(std::max(tail(),std::min(60.,total)),std::memory_order_relaxed);
    return;
  }
  if (lofi_) {
    const double frequency = std::min(rate_, double(minima_[2]));
    // Allow one last held value plus the slowest possible smoother release.
    // The 200 dB margin includes float precision/headroom. Deliberately generated
    // noise continues on silence; this remains a finite export budget, not a
    // promise that a noise-generating device becomes silent.
    const double decay = maxima_[4] > 0 ? (200 + std::max(0.f, maxima_[7])) * std::numbers::ln10 / (20 * 2 * std::numbers::pi * .45 * frequency) : 0;
    const double duration = maxima_[6] > 0 ? 1 / frequency + .005 + decay : 0;
    tail_.store(std::max(tail(), duration), std::memory_order_relaxed);
    return;
  }
  if (kind_ == EffectKind::Comb) {
    if (id == 0 || id == 6) return;
    const double delay = combDelay(minima_[1] + minima_[2], rate_);
    const double feedback = std::max(std::abs(minima_[3]), std::abs(maxima_[3])) / 100.;
    // A decaying echo train, plus interpolation support and the longest glide.
    // A 20 dB margin covers the -160 dB rendering floor; output boost adds its
    // own margin. The host applies its separate total export cap of 60 seconds.
    const double repeats = feedback > 0 ? std::ceil((180 + std::max(0.f, maxima_[7])) * std::numbers::ln10 / (-20 * std::log(feedback))) + 2 : 1;
    const double duration = maxima_[4] > 0 ? ((delay + 4) * repeats / rate_ + maxima_[5] / 1000.) : 0;
    tail_.store(std::max(tail(), std::min(60., duration)), std::memory_order_relaxed);
    return;
  }
  if (!definition_.bands || (id != UINT32_MAX &&
      ((kind_ == EffectKind::DigitalFilter && (id < 1 || id > 3)) || (kind_ == EffectKind::Equalizer && id < 10)))) return;
  for (uint32_t band = 0; band < definition_.bands; ++band) {
    if (kind_ == EffectKind::Equalizer && id != UINT32_MAX && (id - 10) / 4 != band) continue;
    const bool digital = kind_ == EffectKind::DigitalFilter;
    const auto f = digital ? 2 : 10 + 4 * band, q = digital ? 3 : f + 2, shape = digital ? 1 : f + 3;
    uint32_t mask = 0;
    for (int s = int(minima_[shape]); s <= int(maxima_[shape]); ++s) mask |= 1u << unsigned(digital ? digitalTypes[s] : eqTypes[s]);
    bandTails_[band] = StateVariableFilter::decaySecondsRange(mask, minima_[f], maxima_[f], minima_[q], maxima_[q],
      digital ? 0 : minima_[f + 1], digital ? 0 : maxima_[f + 1], rate_);
  }
  double total = 0;
  for (auto tail : bandTails_) total += tail;
  // Include control smoothing if the device has a nontrivial decay. Flat EQs
  // keep zero tail, including after restore. A run's budget can only increase.
  if (total > 0) total += .005;
  tail_.store(std::max(tail(), std::min(60., total)), std::memory_order_relaxed);
}
void NativeEffect::update(uint32_t id) noexcept {
  if (maximizer_) {
    for (const auto &p : definition_.parameters) if (id == UINT32_MAX || p.id == id) maximizer_->parameter(p.id, value(p.id), rendered_);
    return;
  }
  if (dynamics_) {
    for (const auto &p : definition_.parameters) if (id == UINT32_MAX || p.id == id) dynamics_->parameter(p.id, value(p.id), rendered_);
    return;
  }
  if (cabinet_) {
    for (const auto &p : definition_.parameters) if (id == UINT32_MAX || p.id == id) cabinet_->parameter(p.id, value(p.id), rendered_);
    return;
  }
  if (lofi_) {
    for (const auto &p : definition_.parameters) if (id == UINT32_MAX || p.id == id) lofi_->parameter(p.id, value(p.id), rendered_);
    return;
  }
  if (distortion_) {
    for (const auto &p : definition_.parameters) if (id == UINT32_MAX || p.id == id) distortion_->parameter(p.id, value(p.id), rendered_);
    return;
  }
  const auto frames = rendered_ ? smoothingFrames_ : 0;
  ramps_[0].set(value(0), frames);
  if (kind_ == EffectKind::Gainer) {
    const double gain = std::pow(10, double(value(1)) / 20), balance = value(2) / 100.;
    ramps_[1].set(gain * (1 - std::max(0., balance)) * (value(3) ? -1 : 1), frames);
    ramps_[2].set(gain * (1 + std::min(0., balance)) * (value(4) ? -1 : 1), frames);
  } else if (kind_ == EffectKind::DCOffset) {
    ramps_[1].set(value(1) / 100., frames); ramps_[2].set(value(2), frames);
  } else if (kind_ == EffectKind::StereoExpander) {
    ramps_[1].set(value(1) / 100., frames); ramps_[2].set(value(2) / 100., frames);
    ramps_[3].set(value(3) == 1 ? 1 : 0, frames); ramps_[4].set(value(3) == 2 ? 1 : 0, frames);
  } else if (kind_ == EffectKind::Comb) {
    const auto inertia = rendered_ ? uint32_t(std::ceil(rate_ * value(5) / 1000.)) : 0;
    ramps_[1].set(combDelay(value(1) + value(2), rate_), inertia, id == 5);
    ramps_[2].set(value(3) / 100., frames); ramps_[3].set(value(4) / 100., frames);
    ramps_[4].set(std::pow(10., value(7) / 20.), frames);
    for (uint32_t c = 0; c < 5; ++c) ramps_[5 + c].set(uint32_t(value(6)) == c ? 1 : 0, frames);
  } else {
    const bool digital = kind_ == EffectKind::DigitalFilter;
    const auto channel = uint32_t(value(digital ? 4 : 1));
    for (uint32_t c = 0; c < 5; ++c) ramps_[c + 1].set(c == channel ? 1 : 0, frames);
    ramps_[6].set(std::pow(10., double(value(digital ? 5 : 2)) / 20), frames);
    for (uint32_t b = 0; b < definition_.bands; ++b) {
      if (!digital && id != UINT32_MAX && (id < 10 || (id - 10) / 4 != b)) continue;
      if (digital) filters_[b].configure(digitalTypes[uint32_t(value(1))], value(2), value(3), 0, rate_, frames);
      else {
        const auto f = 10 + b * 4;
        filters_[b].configure(eqTypes[uint32_t(value(f + 3))], value(f), value(f + 2), value(f + 1), rate_, frames);
      }
    }
  }
}
std::optional<EffectMeters> NativeEffect::meters() const noexcept {
  if (!dynamics_ && !maximizer_) return std::nullopt;
  const auto reduction=meterReduction_.load(std::memory_order_relaxed), detector=meterDetector_.load(std::memory_order_relaxed);
  return EffectMeters{{std::bit_cast<float>(uint32_t(reduction)),std::bit_cast<float>(uint32_t(reduction>>32))},
    {std::bit_cast<float>(uint32_t(detector)),std::bit_cast<float>(uint32_t(detector>>32))}};
}
bool NativeEffect::process(float *buffer, uint32_t frames, const float *sidechain) noexcept {
  if (frames > 4096 || (!buffer && frames)) return false;
  rendered_ = rendered_ || frames != 0;
  for (uint32_t i = 0; i < frames; ++i) {
    const double left = buffer[2 * i], right = buffer[2 * i + 1];
    if (!std::isfinite(left) || !std::isfinite(right)) return false;
    const double wet = ramps_[0].next(); double l = left, r = right;
    if (kind_ == EffectKind::Gainer) { l *= ramps_[1].next(); r *= ramps_[2].next(); }
    else if (kind_ == EffectKind::DCOffset) {
      const double offset = ramps_[1].next(), automatic = ramps_[2].next();
      const double input[]{left, right}; double *output[]{&l, &r};
      for (size_t c = 0; c < 2; ++c) {
        const double high = flush((input[c] - dcInput_[c]) * ((1 + dcPole_) * .5) + dcPole_ * dcOutput_[c]);
        dcInput_[c] = input[c]; dcOutput_[c] = high;
        *output[c] = input[c] + automatic * (high - input[c]) + offset;
      }
    } else if (kind_ == EffectKind::StereoExpander) {
      const double width = ramps_[1].next(), phase = ramps_[2].next();
      const double mid = (left + right) * .5, side = (left - right) * .5;
      const double selected = mid + ramps_[3].next() * (left - mid) + ramps_[4].next() * (right - mid);
      const double center = mid + (1 - std::min(1., width)) * (selected - mid);
      l = center + side * width; r = center - side * width;
      const double shifted = phaseCoefficient_ * l + phaseMemory_;
      phaseMemory_ = flush(l - phaseCoefficient_ * shifted);
      l += phase * (shifted - l);
    } else if (kind_ == EffectKind::Comb) {
      const auto delayed = comb_->read(ramps_[1].next());
      const auto feedback = ramps_[2].next(), mix = ramps_[3].next(), gain = ramps_[4].next();
      // Normalizing the injection keeps stationary resonance peaks near unity
      // as feedback approaches its limit, instead of adding 26 dB of gain.
      comb_->push((1 - std::abs(feedback)) * left + feedback * delayed[0],
                  (1 - std::abs(feedback)) * right + feedback * delayed[1]);
      l = left + mix * (delayed[0] - left); r = right + mix * (delayed[1] - right);
      const double mid = (left + right) * .5, side = (left - right) * .5;
      const double fm = (l + r) * .5, fs = (l - r) * .5;
      const double weights[]{ramps_[5].next(), ramps_[6].next(), ramps_[7].next(), ramps_[8].next(), ramps_[9].next()};
      const auto selectedL = weights[0] * l + weights[1] * l + weights[2] * left + weights[3] * (fm + side) + weights[4] * (mid + fs);
      const auto selectedR = weights[0] * r + weights[1] * right + weights[2] * r + weights[3] * (fm - side) + weights[4] * (mid - fs);
      l = selectedL * gain; r = selectedR * gain;
    } else if (!distortion_ && !lofi_ && !cabinet_ && !dynamics_ && !maximizer_) {
      // Identical linear banks keep both channel histories warm. Superposition
      // gives mid/side filtering without doubling the number of integrators.
      for (uint32_t b = 0; b < definition_.bands; ++b) filters_[b].process(l, r);
      const double mid = (left + right) * .5, side = (left - right) * .5;
      const double fm = (l + r) * .5, fs = (l - r) * .5;
      const double weights[]{ramps_[1].next(), ramps_[2].next(), ramps_[3].next(), ramps_[4].next(), ramps_[5].next()};
      const double selectedL = weights[0] * l + weights[1] * l + weights[2] * left + weights[3] * (fm + side) + weights[4] * (mid + fs);
      const double selectedR = weights[0] * r + weights[1] * right + weights[2] * r + weights[3] * (fm - side) + weights[4] * (mid - fs);
      const auto gain = ramps_[6].next(); l = selectedL * gain; r = selectedR * gain;
    }
    // Standalone processors own their dry path and control timing.
    const std::array<double,2> key{sidechain ? sidechain[2*i] : 0,sidechain ? sidechain[2*i+1] : 0};
    if (dynamics_ && (!std::isfinite(key[0]) || !std::isfinite(key[1]))) return false;
    const auto output = maximizer_ ? maximizer_->process({left,right}) : dynamics_ ? dynamics_->process({left,right},key) : cabinet_ ? cabinet_->process({left, right}) : distortion_ ? distortion_->process({left, right}) : lofi_ ? lofi_->process({left, right}) : std::array<double, 2>{left + wet * (l - left), right + wet * (r - right)};
    const auto outL = output[0], outR = output[1];
    if (!std::isfinite(outL) || !std::isfinite(outR) || std::abs(outL) > std::numeric_limits<float>::max() || std::abs(outR) > std::numeric_limits<float>::max()) return false;
    buffer[2 * i] = float(outL); buffer[2 * i + 1] = float(outR);
  }
  if ((dynamics_ || maximizer_) && frames) {
    const auto m=maximizer_ ? maximizer_->meters() : dynamics_->meters();
    const auto pack=[](const std::array<float,2> &values) { return uint64_t(std::bit_cast<uint32_t>(values[0])) | (uint64_t(std::bit_cast<uint32_t>(values[1]))<<32); };
    meterReduction_.store(pack(m.reductionDB),std::memory_order_relaxed);
    meterDetector_.store(pack(m.detectorDB),std::memory_order_relaxed);
  }
  return true;
}
std::vector<std::byte> NativeEffect::state() const {
  std::vector<std::byte> out; append(out, 0x53584652); append(out, 1); append(out, uint32_t(definition_.identifier.size()));
  for (char c : definition_.identifier) out.push_back(std::byte(c));
  append(out, uint32_t(definition_.parameters.size()));
  for (const auto &p : definition_.parameters) { append(out, p.id); append(out, std::bit_cast<uint32_t>(value(p.id))); }
  return out;
}
}
