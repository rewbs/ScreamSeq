#pragma once
#include "RecordedGestureRecovery.hpp"
#include "ControlInputRecovery.hpp"

namespace Tracker {
// Read-only, detached evidence. Collection indices address the immutable import
// tree of one document, not live take identity. Native/API owners guard that
// document and revision between list/page requests. Nothing here can publish,
// acknowledge, deduplicate, retime or replay musical input.
inline constexpr const char *gestureRecoveryKey="screamseqPluginGestureRecovery";
inline const char *pluginRecoveryKey(const std::string &kind) {
  if(kind=="gestures")return gestureRecoveryKey;
  if(kind=="nativeControls")return controlInputRecoveryKey;
  throw std::invalid_argument("Unknown plugin recovery evidence kind");
}
inline nlohmann::json pluginInputRecoveryList(const nlohmann::json &preserved,size_t offset=0,size_t limit=64) {
  using Json=nlohmann::json;
  if(!preserved.is_object()||!limit||limit>128)throw std::invalid_argument("Invalid recovery review page");
  size_t total=0;Json entries=Json::array();
  for(const char *kind:{"gestures","nativeControls"}) {
    const auto found=preserved.find(pluginRecoveryKey(kind));if(found==preserved.end())continue;
    const auto count=found->is_array()?found->size():size_t(1);
    if(count>std::numeric_limits<size_t>::max()-total)throw std::invalid_argument("Recovery collection is too large to review");
    for(size_t i=offset>total?std::min(offset-total,count):0;i<count&&entries.size()<limit;++i) {
      const bool collection=found->is_array();const auto &item=collection?found->at(i):*found;
      Json entry={{"kind",kind},{"index",collection?Json(i):Json()},{"headerSupported",false}};
      if(!collection)entry["error"]="Unsupported recovery collection; original project data retained";
      else {
        if(item.is_object()&&item.contains("take")&&item.at("take").is_string()&&!item.at("take").get_ref<const std::string &>().empty()&&
            item.at("take").get_ref<const std::string &>().size()<=80&&item.at("take").get_ref<const std::string &>().find('\0')==std::string::npos)
          entry["take"]=item.at("take");
        entry["headerSupported"]=item.is_object()&&item.contains("version")&&item.at("version").is_number_integer()&&
          item.at("version")==1&&item.value("reviewOnly",Json())==true;
      }
      // A supported header is not validation of the selected archive. Validate
      // only the selected entry, so another malformed/future take cannot hide it.
      entries.push_back(std::move(entry));
    }
    total+=count;
  }
  if(offset>total)throw std::invalid_argument("Recovery review offset exceeds collection size");
  return {{"reviewOnly",true},{"offset",offset},{"total",total},{"entries",std::move(entries)}};
}
inline nlohmann::json pluginInputRecoveryPage(const nlohmann::json &preserved,const std::string &kind,
    size_t index,size_t offset=0,size_t limit=128) {
  using Json=nlohmann::json;
  if(!preserved.is_object()||!limit||limit>512)throw std::invalid_argument("Invalid recovery input page");
  const auto found=preserved.find(pluginRecoveryKey(kind));
  if(found==preserved.end()||!found->is_array()||index>=found->size())throw std::invalid_argument("Recovery archive is unavailable; original project data retained");
  const auto &archive=found->at(index);Json summary,rows=Json::array();size_t total=0;
  if(kind=="nativeControls") {
    (void)readControlInputRecovery(archive);
    summary=Json::object();for(const auto &[key,value]:archive.items())if(key!="events")summary[key]=value;
    total=archive.at("events").size();
    if(offset>total)throw std::invalid_argument("Recovery input offset exceeds archive size");
    for(size_t i=offset;i<std::min(total,offset+limit);++i){auto row=archive.at("events")[i];row["eventIndex"]=i;rows.push_back(std::move(row));}
  }else {
    const auto decoded=readRecordedGestureRecovery(archive);
    summary=Json::object();for(const auto &[key,value]:archive.items())if(key!="targets"&&key!="baseline")summary[key]=value;
    summary["baselineCount"]=archive.at("baseline").size();summary["targets"]=Json::array();
    // Preserve archive target order and event order, including equal timestamps.
    // Frames remain the original 48 kHz song positions, never the current clock.
    for(size_t target=0;target<decoded.targets.size();++target) {
      const auto &entry=archive.at("targets")[target];Json descriptor=Json::object();for(const auto &[key,value]:entry.items())if(key!="events")descriptor[key]=value;
      descriptor["eventCount"]=entry.at("events").size();summary["targets"].push_back(std::move(descriptor));
      const auto &events=entry.at("events");const auto begin=offset>total?std::min(offset-total,events.size()):0;
      for(size_t i=begin;i<events.size()&&rows.size()<limit;++i){auto row=events[i];row["plugin"]=entry.at("plugin");row["parameter"]=entry.at("parameter");
        row["targetIndex"]=target;row["eventIndex"]=i;row["state"]=row.contains("frame")?"captured":"unmapped";rows.push_back(std::move(row));}
      total+=events.size();
    }
    if(offset>total)throw std::invalid_argument("Recovery input offset exceeds archive size");
  }
  return {{"reviewOnly",true},{"kind",kind},{"index",index},{"offset",offset},{"total",total},
    {"sampleRate",48000},{"summary",std::move(summary)},{"events",std::move(rows)}};
}
}
