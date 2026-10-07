#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>

namespace Tracker {
enum class PatternCommandKind : uint8_t;
struct PatternCommand;
inline constexpr size_t maximumNativePatternParameters = 8;
enum class NativePatternOp : uint8_t {
  None, GainSet, GainSlide, PanSet, PanSlide, PitchRelative, TonePortamento,
  Vibrato, Tremolo, Panbrello, Arpeggio, Tremor, Retrigger, NoteRelease, NoteDelay,
  SampleOffset, SampleDirection, EnvelopePosition, EnvelopeEnable,
  TempoSet, TempoSlide, RowLength, Scratch, ScratchStop
};
enum class NativePatternFieldType : uint8_t { Number, Integer, Boolean, Choice };
enum class NativePatternDuration : uint8_t { None, Optional, Required };
struct NativePatternField {
  std::string_view key, name, unit, displayUnit;
  double minimum = 0, maximum = 1, initial = 0;
  NativePatternFieldType type = NativePatternFieldType::Number;
  std::span<const std::string_view> choices{};
  uint8_t width = 8; // Preferred monospaced character count, not device pixels.
  bool inGrid = true;
};
struct NativePatternCommandInfo {
  NativePatternOp operation;
  std::string_view identifier, code, name, family, description, scope, lifetime, equivalents;
  std::span<const NativePatternField> parameters;
  NativePatternDuration duration = NativePatternDuration::None;
  bool offsetInGrid = false;
};
std::span<const NativePatternCommandInfo> nativePatternCommands();
const NativePatternCommandInfo &nativePatternCommand(NativePatternOp operation);
const NativePatternCommandInfo &nativePatternCommand(std::string_view identifier);
std::array<double,maximumNativePatternParameters> nativePatternDefaults(NativePatternOp operation);
void validateNativePatternCommand(const PatternCommand &command);
std::string_view patternCommandKindName(PatternCommandKind kind);
PatternCommandKind patternCommandKind(std::string_view name);
// Existing native commands expose the same field vocabulary as the new family.
std::span<const NativePatternField> patternCommandFields(PatternCommandKind kind);
NativePatternDuration patternCommandDuration(PatternCommandKind kind);
}
