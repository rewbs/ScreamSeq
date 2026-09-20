#pragma once
#include "common/stdafx.h"
#include "soundlib/ModInstrument.h"
#include "AutomationTools.hpp"

namespace Tracker {
inline constexpr uint32_t instrumentEnvelopeLength = 65536;
struct InstrumentEnvelopeToolResult {
  OpenMPT::InstrumentEnvelope envelope;
  uint32_t clipped = 0, rounded = 0, reanchored = 0;
};
bool sameInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &, const OpenMPT::InstrumentEnvelope &);
AutomationClip copyInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &, uint32_t start, uint32_t end);
InstrumentEnvelopeToolResult transformInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &, uint32_t maximumPoints,
                                                         const AutomationTool &);
}
