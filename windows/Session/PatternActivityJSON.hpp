#pragma once
#include "editor/PatternActivityCatalog.hpp"
#include "windows/Api/SessionAdapter.hpp"

namespace ScreamSeq {
using Json=nlohmann::json;
inline Json patternActivityJSON(Tracker::Document &document,const Json &params,const Json &rack) {
  for(auto it=params.begin();it!=params.end();++it)if(it.key()!="pattern"&&it.key()!="order"&&it.key()!="row"&&it.key()!="channel")throw Api::ApiError(-32602,"Unknown activity argument");
  const auto number=[&](const char *key,unsigned maximum){if(!params.contains(key)||!params.at(key).is_number_integer()||params.at(key).get<int64_t>()<0||params.at(key).get<uint64_t>()>maximum)throw Api::ApiError(-32602,std::string("Invalid ")+key);return params.at(key).get<unsigned>();};
  std::vector<Tracker::ActivityPlugin> plugins;
  for(const auto &p:rack){Tracker::ActivityPlugin plugin{p.at("instanceID"),p.value("name","Plugin"),p.value("isInstrument",false),p.value("bypass",false)};
    if(p.value("instrument",0u))plugin.assignments.push_back(p.at("instrument").get<uint16_t>());
    for(const auto &a:p.value("instrumentAssignments",Json::array()))plugin.assignments.push_back(a.at("instrument").get<uint16_t>());
    plugins.push_back(std::move(plugin));}
  try{
    auto result=Tracker::patternActivityCatalog(document,uint16_t(number("pattern",65535)),number("order",UINT32_MAX),uint16_t(number("row",65535)),uint16_t(number("channel",126)),plugins);
    Json items=Json::array();for(const auto &i:result.items)items.push_back({{"category",i.category},{"kind",i.kind},{"label",i.label},{"plugin",i.plugin},{"envelope",i.envelope},{"graph",i.graph?Json("n"+std::to_string(i.graph)):Json(nullptr)},{"node",i.node?Json("n"+std::to_string(i.node)):Json(nullptr)},{"slot",i.slot},{"pattern",i.pattern},{"row",i.row},{"channel",i.channel},{"column",i.column},{"parameter",i.parameter}});
    return {{"reachable",result.playback.reachable},{"complete",result.playback.complete},{"limitation",result.playback.limitation},{"items",items},{"semantics","First visit from sequence start; samples observed throughout row at <=32-frame intervals, held plugin notes and routed effects, enabled pattern/instrument envelopes. Vendor-private tails and modulation are not measured."}};
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
}
}
