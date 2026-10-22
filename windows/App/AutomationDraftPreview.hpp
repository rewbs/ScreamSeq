#pragma once
#include "../../editor/MusicalAutomation.hpp"
#include "../../editor/ScratchGesture.hpp"
#include <nlohmann/json.hpp>
#include <map>

namespace ScreamSeq {
// UI-owned, bounded presentation of unsaved points. No document/API calls,
// message pumping, audio publication or history entries during pointer motion.
// Interpolation and scripted timing are exactly the shared musical evaluator.
class AutomationDraftPreview {
  using Json=nlohmann::json;
  Json signature_,values_=Json::array();
  std::map<std::string,Tracker::CurveFormula> programs_;
  template<bool Scratch>
  const Json &evaluate(const Json &points,double span,double beat,double start,double end,double scratchBeats){
    const Json signature=Json::array({points,span,beat,start,end,scratchBeats,Scratch});
    if(signature==signature_)return values_;
    signature_=signature;values_=Json::array();
    if(points.empty()){programs_.clear();return values_;}
    try {
      if(!points.is_array()||points.size()>4096||!std::isfinite(span)||span<1||span>16777216||
          !std::isfinite(beat)||beat<1||beat>65536||!std::isfinite(start)||!std::isfinite(end)||start<0||end<start||end>span)
        throw std::invalid_argument("Invalid draft preview bounds");
      if constexpr(Scratch)if(!std::isfinite(scratchBeats)||scratchBeats<1.0/8388608||scratchBeats>256||span!=65537)
        throw std::invalid_argument("Invalid scratch preview clock");
      static constexpr const char *names[]={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
      std::vector<Tracker::AutomationPoint> decoded;decoded.reserve(points.size());
      std::map<std::string,Tracker::CurveFormula> nextPrograms;
      for(const auto &point:points){
        const auto position=point.at("position").get<double>(),value=point.at("value").get<double>();
        if(!std::isfinite(position)||position<0||position>=span||std::floor(position)!=position||
            !std::isfinite(value)||value<0||value>1||(!decoded.empty()&&position<=decoded.back().position))
          throw std::invalid_argument("Invalid draft point");
        const auto name=point.value("curve",std::string("linear"));
        const auto kind=std::find(std::begin(names),std::end(names),name);
        if(kind==std::end(names))throw std::invalid_argument("Unknown draft curve");
        Tracker::AutomationPoint parsed{uint32_t(position),value,Tracker::AutomationCurve(kind-std::begin(names))};
        if(parsed.curve==Tracker::AutomationCurve::Scripted){
          const auto source=point.at("formula").get<std::string>();
          if(source.empty()||source.size()>2048)throw std::invalid_argument("Invalid draft formula");
          auto at=nextPrograms.find(source);
          if(at==nextPrograms.end()){
            const auto previous=programs_.find(source);
            at=nextPrograms.emplace(source,previous==programs_.end()?Tracker::CurveFormula(source):previous->second).first;
          }
          parsed.formula=at->second;
        }
        decoded.push_back(std::move(parsed));
      }
      if constexpr(Scratch)Tracker::validateScratchGesture(Tracker::ScratchGesture{"Preview",decoded,decoded});
      programs_=std::move(nextPrograms); // Retain only this draft's programs.
      Json next=Json::array();
      constexpr unsigned samples=257;
      for(unsigned i=0;i<samples;++i){const double position=start+(end-start)*i/(samples-1);
        double value;if constexpr(Scratch)value=Tracker::scratchEnvelopeValue(decoded,position/65536,scratchBeats);else value=Tracker::automationValue(decoded,position,span,beat);
        next.push_back(Json::array({position,value}));}
      values_=std::move(next);
    }catch(const std::exception &){
      // Invalid formulas must not masquerade as an accurate linear curve.
      // The ordinary guarded validation path retains its detailed diagnostic.
      programs_.clear();
    }
    return values_;
  }
public:
  const Json &values(const Json &points,double span,double beat,double start,double end){return evaluate<false>(points,span,beat,start,end,0);}
  const Json &scratchValues(const Json &points,double start,double end,double beats){return evaluate<true>(points,65537,256,start,end,beats);}
};
}
