#pragma once
// Optional diagnostic-only host C++ allocation probe, NOT a malloc/lock/driver audit.
#ifdef SCREAMSEQ_WASAPI_ALLOCATION_AUDIT
#include <atomic>
#include <cstdint>
namespace ScreamSeq::AudioAudit {
inline thread_local bool active = false;
inline std::atomic<std::uint64_t> allocations{0}, deallocations{0};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
inline void allocated() noexcept {
  if (active) allocations.fetch_add(1, std::memory_order_relaxed);
}
inline void freed(void* pointer) noexcept {
  if (active && pointer) deallocations.fetch_add(1, std::memory_order_relaxed);
}
struct Scope {
  bool previous = active;
  Scope() noexcept { active = true; }
  ~Scope() { active = previous; }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
}
#endif
