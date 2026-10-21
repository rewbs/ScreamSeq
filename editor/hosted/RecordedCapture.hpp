#pragma once
#include "PluginTypes.hpp"
#include <cmath>
#include <span>
#include <stdexcept>

namespace Tracker {
inline constexpr size_t maximumRecordedPoints=100000;
inline constexpr uint64_t maximumRecordedFrame=uint64_t(48000)*604800;

// Control-owner arithmetic only: converting a telemetry sample does not make it
// an event timestamp. Accurate capture additionally requires the native adapter
// to map the original event time and reject missing/stale clock coverage.
inline uint64_t canonicalRecordedFrame(uint64_t frame,uint32_t rate) {
  if(!rate||frame/rate>604800)throw std::invalid_argument("Recorded automation time is outside the supported week");
  const auto result=(frame/rate)*48000+(frame%rate)*48000/rate;
  if(result>maximumRecordedFrame)throw std::invalid_argument("Recorded automation time is outside the supported week");
  return result;
}
inline uint64_t canonicalRecordedFrame(uint64_t frame,double rate) {
  if(!std::isfinite(rate)||rate<=0||rate>UINT32_MAX)
    throw std::invalid_argument("Recorded automation sample rate is invalid");
  if(std::floor(rate)==rate)return canonicalRecordedFrame(frame,uint32_t(rate));
  // Core Audio exposes a double nominal rate. Preserve fractional rates rather
  // than narrowing them silently, with bounds checked before integer conversion.
  const long double result=static_cast<long double>(frame)*48000/rate;
  if(!std::isfinite(result)||result>maximumRecordedFrame)
    throw std::invalid_argument("Recorded automation time is outside the supported week");
  return uint64_t(result);
}

struct RecordedCaptureDestination {
  size_t index=0; // size() means append; otherwise replace this same target/time.
  bool changed=false;
};
// Prepare before host publication/history changes. The serial owner resolves a
// stable plugin ID, validates its catalogue/conflicts and keeps the vector fixed
// until adoption. Reserve an append before publication, then assign/push without
// allocation. No sorting/copying of unrelated points or saved-state mutation.
inline RecordedCaptureDestination prepareRecordedCapture(std::span<const ParameterChange> points,
                                                        const ParameterChange &point) {
  if(points.size()>maximumRecordedPoints||!std::isfinite(point.value)||point.frame>maximumRecordedFrame)
    throw std::invalid_argument("Invalid recorded automation point or capacity");
  size_t index=points.size();
  for(size_t i=0;i<points.size();++i)
    if(points[i].slot==point.slot&&points[i].id==point.id&&points[i].frame==point.frame) {
      if(index!=points.size())throw std::invalid_argument("Existing recorded automation has duplicate points at this time; edit the lane first");
      index=i;
    }
  if(index==points.size()&&points.size()==maximumRecordedPoints)
    throw std::invalid_argument("Automation reached 100,000 points");
  return {index,index==points.size()||points[index].value!=point.value};
}
}
