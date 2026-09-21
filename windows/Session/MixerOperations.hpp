#pragma once
#include "HostedProject.hpp"
#include <functional>
namespace ScreamSeq {
struct PlaybackFeedback {
  bool playing=false;
  unsigned sampleRate=48000;
  double latency=0;
  std::vector<Tracker::MixerMeter> meters;
  std::vector<Tracker::SignalActivity> activity;
};
// Application callbacks execute on its UI owner, never the audio callback.
struct PlaybackHooks {
  std::function<PlaybackFeedback()> feedback;
  std::function<bool(const std::vector<Tracker::MixerControls> &)> controls;
};
struct MixerHostHooks {
  std::vector<Tracker::PluginState> plugins;
  std::function<std::vector<Tracker::PluginAudioBus>(size_t,bool)> buses;
  std::function<PlaybackFeedback()> feedback;
  std::function<bool(const std::vector<Tracker::MixerControls> &)> controls;
  std::function<void(const Tracker::NativeSong &)> validateCandidate;
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
