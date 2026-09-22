#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ScreamSeq::Samples {
using Json=nlohmann::json;
struct LibraryEntry {
  std::string path,root,name;
  std::vector<std::string> folders;
  uint64_t bytes=0;
  double modified=0;
  Json dictionary() const;
};
struct LibraryQuery {
  std::string text,tagText;
  std::vector<std::string> tags;
  std::optional<std::string> root;
  size_t offset=0,limit=100;
};
struct FilenameNote {
  std::string family,label;
  int semitone=0;
  std::optional<int> number;
};
// Immutable index. Build and query on utility workers, never the audio callback
// or window procedure. Files are not decoded just to browse/search a library.
class LibraryIndex {
  struct SearchRecord {
    std::string text,pathKey;
    std::vector<std::pair<std::string,std::string>> tags;
  };
  std::vector<LibraryEntry> entries_;
  std::vector<SearchRecord> searchable_;
  std::unordered_map<std::string,std::shared_ptr<const Json>> multisamples_;
  std::vector<std::string> roots_,warnings_;
  std::string indexedAt_;
  void prepare();
public:
  static constexpr size_t maximumFiles=250000;
  LibraryIndex(std::vector<LibraryEntry>,std::vector<std::string> roots,
               std::vector<std::string> warnings={},std::string indexedAt={});
  static std::shared_ptr<const LibraryIndex> scan(const std::vector<std::string> &roots,
                                                const std::function<bool()> &cancelled={});
  static std::string canonicalPath(const std::string &);
  static std::string fold(const std::string &);
  static std::optional<FilenameNote> parseFilename(const std::string &);
  static const std::vector<std::string> &extensions();
  static std::shared_ptr<const LibraryIndex> fromSnapshot(const Json &);
  Json snapshot() const;
  Json search(const LibraryQuery &) const;
  Json multisample(const std::string &path) const;
  size_t count() const noexcept{return entries_.size();}
  const auto &roots() const noexcept{return roots_;}
  const auto &warnings() const noexcept{return warnings_;}
  const auto &indexedAt() const noexcept{return indexedAt_;}
};
}
