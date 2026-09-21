#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <span>
namespace ScreamSeq::Plugins {
// Compatible with mac/Plugins/PluginPreset: saved sound only, never song routes.
class PluginPreset {
public:
  using Json=nlohmann::json;
  static constexpr size_t maximumStateBytes=16u*1024u*1024u;
  static Json read(const std::string &path);
  static Json summary(const Json &preset);
  static bool matches(const Json &a,const Json &b);
  static Json write(const std::string &path,const Json &descriptor,std::span<const std::byte> state,
                    const std::string &name,bool overwrite,bool dryRun);
};
}
