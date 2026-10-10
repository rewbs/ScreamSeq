#pragma once
#include "PluginTypes.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Tracker {
// Control-thread preparation only. Platform adapters parse their wire types;
// every target/value is validated before either host publishes any parameter.
// No plugin calls, history, state serialization or callback work happens here.
inline std::vector<std::pair<uint32_t,float>> prepareParameterEdits(
    std::span<const PluginParameter> catalog,
    std::span<const std::pair<uint32_t,double>> requested) {
  if(requested.empty()||requested.size()>4096)throw std::invalid_argument("Parameter batch must contain 1 to 4096 values");
  std::set<uint32_t> seen;
  std::vector<std::pair<uint32_t,float>> result;result.reserve(requested.size());
  for(const auto &[id,value]:requested) {
    if(!seen.insert(id).second)throw std::invalid_argument("Duplicate parameter");
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==id;});
    if(found==catalog.end()||!found->writable)throw std::invalid_argument("Plugin parameter does not exist, is read-only or unavailable");
    if(!std::isfinite(found->min)||!std::isfinite(found->max)||found->min>found->max)
      throw std::invalid_argument("Plugin parameter range is unavailable");
    if(!std::isfinite(value)||value<found->min||value>found->max)
      throw std::invalid_argument("Plugin parameter value is outside its range");
    result.emplace_back(id,static_cast<float>(value));
  }
  return result;
}
}
