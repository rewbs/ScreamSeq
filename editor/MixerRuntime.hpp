#pragma once
#include "MixerGraph.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace Tracker {
struct MixerControls {
  double preGainDB = 0, gainDB = 0, pan = 0, width = 1;
  bool audible = true;
  double prePan = 0;
};
struct MixerMeter { float left = 0, right = 0; };
struct MixerAudioInput { uint32_t bus = 0; const float *samples = nullptr; };
class MixerRuntime {
public:
  static constexpr size_t maximumFrames = 4096, maximumBuses = 240;
  using Process = bool (*)(void *, size_t, float *, uint32_t, uint64_t) noexcept;
private:
  class Delay {
    std::vector<float> buffer_;
    size_t cursor_ = 0;
  public:
    explicit Delay(uint32_t frames = 0) : buffer_(size_t(frames) * 2, 0) {}
    void add(const float *, float *, uint32_t, float = 1) noexcept;
    void addPlanar(const float *, const float *, float *, uint32_t) noexcept;
  };
  struct Values { float pre = 1, gain = 1, pan = 0, width = 1, audible = 1, prePan = 0; };
  struct Node {
    std::array<float, maximumFrames * 2> input{}, work{};
    Delay direct;
    Values current, target, rampStart;
    uint32_t ramp = 0;
    explicit Node(uint32_t delay) : direct(delay) {}
  };
  MixerGraph graph_;
  MixerPlan plan_;
  double rate_;
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<Delay> edges_, instruments_;
  std::array<float, maximumFrames*2> auxiliaryScratch_{};
  struct Auxiliary {
    size_t processor = 0;
    std::vector<MixerAudioInput> inputs;
    std::vector<std::unique_ptr<std::array<float, maximumFrames * 2>>> buffers;
  };
  std::vector<Auxiliary> auxiliaries_;
  std::vector<Delay> sideDelays_;
  std::vector<float *> sideTargets_;
  std::unique_ptr<std::atomic<float>[]> meters_;
  struct ControlFrame { std::array<MixerControls, maximumBuses> values{}; };
  std::array<ControlFrame, 8> controls_{};
  std::atomic<uint32_t> write_{0}, read_{0};
  uint32_t frames_ = 0;
  uint32_t rampFrames_ = 1;
  size_t next_ = 0;
  uint64_t position_ = 0, through_ = 0;
  bool failed_ = false;
  static Values values(const MixerControls &) noexcept;
  void pending() noexcept;
public:
  MixerRuntime(MixerGraph, MixerPlan, double sampleRate, uint64_t start = 0);
  const MixerPlan &plan() const { return plan_; }
  const MixerGraph &graph() const { return graph_; }
  bool controls(const std::vector<MixerControls> &) noexcept; // Single control-thread producer.
  void begin(uint32_t frames, uint64_t position) noexcept;
  void instrument(size_t processor, uint32_t output, const float *buffer) noexcept;
  std::span<const MixerAudioInput> inputs(size_t processor) const noexcept;
  const float *process(size_t bus, const float *directLeft, const float *directRight,
                       Process, void *) noexcept;
  void complete() noexcept;
  uint64_t through() const { return through_; }
  bool failed() const { return failed_; }
  std::vector<MixerMeter> meters() const;
};
} // namespace Tracker
