#pragma once
#include <cstddef>
#include <cstdint>
namespace Tracker {
// Prepared observation sinks. The renderer supplies the exact samples/values
// for one reusable-graph copy; callbacks cannot allocate, block or retain PCM.
class SignalRuntimeObserver {
public:
  virtual ~SignalRuntimeObserver() = default;
  virtual void activate() noexcept = 0;
  virtual void audio(uint64_t node,bool output,uint32_t port,const float *,uint32_t frames,uint64_t position) noexcept = 0;
  virtual void route(uint32_t index,const float *,uint32_t frames,uint64_t position,double gain) noexcept = 0;
  virtual void control(uint64_t node,double first,double last,uint32_t frames,uint64_t position) noexcept = 0;
  virtual void contribution(uint32_t index,double first,double last,uint32_t frames,uint64_t position) noexcept = 0;
  virtual size_t storageBytes() const noexcept = 0;
};
}
