#pragma once
#include <algorithm>
#include <cstdint>

namespace ScreamSeq {
// WASAPI periods are integral multiples of the fundamental within [min,max].
// Prefer the first legal period at least as large as requested; clamp at max.
// Zero requests the lowest legal period. Widen before rounding near UINT32_MAX.
inline std::uint32_t sharedPeriod(std::uint32_t requested,std::uint32_t fundamental,
                                  std::uint32_t minimum,std::uint32_t maximum) noexcept {
  if(!fundamental||!minimum||minimum>maximum)return 0;
  const auto low=(std::uint64_t(minimum)+fundamental-1)/fundamental*fundamental;
  const auto high=std::uint64_t(maximum)/fundamental*fundamental;
  if(low>high)return 0;
  const auto desired=(std::uint64_t(requested)+fundamental-1)/fundamental*fundamental;
  return std::uint32_t(std::clamp(desired,low,high));
}
}
