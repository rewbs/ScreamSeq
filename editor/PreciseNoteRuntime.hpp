#pragma once
#include "NativeSong.hpp"
namespace Tracker {
class PreciseNoteSchedule {
  friend class PreciseNoteRuntime;
  struct Event {uint32_t position;uint16_t channel,instrument;uint8_t note,velocity,effect,parameter;bool cutCommand=false;};
  std::map<uint16_t,std::vector<Event>> patterns_;
public:
  explicit PreciseNoteSchedule(const NativeSong &);
};
class PreciseNoteRuntime {
  using Event=PreciseNoteSchedule::Event;
  PreciseNoteSchedule initial_;
  const PreciseNoteSchedule *schedule_;
  bool changed_=false;
  const std::vector<Event> *events_=nullptr;
  size_t next_=0;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX;
  double previous_=-1;
  uint32_t row_=UINT32_MAX;
  std::array<bool,127> effectOverrides_{};
public:
  explicit PreciseNoteRuntime(const NativeSong &);
  PreciseNoteRuntime(const PreciseNoteRuntime &)=delete;
  PreciseNoteRuntime &operator=(const PreciseNoteRuntime &)=delete;
  // Audio owner only; storage remains owned by Renderer/control publication.
  void adopt(const PreciseNoteSchedule &schedule) noexcept {schedule_=&schedule;changed_=true;}
  uint32_t prepare(OpenMPT::CSoundFile &,uint32_t) noexcept;
};
}
