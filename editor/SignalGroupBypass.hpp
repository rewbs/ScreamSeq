#pragma once
#include "SignalRouteIdentity.hpp"
#include <cstdint>
#include <set>
#include <map>
#include <vector>
namespace Tracker {
struct SignalDefinition;
struct SignalGraph;
struct MixerGraph;
struct SignalGroupInput {
  uint64_t source=0,target=0;
  uint32_t output=0,input=0;
  bool operator==(const SignalGroupInput &) const = default;
};
struct SignalGroupOutput {
  uint64_t node=0;
  uint32_t port=0;
  bool operator==(const SignalGroupOutput &) const = default;
};
struct SignalGroupDryRoute {
  // An all-zero input denotes prepared silence, only for a zero-ingress group.
  SignalGroupInput input;
  SignalGroupOutput output;
  bool operator==(const SignalGroupDryRoute &) const = default;
};
struct SignalSongGroupDryRoute {
  // Empty input identity denotes prepared silence, only with zero ingress.
  SignalRouteIdentity input,output;
  bool operator==(const SignalSongGroupDryRoute &) const = default;
};
struct SignalGroupBoundary {
  std::vector<SignalGroupInput> inputs;
  std::vector<SignalGroupOutput> outputs;
};
struct SignalSongGroupBoundary {
  std::vector<SignalRouteIdentity> inputs,outputs;
};
std::set<uint64_t> signalGroupMembers(const SignalDefinition &,uint64_t group);
SignalGroupBoundary signalGroupBoundary(const SignalDefinition &,uint64_t group);
// Empty mappings infer a unique boundary, or silence when no audio enters.
// Ambiguous dry maps must be supplied explicitly before bypassing the group.
std::vector<SignalGroupDryRoute> resolvedSignalGroupDryRoutes(const SignalDefinition &,uint64_t group,bool requireComplete=true);
SignalSongGroupBoundary signalSongGroupBoundary(const SignalGraph &,const MixerGraph &,const std::vector<std::string> &orderedEffectRack,uint64_t group);
std::vector<SignalSongGroupDryRoute> resolvedSongGroupDryRoutes(const SignalGraph &,const MixerGraph &,const std::vector<std::string> &orderedEffectRack,uint64_t group,bool requireComplete=true);
void remapSignalGroupDryRoutes(SignalDefinition &,const std::map<uint64_t,uint64_t> &);
// Only remove boundaries which no longer exist. A changed surviving ingress is
// deliberately not guessed; callers must choose it in the same atomic edit.
void pruneSignalGroupDryRoutes(SignalDefinition &);
}
