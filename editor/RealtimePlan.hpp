#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
namespace Tracker {
// One control producer, one audio consumer. The current plan occupies a slot
// until the next consume; only the producer reclaims retired storage.
template<class T, size_t N = 4> class RealtimePlan {
  static_assert(N >= 3);
  std::array<std::unique_ptr<T>, N> slots_;
  std::atomic<uint32_t> written_{0}, consumed_{0};
public:
  bool available() const noexcept { return written_.load(std::memory_order_relaxed)-consumed_.load(std::memory_order_acquire)<N-1; }
  bool publish(std::unique_ptr<T> plan) {
    const auto w=written_.load(std::memory_order_relaxed);
    if(w-consumed_.load(std::memory_order_acquire)>=N-1)return false;
    slots_[w%N]=std::move(plan);written_.store(w+1,std::memory_order_release);return true;
  }
  // Producer only: the callback never changes ownership in these slots.
  template<class F> void forEachRetained(F &&visit) const {for(const auto &slot:slots_)if(slot)visit(*slot);}
  const T *consume() noexcept {
    const auto r=consumed_.load(std::memory_order_relaxed),w=written_.load(std::memory_order_acquire);
    if(r==w)return nullptr;
    const auto *plan=slots_[(w-1)%N].get();consumed_.store(w,std::memory_order_release);return plan;
  }
};
}
