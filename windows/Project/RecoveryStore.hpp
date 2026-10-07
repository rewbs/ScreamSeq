#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ScreamSeq::Project {

struct RecoveryEntry {
  std::string id, document, savedAt, title;
  std::optional<std::filesystem::path> source;
  bool hasRecording = false;
  nlohmann::json dictionary() const;
};

// Disk-worker operations only. The caller owns the store location; inspection
// sessions should never construct or use a store at the musician's directory.
// Final project publication is the commit point. Sidecar damage loses labels,
// never access to otherwise complete project bytes.
class RecoveryStore {
public:
  static constexpr std::size_t generations = 10;
  static constexpr std::size_t maximumMetadataBytes = 64u * 1024u;
  static constexpr std::size_t maximumBytes = 600u * 1024u * 1024u;
  enum class FaultPoint { projectFlush, metadataFlush, metadataPublish, projectPublish, cleanup };
  using FaultHook = std::function<void(FaultPoint, const std::filesystem::path &)>;

  explicit RecoveryStore(std::filesystem::path directory, FaultHook fault = {});
  std::vector<RecoveryEntry> entries() const;
  RecoveryEntry save(const std::string &sessionUUID, std::span<const std::byte> bytes,
                     std::string title = "Untitled", std::optional<std::filesystem::path> source = {},
                     bool hasRecording = false);
  std::vector<std::byte> read(const std::string &id) const;
  void clear(const std::string &sessionUUID);
  static std::string newSessionID();

private:
  std::filesystem::path directory_;
  FaultHook fault_;
};

} // namespace ScreamSeq::Project
