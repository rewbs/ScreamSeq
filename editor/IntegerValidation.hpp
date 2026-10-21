#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace Tracker {
// JSON numbers with an integral value are valid integers, regardless of their
// lexical representation. Preserve exact integer storage; never round through
// double before checking integer bounds. Adapters reject booleans/non-numbers.
inline std::optional<uint64_t> boundedUnsignedInteger(uint64_t value,uint64_t low,uint64_t high) {
  if(value<low||value>high)return {};
  return value;
}
inline std::optional<uint64_t> boundedUnsignedInteger(int64_t value,uint64_t low,uint64_t high) {
  if(value<0)return {};
  return boundedUnsignedInteger(uint64_t(value),low,high);
}
inline std::optional<uint64_t> boundedUnsignedInteger(double value,uint64_t low,uint64_t high) {
  // double(UINT64_MAX) rounds UP to 2^64. Exclude that boundary before casting;
  // compare exact integer bounds only after the conversion is known to be safe.
  if(!std::isfinite(value)||value<0||value>=0x1p64||std::floor(value)!=value)return {};
  return boundedUnsignedInteger(uint64_t(value),low,high);
}
inline std::optional<uint64_t> boundedUnsignedInteger(bool,uint64_t,uint64_t)=delete;
}
