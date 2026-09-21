#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <tuple>
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
// Control-thread candidate editing only: allocates and sorts, never a callback
// operation. NativeSong::validate remains the authority for event validity.
// Stored order is not musical content. Leave the entire original vector intact
// for equal multisets, and never reorder another pattern on a real replacement.
inline bool replacePreciseNotesForPattern(std::vector<PreciseNote> &notes,
                                         uint64_t pattern,
                                         std::vector<PreciseNote> replacement) {
  const auto less=[](const PreciseNote &a,const PreciseNote &b) {
    // As in PreciseNoteRuntime: release before onset on the same track/position.
    // Include every field so comparison also distinguishes changed payloads.
    const auto key=[](const PreciseNote &n) {
      return std::tuple(n.pattern,n.position,n.track,n.note<128,n.note,
                        n.instrument,n.velocity,n.effect,n.parameter);
    };
    return key(a)<key(b);
  };
  std::vector<PreciseNote> before;
  for(const auto &note:notes)if(note.pattern==pattern)before.push_back(note);
  std::sort(before.begin(),before.end(),less);
  std::sort(replacement.begin(),replacement.end(),less);
  if(before==replacement)return false;
  std::erase_if(notes,[&](const auto &note){return note.pattern==pattern;});
  notes.insert(notes.end(),replacement.begin(),replacement.end());
  return true;
}
}
