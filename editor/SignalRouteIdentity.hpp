#pragma once
#include <cstdint>
#include <string>
namespace Tracker {
// Portable identity of one contribution, independent of its meter token or gain.
struct SignalRouteIdentity {
  std::string kind,source,target,plugin,tap="post-gain";
  uint32_t input=0,output=0;
  bool operator==(const SignalRouteIdentity &) const = default;
};
}
