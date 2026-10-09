#pragma once
#include "NativeSong.hpp"
namespace Tracker {
// Immutable event lists and bounded repeat state are prepared on the control owner.
class NativePatternRuntime {
 struct Event { PatternCommand command; uint16_t channel; };
 struct Repeat { double next=0,interval=0,factor=1,step=0,gain=1;uint32_t remaining=0;uint64_t generation=0;uint8_t note=0,instrument=0; };
 std::map<uint16_t,std::vector<Event>> patterns_;
 std::array<Repeat,OpenMPT::MAX_BASECHANNELS> repeats_{};
 std::array<OpenMPT::NativePatternCurve,OpenMPT::MAX_BASECHANNELS> gains_{};
 OpenMPT::NativePatternCurve master_;
 const std::vector<Event> *events_=nullptr;
 size_t next_=0;
 uint32_t pattern_=UINT32_MAX,order_=UINT32_MAX,row_=UINT32_MAX;
 double previous_=-1;
 void apply(OpenMPT::CSoundFile &,const Event &,double,double) noexcept;
public:
 NativePatternRuntime(const NativeSong &,OpenMPT::CSoundFile &);
 uint32_t prepare(OpenMPT::CSoundFile &,uint32_t) noexcept;
};
}
