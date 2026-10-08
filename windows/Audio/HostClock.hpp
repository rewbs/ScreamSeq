#pragma once
#include <cstdint>
namespace ScreamSeq {
inline constexpr std::uint64_t hostTicksPerSecond = 10000000;
// QPC domain converted to the same 100-ns units advertised by IAudioClock.
std::uint64_t hostTime100ns() noexcept;
}
