#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>
namespace Tracker {class Document;}
namespace ScreamSeq {
// Worker/control owner only. Revision guards belong to the session dispatcher.
class TimelineOperations {
  Tracker::Document &document_;
  std::function<void()> stopPlayback_;
public:
  explicit TimelineOperations(Tracker::Document &,std::function<void()> stopPlayback={});
  nlohmann::json invoke(const std::string &,const nlohmann::json &params);
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
