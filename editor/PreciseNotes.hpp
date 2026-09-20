#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace Tracker {
// Independent on/off events; several notes may occur inside one tracker row.
// Off/cut use the core's 255/254 note codes. Instrument 0 recalls the voice.
struct PreciseNote {
  uint64_t pattern=0,track=0;
  uint32_t position=0; // 1/65536 row
  uint16_t instrument=0;
  uint8_t note=0,velocity=127;
  uint8_t effect=0,parameter=0; // Note-local tracker effect, active until the next onset or row.
  bool operator==(const PreciseNote &) const = default;
};
inline constexpr size_t maximumPreciseNotes=65536;
}
