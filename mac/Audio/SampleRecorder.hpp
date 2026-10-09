#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Tracker {

// Single input-callback writer; preparation and PCM reads require a quiescent
// writer. The callback never grows storage, locks, logs or calls Objective-C.
class SampleCaptureBuffer {
public:
  static constexpr uint32_t maximumFrames = 16 * 1024 * 1024;
  static constexpr uint32_t maximumBlockFrames = 16384;
  static constexpr uint32_t maximumInputChannels = 256;
  enum class End : uint32_t { None, Stopped, Limit, DeviceChanged, RenderError, OversizedBlock, InvalidInput };
  struct Reading {
    bool capturing = false;
    uint32_t frames = 0, sampleRate = 0, channels = 0, capacityFrames = 0;
    float peak = 0;
    uint64_t clipped = 0;
    End end = End::None;
    int32_t systemError = 0;
  };
  void prepare(uint32_t rate, uint32_t inputChannels, uint32_t firstChannel, uint32_t channels, double maxSeconds);
  void ingest(const float *interleaved, uint32_t frames) noexcept;
  void finish(End reason, int32_t systemError = 0) noexcept;
  Reading reading() const noexcept;
  // Call only after the AudioUnit has stopped and its callback is quiescent.
  std::span<const float> pcm() const;
private:
  std::vector<float> storage_;
  uint32_t sampleRate_ = 0, inputChannels_ = 0, firstChannel_ = 0, channels_ = 0, capacity_ = 0;
  std::atomic<uint32_t> frames_{0}, end_{uint32_t(End::Stopped)};
  std::atomic<float> peak_{0};
  std::atomic<uint64_t> clipped_{0};
  std::atomic<int32_t> systemError_{0};
};

class SampleRecorder {
public:
  struct Device { std::string id, name; uint32_t channels = 0; bool isDefault = false; };
  struct Options { std::string device; uint32_t firstChannel = 0, channels = 1; double maxSeconds = 60; };
  SampleRecorder();
  ~SampleRecorder();
  SampleRecorder(const SampleRecorder &) = delete;
  SampleRecorder &operator=(const SampleRecorder &) = delete;
  static std::vector<Device> devices();
  // Read-only: the explicit native Record action owns the permission prompt.
  static const char *permission();
  static const char *endMessage(SampleCaptureBuffer::End);
  void start(const Options &);
  void stop() noexcept;
  SampleCaptureBuffer::Reading reading();
  std::span<const float> pcm() const;
  const Device &device() const noexcept;
  uint32_t firstChannel() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A take belongs to a document, not to a particular edit revision. This guard
// intentionally allows pattern edits during capture; appending does not replace
// a previously selected sample slot. The API also guards the current revision.
bool sampleRecordingOwnsDocument(const std::string &capturedDocument, const std::string &currentDocument) noexcept;

} // namespace Tracker
