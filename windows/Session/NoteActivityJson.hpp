#pragma once
#include "editor/NoteRouting.hpp"
#include <nlohmann/json.hpp>
namespace ScreamSeq {
inline nlohmann::json noteActivityJson(const Tracker::NoteActivitySnapshot &snapshot,bool active) {
  using Json=nlohmann::json;Json routes=Json::array();
  for(const auto &r:snapshot.routes)routes.push_back({{"token",std::to_string(r.token)},{"copy",std::to_string(r.copy)},{"route",r.implicit?Json(nullptr):Json("n"+std::to_string(r.route))},{"implicit",r.implicit},{"sourceKind",r.sourceKind==Tracker::NoteSourceKind::Channel?"channel":"instrument"},{"source","n"+std::to_string(r.source)},{"plugin",r.plugin},{"midiChannel",r.midiChannel},{"current",r.current},{"member",r.member},{"adoptedGeneration",r.adoptedGeneration},{"events",r.events},{"noteOns",r.noteOns},{"noteOffs",r.noteOffs},{"failures",r.failures},{"routingReleases",r.routingReleases},{"lastFrame",r.lastFrame},{"heldNotes",r.heldNotes},{"heldPedals",r.heldPedals}});
  return {{"available",snapshot.available},{"active",active},{"engine",snapshot.engine?Json(std::to_string(snapshot.engine)):Json(nullptr)},{"sampleRate",snapshot.sampleRate},{"requestedGeneration",snapshot.requestedGeneration},{"adoptedGeneration",snapshot.adoptedGeneration},{"pending",snapshot.requestedGeneration!=snapshot.adoptedGeneration},{"fresh",snapshot.fresh},{"routes",std::move(routes)}};
}
}
