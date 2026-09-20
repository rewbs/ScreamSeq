#pragma once
#include "OversamplingCoefficients.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <tuple>

namespace Tracker {
// Linear-phase, stereo half-band interpolation/decimation. The first stage has
// the narrowest transition; later stages have a wider transition relative to
// their rate. All history is fixed-size. Construction and reset belong outside
// rendering; process performs no allocation, locking or filter design.
// Polyphase decomposition: https://www.dsprelated.com/freebooks/sasp/Polyphase_Decomposition.html
class Oversampler {
public:
  using Frame = std::array<double, 2>;
private:
  template<size_t N> struct History {
    std::array<Frame, N * 2> data{};
    size_t head = 0;
    void push(Frame value) noexcept {
      head = head ? head - 1 : N - 1;
      data[head] = data[head + N] = value;
    }
    const Frame &operator[](size_t ago) const noexcept { return data[head + ago]; }
  };
  template<size_t N> struct HalfBand {
    History<N> up, even, odd;
    Frame convolution(const History<N> &history, const std::array<double, N / 2> &coefficients) noexcept {
      Frame result{};
      for (size_t i = 0; i < N / 2; ++i) for (size_t c = 0; c < 2; ++c)
        result[c] += coefficients[i] * (history[i][c] + history[N - 1 - i][c]);
      return result;
    }
    std::array<Frame, 2> interpolate(Frame input, const std::array<double, N / 2> &coefficients) noexcept {
      up.push(input);
      auto filtered = convolution(up, coefficients);
      for (auto &x : filtered) x *= 2;
      return {up[N / 2], filtered};
    }
    Frame decimate(Frame a, Frame b, const std::array<double, N / 2> &coefficients) noexcept {
      even.push(a);
      auto result = convolution(odd, coefficients);
      for (size_t c = 0; c < 2; ++c) result[c] += .5 * even[N / 2][c];
      // The odd phase precedes this output by one high-rate sample.
      odd.push(b);
      return result;
    }
  };
  std::tuple<HalfBand<72>, HalfBand<24>, HalfBand<16>, HalfBand<16>> stages_;
  uint32_t levels_;
  template<size_t Stage, typename Process> Frame run(Frame input, Process &process) noexcept {
    auto &stage = std::get<Stage>(stages_);
    constexpr auto &coefficients = []() -> const auto & {
      if constexpr (Stage == 0) return OversamplingTables::first;
      else if constexpr (Stage == 1) return OversamplingTables::second;
      else return OversamplingTables::later;
    }();
    auto pair = stage.interpolate(input, coefficients);
    for (auto &frame : pair) {
      if constexpr (Stage < 3) {
        if (levels_ > Stage + 1) frame = run<Stage + 1>(frame, process);
        else process(frame);
      } else process(frame);
    }
    return stage.decimate(pair[0], pair[1], coefficients);
  }
public:
  explicit Oversampler(uint32_t factor) : levels_(factor == 2 ? 1 : factor == 4 ? 2 : factor == 8 ? 3 : factor == 16 ? 4 : 0) {
    if (!levels_) throw std::invalid_argument("Oversampling factor must be 2, 4, 8 or 16");
  }
  uint32_t factor() const noexcept { return 1u << levels_; }
  // Exact group delay in host samples, including interpolation and decimation.
  uint32_t latencyFrames() const noexcept { constexpr uint32_t frames[]{0, 72, 84, 88, 90}; return frames[levels_]; }
  void reset() noexcept { stages_ = {}; }
  template<typename Process> Frame process(Frame input, Process &&process) noexcept { return run<0>(input, process); }
};
}
