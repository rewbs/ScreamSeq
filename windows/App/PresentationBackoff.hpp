#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace ScreamSeq {
// UI-owner pacing only. Keep dispatch/worker/audio service outside this gate.
// DXGI can keep its frame-latency event ready when a window is occluded; painting
// every ready notification wastes the full workspace draw on an invisible frame.
class PresentationBackoff {
  uint64_t next_=0,count_=0;
  bool occluded_=false;
public:
  static constexpr uint64_t retryMilliseconds=250;
  bool occluded()const noexcept{return occluded_;}
  uint64_t count()const noexcept{return count_;}
  bool ready(uint64_t now)const noexcept{return !occluded_||now>=next_;}
  void presented(bool occluded,uint64_t now)noexcept {
    occluded_=occluded;
    if(!occluded){next_=0;return;}
    if(count_!=std::numeric_limits<uint64_t>::max())++count_;
    next_=now+std::min(retryMilliseconds,std::numeric_limits<uint64_t>::max()-now);
  }
  // Native input/visibility changes request an immediate redraw. Service timers
  // alone must not undo the backoff on every playback poll.
  void resume()noexcept{occluded_=false;next_=0;}
  uint32_t waitMilliseconds(uint64_t now,uint32_t maximum)const noexcept {
    return occluded_&&now<next_?uint32_t(std::min<uint64_t>(next_-now,maximum)):maximum;
  }
};
}
