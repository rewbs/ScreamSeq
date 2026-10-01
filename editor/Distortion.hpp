#pragma once
#include "Oversampler.hpp"
#include "StateVariableFilter.hpp"
#include <array>
#include <cstdint>

namespace Tracker {
// Four bounded transfer curves, pre-emphasis and DC removal. A fixed 16x path
// avoids audible filter/latency changes during automation. NativeEffect owns
// parameter validation, persistence and the single-render-thread contract.
class Distortion {
  struct Ramp {
    double current = 0, target = 0, increment = 0;
    uint32_t remaining = 0;
    void set(double value, uint32_t frames) noexcept;
    double next() noexcept;
  };
  template<size_t Width, size_t Length> struct Delay {
    using Value = std::array<double, Width>;
    std::array<Value, Length> values{};
    size_t cursor = 0;
    Value process(Value next) noexcept {
      const auto out = values[cursor]; values[cursor] = next;
      if (++cursor == Length) cursor = 0;
      return out;
    }
    void fill(size_t field, double value) noexcept { for (auto &frame : values) frame[field] = value; }
  };
  static constexpr uint32_t factor = 16, latency = 90;
  double rate_, dcPole_;
  Oversampler oversampler_{factor};
  StateVariableFilter tone_;
  std::array<Ramp, 5> shapeControls_; // Drive, four mode weights at the high rate.
  std::array<Ramp, 4> outputControls_; // Enabled, dry, wet, gain at the host rate.
  Delay<5, latency * factor / 2> shapeDelay_;
  Delay<4, latency> outputDelay_;
  Delay<2, latency> dryDelay_;
  std::array<double, 2> dcInput_{}, dcOutput_{};
public:
  explicit Distortion(double rate);
  // The caller supplies validated values and whether audio has started. Initial
  // controls prefill their delay lines; edits ramp for 5 ms without resetting DSP.
  void parameter(uint32_t id, double value, bool rendered) noexcept;
  Oversampler::Frame process(Oversampler::Frame input) noexcept;
  static constexpr uint32_t latencyFrames() noexcept { return latency; }
};
}
