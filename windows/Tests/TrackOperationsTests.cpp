#include "common/stdafx.h"
#include "windows/Session/TrackOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeProject.hpp"
#include <iostream>

using namespace Tracker;
using namespace ScreamSeq;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void rejected(const std::function<void()> &call, int expected = -32602) {
  try { call(); } catch (const Api::ApiError &error) { check(error.code == expected, "Wrong track API error code"); return; }
  throw std::runtime_error("Invalid track request was accepted");
}
void validationAndPublication() {
  auto doc = Document::demo(); unsigned stops = 0, preparations = 0, publications = 0; bool refuse = false;
  TrackHostHooks hooks;
  hooks.prepareColumnMutes = [&](const NativeSong &, const NativeSong &) {
    ++preparations;
    return [&] { if (refuse) throw Api::ApiError(-32002, "Owned publication refusal"); ++publications; };
  };
  TrackOperations api(*doc, [&] { ++stops; }, hooks);
  const auto layout = api.invoke("track.get", Json::object());
  const auto column = layout.at("columns")[0].at("id");
  const auto original = doc->native(); const auto revision = doc->revision; const auto undo = doc->historyHead(false);
  const auto dry = api.invoke("track.group", {{"channels", {0, 1}}, {"name", "Chord"}, {"dryRun", true}});
  check(dry.at("wouldChange") == true && dry.at("appendedColumns") == 0 && dry.at("preservesRoutingOnUngroup") == true &&
    doc->native() == original && doc->revision == revision && stops == 0, "Dry group changed the document or transport");
  for (const auto value : {Json(true), Json(-1), Json(1.5), Json(128), Json("2")})
    rejected([&] { api.invoke("track.create", {{"columns", value}}); });
  for (const auto channels : {Json(), Json::object(), Json::array({false}), Json::array({0, 2}), Json::array({1, 0})})
    rejected([&] { api.invoke("track.group", {{"channels", channels}}); });
  rejected([&] { api.invoke("track.group", {{"channels", {0, 1}}, {"extra", 1}}); });
  rejected([&] { api.invoke("track.get", {{"extra", 1}}); });
  rejected([&] { api.invoke("track.column.set", {{"column", column}, {"mute", 1}}); });
  for (const auto id : {"n0", "n01", "n-1", "x1", "n99999999999999999999999999999999"})
    rejected([&] { api.invoke("track.column.set", {{"column", id}, {"mute", true}}); });
  rejected([&] { api.invoke("track.unknown", Json::object()); }, -32601);
  check(doc->native() == original && doc->revision == revision && doc->historyHead(false) == undo && stops == 0,
    "Invalid request changed state, history or transport");
  refuse = true;
  rejected([&] { api.invoke("track.column.set", {{"column", column}, {"mute", true}}); }, -32002);
  check(preparations == 1 && publications == 0 && doc->native() == original && doc->revision == revision &&
    doc->historyHead(false) == undo, "Publication refusal partly committed persistent mute");
  refuse = false;
  api.invoke("track.column.set", {{"column", column}, {"mute", true}, {"dryRun", true}});
  check(preparations == 1 && doc->native() == original, "Dry mute prepared or published an audible change");
  api.invoke("track.column.set", {{"column", column}, {"mute", true}});
  check(publications == 1 && stops == 0 && api.invoke("track.get", Json::object()).at("columns")[0].at("mute") == true,
    "Column mute did not use the admitted publication callback");
  const auto changed = doc->revision;
  check(api.invoke("track.column.set", {{"column", column}, {"mute", true}}).at("wouldChange") == false &&
    publications == 1 && doc->revision == changed, "No-op mute allocated history or republished");
  doc->undo(); check(doc->native() == original, "Persistent mute needed more than one Undo");
}
void persistence() {
  for (auto type : {OpenMPT::MOD_TYPE_MOD, OpenMPT::MOD_TYPE_XM, OpenMPT::MOD_TYPE_S3M, OpenMPT::MOD_TYPE_IT, OpenMPT::MOD_TYPE_MPT}) {
    auto doc = Document::demo(type); TrackOperations api(*doc);
    const auto before = api.invoke("track.get", Json::object());
    const auto group = api.invoke("track.group", {{"channels", {0, 1}}, {"name", "Chord"}});
    check(group.at("layout").at("noteTracks")[0].at("channels") == Json::array({0, 1}), "Group layout lost channel coordinates");
    api.invoke("track.column.set", {{"column", before.at("columns")[1].at("id")}, {"mute", true}});
    const auto created = api.invoke("track.create", {{"columns", 2.0}, {"name", "New track"}});
    check(created.at("appendedColumns") == 2 && created.at("layout") == api.invoke("track.get", Json::object()),
      "Create result projection differs from the adopted layout");
    api.invoke("track.ungroup", {{"track", group.at("affectedID")}});
    const auto saved = api.invoke("track.get", Json::object());
    auto state = Project::newProjectState(*doc);
    auto opened = Project::openNativeProjectBytes(Project::serializeNativeProject(*doc, state));
    TrackOperations reopened(*opened.document);
    check(reopened.invoke("track.get", Json::object()) == saved && opened.document->native() == doc->native(),
      "Native codec roundtrip lost grouping, mute, routing or stable identities");
  }
}
}
int main() {
  try { validationAndPublication(); persistence(); std::cout << "PASS Windows track adapter contracts and native codec roundtrip\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
