#include "PatternCommands.hpp"
#include "soundlib/mod_specifications.h"
#include <string_view>
namespace Tracker {
using namespace OpenMPT;
namespace {
constexpr auto MOD_TYPE_MODXM = MOD_TYPE_MOD | MOD_TYPE_XM;
constexpr auto MOD_TYPE_S3MITMPT = MOD_TYPE_S3M | MOD_TYPE_IT | MOD_TYPE_MPT;
constexpr auto MOD_TYPE_NOMOD = MOD_TYPE_S3M | MOD_TYPE_XM | MOD_TYPE_IT | MOD_TYPE_MPT;
constexpr auto MOD_TYPE_XMITMPT = MOD_TYPE_XM | MOD_TYPE_IT | MOD_TYPE_MPT;
constexpr auto MOD_TYPE_ITMPT = MOD_TYPE_IT | MOD_TYPE_MPT;
constexpr auto MOD_TYPE_ALL = MOD_TYPE_MODXM | MOD_TYPE_S3MITMPT;
struct Definition { uint8_t command, mask, value; FlagSet<MODTYPE> formats; const char *name; };
#include "PatternCommandsData.inc"
std::string family(const Definition &d, bool volume) {
  const std::string_view name = d.name;
  if (name.find("Pattern") != name.npos || name.find("Speed") != name.npos || name.find("Tempo") != name.npos || name.find("Position Jump") != name.npos) return "timing";
  if (name.find("Panning") != name.npos || name.find("Pan ") != name.npos || name.find("Panbrello") != name.npos) return "panning";
  if (name.find("Vol") != name.npos || name.find("Tremolo") != name.npos || name.find("Tremor") != name.npos) return "volume";
  if (name.find("Porta") != name.npos || name.find("portamento") != name.npos || name.find("Vibrato") != name.npos || name.find("vibrato") != name.npos || name.find("Finetune") != name.npos || name.find("Arpeggio") != name.npos || name.find("Glissando") != name.npos) return "pitch";
  const auto kind = volume ? ModCommand::GetVolumeEffectType(VolumeCommand(d.command)) : ModCommand::GetEffectType(EffectCommand(d.command));
  switch (kind) { case EffectType::Global: return "timing"; case EffectType::Volume: return "volume"; case EffectType::Panning: return "panning"; case EffectType::Pitch: return "pitch"; default: return "sound"; }
}
std::string hint(const Definition &d, MODTYPE type, bool volume) {
  const bool modxm = bool(type & MOD_TYPE_MODXM);
  const std::string_view name = d.name;
  if (volume) {
    switch (VolumeCommand(d.command)) {
    case VOLCMD_VOLUME: return "Set note volume, 0 to 64 (decimal).";
    case VOLCMD_PANNING: return "Set note panning, 0 left to 64 right (decimal).";
    case VOLCMD_OFFSET: return "Select a saved sample cue, 0 to 9. This is a cue index, not a frame offset.";
    default: return type == MOD_TYPE_XM ? "Volume-column amount 0 to 15 (decimal); zero and effect memory follow XM playback rules." : "Volume-column amount 0 to 9 (decimal); zero and effect memory follow the source format.";
    }
  }
  if (d.mask) {
    if (name == "Sound Control") return "Low digit E plays forward; F plays backward. 0/1 disable/enable surround. Other values follow OpenMPT sound-control rules.";
    if (name == "Note Cut" || name == "Note Delay") return "Low digit is the tick within the row, not 1/256-row time. IT/MPTM treat zero as tick 1; MOD/XM use their original timing rules.";
    if (name == "Pattern Loop") return "Low digit 0 marks loop start; 1 to F repeat from that marker that many times.";
    if (name == "Pattern Delay") return "Low digit adds that many row durations before advancing.";
    if (name == "Fine Pattern Delay") return "Low digit adds that many ticks to this row.";
    if (name == "Set High Offset") return "Low digit sets the high sample-offset part, in units of 65536 sample frames.";
    if (name == "Set Panning") return "Low digit sets panning, 0 left to F right.";
    if (name == "Set Active Macro") return "Low digit selects a source-module MIDI macro. Embedded tracker plugin routing is not native AU/VST3 parameter automation.";
    if (name == "Invert Loop") return "MOD sample-loop inversion speed; low digit 0 disables it. This modifies sample playback and is not reverse playback.";
    if (name == "Set Filter") return "Low digit 0 enables the classic LED filter; 1 disables it.";
    if (name == "Glissando Control") return "Low digit 0 gives continuous portamento; nonzero quantizes portamento to semitones.";
    if (name.find("Waveform") != name.npos) return "Low digit selects the modulation waveform. Shapes and retrigger behavior depend on source-format compatibility settings.";
    if (name == "Retrigger Note") return "Low digit is the retrigger interval in ticks.";
    if (name == "Instr. Control") return "Low digit selects note-action or envelope controls; interpretation follows the source module's instrument playback rules.";
    return "Low hexadecimal digit sets the amount; the high digit selects this command. Zero and effect memory follow the source format.";
  }
  switch (EffectCommand(d.command)) {
  case CMD_DUMMY: return "Empty source-format effect placeholder; it produces no effect.";
  case CMD_ARPEGGIO: return "Two hexadecimal digits are semitone offsets above the note, cycled with the original pitch. MOD/XM 00 means no arpeggio.";
  case CMD_PORTAMENTOUP: case CMD_PORTAMENTODOWN: return modxm ? "Pitch slide amount per tick; 00 follows source-format effect memory." : "Pitch slide amount; Fx and Ex select fine/extra-fine slides. 00 reuses effect memory.";
  case CMD_TONEPORTAMENTO: return "Slide toward the row's note at this speed without normally retriggering the sample; 00 reuses speed.";
  case CMD_VIBRATO: case CMD_FINEVIBRATO: case CMD_TREMOLO: case CMD_PANBRELLO: return "High hexadecimal digit is speed; low digit is depth. Zero digits and modulation timing follow source-format memory.";
  case CMD_VOLUMESLIDE: case CMD_CHANNELVOLSLIDE: case CMD_GLOBALVOLSLIDE: case CMD_TONEPORTAVOL: case CMD_VIBRATOVOL: return "x0 slides up and 0y slides down; S3M/IT/MPTM also support fine-slide forms. Combined commands retain the prior pitch modulation.";
  case CMD_PANNINGSLIDE: return "Slide panning using the two hexadecimal digits. Direction, fine slides and zero memory follow the source format.";
  case CMD_PANNING8: return type == MOD_TYPE_S3M ? "00 left, 40 center, 80 right; A4 enables surround." : "00 left, 80 center, FF right.";
  case CMD_OFFSET: return "Start sample playback at parameter times 256 frames; 00 uses source-format offset memory. High-offset commands may extend it.";
  case CMD_VOLUME: case CMD_CHANNELVOLUME: return "Set volume from hexadecimal 00 (silent) to 40 (full).";
  case CMD_GLOBALVOLUME: return type == MOD_TYPE_IT || type == MOD_TYPE_MPT ? "Set song volume from hexadecimal 00 to 80." : "Set song volume from hexadecimal 00 to 40.";
  case CMD_POSITIONJUMP: return "Jump to this zero-based order index (hexadecimal). Pattern-break commands can also select its starting row.";
  case CMD_PATTERNBREAK: return "Break to this zero-based row in the next order. The API and editor use the decoded row number in hexadecimal, not the MOD/XM file's BCD encoding.";
  case CMD_SPEED: return modxm ? "Set ticks per row, hexadecimal 01 to 1F. F20 and above select tempo instead." : "Set ticks per row, hexadecimal 01 to FF.";
  case CMD_TEMPO: return modxm ? "Set tempo, hexadecimal 20 to FF." : "20 to FF set tempo; 0x/1x slide tempo down/up; 00 uses tempo memory where supported.";
  case CMD_RETRIG: return "Low digit is the retrigger tick interval; high digit selects the volume change applied to each retrigger.";
  case CMD_TREMOR: return "High digit controls sound-on time; low digit sound-off time. Tick counts depend on old-effects/compatibility mode.";
  case CMD_KEYOFF: return "Release the instrument at this tick within the row, using its key-off/envelope behavior.";
  case CMD_SETENVPOSITION: return "Set the instrument envelope position in ticks.";
  case CMD_MIDI: case CMD_SMOOTHMIDI: return "Execute a source-module MIDI macro. Embedded tracker plugin routing is distinct from native AU/VST3 parameter automation.";
  case CMD_DELAYCUT: return "High digit delays the note; low digit controls its subsequent length, in ticks.";
  case CMD_XPARAM: return "Extend a supported effect parameter from the preceding row; interpretation depends on that command.";
  case CMD_FINETUNE: case CMD_FINETUNE_SMOOTH: return "Adjust note tuning; 80 is the center value. Smooth form interpolates over the row.";
  default: return "Parameter interpretation follows the source module format and its compatibility settings.";
  }
}
}
std::vector<PatternCommandInfo> patternCommands(MODTYPE type, bool volume) {
  const auto &spec = CSoundFile::GetModSpecifications(type);
  std::vector<PatternCommandInfo> result{{0, 0, 0, 0, "...", "Clear command", "sound", "Clear this command and its value; preserve the other cell fields.", 0, 0}};
  const auto add = [&](const Definition &d) {
    // Engine specifications are authoritative for whole commands (including
    // native MPT extensions). Format masks distinguish extended subcommands.
    if ((d.mask && !(d.formats & type)) || (volume ? !spec.HasVolCommand(VolumeCommand(d.command)) : !spec.HasCommand(EffectCommand(d.command)))) return;
    const char letter = volume ? spec.GetVolEffectLetter(VolumeCommand(d.command)) : spec.GetEffectLetter(EffectCommand(d.command));
    std::string label(1, letter);
    label += d.mask ? std::string(1, "0123456789ABCDEF"[d.value >> 4]) + "x" : "xx";
    uint8_t suggested = d.value;
    if (!volume && (d.command == CMD_SPEED || (d.command == CMD_ARPEGGIO && (type & MOD_TYPE_MODXM)))) suggested = 1;
    if (!volume && d.command == CMD_TEMPO) suggested = 125;
    result.push_back({d.command, d.mask, d.value, suggested, label, d.name, family(d, volume), hint(d, type, volume)});
    auto &entry = result.back();
    if (volume) {
      entry.maximum = (d.command == VOLCMD_VOLUME || d.command == VOLCMD_PANNING) ? 64 : type == MOD_TYPE_XM ? 15 : 9;
    } else if (d.mask) { entry.minimum = d.value; entry.maximum = d.value | 15; }
    else {
      if (d.command == CMD_VOLUME || d.command == CMD_CHANNELVOLUME) entry.maximum = 64;
      if (d.command == CMD_GLOBALVOLUME) entry.maximum = type == MOD_TYPE_IT || type == MOD_TYPE_MPT ? 128 : 64;
      if (d.command == CMD_SPEED) { entry.minimum = 1; if (type & MOD_TYPE_MODXM) entry.maximum = 31; }
      if (d.command == CMD_TEMPO && (type & MOD_TYPE_MODXM)) entry.minimum = 32;
      if (d.command == CMD_ARPEGGIO && (type & MOD_TYPE_MODXM)) entry.minimum = 1;
      if (d.command == CMD_PATTERNBREAK && (type & (MOD_TYPE_MODXM | MOD_TYPE_S3M))) entry.maximum = 63;
    }
  };
  if (volume) { for (const auto &d : volumes) add(d); }
  else { for (const auto &d : effects) add(d); }
  return result;
}
}
