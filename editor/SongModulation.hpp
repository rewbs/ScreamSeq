#pragma once
#include "SignalRuntime.hpp"
#include <array>
#include <span>

namespace Tracker {
// Host metadata is prepared off the audio thread. Step is expressed in the
// normalized 0..1 range (e.g. 1/(choiceCount-1)); zero means continuous.
struct SongModulationParameter {
  std::string plugin;
  uint32_t parameter = 0;
  double normalizedStep = 0;
  bool writable = true, continuous = true;
};
struct SongModulationTarget {
  std::string plugin;
  uint32_t parameter = 0;
  double normalizedStep = 0;
  bool quantized = false;
  struct Contribution { size_t source = 0; double minimum = 0, maximum = 0; };
  std::vector<Contribution> contributions;
};
// Prepared, bounded song controls. renderSource/note/controller/amount/overlay
// are audio-thread-only and allocate, destroy and lock nothing. The host renders
// a follower after its actual bus/plugin tap is available, before its targets.
// It must reject feedback dependencies when preparing the audio schedule.
class SongModulationRuntime {
public:
  static constexpr uint32_t maximumFrames = 4096, quantum = 32;
private:
  struct Source {
    SignalSongSource spec;
    std::array<double,maximumFrames> values{};
    double envelope = 0, attack = 0, release = 0, amount = 1;
    uint32_t held = 0, frames = 0;
    uint64_t frame = 0;
  };
  double sampleRate_;
  std::vector<Source> sources_;
  std::vector<SongModulationTarget> targets_;
  std::array<double,128> midi_{};
  const SignalPatternEnvelope *envelope(const Source &,uint64_t) const noexcept;
  double source(const Source &,double beat,double position,const SignalClock &) const noexcept;
  double sampledSource(const Source &,uint64_t,double,double,double,const SignalClock &) const noexcept;
  uint32_t segmentFrames(const Source &,uint64_t,uint32_t,const SignalClock &) const noexcept;
public:
  SongModulationRuntime(const SignalGraph &,std::span<const SongModulationParameter>,double sampleRate);
  SongModulationRuntime(const SongModulationRuntime &) = delete;
  SongModulationRuntime &operator=(const SongModulationRuntime &) = delete;
  size_t sourceCount() const noexcept { return sources_.size(); }
  const SignalSongSource &source(size_t index) const noexcept { return sources_[index].spec; }
  std::span<const SongModulationTarget> targets() const noexcept { return targets_; }
  // Buffers are local to this render call. A connected follower requires PCM;
  // nullptr for a disconnected follower means silence (including AR release).
  bool renderSource(size_t index,uint32_t frames,uint64_t absoluteFrame,SignalClock,
                    const float *interleavedStereo = nullptr) noexcept;
  std::span<const double> values(size_t index) const noexcept;
  // Baseline is the host's current manual/pattern/recorded value, never a value
  // returned by overlay. Add every contribution, clamp once, then quantize.
  // False means invalid baseline/index or missing/stale source PCM; no output
  // is written, allowing the host to fail safely instead of reusing old audio.
  bool overlay(size_t target,uint64_t absoluteFrame,double baseline,double &result) const noexcept;
  bool contribution(size_t source,uint64_t absoluteFrame,double &value) const noexcept;
  // Limits linear host ramp segments at the absolute grid, random changes and
  // automation step/point boundaries. Quantized targets always use one sample.
  uint32_t rampFrames(size_t target,uint64_t absoluteFrame,uint32_t maximum,SignalClock) const noexcept;
  // Events invalidate affected cached ranges. Split at the event sample and
  // render that source again before reading subsequent values.
  void controller(uint32_t cc,double normalizedValue) noexcept;
  void amount(uint64_t source,double normalizedValue) noexcept;
  void note(uint64_t target,uint64_t instrument,bool on,bool retrigger = false) noexcept;
  void gate(uint64_t source,bool on,bool retrigger = false) noexcept;
  void panic() noexcept;
  // Called at publication on the audio thread; immutable configuration and
  // new amount values remain unchanged, matching active source identities.
  void inheritState(const SongModulationRuntime &) noexcept;
  size_t storageBytes() const noexcept;
};
} // namespace Tracker
