#pragma once
#include "MusicalAutomation.hpp"
#include <array>
#include <optional>
namespace OpenMPT { class CSoundFile; struct InstrumentEnvelope; }
namespace Tracker {
struct NativeSong;
struct EnvelopeShape {
  uint32_t span=16384, rowsPerBeat=4;
  std::vector<AutomationPoint> points;
  // Instrument markers are positions in the same coordinate system as points.
  bool instrument=false;
  uint8_t flags=1;
  std::array<uint32_t,5> markers{0,0,0,0,UINT32_MAX};
  bool operator==(const EnvelopeShape &) const = default;
};
struct EnvelopeTemplate {
  uint64_t id=0;
  std::string name;
  EnvelopeShape shape;
  bool operator==(const EnvelopeTemplate &) const = default;
};
enum class EnvelopeTargetKind:uint8_t { Parameter, Graph, Volume, Pan, Pitch };
struct EnvelopeTarget {
  EnvelopeTargetKind kind=EnvelopeTargetKind::Parameter;
  uint64_t owner=0, pattern=0; // lane ID / graph node ID / instrument ID; pattern for graph only
  auto operator<=>(const EnvelopeTarget &) const = default;
};
struct EnvelopeLink {
  EnvelopeTarget target;
  uint64_t templateID=0;
  uint32_t span=0;
  bool operator==(const EnvelopeLink &) const = default;
};
void validateEnvelopeShape(const EnvelopeShape &);
std::vector<AutomationPoint> fitEnvelope(const EnvelopeShape &,uint32_t span);
EnvelopeShape captureInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &);
OpenMPT::InstrumentEnvelope bakeInstrumentEnvelope(const EnvelopeShape &,uint32_t span,uint32_t maximumPoints);
bool envelopeTargetExists(const NativeSong &,const OpenMPT::CSoundFile &,const EnvelopeTarget &);
uint32_t envelopeTargetSpan(const NativeSong &,const OpenMPT::CSoundFile &,const EnvelopeTarget &);
EnvelopeShape captureEnvelope(const NativeSong &,const OpenMPT::CSoundFile &,const EnvelopeTarget &);
void applyEnvelope(NativeSong &,OpenMPT::CSoundFile &,const EnvelopeTarget &,const EnvelopeShape &,uint32_t span);
void validateEnvelopeBank(const NativeSong &,const OpenMPT::CSoundFile &);
void reconcileEnvelopeLinks(NativeSong &,const OpenMPT::CSoundFile &);
}
