#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace ScreamSeq {
// Disk-only jobs. Never capture a HWND, Application or mutable Document here.
// FIFO also ensures Save's cleanup follows every previously queued snapshot.
class RecoveryWriter {
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool closing_=false;
  std::thread worker_{[this]{
    for(;;) {
      std::function<void()> job;
      {std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return closing_||!jobs_.empty();});
       if(jobs_.empty())return;job=std::move(jobs_.front());jobs_.pop_front();}
      job();
    }
  }};
public:
  ~RecoveryWriter(){ {std::lock_guard lock(mutex_);closing_=true;}wake_.notify_one();if(worker_.joinable())worker_.join(); }
  template<class F> auto submit(F task) {
    using T=std::invoke_result_t<F>;
    auto job=std::make_shared<std::packaged_task<T()>>(std::move(task));auto result=job->get_future();
    {std::lock_guard lock(mutex_);if(closing_)throw std::runtime_error("Recovery writer is closing");jobs_.push_back([job]{(*job)();});}
    wake_.notify_one();return result;
  }
};
}
