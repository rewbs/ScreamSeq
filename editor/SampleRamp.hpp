#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace Tracker {
// Absolute sample positions avoid accumulated error and make callback size irrelevant.
// The endpoint belongs to the sample at start + duration; duration zero is a step.
struct SampleRamp {
  uint64_t start = 0, duration = 0;
  double from = 0, to = 0;
  bool valid() const noexcept { return start <= UINT64_MAX-duration && std::isfinite(from) && std::isfinite(to) && std::isfinite(to-from); }
  double value(uint64_t sample) const noexcept {
    if (sample < start) return from;
    if (!duration || sample-start >= duration) return to;
    return from + (to-from)*(double(sample-start)/double(duration));
  }
  bool finished(uint64_t sample) const noexcept { return sample >= start && sample-start >= duration; }
};
}
