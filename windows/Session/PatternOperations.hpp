#pragma once
#include "DocumentOperations.hpp"
#include <cstdint>
namespace Tracker {struct NativeSong;}
namespace ScreamSeq {
struct PatternHostHooks {
  std::vector<std::string> plugins; // Persistent IDs in current rack order.
  std::function<Json(size_t)> parameters; // Current native-unit parameter catalog.
  std::function<bool(size_t,uint32_t)> absoluteAutomation;
  std::function<void(const Tracker::NativeSong &)> validateCandidate;
};
// Serial document owner only. The dispatcher owns expectedRevision. Every
// musical mutation uses shared editNative validation/history and stops playback
// only after complete validation. Omitted replacement collections are retained.
class PatternOperations {
  Tracker::Document &document_;
  std::function<void()> stop_;
  PatternHostHooks host_;
public:
  PatternOperations(Tracker::Document &,std::function<void()> stop={},PatternHostHooks host={});
  Json invoke(const std::string &,const Json &);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
