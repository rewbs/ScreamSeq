#include "editor/StateVariableFilter.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
#include "FilterReference.hpp"
using FilterReference::Reference;
int main() { try {
  double maximum = 0, modulationPeak = 0;
  for (double rate : {8000., 44100., 48000., 96000., 192000.}) {
    for (int type = 0; type < 8; ++type) for (double frequency : {20., 1000., 20000.}) for (double q : {.1, .7071067811865476, 20.}) for (double gain : {-24., 0., 24.}) {
      StateVariableFilter filter; check(filter.configure(FilterShape(type), frequency, q, gain, rate), "Valid filter settings accepted");
      Reference reference(FilterShape(type), frequency, q, gain, rate);
      uint64_t a, f, l; tracker_audit_begin();
      for (uint32_t i = 0; i < 8192; ++i) {
        double left = i == 0 ? 1 : 0, right = 0, expected = reference.process(left);
        filter.process(left, right); maximum = std::max(maximum, std::abs(left - expected));
        if (!std::isfinite(left) || right != 0) { tracker_audit_end(&a, &f, &l); throw std::runtime_error("Filter response nonfinite or leaks channels"); }
      }
      tracker_audit_end(&a, &f, &l); check(!(a + f + l), "Filters have no realtime allocation/free/lock");
      filter.reset(); double left = 0, right = 0; filter.process(left, right); check(left == 0 && right == 0, "Reset clears resonant history");
    }
    // Fast, interrupted transitions traverse every shape, gain/Q extreme and
    // frequency extreme, including rates where the requested cutoff is clamped.
    StateVariableFilter filter; filter.configure(FilterShape::LowPass, 1000, .707, 0, rate);
    uint32_t seed = 1729; uint64_t a, f, l; tracker_audit_begin();
    for (uint32_t i = 0; i < uint32_t(rate * 2); ++i) {
      if (i % 97 == 0) {
        seed = seed * 1664525 + 1013904223;
        if (!filter.configure(FilterShape(seed % 8), seed & 256 ? 20 : 20000, seed & 512 ? .1 : 20, seed & 1024 ? -24 : 24, rate, uint32_t(std::ceil(rate * .005)))) {
          tracker_audit_end(&a, &f, &l); throw std::runtime_error("Modulation rejected valid settings");
        }
      }
      double left = .1 * std::sin(i * .07), right = -.07 * std::cos(i * .11);
      filter.process(left, right); modulationPeak = std::max({modulationPeak, std::abs(left), std::abs(right)});
      if (!std::isfinite(left) || !std::isfinite(right)) { tracker_audit_end(&a, &f, &l); throw std::runtime_error("Rapid filter modulation destabilized"); }
    }
    tracker_audit_end(&a, &f, &l); check(!(a + f + l), "Automated filter changes allocate/free/lock nothing");
  }
  check(maximum < 2e-8, "All SVF impulse responses agree with independent RBJ reference");
  check(modulationPeak < 64, "Adversarial interrupted smoothing stays bounded");
  double tailResidual = 0;
  for (int type = 0; type < 8; ++type) for (double rate : {8000., 48000.}) for (double q : {.5, 20.}) {
    StateVariableFilter filter; filter.configure(FilterShape(type), 20, q, 24, rate);
    const auto tail = StateVariableFilter::decaySeconds(FilterShape(type), 20, q, 24, rate);
    check(tail > 0 && tail <= 60, "Resonant tail estimate is positive and bounded");
    const uint32_t driven = uint32_t(rate * 2), end = driven + uint32_t(std::ceil(tail * rate));
    for (uint32_t i = 0; i < end + 1024; ++i) {
      double left = i < driven ? std::sin(i * 2 * std::numbers::pi * 20 / rate) : 0, right = i < driven ? 1 : 0;
      filter.process(left, right);
      if (i >= end) tailResidual = std::max({tailResidual, std::abs(left), std::abs(right)});
    }
  }
  check(tailResidual < 1e-8, "Estimated tails retain resonant sine/DC releases until below -160 dB");
  for (auto type : {FilterShape::Bell, FilterShape::LowShelf, FilterShape::HighShelf})
    check(StateVariableFilter::decaySeconds(type, 1000, 1, 0, 48000) == 0, "Flat EQ adds no silent tail");
  StateVariableFilter bad;
  check(!bad.configure(FilterShape::Bell, NAN, 1, 0, 48000) && !bad.configure(FilterShape::Bell, 1000, 0, 0, 48000) &&
        !bad.configure(FilterShape(99), 1000, 1, 0, 48000), "Invalid filter values reject");
  std::cout << "PASS state-variable filter prototype: 8 shapes, independent RBJ impulse comparison maximum " << maximum
    << ", 8/44.1/48/96/192 kHz, cutoff/Q/gain boundaries, silent/reset/channel isolation, no RT allocation/free/lock; rapid automation peak " << modulationPeak << "; estimated tail residual " << tailResidual << '\n';
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
