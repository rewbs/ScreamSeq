#include "HostClock.hpp"
#include <windows.h>
#include <limits>
namespace ScreamSeq {
namespace {
// Eager process initialization, never a first-use static guard in a callback.
const std::uint64_t frequency = [] {LARGE_INTEGER value{};return QueryPerformanceFrequency(&value) && value.QuadPart>0 ? std::uint64_t(value.QuadPart) : 0;}();
}
std::uint64_t hostTime100ns() noexcept {
  LARGE_INTEGER value{};
  if(!frequency || !QueryPerformanceCounter(&value) || value.QuadPart<=0 || frequency>UINT64_MAX/hostTicksPerSecond)return 0;
  const auto ticks=std::uint64_t(value.QuadPart), seconds=ticks/frequency;
  if(seconds>UINT64_MAX/hostTicksPerSecond)return 0;
  return seconds*hostTicksPerSecond+(ticks%frequency)*hostTicksPerSecond/frequency;
}
}
