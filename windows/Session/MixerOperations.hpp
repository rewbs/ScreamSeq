#pragma once
#include "HostedProject.hpp"
#include <functional>
namespace ScreamSeq {
struct PlaybackFeedback {
  bool playing=false;
  bool audioActive=false; // Includes independent instrument/sample audition.
  unsigned sampleRate=48000;
  double latency=0;
  uint64_t generation=0; // Host transport epoch, including stopped/restarted devices.
  std::vector<Tracker::MixerMeter> meters;
  std::vector<Tracker::SignalActivity> activity;
};
// Application callbacks execute on its UI owner, never the audio callback.
struct PlaybackHooks {
  std::function<void(size_t,bool)> pluginBypass;
  std::function<PlaybackFeedback()> feedback;
  std::function<bool(const std::vector<Tracker::MixerControls> &)> controls;
  std::function<bool(HostedProjectPlayback *,HostedProjectPlayback::PreparedNativeUpdate &)> publishNativeUpdate;
  std::function<std::function<void()>(unsigned,const Tracker::SampleEditGeometry &)> prepareSampleLoops;
};
struct MixerHostHooks {
  std::vector<Tracker::PluginState> plugins;
  std::function<std::vector<Tracker::PluginAudioBus>(size_t,bool)> buses;
  std::function<PlaybackFeedback()> feedback;
  std::function<bool(const std::vector<Tracker::MixerControls> &)> controls;
  std::function<void(const Tracker::NativeSong &)> validateCandidate;
  std::function<std::function<void()>(const Tracker::NativeSong &,const Tracker::NativeSong &)> prepareNativeUpdate;
};
class MixerOperations {
  Tracker::Document &document_;
  std::function<void()> stop_;
  MixerHostHooks host_;
public:
  MixerOperations(Tracker::Document &,std::function<void()>,MixerHostHooks);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
  Json invoke(const std::string &,const Json &);
};
}
