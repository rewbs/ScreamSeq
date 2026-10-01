#pragma once
#include "SignalGraph.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
// A reusable recipe has one editable template base, but each playing copy has
// its own effective modulation output. This control-thread helper never reads
// a playing processor or turns an effective value into a new template base.
inline double signalManualParameterValue(const SignalDefinition &definition,uint64_t node,
    uint32_t parameter,double minimum,double maximum,double presetValue) {
  for(const auto &edge:definition.modulation)
    if(edge.enabled&&edge.target==node&&edge.parameter==parameter)
      return minimum+(maximum-minimum)*edge.base;
  for(const auto &entry:definition.nodes)if(entry.id==node) {
    if(const auto override=entry.plugin.parameters.find(parameter);override!=entry.plugin.parameters.end())return override->second;
    break;
  }
  return presetValue;
}

// Custom editor drafts start from the same manual base shown by host controls.
// Capture their final preset catalog on commit, before runtime modulation can
// replace it. Validate the entire catalog mapping before changing any edge.
template<class Parameters>
void captureSignalParameterBases(SignalDefinition &definition,uint64_t node,const Parameters &parameters) {
  for(const auto &edge:definition.modulation)if(edge.target==node) {
    const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==edge.parameter;});
    if(p==parameters.end()||!std::isfinite(p->min)||!std::isfinite(p->max)||!(p->max>p->min)||!std::isfinite(double(p->max)-double(p->min))||!std::isfinite(p->value)||p->value<p->min||p->value>p->max)
      throw std::invalid_argument("Preset has no usable value for an existing modulation target");
  }
  for(auto &edge:definition.modulation)if(edge.target==node) {
    const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==edge.parameter;});
    edge.base=(double(p->value)-double(p->min))/(double(p->max)-double(p->min));
  }
}
}
