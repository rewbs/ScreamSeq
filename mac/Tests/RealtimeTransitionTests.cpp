#include "editor/RealtimeTransition.hpp"
#include <array>
#include <iostream>
#include <thread>
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
using namespace Tracker;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
namespace {
thread_local bool renderThread = false;
struct Lifetime {
  std::atomic<unsigned> live{0}, made{0}, destroyed{0}, wrongThread{0};
};
struct Plan {
  Lifetime &lifetime;
  uint64_t revision;
  std::array<uint64_t, 128> data;
  Plan(Lifetime &owner, uint64_t value) : lifetime(owner), revision(value) {
    ++lifetime.live; ++lifetime.made;
    data.fill(value);
  }
  ~Plan() {
    if (renderThread) ++lifetime.wrongThread;
    --lifetime.live; ++lifetime.destroyed;
  }
  bool intact() const noexcept {
    for (const auto value : data) if (value != revision) return false;
    return true;
  }
};
void retainedTransition() {
  Lifetime lifetime;
  {
    RealtimeTransition<Plan> exchange(std::make_unique<Plan>(lifetime, 1));
    auto next = std::make_unique<Plan>(lifetime, 2);
    check(exchange.publish(next, 2) && !next && exchange.preparing(), "Publishing transfers ownership and reports preparation");
    uint64_t a, f, l;
    renderThread = true; tracker_audit_begin();
    const bool began = exchange.begin();
    auto *old = exchange.previous(); auto *current = &exchange.current();
    tracker_audit_end(&a, &f, &l); renderThread = false;
    check(began && old && old->revision == 1 && current->revision == 2 && a + f + l == 0, "A render boundary retains old and new plans without allocation/free/lock");
    for (uint64_t revision = 3; revision <= 1000; ++revision) {
      next = std::make_unique<Plan>(lifetime, revision);
      check(exchange.publish(next, revision), "A full edit burst must remain publishable while a transition is retained");
      exchange.collect();
      check(old->intact() && current->intact() && old->revision == 1 && current->revision == 2 && lifetime.live == 3,
            "Collecting superseded preparations preserves both audible plans");
      check(!exchange.begin(), "A rapid edit cannot replace an in-flight transition");
    }
    next = std::make_unique<Plan>(lifetime, 999);
    check(!exchange.publish(next, 999) && next && exchange.requestedRevision() == 1000,
          "A stale prepared result cannot replace the newest request or consume caller ownership");
    next.reset();
    renderThread = true; tracker_audit_begin();
    exchange.finish();
    tracker_audit_end(&a, &f, &l); renderThread = false;
    check(a + f + l == 0 && lifetime.live == 3 && exchange.renderedRevision() == 2 && exchange.preparing(),
          "Retirement acknowledges the audible revision but never frees on the render thread");
    exchange.collect();
    check(lifetime.live == 2 && exchange.begin() && exchange.currentRevision() == 1000,
          "The next transition takes the newest preparation, skipping obsolete revisions");
    exchange.finish(); exchange.collect();
    check(lifetime.live == 1 && !exchange.preparing() && exchange.current().revision == 1000,
          "The final acknowledgement clears Preparing only when the latest plan is active");
  }
  check(lifetime.live == 0 && lifetime.made == lifetime.destroyed && lifetime.wrongThread == 0,
        "Every retained and superseded plan is destroyed on the control thread");
}
void concurrentPublication() {
  Lifetime lifetime;
  std::atomic<bool> producerDone{false}, corrupt{false};
  std::atomic<uint64_t> realtimeViolations{0}, transitions{0};
  {
    RealtimeTransition<Plan> exchange(std::make_unique<Plan>(lifetime, 1));
    std::thread audio([&] {
      renderThread = true;
      while (!producerDone.load(std::memory_order_acquire) || exchange.preparing()) {
        uint64_t a, f, l; tracker_audit_begin();
        if (exchange.begin()) {
          const auto revision = exchange.currentRevision();
          const auto prior = exchange.previous()->revision;
          // Keep both pointers across many reads while the producer replaces
          // and collects pending plans. None may be mutated or reclaimed.
          for (unsigned i = 0; i < 97; ++i)
            if (!exchange.current().intact() || !exchange.previous()->intact() ||
                exchange.current().revision != revision || exchange.previous()->revision != prior || prior >= revision)
              corrupt = true;
          exchange.finish(); ++transitions;
        }
        tracker_audit_end(&a, &f, &l); realtimeViolations += a + f + l;
      }
      renderThread = false;
    });
    bool published = true;
    for (uint64_t revision = 2; revision <= 100000; ++revision) {
      auto next = std::make_unique<Plan>(lifetime, revision);
      if (!exchange.publish(next, revision)) { published = false; break; }
      exchange.collect();
    }
    producerDone.store(true, std::memory_order_release);
    audio.join(); exchange.collect();
    check(published && !corrupt && transitions > 0 && realtimeViolations == 0,
          "Concurrent plan preparation/retirement preserves revisions, memory and realtime safety");
    check(exchange.renderedRevision() == 100000 && lifetime.live == 1,
          "The final prepared revision becomes active without leaked pending/retired plans");
  }
  check(lifetime.live == 0 && lifetime.made == lifetime.destroyed && lifetime.wrongThread == 0,
        "Concurrent replacement has no render-thread destruction or leaked plans");
}
void failedCandidate() {
  Lifetime lifetime;
  {
    RealtimeTransition<Plan> exchange(std::make_unique<Plan>(lifetime,1));
    auto candidate=std::make_unique<Plan>(lifetime,2);
    check(exchange.publish(candidate,2) && exchange.begin(),"Activate failure fixture");
    uint64_t a,f,l;renderThread=true;tracker_audit_begin();
    const bool cancelled=exchange.cancel();
    tracker_audit_end(&a,&f,&l);renderThread=false;
    check(cancelled && a+f+l==0 && exchange.currentRevision()==1 && exchange.renderedRevision()==1 &&
      exchange.requestedRevision()==2 && !exchange.previous(),"Cancellation retains the old plan without acknowledging the failed request");
    exchange.collect();check(lifetime.live==1,"Rejected processors are reclaimed on the control owner");
    candidate=std::make_unique<Plan>(lifetime,3);
    check(exchange.publish(candidate,3) && exchange.begin(),"A new request follows a cancelled candidate");
    exchange.finish();exchange.collect();
    check(exchange.renderedRevision()==3 && !exchange.preparing(),"Successful retry settles the new revision");
  }
  check(lifetime.live==0 && lifetime.wrongThread==0,"Cancellation never destroys a processor on audio");
}
}
int main() {
  try {
    retainedTransition(); concurrentPublication(); failedCandidate();
    std::cout << "PASS retained routing-plan ownership: latest preparation, stale rejection, explicit retirement, concurrent reclamation and realtime audit\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
