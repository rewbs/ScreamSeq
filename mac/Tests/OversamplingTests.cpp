#include "editor/Oversampler.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <vector>
using namespace Tracker;
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
#include "OversamplingReference.hpp"
using namespace OversamplingReference;
int main() { try {
  double error = 0, ripple = 0, rejection = 0;
  for (int levels = 1; levels <= 4; ++levels) {
    for (bool nonlinear : {false, true}) {
      std::vector<double> input(1024); uint32_t seed = 1729;
      for (size_t i = 0; i < 600; ++i) { seed = seed * 1664525 + 1013904223; input[i] = double(int32_t(seed)) / 2147483648.; }
      const auto expected = reference(input, levels, nonlinear); Oversampler actual(1u << levels);
      bool silentRight = true; uint64_t a, f, l; tracker_audit_begin();
      for (size_t i = 0; i < input.size(); ++i) {
        const auto result = actual.process({input[i], 0}, [nonlinear](auto &frame) noexcept {
          if (nonlinear) for (auto &x : frame) x = std::tanh(x * 4);
        });
        error = std::max(error, std::abs(result[0] - expected[i])); silentRight &= result[1] == 0;
      }
      tracker_audit_end(&a, &f, &l); check(!(a + f + l), "Oversampling callbacks allocate/free/lock nothing");
      check(silentRight, "Oversampling never leaks between channels");
      actual.reset(); const auto zero = actual.process({0, 0}, [](auto &) noexcept {});
      check(zero[0] == 0 && zero[1] == 0, "Reset clears all oversampling history");
    }
    Oversampler filter(1u << levels); std::vector<double> impulse(512);
    for (size_t i = 0; i < impulse.size(); ++i) impulse[i] = filter.process({i == 0 ? 1. : 0., 0}, [](auto &) noexcept {})[0];
    const auto delay = filter.latencyFrames();
    check(size_t(std::max_element(impulse.begin(), impulse.end()) - impulse.begin()) == delay, "Measured impulse peak matches reported latency");
    for (uint32_t i = 0; i <= delay * 2; ++i) check(std::abs(impulse[i] - impulse[delay * 2 - i]) < 1e-15, "Full path has symmetric linear-phase impulse");
    for (size_t i = delay * 2 + 1; i < impulse.size(); ++i) check(impulse[i] == 0, "FIR stops exactly at its finite support");
    for (int k = 0; k <= 900; ++k) {
      const double w = std::numbers::pi * k / 1000;
      std::complex<double> response{};
      for (size_t i = 0; i < impulse.size(); ++i) response += impulse[i] * std::polar(1., -w * (double(i) - delay));
      ripple = std::max(ripple, std::abs(response.real() - 1));
      check(std::abs(response.imag()) < 1e-13, "Passband phase agrees with reported delay");
    }
  }
  for (int level = 0; level < 4; ++level) {
    const auto h = coefficients(level == 0 ? 145 : level == 1 ? 49 : 33, level == 0 ? 11 : 13);
    const double edge = .45 / (1u << level);
    for (int k = 0; k <= 8192; ++k) {
      const double w = std::numbers::pi * (1 - edge + edge * k / 8192);
      std::complex<double> response{};
      for (size_t i = 0; i < h.size(); ++i) response += h[i] * std::polar(1., -w * double(i));
      rejection = std::max(rejection, std::abs(response));
    }
  }
  check(error < 2e-13, "Polyphase outputs agree with independent direct convolution through nonlinear processing");
  check(ripple < 2e-5, "Roundtrip response stays within 0.00018 dB through 90% of native Nyquist");
  check(rejection < 4.4e-6, "Each stage rejects images that can fall into the passband by more than 107 dB");
  // At bin 191/1024 the third harmonic is already above native Nyquist. A
  // high-rate quadrature of the transfer curve therefore supplies an independent
  // ideal band-limited reference, without using our interpolation coefficients.
  for (bool hard : {false, true}) {
    auto shape = [hard](double x) { return hard ? std::clamp(x, -1., 1.) : std::tanh(x); };
    double fundamental = 0;
    for (int i = 0; i < 262144; ++i) {
      const double s = std::sin(2 * std::numbers::pi * (i + .5) / 262144);
      fundamental += shape(4 * s) * s * 2 / 262144;
    }
    double nativeError = 0, oversampledError = 0; Oversampler filter(16);
    for (int i = 0; i < 4096; ++i) {
      const double w = 2 * std::numbers::pi * 191 / 1024;
      const auto result = filter.process({std::sin(w * i), 0}, [&](auto &frame) noexcept { for (auto &x : frame) x = shape(x * 4); });
      if (i >= 2048) {
        const double sine = std::sin(w * (i - int(filter.latencyFrames()))), expected = fundamental * sine;
        oversampledError += std::pow(result[0] - expected, 2); nativeError += std::pow(shape(4 * sine) - expected, 2);
      }
    }
    const double rms = std::sqrt(oversampledError / 2048), improvement = 10 * std::log10(nativeError / oversampledError);
    std::cout << (hard ? "Hard" : "Soft") << " clipping reference RMS " << rms << ", improvement " << improvement << " dB\n";
    check(improvement > (hard ? 35 : 75), "16x processing materially reduces folded harmonic error against the ideal reference");
  }
  bool rejected = false; try { Oversampler bad(3); } catch (const std::invalid_argument &) { rejected = true; }
  check(rejected, "Unsupported factor rejects");
  std::cout << "PASS 2x/4x/8x/16x stereo oversampling: independent convolution error " << error << ", passband error " << ripple
    << ", worst image rejection " << 20 * std::log10(rejection) << " dB; exact latency/support, reset, isolation and RT audit\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
