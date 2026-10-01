#include "SampleArchive.hpp"
#include "SampleProcessing.hpp"
#include "InstrumentEnvelopeTools.hpp"
#include "soundlib/ModInstrument.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace Tracker {
using namespace OpenMPT;
namespace {
constexpr char timingMagic[8] = {'R', 'S', 'O', 'N', 'G', 'S', '2', '\0'};
constexpr char reverseMagic[8] = {'R', 'S', 'L', 'O', 'O', 'P', '1', '\0'};
constexpr char envelopeMagic[8] = {'R', 'S', 'E', 'N', 'V', 'S', '1', '\0'};
constexpr char structureMagic[8] = {'R', 'S', 'C', 'O', 'R', 'E', '1', '\0'};
constexpr uint16_t storedFlags = 0xA3FF; // PCM/loop flags, modified, no-default-volume; always embedded.
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Writer {
  std::vector<std::byte> bytes;
  void raw(const void *source, size_t size) {
    require(size <= maximumSongSnapshotBytes - bytes.size(), "Native sample snapshot exceeds 512 MB");
    const auto start = bytes.size();
    bytes.resize(start + size);
    if (size)
      std::memcpy(bytes.data() + start, source, size);
  }
  void u8(uint8_t value) { raw(&value, 1); }
  void u16(uint16_t value) {
    u8(uint8_t(value));
    u8(uint8_t(value >> 8));
  }
  void u32(uint32_t value) {
    u16(uint16_t(value));
    u16(uint16_t(value >> 16));
  }
};
struct Reader {
  std::span<const std::byte> bytes;
  size_t at = 0;
  std::span<const std::byte> raw(size_t size) {
    require(size <= bytes.size() - at, "Truncated native sample snapshot");
    auto result = bytes.subspan(at, size);
    at += size;
    return result;
  }
  uint8_t u8() { return std::to_integer<uint8_t>(raw(1)[0]); }
  uint16_t u16() {
    auto lo = u8();
    return uint16_t(lo) | (uint16_t(u8()) << 8);
  }
  uint32_t u32() {
    auto lo = u16();
    return uint32_t(lo) | (uint32_t(u16()) << 16);
  }
};
void text(Writer &w, const std::string &value) {
  require(value.size() <= maximumSongSnapshotBytes, "Oversized native text");
  w.u32(uint32_t(value.size())); w.raw(value.data(), value.size());
}
std::string text(Reader &r) {
  const auto value = r.raw(r.u32());
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}
bool samePatterns(const CSoundFile &source, const CSoundFile &base) {
  if (source.GetNumChannels() != base.GetNumChannels()) return false;
  for (PATTERNINDEX i = 0, end = std::max(source.Patterns.Size(), base.Patterns.Size()); i < end; ++i) {
    if (source.Patterns.IsValidPat(i) != base.Patterns.IsValidPat(i)) return false;
    if (!source.Patterns.IsValidPat(i)) continue;
    const auto &a = source.Patterns[i], &b = base.Patterns[i];
    if (a.GetNumRows() != b.GetNumRows() || a.GetNumChannels() != b.GetNumChannels() ||
        a.GetRowsPerBeat() != b.GetRowsPerBeat() || a.GetRowsPerMeasure() != b.GetRowsPerMeasure() ||
        a.GetTempoSwing() != b.GetTempoSwing() || a.GetColor() != b.GetColor() ||
        a.GetName() != b.GetName()) return false;
    // Upstream ModCommand/CPattern equality is musical, not byte-exact: it
    // ignores dormant volume/parameter values. Document::Cell edits those too.
    if (!std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const ModCommand &x, const ModCommand &y) {
          return x.note == y.note && x.instr == y.instr && x.volcmd == y.volcmd &&
                 x.vol == y.vol && x.command == y.command && x.param == y.param;
        })) return false;
  }
  return true;
}
bool sameOrders(const CSoundFile &source, const CSoundFile &base) {
  if (source.Order.GetNumSequences() != base.Order.GetNumSequences()) return false;
  for (SEQUENCEINDEX i = 0; i < source.Order.GetNumSequences(); ++i) {
    const auto &a = source.Order(i), &b = base.Order(i);
    if (a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin()) ||
        a.GetRestartPos() != b.GetRestartPos() || a.GetName() != b.GetName()) return false;
  }
  return true;
}
// The module is an interchange base, not an exact snapshot. Correct only fields
// it changed. This extension is parsed on a new unpublished CSoundFile, like PCM.
void encodeStructure(Writer &w, const CSoundFile &source, const CSoundFile &base) {
  const bool title = source.m_songName != base.m_songName;
  const bool patterns = !samePatterns(source, base) || source.Patterns.Size() != base.Patterns.Size();
  const bool orders = !sameOrders(source, base);
  if (!title && !patterns && !orders) return;
  w.raw(structureMagic, sizeof(structureMagic));
  w.u8((title ? 1 : 0) | (patterns ? 2 : 0) | (orders ? 4 : 0));
  if (title) text(w, source.m_songName); // Bytes in the archived core charset, not a module title field.
  if (patterns) {
    require(source.GetNumChannels() == base.GetNumChannels(), "Module changed the pattern channel inventory");
    require(source.Patterns.Size() <= MAX_PATTERNS, "Invalid native pattern inventory");
    w.u16(source.GetNumChannels()); w.u16(source.Patterns.Size());
    uint16_t count = 0;
    for (const auto &pattern : source.Patterns) if (pattern.IsValid()) ++count;
    w.u16(count);
    for (PATTERNINDEX index = 0; index < source.Patterns.Size(); ++index) {
      if (!source.Patterns.IsValidPat(index)) continue;
      const auto &p = source.Patterns[index];
      require(p.GetNumRows() && p.GetNumRows() <= MAX_PATTERN_ROWS, "Invalid native pattern rows");
      w.u16(index); w.u32(p.GetNumRows());
      w.u32(p.GetRowsPerBeat()); w.u32(p.GetRowsPerMeasure()); w.u32(p.GetColor());
      text(w, p.GetName());
      require(p.GetTempoSwing().size() <= MAX_ROWS_PER_BEAT, "Oversized native pattern groove");
      w.u32(uint32_t(p.GetTempoSwing().size()));
      for (auto value : p.GetTempoSwing()) w.u32(value);
      // Explicit fields: never depend on enum size, packing, padding or host endian.
      for (const auto &cell : p) {
        w.u8(cell.note); w.u8(cell.instr); w.u8(uint8_t(cell.volcmd));
        w.u8(cell.vol); w.u8(uint8_t(cell.command)); w.u8(cell.param);
      }
    }
  }
  if (orders) {
    require(source.Order.GetNumSequences() == base.Order.GetNumSequences(), "Module changed the sequence inventory");
    w.u16(source.Order.GetNumSequences());
    for (const auto &seq : source.Order) {
      require(seq.size() <= MAX_ORDERS, "Invalid native order inventory");
      w.u32(uint32_t(seq.size())); w.u16(seq.GetRestartPos());
      text(w, ::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8, seq.GetName()));
      for (auto pattern : seq) w.u16(pattern);
    }
  }
}
void restoreStructure(CSoundFile &base, Reader &r, uint64_t &decoded) {
  const auto flags = r.u8();
  require(flags && !(flags & ~7u), "Unsupported native structure flags");
  if (flags & 1) base.m_songName = text(r);
  if (flags & 2) {
    const auto channels = r.u16(), slots = r.u16(), count = r.u16();
    require(channels && channels <= MAX_BASECHANNELS && channels == base.GetNumChannels() &&
      slots <= MAX_PATTERNS && count <= slots, "Invalid native pattern inventory");
    base.Patterns.DestroyPatterns();
    base.Patterns.ResizeArray(slots); // Construct destination-owned pattern back-references.
    int previous = -1;
    for (uint16_t entry = 0; entry < count; ++entry) {
      const auto index = r.u16(); const auto rows = r.u32();
      require(index < slots && int(index) > previous && rows && rows <= MAX_PATTERN_ROWS, "Invalid native pattern entry");
      const auto beat = r.u32(), measure = r.u32(), color = r.u32();
      require((!beat && !measure) || CPattern::IsValidSignature(beat, measure), "Invalid native pattern signature");
      auto name = text(r);
      const auto size = r.u32();
      require(size <= MAX_ROWS_PER_BEAT, "Invalid native pattern groove length");
      Reader groove{r.raw(size_t(size) * 4)};
      TempoSwing swing;
      uint64_t total = 0;
      for (uint32_t i = 0; i < size; ++i) {
        const auto value = groove.u32();
        require(value && value <= 16u * TempoSwing::Unity, "Invalid native pattern groove duration");
        swing.push_back(value); total += value;
      }
      require(!size || total == uint64_t(size) * TempoSwing::Unity, "Unnormalized native pattern groove");
      const auto cells = uint64_t(rows) * channels;
      decoded += cells * sizeof(ModCommand);
      require(decoded <= maximumSongSnapshotBytes, "Decoded native song exceeds 512 MB");
      Reader data{r.raw(size_t(cells) * 6)}; // Reject truncated payload before allocating cells.
      require(base.Patterns.Insert(index, rows), "Cannot allocate native pattern");
      auto &p = base.Patterns[index];
      if (beat) require(p.SetSignature(beat, measure), "Invalid native pattern signature");
      p.SetName(std::move(name)); p.SetColor(color);
      // SetTempoSwing normalizes user weights, but Normalize is not idempotent:
      // a valid normalized extreme can exceed the input weight clamp. This is a
      // mutable, unpublished pattern; restore the validated native values exactly.
      const_cast<TempoSwing &>(p.GetTempoSwing()) = std::move(swing);
      for (auto &cell : p) {
        cell.note = data.u8(); cell.instr = data.u8(); cell.volcmd = VolumeCommand(data.u8());
        cell.vol = data.u8(); cell.command = EffectCommand(data.u8()); cell.param = data.u8();
      }
      previous = index;
    }
  }
  if (flags & 4) {
    const auto sequences = r.u16();
    require(sequences && sequences == base.Order.GetNumSequences(), "Invalid native sequence inventory");
    for (SEQUENCEINDEX i = 0; i < sequences; ++i) {
      const auto count = r.u32(); const auto restart = r.u16();
      require(count <= MAX_ORDERS, "Invalid native order inventory");
      const auto name = text(r);
      auto unicode = ::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8, name);
      require(::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8, unicode) == name, "Invalid native sequence name encoding");
      Reader data{r.raw(size_t(count) * 2)};
      auto &seq = base.Order(i); seq.resize(ORDERINDEX(count));
      seq.SetName(std::move(unicode)); seq.SetRestartPos(restart);
      for (auto &pattern : seq) pattern = data.u16(); // Keep skip/stop and unallocated references exactly.
    }
  }
}
InstrumentEnvelope &envelope(ModInstrument &instrument, uint8_t kind) {
  return kind == 0 ? instrument.VolEnv : kind == 1 ? instrument.PanEnv : instrument.PitchEnv;
}
const InstrumentEnvelope &envelope(const ModInstrument &instrument, uint8_t kind) {
  return kind == 0 ? instrument.VolEnv : kind == 1 ? instrument.PanEnv : instrument.PitchEnv;
}
void validateEnvelopeArchive(const InstrumentEnvelope &value) {
  require(value.size() <= MAX_ENVPOINTS && !(value.dwFlags.GetRaw() & ~31), "Invalid native instrument envelope flags or size");
  uint16_t previous = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    require(value[i].value <= 64 && (!i ? value[i].tick == 0 : value[i].tick >= previous), "Invalid native instrument envelope node");
    previous = value[i].tick;
  }
  const auto last = value.empty() ? 0 : value.size() - 1;
  require(value.nLoopStart <= value.nLoopEnd && value.nLoopEnd <= last && value.nSustainStart <= value.nSustainEnd && value.nSustainEnd <= last &&
    (value.nReleaseNode == ENV_RELEASE_NODE_UNSET || (!value.empty() && value.nReleaseNode <= last)), "Invalid native instrument envelope marker");
}
// Stable archive codes, independent of changes to the core's Charset enum.
constexpr std::array charsets = {::OpenMPT::mpt::Charset::UTF8,
                                 ::OpenMPT::mpt::Charset::ASCII,
                                 ::OpenMPT::mpt::Charset::ISO8859_1,
                                 ::OpenMPT::mpt::Charset::ISO8859_15,
                                 ::OpenMPT::mpt::Charset::CP437,
                                 ::OpenMPT::mpt::Charset::CP737,
                                 ::OpenMPT::mpt::Charset::CP775,
                                 ::OpenMPT::mpt::Charset::CP850,
                                 ::OpenMPT::mpt::Charset::CP852,
                                 ::OpenMPT::mpt::Charset::CP855,
                                 ::OpenMPT::mpt::Charset::CP857,
                                 ::OpenMPT::mpt::Charset::CP860,
                                 ::OpenMPT::mpt::Charset::CP861,
                                 ::OpenMPT::mpt::Charset::CP862,
                                 ::OpenMPT::mpt::Charset::CP863,
                                 ::OpenMPT::mpt::Charset::CP864,
                                 ::OpenMPT::mpt::Charset::CP865,
                                 ::OpenMPT::mpt::Charset::CP866,
                                 ::OpenMPT::mpt::Charset::CP869,
                                 ::OpenMPT::mpt::Charset::CP874,
                                 ::OpenMPT::mpt::Charset::CP437AMS,
                                 ::OpenMPT::mpt::Charset::CP437AMS2,
                                 ::OpenMPT::mpt::Charset::Windows1252,
                                 ::OpenMPT::mpt::Charset::Amiga,
                                 ::OpenMPT::mpt::Charset::RISC_OS,
                                 ::OpenMPT::mpt::Charset::AtariST,
                                 ::OpenMPT::mpt::Charset::ISO8859_1_no_C1,
                                 ::OpenMPT::mpt::Charset::ISO8859_15_no_C1,
                                 ::OpenMPT::mpt::Charset::Amiga_no_C1};
void header(Writer &w, const ModSample &s) {
  require(std::memchr(s.filename.buf, 0, MAX_SAMPLEFILENAME), "Unterminated native sample filename");
  w.u32(s.nLength);
  w.u32(s.nLoopStart);
  w.u32(s.nLoopEnd);
  w.u32(s.nSustainStart);
  w.u32(s.nSustainEnd);
  w.u32(s.nC5Speed);
  w.u16(s.nPan);
  w.u16(s.nVolume);
  w.u16(s.nGlobalVol);
  w.u16(uint16_t(s.uFlags.GetRaw()) & storedFlags);
  w.u8(uint8_t(s.RelativeTone));
  w.u8(uint8_t(s.nFineTune));
  w.u8(uint8_t(s.nVibType));
  w.u8(s.nVibSweep);
  w.u8(s.nVibDepth);
  w.u8(s.nVibRate);
  w.u8(s.rootNote);
  w.raw(s.filename.buf, MAX_SAMPLEFILENAME);
  if (s.uFlags[CHN_ADLIB]) {
    w.raw(s.adlib.data(), s.adlib.size());
    for (size_t i = s.adlib.size(); i < 36; ++i)
      w.u8(0);
  } else
    for (auto cue : s.cues)
      w.u32(cue);
}
ModSample header(Reader &r) {
  ModSample s;
  s.nLength = r.u32();
  s.nLoopStart = r.u32();
  s.nLoopEnd = r.u32();
  s.nSustainStart = r.u32();
  s.nSustainEnd = r.u32();
  s.nC5Speed = r.u32();
  s.nPan = r.u16();
  s.nVolume = r.u16();
  s.nGlobalVol = r.u16();
  const auto flags = r.u16();
  require(!(flags & ~storedFlags), "Unsupported native sample flags");
  s.uFlags = SampleFlags(static_cast<ChannelFlags>(flags));
  s.RelativeTone = int8_t(r.u8());
  s.nFineTune = int8_t(r.u8());
  s.nVibType = VibratoType(r.u8());
  s.nVibSweep = r.u8();
  s.nVibDepth = r.u8();
  s.nVibRate = r.u8();
  s.rootNote = r.u8();
  const auto file = r.raw(MAX_SAMPLEFILENAME);
  require(std::find(file.begin(), file.end(), std::byte{0}) != file.end(), "Unterminated native sample filename");
  std::memcpy(s.filename.buf, file.data(), file.size());
  require(s.nLength <= MAX_SAMPLE_LENGTH && s.nPan <= 256 && s.nVolume <= 256 && s.nGlobalVol <= 64 &&
              s.nVibType <= VIB_RANDOM,
          "Invalid native sample header");
  require(!s.uFlags[CHN_LOOP] || (s.nLoopStart < s.nLoopEnd && s.nLoopEnd <= s.nLength), "Invalid native sample loop");
  require(!s.uFlags[CHN_SUSTAINLOOP] || (s.nSustainStart < s.nSustainEnd && s.nSustainEnd <= s.nLength),
          "Invalid native sample sustain loop");
  if (s.uFlags[CHN_ADLIB]) {
    auto patch = r.raw(12);
    std::memcpy(s.adlib.data(), patch.data(), 12);
    auto padding = r.raw(24);
    for (auto b : padding)
      require(b == std::byte{0}, "Invalid native OPL padding");
  } else
    for (auto &cue : s.cues)
      cue = r.u32();
  return s;
}
bool layout(const ModSample &a, const ModSample &b) {
  return a.nLength == b.nLength && a.GetElementarySampleSize() == b.GetElementarySampleSize() &&
         a.GetNumChannels() == b.GetNumChannels() && (!a.nLength || b.HasSampleData());
}
void pcm(Writer &w, const ModSample &s, uint32_t first, uint32_t frames) {
  w.u32(first);
  w.u32(frames);
  if (s.uFlags[CHN_16BIT])
    for (size_t i = size_t(first) * s.GetNumChannels(), end = size_t(first + frames) * s.GetNumChannels(); i < end; ++i)
      w.u16(uint16_t(s.sample16()[i]));
  else
    w.raw(s.sample8() + size_t(first) * s.GetNumChannels(), size_t(frames) * s.GetNumChannels());
}
} // namespace
std::vector<std::byte> encodeSampleArchive(const CSoundFile &source, const CSoundFile &base) {
  Writer w;
  w.u32(uint32_t(source.GetType()));
  w.u16(source.GetNumSamples());
  w.u16(source.GetNumInstruments());
  w.u16(source.Order.GetCurrentSequenceIndex());
  const auto charset = std::find(charsets.begin(), charsets.end(), source.GetCharsetInternal());
  require(charset != charsets.end(), "Unsupported native sample text encoding");
  w.u16(uint16_t(charset - charsets.begin()));
  for (SAMPLEINDEX i = 1; i <= source.GetNumSamples(); ++i) {
    const auto &s = source.GetSample(i), &b = base.GetSample(i);
    require(!s.nLength || s.HasSampleData(), "Sample audio is missing; replace it before saving or editing");
    header(w, s);
    require(std::memchr(source.m_szNames[i].buf, 0, MAX_SAMPLENAME), "Unterminated native sample name");
    w.raw(source.m_szNames[i].buf, MAX_SAMPLENAME);
    const bool same = layout(s, b);
    w.u8(same ? 0 : 1);
    std::vector<std::pair<uint32_t, uint32_t>> chunks;
    if (!same && s.nLength)
      chunks.emplace_back(0, s.nLength);
    else
      for (uint32_t f = 0; f < s.nLength;) {
        const auto count = std::min(samplePatchFrames, s.nLength - f);
        if (std::memcmp(s.sampleb() + size_t(f) * s.GetBytesPerSample(),
                        b.sampleb() + size_t(f) * b.GetBytesPerSample(), size_t(count) * s.GetBytesPerSample()))
          chunks.emplace_back(f, count);
        f += count;
      }
    w.u32(uint32_t(chunks.size()));
    for (const auto &[first, frames] : chunks)
      pcm(w, s, first, frames);
  }
  for (INSTRUMENTINDEX i = 1; i <= source.GetNumInstruments(); ++i) {
    const auto *instrument = source.Instruments[i];
    w.u8(instrument ? 1 : 0);
    if (!instrument)
      continue;
    for (auto sample : instrument->Keyboard)
      w.u16(sample);
    for (auto note : instrument->NoteMap)
      w.u8(note);
  }
  uint16_t reverseCount = 0;
  for (SAMPLEINDEX i = 1; i <= source.GetNumSamples(); ++i) {
    require(source.GetSample(i).nativeReverseLoops <= 3, "Invalid native reverse loop flags");
    require(!source.GetSample(i).nativeReverseLoops || !source.GetSample(i).uFlags[CHN_ADLIB], "OPL samples cannot have native reverse loops");
    if (source.GetSample(i).nativeReverseLoops) ++reverseCount;
  }
  // Optional versioned extension: old snapshots remain byte-identical and old
  // readers reject extended snapshots instead of silently changing playback.
  if (reverseCount) {
    w.raw(reverseMagic, sizeof(reverseMagic)); w.u16(reverseCount);
    for (SAMPLEINDEX i = 1; i <= source.GetNumSamples(); ++i)
      if (source.GetSample(i).nativeReverseLoops) { w.u16(i); w.u8(source.GetSample(i).nativeReverseLoops); }
  }
  std::vector<std::pair<INSTRUMENTINDEX, uint8_t>> envelopes;
  for (INSTRUMENTINDEX index = 1; index <= source.GetNumInstruments(); ++index) if (source.Instruments[index]) {
    require(base.Instruments[index] != nullptr, "Module conversion lost an instrument");
    for (uint8_t kind = 0; kind < 3; ++kind)
      if (!sameInstrumentEnvelope(envelope(*source.Instruments[index], kind), envelope(*base.Instruments[index], kind))) envelopes.emplace_back(index, kind);
  }
  if (!envelopes.empty()) {
    w.raw(envelopeMagic, sizeof(envelopeMagic)); w.u16(uint16_t(envelopes.size()));
    for (const auto &[index, kind] : envelopes) {
      const auto &value = envelope(*source.Instruments[index], kind); validateEnvelopeArchive(value);
      w.u16(index); w.u8(kind); w.u8(uint8_t(value.size())); w.u8(value.dwFlags.GetRaw());
      w.u8(value.nLoopStart); w.u8(value.nLoopEnd); w.u8(value.nSustainStart); w.u8(value.nSustainEnd); w.u8(value.nReleaseNode);
      for (const auto &point : value) { w.u16(point.tick); w.u8(point.value); }
    }
  }
  encodeStructure(w, source, base);
  return std::move(w.bytes);
}
void validateSongStructureExport(const CSoundFile &source, const CSoundFile &converted) {
  require(source.m_songName == converted.m_songName &&
    ::OpenMPT::mpt::ToUnicode(source.GetCharsetInternal(), source.m_songName) ==
      ::OpenMPT::mpt::ToUnicode(converted.GetCharsetInternal(), converted.m_songName),
    "Module export would change the song title. Save a .resonance project.");
  require(samePatterns(source, converted),
    "Module export would change pattern allocation, cells or settings. Save a .resonance project.");
  // Native snapshots restore the archived charset; standalone modules do not.
  // Equal name bytes must also decode to the same text in the reopened module.
  for (PATTERNINDEX i = 0; i < source.Patterns.Size(); ++i) {
    if (!source.Patterns.IsValidPat(i)) continue;
    require(::OpenMPT::mpt::ToUnicode(source.GetCharsetInternal(), source.Patterns[i].GetName()) ==
      ::OpenMPT::mpt::ToUnicode(converted.GetCharsetInternal(), converted.Patterns[i].GetName()),
      "Module export would change pattern names. Save a .resonance project.");
  }
  require(sameOrders(source, converted),
    "Module export would change order lists or sequence settings. Save a .resonance project.");
}
void validateSampleExport(const CSoundFile &source, const CSoundFile &converted) {
  for (SAMPLEINDEX i = 1; i <= source.GetNumSamples(); ++i) {
    const auto &s = source.GetSample(i), &c = converted.GetSample(i);
    require(!s.nLength || s.HasSampleData(), "Sample audio is missing; replace it before exporting");
    auto a = s, b = c;
    a.uFlags.reset(SMP_MODIFIED);
    b.uFlags.reset(SMP_MODIFIED);
    // Different out-of-range cue sentinels all mean "no playable cue". IT's
    // loader supplies historical defaults when the writer omits these values.
    if (!a.uFlags[CHN_ADLIB] && !b.uFlags[CHN_ADLIB])
      for (size_t cue = 0; cue < a.cues.size(); ++cue)
        if (a.cues[cue] >= a.nLength && b.cues[cue] >= b.nLength)
          a.cues[cue] = b.cues[cue] = MAX_SAMPLE_LENGTH;
    Writer original, saved;
    header(original, a);
    header(saved, b);
    const bool same =
        layout(s, c) && original.bytes == saved.bytes && s.nativeReverseLoops == c.nativeReverseLoops &&
        std::strncmp(source.m_szNames[i].buf, converted.m_szNames[i].buf, MAX_SAMPLENAME) == 0 &&
        (!s.nLength || std::memcmp(s.sampleb(), c.sampleb(), size_t(s.nLength) * s.GetBytesPerSample()) == 0);
    if (!same)
      throw std::runtime_error("Module export would change sample " + std::to_string(i) +
                               " audio or settings. Save a .resonance project to preserve the edit.");
  }
  if (source.GetNumInstruments() != converted.GetNumInstruments())
    throw std::runtime_error("Module export would change the instrument inventory. Save a .resonance project.");
  for (INSTRUMENTINDEX i = 1; i <= source.GetNumInstruments(); ++i) {
    const auto *s = source.Instruments[i], *c = converted.Instruments[i];
    if (bool(s) != bool(c) || (s && (s->Keyboard != c->Keyboard || s->NoteMap != c->NoteMap)))
      throw std::runtime_error("Module export would change instrument " + std::to_string(i) +
                               " sample mapping. Save a .resonance project.");
    if (s) for (uint8_t kind = 0; kind < 3; ++kind)
      require(sameInstrumentEnvelope(envelope(*s, kind), envelope(*c, kind)), "Module export would change an instrument envelope. Save a .resonance project.");
  }
}
void restoreSampleArchive(CSoundFile &base, std::span<const std::byte> archive) {
  require(archive.size() <= maximumSongSnapshotBytes, "Oversized native sample archive");
  Reader r{archive};
  const auto type = r.u32();
  const auto samples = r.u16(), instruments = r.u16(), sequence = r.u16(), charset = r.u16();
  require(type == uint32_t(base.GetType()) && samples < MAX_SAMPLES && instruments < MAX_INSTRUMENTS &&
              charset < charsets.size() && sequence < base.Order.GetNumSequences(),
          "Invalid native sample snapshot inventory");
  uint64_t decoded = 0;
  // The destination is a new, unpublished song. Parse failure destroys it; no
  // active document or audio renderer can observe partially restored state.
  for (SAMPLEINDEX i = 1; i <= samples; ++i) {
    auto h = header(r);
    const auto name = r.raw(MAX_SAMPLENAME);
    require(std::find(name.begin(), name.end(), std::byte{0}) != name.end(), "Unterminated native sample name");
    const auto mode = r.u8();
    const auto chunks = r.u32();
    auto &s = base.GetSample(i);
    const auto stride = h.GetBytesPerSample();
    decoded += uint64_t(h.nLength) * stride;
    require(decoded <= maximumSongSnapshotBytes, "Decoded sample audio exceeds 512 MB");
    require(mode <= 1 && chunks <= (uint64_t(h.nLength) + samplePatchFrames - 1) / samplePatchFrames,
            "Invalid native PCM patch count");
    require(mode == 1 || layout(h, s), "Native PCM patch base does not match its layout");
    if (mode == 1) {
      require(chunks == (h.nLength ? 1u : 0u), "A replaced native sample requires one complete payload");
      if (h.nLength) {
        Reader payload = r;
        require(payload.u32() == 0 && payload.u32() == h.nLength, "Incomplete native PCM replacement");
        payload.raw(size_t(h.nLength) * stride); // Reject truncated replacements before allocating PCM.
        h.pData.pSample = ModSample::AllocateSample(h.nLength, stride);
        if (!h.pData.pSample)
          throw std::bad_alloc();
      }
      s.FreeSample();
      s = h;
    } else {
      h.pData = s.pData;
      s = h;
    }
    uint32_t previous = 0;
    for (uint32_t c = 0; c < chunks; ++c) {
      const auto first = r.u32(), frames = r.u32();
      require(frames && first >= previous && first <= s.nLength && frames <= s.nLength - first,
              "Invalid native PCM patch range");
      require(mode == 0 || (first == 0 && frames == s.nLength), "Incomplete native PCM replacement");
      if (s.uFlags[CHN_16BIT])
        for (size_t at = size_t(first) * s.GetNumChannels(), end = size_t(first + frames) * s.GetNumChannels();
             at < end; ++at)
          s.sample16()[at] = int16_t(r.u16());
      else {
        auto data = r.raw(size_t(frames) * s.GetNumChannels());
        std::memcpy(s.sampleb() + size_t(first) * stride, data.data(), data.size());
      }
      previous = first + frames;
    }
    std::memcpy(base.m_szNames[i].buf, name.data(), name.size());
    base.ResetSamplePath(i);
    s.uFlags = h.uFlags;
    const auto flags = s.uFlags;
    const auto loopStart = s.nLoopStart, loopEnd = s.nLoopEnd, sustainStart = s.nSustainStart,
               sustainEnd = s.nSustainEnd;
    s.PrecomputeLoops(base, false);
    s.uFlags = flags;
    s.nLoopStart = loopStart;
    s.nLoopEnd = loopEnd;
    s.nSustainStart = sustainStart;
    s.nSustainEnd = sustainEnd;
    if (s.uFlags[CHN_ADLIB])
      base.InitOPL();
  }
  for (SAMPLEINDEX i = samples + 1; i <= base.GetNumSamples(); ++i) {
    base.GetSample(i).FreeSample();
    base.GetSample(i).Initialize();
    base.m_szNames[i] = "";
    base.ResetSamplePath(i);
  }
  base.m_nSamples = samples;
  for (INSTRUMENTINDEX i = 1; i <= instruments; ++i) {
    const auto present = r.u8();
    require(present <= 1, "Invalid native instrument presence");
    if (!present) {
      delete base.Instruments[i];
      base.Instruments[i] = nullptr;
      continue;
    }
    require(base.Instruments[i] != nullptr, "Native sample map refers to a missing instrument");
    for (auto &sample : base.Instruments[i]->Keyboard) {
      sample = r.u16();
      require(sample < MAX_SAMPLES, "Invalid native sample mapping");
    }
    for (auto &note : base.Instruments[i]->NoteMap) {
      note = r.u8();
      require(note >= NOTE_MIN && note <= NOTE_MAX, "Invalid native note mapping");
    }
  }
  for (INSTRUMENTINDEX i = instruments + 1; i <= base.GetNumInstruments(); ++i) {
    delete base.Instruments[i];
    base.Instruments[i] = nullptr;
  }
  base.m_nInstruments = instruments;
  base.Order.SetSequence(SEQUENCEINDEX(sequence));
  base.m_modFormat.charset = charsets[charset];
  bool hasReverse = false, hasEnvelopes = false;
  while (r.at != archive.size()) {
    const auto extension = r.raw(sizeof(reverseMagic));
    if (std::memcmp(extension.data(), structureMagic, sizeof(structureMagic)) == 0) {
      restoreStructure(base, r, decoded);
      require(r.at == archive.size(), "Trailing or duplicate native structure extension");
      break;
    }
    if (std::memcmp(extension.data(), envelopeMagic, sizeof(envelopeMagic)) == 0) {
      require(!hasEnvelopes, "Duplicate native envelope extension"); hasEnvelopes = true;
      const auto count = r.u16(); require(count && count <= uint32_t(instruments) * 3, "Invalid native envelope inventory");
      uint32_t previous = 0;
      for (uint16_t entry = 0; entry < count; ++entry) {
        const auto index = r.u16(); const auto kind = r.u8(); const auto size = r.u8();
        const uint32_t key = uint32_t(index) * 3 + kind;
        require(index && index <= instruments && base.Instruments[index] && kind < 3 && key > previous && size <= MAX_ENVPOINTS, "Invalid native envelope entry");
        InstrumentEnvelope value; value.dwFlags = EnvelopeFlags(r.u8());
        value.nLoopStart=r.u8();value.nLoopEnd=r.u8();value.nSustainStart=r.u8();value.nSustainEnd=r.u8();value.nReleaseNode=r.u8();
        for (uint16_t point = 0; point < size; ++point) { const auto tick=r.u16(); const auto level=r.u8(); value.push_back(tick,level); }
        validateEnvelopeArchive(value); envelope(*base.Instruments[index],kind)=std::move(value); previous=key;
      }
      continue;
    }
    require(!hasReverse && !hasEnvelopes && std::memcmp(extension.data(), reverseMagic, sizeof(reverseMagic)) == 0, "Unknown or unordered native sample extension");
    hasReverse = true;
    const auto count = r.u16();
    require(count && count <= samples, "Invalid native reverse loop inventory");
    SAMPLEINDEX previous = 0;
    for (uint16_t i = 0; i < count; ++i) {
      const auto index = r.u16(); const auto flags = r.u8();
      require(index > previous && index <= samples && flags && flags <= 3, "Invalid native reverse loop entry");
      require(!base.GetSample(index).uFlags[CHN_ADLIB], "OPL samples cannot have native reverse loops");
      base.GetSample(index).nativeReverseLoops = flags; previous = index;
    }
  }
  require(r.at == archive.size(), "Trailing native sample snapshot data");
}
bool isSongSnapshot(std::span<const std::byte> bytes) {
  return bytes.size() >= sizeof(timingMagic) && std::memcmp(bytes.data(), timingMagic, sizeof(timingMagic)) == 0;
}
SongSnapshotParts splitSongSnapshot(std::span<const std::byte> bytes) {
  require(isSongSnapshot(bytes) && bytes.size() <= maximumSongSnapshotBytes, "Invalid native song snapshot");
  Reader r{bytes};
  r.raw(sizeof(timingMagic));
  const auto module = r.u32(), samples = r.u32();
  const auto timing = r.u32();
  require(module && samples && uint64_t(module) + samples + timing + 20 == bytes.size(), "Invalid native song snapshot lengths");
  return {r.raw(module), r.raw(samples), r.raw(timing)};
}
std::vector<std::byte> packSongSnapshot(std::span<const std::byte> module, std::span<const std::byte> samples, std::span<const std::byte> timing) {
  require(!module.empty() && !samples.empty() && module.size() <= maximumSongSnapshotBytes &&
              samples.size() <= maximumSongSnapshotBytes &&
              timing.size() <= maximumSongSnapshotBytes && module.size() + samples.size() + timing.size() + 20 <= maximumSongSnapshotBytes,
          "Native song snapshot exceeds 512 MB");
  Writer w;
  w.raw(timingMagic, 8);
  w.u32(uint32_t(module.size()));
  w.u32(uint32_t(samples.size()));
  w.u32(uint32_t(timing.size()));
  w.raw(module.data(), module.size());
  w.raw(samples.data(), samples.size());
  w.raw(timing.data(), timing.size());
  return std::move(w.bytes);
}
} // namespace Tracker
