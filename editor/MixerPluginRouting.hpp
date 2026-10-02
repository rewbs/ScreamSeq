#pragma once
#include "MixerGraph.hpp"
namespace Tracker {
// Rebuild arrival times and execution order at processor granularity when an
// exact processor-to-processor cable can cross a bus's serial boundary.
void compileMixerPluginRouting(const MixerGraph &,MixerPlan &,
                              const std::vector<MixerProcessorInfo> &,uint32_t sampleRate);
}
