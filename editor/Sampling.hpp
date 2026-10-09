#pragma once
#include "TrackerDocument.hpp"
#include <span>

namespace Tracker {
inline constexpr uint32_t maximumRecordedSampleFrames = 16u * 1024u * 1024u;
struct SamplingImportResult {
  int sample = 0, instrument = 0;
  uint32_t frames = 0, sampleRate = 0, channels = 0;
  uint64_t clippedValues = 0;
  bool convertsToInstruments = false;
};
// Control/document owner only. No normalisation; finite over-range values are
// saturated to the native 16-bit representation. The complete edit is one Undo.
SamplingImportResult importRecordedAudio(Document &, std::span<const float>, uint32_t sampleRate,
                                         uint32_t channels, const std::string &name,
                                         bool createInstrument, bool dryRun = false);
// Validation without allocating PCM, also useful before a potentially long render.
SamplingImportResult validateRecordedAudioImport(const Document &, uint32_t frames, uint32_t sampleRate,
                                                 uint32_t channels, const std::string &name, bool createInstrument);
struct SamplingSelection {
  uint16_t pattern = 0, firstRow = 0, lastRow = 0, firstChannel = 0, lastChannel = 0;
};
struct PreparedSamplingSelection {
  std::vector<std::byte> module;
  NativeSong native;
  SamplingSelection selection;
  uint32_t sequence = 0, order = 0;
  bool arranged = false;
};
void validateSamplingSelection(const Document &, const SamplingSelection &);
// Keeps channel identities, effects, automation and all routing. Only transport
// jumps/pattern loops are removed, from a private snapshot; row delays remain.
PreparedSamplingSelection prepareSamplingSelection(Document &, const SamplingSelection &);

// Offline-only adapter around the renderer's existing hooks. Observe exact mixed
// frame boundaries, including native tempo/RL, without predicting row durations.
// Construct after plugin attachment and destroy before Renderer. No allocations
// or locks in the hooks. Rendering must stay on the owning thread.
class SamplingRenderWindow {
  CSoundFile &song_;
  void *rowContext_ = nullptr, *mixContext_ = nullptr;
  bool (*row_)(void *) noexcept = nullptr;
  uint32_t (*mix_)(void *, uint32_t) noexcept = nullptr;
  uint32_t firstRow_ = 0;
  uint64_t frames_ = 0, firstFrame_ = UINT64_MAX;
  static bool row(void *) noexcept;
  static uint32_t mix(void *, uint32_t) noexcept;
public:
  SamplingRenderWindow(Renderer &, uint32_t firstRow);
  ~SamplingRenderWindow();
  SamplingRenderWindow(const SamplingRenderWindow &) = delete;
  SamplingRenderWindow &operator=(const SamplingRenderWindow &) = delete;
  uint64_t firstFrame() const noexcept { return firstFrame_; }
  uint64_t frames() const noexcept { return frames_; }
};
}
