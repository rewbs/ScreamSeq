#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Tracker {
struct EffectRamp {
  double current = 0, target = 0, increment = 0;
  uint32_t remaining = 0;
  void set(double value, uint32_t frames, bool restart = false) noexcept {
    if (frames && value == target && !restart) return;
    target = value; remaining = frames;
    if (!frames) { current = value; increment = 0; }
    else increment = (target - current) / frames;
  }
  double next() noexcept {
    if (remaining) { current += increment; if (!--remaining) current = target; }
    return current;
  }
};
template<typename T, size_t Capacity> class EffectDelay {
  std::array<T, Capacity> values_{};
  size_t length_, cursor_ = 0;
public:
  explicit EffectDelay(size_t length) : length_(length) {
    if (!length || length > Capacity) throw std::invalid_argument("Effect delay exceeds its fixed capacity");
  }
  T process(const T &input) noexcept {
    const auto output = values_[cursor_]; values_[cursor_] = input;
    if (++cursor_ == length_) cursor_ = 0;
    return output;
  }
  void fill(const T &value) noexcept { values_.fill(value); }
};
}
