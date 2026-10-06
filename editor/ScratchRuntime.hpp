#pragma once
#include "NativeSong.hpp"
#include "RealtimeTransition.hpp"
#include <array>

namespace Tracker {
// Prepared event lists and library storage belong to the control owner. Audio
// keeps only slot numbers, generation ownership and fixed sample-clock buffers.
class ScratchRuntime {
  struct Event { uint32_t position; uint16_t channel,gesture; uint8_t column; double beats,travelMs; uint16_t repeats; bool reverse; };
  struct Voice {
    uint16_t channel=0,gesture=0,repeats=1;
    uint64_t generation=0;
    double elapsed=0,beats=1,cue=0,travel=0,gain=1,correction=0,motionStart=0,motionDelta=0;
    uint32_t correctionFrames=0;
    bool active=false;
    std::array<double,4097> positions{};
    std::array<double,4096> gains{};
  };
  std::vector<Voice> voices_;
  std::map<uint16_t,std::vector<Event>> patterns_;
  const std::vector<Event> *events_=nullptr;
  size_t next_=0;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX,row_=UINT32_MAX;
  double previous_=-1;
  RealtimeTransition<ScratchGestureLibrary> library_;
  static double position(const Voice &, const ScratchGesture &, double elapsed) noexcept;
public:
  explicit ScratchRuntime(const NativeSong &);
  static std::unique_ptr<ScratchGestureLibrary> prepareUpdate(const NativeSong &);
  bool publishUpdate(std::unique_ptr<ScratchGestureLibrary> &) noexcept;
  uint32_t limit(const OpenMPT::CSoundFile &, uint32_t count) const noexcept;
  uint32_t prepare(OpenMPT::CSoundFile &, uint32_t count) noexcept;
};
}
