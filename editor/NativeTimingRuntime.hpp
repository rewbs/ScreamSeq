#pragma once
namespace OpenMPT { class CSoundFile; }
namespace Tracker {
struct NativeSong;
// Build immutable event lookup and bounded audio scratch storage on the control
// thread. Ordinary OpenMPT songs retain their original clock and rounding.
void prepareNativeTiming(OpenMPT::CSoundFile &, const NativeSong &);
}
