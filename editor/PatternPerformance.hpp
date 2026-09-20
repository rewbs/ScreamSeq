#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace Tracker {
inline constexpr uint32_t performanceUnitsPerRow = 65536;
inline constexpr uint8_t maximumEffectColumns = 8;
inline constexpr size_t maximumPatternCommands = 65536;
enum class PatternCommandKind : uint8_t { ParameterSet, ParameterSlide, PitchSet, PitchSlide };
struct ParameterBinding {
  std::string plugin; // Persistent plugin instance UUID, never a rack slot.
  uint32_t parameter = 0; // Native AU/VST3/built-in parameter identifier.
  std::string name;
  bool operator==(const ParameterBinding &) const = default;
};
struct PatternCommand {
  uint64_t pattern = 0, track = 0; // Stable native pattern/note-column IDs.
  uint32_t position = 0, duration = 0; // 1/65536-row units; slides stop at pattern end.
  uint8_t column = 0; // Extra effect subcolumn; the source-format column is separate.
  PatternCommandKind kind = PatternCommandKind::ParameterSet;
  uint16_t binding = 0; // 1..255 for parameters; 0 for the current track's pitch.
  double value = 0; // Normalized parameter [0,1], or semitones [-96,96].
  uint8_t pitchRange = 2; // Match a plugin instrument's configured MIDI wheel range.
  bool operator==(const PatternCommand &) const = default;
};
struct PatternPerformance {
  std::map<uint64_t,uint8_t> columns;
  std::map<uint16_t,ParameterBinding> bindings;
  std::vector<PatternCommand> commands;
  bool empty() const {return columns.empty() && bindings.empty() && commands.empty();}
  bool controls(const std::string &plugin, uint32_t parameter) const;
  size_t bytes() const;
  bool operator==(const PatternPerformance &) const = default;
};
}
