#pragma once
#include "NativeSong.hpp"
#include <optional>

namespace Tracker {
struct SignalCloneResult {
  uint64_t graph=0;
  std::map<uint64_t,uint64_t> identities; // Original node/group -> fresh identity.
};
// Control-thread transactions: rejection leaves both model and allocator intact.
SignalCloneResult cloneSignalGraph(NativeSong &,uint64_t graph,
    std::optional<std::string> name={},uint16_t number=0);
// A channel copy includes all references to the recipe on that target, so its
// Start/Stop/Amount/Wet commands remain paired. Instrument scope changes only
// that instrument's assignment. Every other target retains the original recipe.
SignalCloneResult makeSignalUseIndependent(NativeSong &,uint64_t graph,uint64_t target,
    bool instrument,std::optional<std::string> name={},uint16_t number=0);
void muteSignalSource(NativeSong &,uint64_t graph,uint64_t node,bool muted);
// Attach a newly created song source to its current visual/processing boundary.
// Call on the staged source-add model so allocation, edge and membership share
// one validation/Undo transaction. This does not reparent an existing source.
void assignSongSourceToGroup(NativeSong &,uint64_t node,uint64_t parent);
}
