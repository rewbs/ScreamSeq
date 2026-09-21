#include "MixerRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
void MixerRuntime::Delay::add(const float *source, float *destination, uint32_t frames, float gain) noexcept {
  if (buffer_.empty()) {
    if (source) for (size_t i = 0; i < size_t(frames) * 2; ++i) destination[i] += source[i] * gain;
    return;
  }
  for (size_t i = 0; i < size_t(frames) * 2; ++i) {
    destination[i] += buffer_[cursor_] * gain;
    buffer_[cursor_] = source ? source[i] : 0;
    if (++cursor_ == buffer_.size()) cursor_ = 0;
  }
}
void MixerRuntime::Delay::addPlanar(const float *left, const float *right, float *destination, uint32_t frames) noexcept {
  if (buffer_.empty()) {
    for (size_t i = 0; i < frames; ++i) {
      destination[i * 2] += left ? left[i] : 0;
      destination[i * 2 + 1] += right ? right[i] : 0;
    }
    return;
  }
  for (size_t i = 0; i < frames; ++i) {
    destination[i * 2] += buffer_[cursor_]; buffer_[cursor_++] = left ? left[i] : 0;
    destination[i * 2 + 1] += buffer_[cursor_]; buffer_[cursor_++] = right ? right[i] : 0;
    if (cursor_ == buffer_.size()) cursor_ = 0;
  }
}
MixerRuntime::Values MixerRuntime::values(const MixerControls &control) noexcept {
  return {float(std::pow(10.0, control.preGainDB / 20)), float(std::pow(10.0, control.gainDB / 20)),
          float(control.pan), float(control.width), control.audible ? 1.f : 0.f, float(control.prePan)};
}
MixerRuntime::MixerRuntime(MixerGraph graph, MixerPlan plan, double rate, uint64_t start)
    : graph_(std::move(graph)), plan_(std::move(plan)), rate_(rate), position_(start), through_(start) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || plan_.nodes.empty() ||
      plan_.nodes.size() > maximumBuses || plan_.nodes.size() != graph_.buses.size() ||
      plan_.order.size() != plan_.nodes.size() || plan_.master >= plan_.nodes.size())
    throw std::invalid_argument("Invalid compiled mixer plan");
  rampFrames_ = uint32_t(std::max(1.0, std::round(rate_ * .005)));
  for (size_t i = 0; i < plan_.nodes.size(); ++i) {
    nodes_.push_back(std::make_unique<Node>(plan_.nodes[i].directDelay));
    const auto &bus = graph_.buses[i];
    nodes_.back()->current = nodes_.back()->target = values({bus.preGainDB, bus.gainDB, bus.pan, bus.width, plan_.nodes[i].audible, bus.prePan});
  }
  for (const auto &edge : plan_.connections) edges_.emplace_back(edge.delay);
  for (const auto &instrument : plan_.instruments) instruments_.emplace_back(instrument.delay);
  for (const auto &side : plan_.sidechains) {
    auto target = std::find_if(auxiliaries_.begin(), auxiliaries_.end(), [&](const auto &a) { return a.processor == side.processor; });
    if (target == auxiliaries_.end()) { auxiliaries_.push_back({side.processor}); target = auxiliaries_.end() - 1; }
    auto port = std::find_if(target->inputs.begin(), target->inputs.end(), [&](const auto &i) { return i.bus == side.input; });
    if (port == target->inputs.end()) {
      target->buffers.push_back(std::make_unique<std::array<float, maximumFrames * 2>>());
      target->inputs.push_back({side.input, target->buffers.back()->data()}); port = target->inputs.end() - 1;
    }
    sideTargets_.push_back(target->buffers[size_t(port - target->inputs.begin())]->data());
    sideDelays_.emplace_back(side.delay);
  }
  meters_ = std::make_unique<std::atomic<float>[]>(nodes_.size() * 2);
  for (size_t i = 0; i < nodes_.size() * 2; ++i) meters_[i].store(0, std::memory_order_relaxed);
}
void MixerRuntime::updateLatencyPlan(MixerPlan plan) {
  if (plan.order != plan_.order || plan.nodes.size() != nodes_.size() ||
      plan.connections.size() != edges_.size() || plan.instruments.size() != instruments_.size() ||
      plan.sidechains.size() != sideDelays_.size())
    throw std::invalid_argument("Latency update changed mixer topology");
  for (size_t i = 0; i < nodes_.size(); ++i)
    if (plan.nodes[i].directDelay != plan_.nodes[i].directDelay) nodes_[i]->direct = Delay(plan.nodes[i].directDelay);
  for (size_t i = 0; i < edges_.size(); ++i)
    if (plan.connections[i].delay != plan_.connections[i].delay) edges_[i] = Delay(plan.connections[i].delay);
  for (size_t i = 0; i < instruments_.size(); ++i)
    if (plan.instruments[i].delay != plan_.instruments[i].delay) instruments_[i] = Delay(plan.instruments[i].delay);
  for (size_t i = 0; i < sideDelays_.size(); ++i)
    if (plan.sidechains[i].delay != plan_.sidechains[i].delay) sideDelays_[i] = Delay(plan.sidechains[i].delay);
  plan_ = std::move(plan);
}
bool MixerRuntime::controls(const std::vector<MixerControls> &controls) noexcept {
  if (controls.size() != nodes_.size()) return false;
  auto valid = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
  for (const auto &c : controls)
    if (!valid(c.preGainDB, -96, 24) || !valid(c.prePan, -1, 1) || !valid(c.gainDB, -96, 24) || !valid(c.pan, -1, 1) || !valid(c.width, 0, 2)) return false;
  const auto write = write_.load(std::memory_order_relaxed);
  if (write - read_.load(std::memory_order_acquire) == controls_.size()) return false;
  std::copy(controls.begin(), controls.end(), controls_[write % controls_.size()].values.begin());
  write_.store(write + 1, std::memory_order_release);
  return true;
}
void MixerRuntime::pending() noexcept {
  const auto write = write_.load(std::memory_order_acquire);
  if (write == read_.load(std::memory_order_relaxed)) return;
  // The producer cannot recycle any consumed slot until this complete snapshot
  // has been copied, so a solo gesture is applied atomically across all buses.
  const auto &latest = controls_[(write - 1) % controls_.size()].values;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    auto &node = *nodes_[i]; const auto target = values(latest[i]);
    if (target.pre != node.target.pre || target.gain != node.target.gain || target.pan != node.target.pan ||
        target.width != node.target.width || target.audible != node.target.audible || target.prePan != node.target.prePan) {
      node.rampStart = node.current; node.target = target; node.ramp = rampFrames_;
    }
  }
  read_.store(write, std::memory_order_release);
}
void MixerRuntime::begin(uint32_t frames, uint64_t position) noexcept {
  if (!frames || frames > maximumFrames || position < through_ || position > UINT64_MAX - frames) {
    failed_ = true; frames_ = 0; return;
  }
  frames_ = frames; position_ = position; next_ = 0;
  pending();
  for (auto &node : nodes_) std::fill_n(node->input.data(), frames * 2, 0);
  for (auto &aux : auxiliaries_) for (auto &buffer : aux.buffers) std::fill_n(buffer->data(), frames * 2, 0);
}
std::span<const MixerAudioInput> MixerRuntime::inputs(size_t processor) const noexcept {
  for (const auto &aux : auxiliaries_) if (aux.processor == processor) return aux.inputs;
  return {};
}
void MixerRuntime::instrument(size_t processor, uint32_t output, const float *buffer) noexcept {
  if (!frames_) return;
  // Native sources run before graph nodes, once per core mix chunk.

  for (size_t i = 0; i < plan_.instruments.size(); ++i) {
    const auto &source = plan_.instruments[i];
    if (source.processor == processor && source.output == output) {
      if(source.owner==SIZE_MAX){if(next_){failed_=true;return;}instruments_[i].add(buffer,nodes_[source.target]->input.data(),frames_);}
      else {
        if(!next_||plan_.order[next_-1]!=source.owner){failed_=true;return;}
        const auto &owner=*nodes_[source.owner];
        for(uint32_t frame=0;frame<frames_;++frame){float audible=owner.target.audible;if(owner.ramp&&frame<owner.ramp){const float t=float(rampFrames_-owner.ramp+frame+1)/rampFrames_;audible=owner.rampStart.audible+(owner.target.audible-owner.rampStart.audible)*t;}for(int ch=0;ch<2;++ch)auxiliaryScratch_[frame*2+ch]=(buffer?buffer[frame*2+ch]:0)*audible;}
        instruments_[i].add(auxiliaryScratch_.data(),nodes_[source.target]->input.data(),frames_);
      }
    }
  }
}
const float *MixerRuntime::process(size_t bus, const float *directLeft, const float *directRight,
                                   Process callback, void *context) noexcept {
  if (!frames_ || next_ >= plan_.order.size() || plan_.order[next_] != bus) { failed_ = true; return nullptr; }
  ++next_;
  auto &node = *nodes_[bus]; const auto &plan = plan_.nodes[bus];
  node.direct.addPlanar(directLeft, directRight, node.input.data(), frames_);
  auto at = [&](uint32_t sample) noexcept {
    if (!node.ramp || sample >= node.ramp) return node.target;
    // Evaluate from a fixed start and absolute ramp offset. Repeatedly
    // interpolating from the last block's rounded value accumulates error
    // with very short callbacks and makes the gesture depend on block size.
    const float t = float(rampFrames_ - node.ramp + sample + 1) / rampFrames_;
    auto interpolate = [t](float a, float b) { return a + (b - a) * t; };
    return Values{interpolate(node.rampStart.pre, node.target.pre), interpolate(node.rampStart.gain, node.target.gain),
                  interpolate(node.rampStart.pan, node.target.pan), interpolate(node.rampStart.width, node.target.width),
                  interpolate(node.rampStart.audible, node.target.audible), interpolate(node.rampStart.prePan, node.target.prePan)};
  };
  for (uint32_t i = 0; i < frames_; ++i) {
    auto v = at(i);
    node.work[i * 2] = node.input[i * 2] * v.pre;
    node.work[i * 2 + 1] = node.input[i * 2 + 1] * v.pre;
    if (v.prePan != 0) {
      node.work[i * 2] *= 1 - std::max(0.f, v.prePan);
      node.work[i * 2 + 1] *= 1 + std::min(0.f, v.prePan);
    }
  }
  for (auto processor : plan.processors)
    if (!callback || !callback(context, processor, node.work.data(), frames_, position_)) {
      failed_ = true; std::fill_n(node.work.data(), frames_ * 2, 0); break;
    }
  float peakL = 0, peakR = 0;
  for (uint32_t i = 0; i < frames_; ++i) {
    auto v = at(i);
    float left = node.work[i * 2], right = node.work[i * 2 + 1];
    if (!std::isfinite(left) || !std::isfinite(right)) { failed_ = true; left = right = 0; }
    node.input[i * 2] = left * v.audible; node.input[i * 2 + 1] = right * v.audible;
    // Preserve the exact unity path; M/S conversion is only needed for width.
    if (v.width != 1) { const float mid = (left + right) * .5f, side = (left - right) * .5f * v.width; left = mid + side; right = mid - side; }
    left *= v.gain * v.audible * (1 - std::max(0.f, v.pan));
    right *= v.gain * v.audible * (1 + std::min(0.f, v.pan));
    if (!std::isfinite(left) || !std::isfinite(right)) { failed_ = true; left = right = 0; }
    node.work[i * 2] = left; node.work[i * 2 + 1] = right;
    peakL = std::max(peakL, std::abs(left)); peakR = std::max(peakR, std::abs(right));
  }
  node.current = at(frames_ - 1); node.ramp = frames_ >= node.ramp ? 0 : node.ramp - frames_;
  for (auto index : plan.outputs) {
    const auto &edge = plan_.connections[index];
    edges_[index].add(edge.preFader ? node.input.data() : node.work.data(), nodes_[edge.target]->input.data(), frames_, float(edge.gain));
  }
  for (auto index : plan.sidechains) {
    const auto &side = plan_.sidechains[index];
    sideDelays_[index].add(side.preFader ? node.input.data() : node.work.data(), sideTargets_[index], frames_, float(side.gain));
  }
  const float decay = float(std::exp(-frames_ / (rate_ * .2)));
  meters_[bus * 2].store(std::max(peakL, meters_[bus * 2].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
  meters_[bus * 2 + 1].store(std::max(peakR, meters_[bus * 2 + 1].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
  return node.work.data();
}
void MixerRuntime::complete() noexcept {
  if (!frames_ || next_ != nodes_.size()) failed_ = true;
  else through_ = position_ + frames_;
  frames_ = 0;
}
std::vector<MixerMeter> MixerRuntime::meters() const {
  std::vector<MixerMeter> result(nodes_.size());
  for (size_t i = 0; i < result.size(); ++i) result[i] = {meters_[i * 2].load(std::memory_order_relaxed), meters_[i * 2 + 1].load(std::memory_order_relaxed)};
  return result;
}
} // namespace Tracker
