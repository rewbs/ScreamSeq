#pragma once
#include "GraphEditing.hpp"
#include <set>
namespace Tracker {
// A disconnected recipe fragment: permanent input/output nodes are retained for
// validation but their boundary cables are not copied. Only selected processors,
// sources, internal cables and relevant group/presentation metadata are included.
SignalDefinition copySignalSelection(const SignalDefinition &,const std::vector<uint64_t> &nodes);
SignalDefinition cutSignalSelection(NativeSong &,uint64_t graph,const std::vector<uint64_t> &nodes);
// Pattern references must be explicitly mapped, including same-song copies.
// This prevents coincident numeric IDs in different songs from binding silently.
SignalCloneResult pasteSignalSelection(NativeSong &,uint64_t graph,const SignalDefinition &fragment,
    const std::map<uint64_t,uint64_t> &patterns={},uint64_t parent=0,double x=0,double y=0);
}
