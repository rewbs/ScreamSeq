#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
namespace ScreamSeq::Plugins {
// Per-user browser preferences; never part of a song or its Undo history.
class PluginLibrary {
  std::optional<std::filesystem::path> path_;
public:
  using Json=nlohmann::json;
  explicit PluginLibrary(std::optional<std::filesystem::path> path={}):path_(std::move(path)){}
  static std::string identifier(const Json &descriptor);
  static Json decorate(const Json &plugins,const Json &library);
  Json read() const;
  Json set(const std::string &expectedRevision,const std::string &catalogID,const Json &patch,bool dryRun) const;
};
}
