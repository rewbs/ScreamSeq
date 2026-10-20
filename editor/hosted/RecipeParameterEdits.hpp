#pragma once
#include "ParameterEdits.hpp"
#include "editor/ParameterBaseline.hpp"

namespace Tracker {
// Edit a control-owned candidate recipe, never a playing processor. The native
// adapter parses wire types and supplies a disposable provider for acceptance.
// Prepare every value and allocation before calling that provider. Refusal may
// alter its disposable probe, but cannot partially alter the candidate recipe.
// Preview callers may use the same rule on an unpublished NativeSong copy.
template<class Accept>
void editSignalParameters(SignalDefinition &definition,uint64_t nodeID,
    std::span<const PluginParameter> catalog,
    std::span<const std::pair<uint32_t,double>> requested,Accept accept) {
  const auto node=std::find_if(definition.nodes.begin(),definition.nodes.end(),[&](const auto &n){return n.id==nodeID;});
  if(node==definition.nodes.end()||node->kind!=SignalNodeKind::Plugin)
    throw std::invalid_argument("Select an existing graph plugin node");
  // Empty batches are valid for bus-only recipe changes and exact no-ops.
  if(requested.size()>1024)throw std::invalid_argument("Graph parameter batch exceeds 1024 values");
  if(requested.empty())return;
  const auto edits=prepareParameterEdits(catalog,requested);
  auto overrides=node->plugin.parameters;
  std::vector<std::pair<uint32_t,float>> changed;changed.reserve(edits.size());
  std::vector<std::pair<size_t,double>> bases;
  for(const auto &[id,value]:edits) {
    const auto p=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.id==id;});
    const double minimum=p->min,maximum=p->max;
    const auto before=signalManualParameterValue(definition,nodeID,id,minimum,maximum,p->value);
    if(!std::isfinite(before)||before<minimum||before>maximum)
      throw std::invalid_argument("Graph manual parameter baseline is unavailable");
    // Providers accept floats. A get/set of the same representable value must
    // preserve the exact saved map and all enabled/disabled modulation bases.
    if(value==static_cast<float>(before))continue;
    overrides[id]=value;changed.emplace_back(id,value);
    if(maximum>minimum) {
      const auto normalized=(double(value)-minimum)/(maximum-minimum);
      for(size_t i=0;i<definition.modulation.size();++i) {
        const auto &edge=definition.modulation[i];
        if(edge.target==nodeID&&edge.parameter==id)bases.emplace_back(i,normalized);
      }
    }
  }
  for(const auto &[id,value]:changed)
    if(!accept(id,value))throw std::invalid_argument("Plugin rejected parameter value");
  // Only no-throw adoption remains after native acceptance. Opaque state,
  // topology, ports and unrelated nodes/parameters are unchanged.
  node->plugin.parameters.swap(overrides);
  for(const auto &[index,base]:bases)definition.modulation[index].base=base;
}
}
