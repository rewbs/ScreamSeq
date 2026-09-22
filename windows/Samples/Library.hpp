#pragma once
#include "LibraryIndex.hpp"
#include <future>

namespace ScreamSeq::Samples {
// Separate from Document and musical Undo. The request owner and scanner have
// independent queues so searches keep using the old index during a rescan.
class Library final {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  // beforeScan is a native test seam for publication/cancellation races; it is
  // never configurable through the application, environment or agent API.
  explicit Library(std::optional<std::filesystem::path> directory={},std::vector<std::string> defaultRoots={},std::function<void()> beforeScan={});
  ~Library();
  Library(const Library &)=delete;
  Library &operator=(const Library &)=delete;
  std::future<Json> invoke(std::string method,Json params);
  Json status() const;
  static std::vector<std::string> reads();
  static std::vector<std::string> writes();
};
}
