#pragma once
#include "../Api/SessionAdapter.hpp"
#include <atomic>

namespace ScreamSeq {
// One in-process native request owns one receipt. This is neither a replay
// cache nor a persisted/wire request ID. Only its worker publishes a result;
// the UI may inspect it after a lost or failed native completion callback.
class NativeCallReceipt final {
  std::atomic<std::shared_ptr<const Api::CompletedCall>> completed_;
public:
  void publish(std::shared_ptr<const Api::CompletedCall> result)noexcept {
    completed_.store(std::move(result),std::memory_order_release);
  }
  std::shared_ptr<const Api::CompletedCall> read()const noexcept {
    return completed_.load(std::memory_order_acquire);
  }
};
}
