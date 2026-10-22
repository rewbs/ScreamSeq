#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Tracker {
// Latest audible editor value per prepared parameter. This is deliberately not
// the original-time notification FIFO used for recording/history. One native
// UI producer and one render consumer; quiescent state flushes may substitute
// for the render consumer. Allocate only while preparing the provider.
class LatestParameterEdits {
  static_assert(std::atomic<float>::is_always_lock_free);
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  static constexpr float empty=std::numeric_limits<float>::quiet_NaN();
  const size_t size_;
  std::unique_ptr<std::atomic<float>[]> values_;
  std::atomic<uint64_t> generation_{0};
  uint64_t applied_=0; // Consumer only.
  bool carry_=false;  // Consumer only; a full native change list deferred input.
public:
  explicit LatestParameterEdits(size_t count):size_(count) {
    if(count>4096)throw std::invalid_argument("Editor parameter mailbox exceeds the native catalog limit");
    values_=std::make_unique<std::atomic<float>[]>(count);
    for(size_t i=0;i<count;++i)values_[i].store(empty,std::memory_order_relaxed);
  }
  bool publish(size_t index,float value)noexcept {
    if(index>=size_||!std::isfinite(value))return false;
    values_[index].store(value,std::memory_order_release);
    generation_.fetch_add(1,std::memory_order_release);return true;
  }
  bool pending()const noexcept{return carry_||generation_.load(std::memory_order_acquire)!=applied_;}
  template<class Accept>void consume(Accept &&accept)noexcept {
    const auto generation=generation_.load(std::memory_order_acquire);
    if(!carry_&&generation==applied_)return;
    carry_=false;
    for(size_t index=0;index<size_;++index){
      if(std::isnan(values_[index].load(std::memory_order_relaxed)))continue;
      const auto value=values_[index].exchange(empty,std::memory_order_acq_rel);
      if(std::isnan(value)||accept(index,value))continue;
      // Preserve a newer native gesture if it arrived during failed delivery.
      float expected=empty;values_[index].compare_exchange_strong(expected,value,std::memory_order_acq_rel);
      carry_=true;
    }
    applied_=generation;
  }
};
}
