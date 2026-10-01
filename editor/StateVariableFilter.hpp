#pragma once
#include <array>
#include <cstdint>

namespace Tracker {
enum class FilterShape { LowPass, HighPass, BandPass, Notch, AllPass, Bell, LowShelf, HighShelf };

// Trapezoidal state-variable topology, double precision integrator state.
// The formulation and output mixing are described by Andrew Simper:
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf (pp. 31–32).
// Interpolate conductance, damping and output mix, retaining integrator history.
class StateVariableFilter {
  struct Coefficients {
    double conductance = 1, damping = 1, direct = 1, band = 0, low = 0;
  } current_, target_, increment_;
  std::array<double, 2> first_{}, second_{};
  uint32_t remaining_ = 0;
public:
  // Caller configures off-thread or on the single render thread. No allocation.
  // Frequencies above 45% of the rate are clamped to keep the prewarp finite.
  bool configure(FilterShape, double frequency, double q, double gainDB, double rate,
                 uint32_t transitionFrames = 0) noexcept;
  // Conservative single-section decay estimate below -160 dB, allowing for
  // resonant/boost gain. Cascaded devices sum section estimates. An IIR has an
  // infinite mathematical tail; this is a rendering floor, not exact extinction.
  static double decaySeconds(FilterShape, double frequency, double q, double gainDB, double rate) noexcept;
  // Covers every shape in the bit mask, the full parameter box and coefficient
  // interpolation between its corners. Used to prepare future automation tails.
  static double decaySecondsRange(uint32_t shapes, double minFrequency, double maxFrequency,
                                  double minQ, double maxQ, double minGain, double maxGain, double rate) noexcept;
  void process(double &left, double &right) noexcept;
  void reset() noexcept { first_ = {}; second_ = {}; }
};
}
