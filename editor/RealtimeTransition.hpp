#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Tracker {
// Single control producer / single render consumer. Unlike RealtimePlan, an
// exchange keeps the outgoing plan alive until the render consumer explicitly
// finishes its transition. Superseded pending plans and retired plans are
// reclaimed by the producer only. Destruction requires a stopped consumer.
template<class T, size_t N = 4> class RealtimeTransition {
  static_assert(N >= 4); // Outgoing, incoming, pending, and preparation slot.
  enum class State : uint8_t { Free, Held, Retired };
  static_assert(std::atomic<State>::is_always_lock_free && std::atomic<size_t>::is_always_lock_free &&
                std::atomic<uint64_t>::is_always_lock_free);
  struct Slot {
    std::unique_ptr<T> plan;
    uint64_t revision = 0;
    std::atomic<State> state{State::Free};
  };
  static constexpr size_t none = N;
  std::array<Slot, N> slots_;
  std::atomic<size_t> pending_{none};
  std::atomic<uint64_t> requested_{0}, rendered_{0};
  size_t current_ = 0, previous_ = none; // Render owner only.
public:
  explicit RealtimeTransition(std::unique_ptr<T> initial, uint64_t revision = 1) {
    if (!initial || !revision) throw std::invalid_argument("A transition requires an initial plan and revision");
    slots_[0].plan = std::move(initial);
    slots_[0].revision = revision;
    slots_[0].state.store(State::Held, std::memory_order_relaxed);
    requested_.store(revision, std::memory_order_relaxed);
    rendered_.store(revision, std::memory_order_relaxed);
  }
  // Control owner. A failed/stale preparation retains caller ownership and
  // leaves both the audible plan and the latest pending plan unchanged.
  bool publish(std::unique_ptr<T> &plan, uint64_t revision) noexcept {
    if (!plan || revision <= requested_.load(std::memory_order_relaxed)) return false;
    for (size_t i = 0; i < N; ++i) {
      if (slots_[i].state.load(std::memory_order_acquire) == State::Held) continue;
      slots_[i].plan = std::move(plan); // Old storage is freed on this thread.
      slots_[i].revision = revision;
      slots_[i].state.store(State::Held, std::memory_order_relaxed);
      requested_.store(revision, std::memory_order_release);
      // Exchange is also the ownership transfer for a replaced pending slot.
      // Either the consumer took that slot, or this producer retires it; never
      // both. The consumer does not inspect slot state while acquiring a plan.
      const auto replaced = pending_.exchange(i, std::memory_order_acq_rel);
      if (replaced != none) slots_[replaced].state.store(State::Retired, std::memory_order_release);
      return true;
    }
    return false;
  }
  // Control owner. May be called while either render plan is in use.
  void collect() noexcept {
    for (auto &slot : slots_) if (slot.state.load(std::memory_order_acquire) == State::Retired) {
      slot.plan.reset();
      slot.state.store(State::Free, std::memory_order_relaxed);
    }
  }
  uint64_t requestedRevision() const noexcept { return requested_.load(std::memory_order_acquire); }
  uint64_t renderedRevision() const noexcept { return rendered_.load(std::memory_order_acquire); }
  bool preparing() const noexcept { return renderedRevision() != requestedRevision(); }

  // Render owner, at a block boundary. Never interrupts an in-flight fade.
  // A burst of control edits collapses to the newest prepared pending plan.
  bool begin() noexcept {
    if (previous_ != none) return false;
    const auto next = pending_.exchange(none, std::memory_order_acq_rel);
    if (next == none) return false;
    previous_ = current_;
    current_ = next;
    return true;
  }
  T &current() noexcept { return *slots_[current_].plan; }
  T *previous() noexcept { return previous_ == none ? nullptr : slots_[previous_].plan.get(); }
  uint64_t currentRevision() const noexcept { return slots_[current_].revision; }
  // Render owner, after the last use of the outgoing plan and its buffers.
  void finish() noexcept {
    if (previous_ == none) return;
    slots_[previous_].state.store(State::Retired, std::memory_order_release);
    previous_ = none;
    rendered_.store(slots_[current_].revision, std::memory_order_release);
  }
  // Render owner. A newly activated plan may fail before its output is made
  // audible. Keep the outgoing plan and retire the failed candidate off-thread.
  // requestedRevision remains monotonic; the caller reports the failed request
  // separately instead of pretending that it was rendered successfully.
  bool cancel() noexcept {
    if (previous_ == none) return false;
    slots_[current_].state.store(State::Retired, std::memory_order_release);
    current_ = previous_;
    previous_ = none;
    return true;
  }
};
} // namespace Tracker
