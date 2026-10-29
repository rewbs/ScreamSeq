#pragma once
#include "NativeSong.hpp"
#include "GateSignal.hpp"

namespace Tracker {
class PatternGateRuntime {
public:
  struct Target {uint16_t channel=0;GateTarget kind=GateTarget::Volume;std::string plugin;uint32_t parameter=0;};
private:
  struct Slot {Target target;GateSnapshot state,snapshot;};
  struct Event {PatternCommand command;uint16_t channel=0;size_t slot=SIZE_MAX;};
  std::vector<Slot> slots_;
  std::map<uint16_t,std::vector<Event>> events_;
  uint64_t serial_=0;
  uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX,row_=UINT32_MAX;
  double previous_=-1;
  const std::vector<Event> *current_=nullptr;
  size_t next_=0;
  void apply(const Event &,double late=0) noexcept;
  void chase(const NativeSong &,OpenMPT::CSoundFile &);
public:
  PatternGateRuntime(const NativeSong &,OpenMPT::CSoundFile &);
  uint32_t limit(const OpenMPT::CSoundFile &,uint32_t) const noexcept;
  void prepare(OpenMPT::CSoundFile &,uint32_t) noexcept;
  size_t size()const noexcept{return slots_.size();}
  const Target &target(size_t i)const noexcept{return slots_[i].target;}
  const GateSnapshot &snapshot(size_t i)const noexcept{return slots_[i].snapshot;}
  const GateSnapshot *channel(uint16_t channel,GateTarget kind) const noexcept;
  size_t storageBytes()const noexcept;
};
}
