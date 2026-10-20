#pragma once
#include "DocumentOperations.hpp"
#include "editor/TrackLayout.hpp"

namespace ScreamSeq {
Json noteTrackLayout(const Tracker::NativeSong &, const OpenMPT::CSoundFile &);
struct TrackHostHooks {
  std::function<void(const Tracker::PreparedNoteTrackEdit &)> validateCandidate;
  std::function<std::function<void()>(const Tracker::NativeSong &, const Tracker::NativeSong &)> prepareColumnMutes;
};
// Document-worker adapter. Its caller owns expectedRevision admission. Empty
// hooks are for offline documents; a live host must provide mute publication.
class TrackOperations {
  Tracker::Document &document_;
  std::function<void()> stop_;
  TrackHostHooks host_;
public:
  TrackOperations(Tracker::Document &, std::function<void()> stop = {}, TrackHostHooks host = {});
  Json invoke(const std::string &, const Json &);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
