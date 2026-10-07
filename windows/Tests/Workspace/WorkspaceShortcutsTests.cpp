#include "../../App/WorkspaceShortcuts.hpp"
#include <fstream>
#include <iostream>

using ScreamSeq::Api::ApiError;
using ScreamSeq::WorkspaceShortcuts;
using Shortcuts = WorkspaceShortcuts;
namespace {
void check(bool condition, const char *message) {
  if(!condition) throw std::runtime_error(message);
}
template<typename Action> void rejected(Action action, int code = -32602) {
  try { action(); }
  catch(const ApiError &error) { check(error.code == code, "rejection used the wrong API error code"); return; }
  throw std::runtime_error("invalid shortcut change was accepted");
}
template<typename Action> void failed(Action action) {
  try { action(); } catch(const std::exception &) { return; }
  throw std::runtime_error("expected preference write failure");
}
std::vector<Shortcuts::Definition> definitions() {
  return {{"palette", {"ctrl+k"}}, {"open", {"ctrl+o"}}, {"save", {"ctrl+s"}},
    {"play", {"space"}}, {"stop", {"escape"}}, {"follow", {"f"}}, {"next", {"f6"}},
    {"chord", {"ctrl+alt+q", "m"}}, {"graph", {}}, {"mixer", {}}, {"other", {}}};
}
Shortcuts::Decision press(Shortcuts &shortcuts, const char *stroke, uint64_t now = 0,
    uint64_t context = 7, bool repeat = false) {
  return shortcuts.handle(Shortcuts::parseStroke(stroke), repeat, context, now);
}
struct Scratch {
  std::filesystem::path root, path;
  std::wstring name;
  Scratch() {
    root = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    name = L"screamseq-shortcut-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    path = root / name;
    check(std::filesystem::create_directory(path), "cannot reserve shortcut scratch directory");
  }
  ~Scratch() {
    std::error_code ignored;
    // Only remove the exact scratch directory reserved by this test instance.
    if(path.is_absolute() && path.parent_path() == root && path.filename() == name)
      std::filesystem::remove_all(path, ignored);
  }
};
std::string contents(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(bytes.data(), std::streamsize(bytes.size()));
  check(bool(file), "cannot write shortcut fixture");
}
void parserAndDefaults() {
  check(Shortcuts::parseStroke("SHIFT+Alt+CTRL+K").encoded() == "ctrl+alt+shift+k", "modifiers did not normalize");
  check(Shortcuts::parseStroke("ctrl+Enter").encoded() == "ctrl+return", "Enter alias did not normalize");
  check(Shortcuts::parseStroke("ESC").encoded() == "escape", "Escape alias did not normalize");
  check(Shortcuts::parseStroke("ctrl+pgup").encoded() == "ctrl+pageup", "Page Up alias did not normalize");
  check(Shortcuts::parseStroke("ctrl+pgdn").encoded() == "ctrl+pagedown", "Page Down alias did not normalize");
  for(const auto *key : {"space", "tab", "return", "escape", "backspace", "delete", "insert", "home", "end",
      "left", "right", "up", "down", "pageup", "pagedown", "f1", "f12", "f24", "plus", "0", "[", "/"})
    check(!Shortcuts::parseStroke(key).key.empty(), "valid key was rejected");
  for(const auto &key : std::vector<std::string>{"", "ctrl+", "+k", "ctrl++k", "ctrl+ctrl+k", "alt+ALT+k",
      "cmd+k", "opt+k", "win+k", "super+k", " ctrl+k", "ctrl+ k", "ctrl+k ", "ctrl+spacebar", "f0", "f01", "f25",
      "twoletters", std::string("ctrl+k\0x", 8), "ctrl+\n", "ctrl+\xC3\xA9", std::string(65, 'a')})
    rejected([&] { Shortcuts::parseStroke(key); });
  Shortcuts shortcuts(definitions());
  check(shortcuts.keys("play") == std::vector<std::string>{"space"}, "trusted unmodified default rejected");
  check(shortcuts.keys("follow") == std::vector<std::string>{"f"}, "note-like trusted default rejected");
  check(shortcuts.set("play", {"ctrl+alt+p"}), "custom playback shortcut was not applied");
  check(shortcuts.reset("play") && shortcuts.keys("play") == std::vector<std::string>{"space"}, "reset did not restore unmodified default");
  check(!shortcuts.reset("play"), "default reset was not a no-op");
  rejected([&] { shortcuts.set("graph", {"g", "m"}); });
  rejected([&] { shortcuts.set("graph", {"shift+g", "m"}); });
  rejected([&] { shortcuts.set("graph", {"ctrl+g", "a", "b", "c", "d"}); });
  rejected([&] { shortcuts.set("missing", {"ctrl+g"}); });
  rejected([&] { shortcuts.reset("missing"); });
  rejected([&] { shortcuts.keys("missing"); });
  rejected([] { Shortcuts duplicate({{"id", {"ctrl+k"}}, {"id", {"ctrl+o"}}}); });
  rejected([] { Shortcuts collision({{"one", {"ctrl+k"}}, {"two", {"ctrl+k", "m"}}}); });
  rejected([] { Shortcuts empty({{"", {}}}); });
  rejected([] { Shortcuts invalid({{std::string("bad\0id", 6), {}}}); });
  rejected([] { Shortcuts invalid({{"\xC0\xAF", {}}}); });
  rejected([] { Shortcuts invalid({{std::string(1025, 'a'), {}}}); });
  std::vector<Shortcuts::Definition> tooMany;
  for(size_t i = 0; i <= Shortcuts::maximumCommands; ++i) tooMany.push_back({"id" + std::to_string(i), {}});
  rejected([&] { Shortcuts excessive(tooMany); });
}
void reservedKeysAndDefaultRoundTrips() {
  Shortcuts shortcuts(definitions());
  press(shortcuts, "ctrl+alt+q", 100);
  const auto hint = shortcuts.hint();
  const auto deadline = shortcuts.deadline();
  for(const auto &definition : definitions())
    check(!shortcuts.set(definition.id, shortcuts.defaults(definition.id)), "exact default round-trip was not a no-op");
  check(shortcuts.hint() == hint && shortcuts.deadline() == deadline, "default round-trip cancelled a sequence");
  for(const auto *reserved : {"ctrl+alt+delete", "ctrl+alt+shift+delete", "ctrl+escape", "ctrl+shift+escape",
      "ctrl+alt+escape", "ctrl+alt+shift+escape", "alt+escape", "alt+shift+escape", "alt+tab", "alt+shift+tab", "ctrl+alt+tab", "ctrl+alt+shift+tab"}) {
    rejected([&] { Shortcuts::parseStroke(reserved); });
    for(size_t position = 0; position < 4; ++position) {
      std::vector<std::string> keys{"ctrl+alt+g", "a", "b", "c"};
      keys[position] = reserved;
      rejected([&] { shortcuts.set("graph", keys); });
      rejected([&] { Shortcuts invalid({{"reserved", keys}}); });
    }
  }
  for(const auto *escape : {"escape", "esc", "shift+escape"})
    for(size_t position = 1; position < 4; ++position) {
      std::vector<std::string> keys{"ctrl+alt+g", "a", "b", "c"};keys[position] = escape;
      rejected([&] { shortcuts.set("graph", keys); });
      rejected([&] { Shortcuts invalid({{"escape-continuation", keys}}); });
    }
  check(shortcuts.keys("graph").empty() && shortcuts.hint() == hint && shortcuts.deadline() == deadline,
    "reserved or Escape continuation rejection changed bindings or pending input");
  for(const auto *available : {"ctrl+delete", "alt+delete", "ctrl+tab", "shift+escape", "alt+f4", "alt+space"})
    check(!Shortcuts::parseStroke(available).key.empty(), "app-available key was rejected as OS-reserved");

  for(const auto *id : {"play", "stop", "follow", "next", "palette", "chord", "graph"}) {
    shortcuts.set(id, {"ctrl+alt+h"});
    check(shortcuts.set(id, shortcuts.defaults(id)) && !shortcuts.overridden(id), "setting exact default did not restore it and remove override");
    check(shortcuts.keys(id) == shortcuts.defaults(id), "exact default restoration lost its normalized keys");
  }
  shortcuts.set("play", {});
  rejected([&] { shortcuts.set("graph", {"space"}); });
  check(shortcuts.set("play", {"SPACE"}) && !shortcuts.overridden("play"), "normalized unmodified default was rejected");
  Shortcuts trustedSequence({{"trusted", {"f6", "m"}}, {"other", {}}});
  trustedSequence.set("trusted", {"ctrl+alt+h"});
  check(trustedSequence.set("trusted", {"F6", "M"}), "exact trusted multi-stroke default was not accepted");
  rejected([&] { trustedSequence.set("trusted", {"f6", "n"}); });
  rejected([&] { trustedSequence.set("other", {"f6", "m"}); });

  shortcuts.set("palette", {});shortcuts.set("other", {"ctrl+k"});
  rejected([&] { shortcuts.set("palette", shortcuts.defaults("palette")); });
  check(shortcuts.keys("palette").empty() && shortcuts.keys("other") == std::vector<std::string>{"ctrl+k"},
    "default restoration bypassed conflict validation");
}
void conflictsAndAtomicity() {
  Shortcuts shortcuts(definitions());
  shortcuts.set("graph", {"ctrl+alt+g", "m"});
  shortcuts.set("mixer", {"ctrl+alt+g", "p"}); // A shared prefix can have distinct completions.
  press(shortcuts, "ctrl+alt+g", 100);
  const auto hint = shortcuts.hint();
  const auto deadline = shortcuts.deadline();
  rejected([&] { shortcuts.set("other", {"ctrl+alt+g"}); });
  rejected([&] { shortcuts.set("other", {"ctrl+alt+g", "m"}); });
  rejected([&] { shortcuts.set("other", {"ctrl+alt+g", "m", "x"}); });
  rejected([&] { shortcuts.set("graph", {"ctrl+k", "g"}); });
  check(shortcuts.keys("other").empty() && shortcuts.keys("graph") == std::vector<std::string>({"ctrl+alt+g", "m"}), "conflict changed bindings");
  check(shortcuts.hint() == hint && shortcuts.deadline() == deadline, "rejected edit cancelled a pending sequence");
  check(!shortcuts.set("graph", {"ALT+CTRL+G", "M"}), "equivalent normalized binding was not a no-op");
  check(shortcuts.hint() == hint && !shortcuts.reset("other") && shortcuts.pending(), "no-op changed pending input");
  check(press(shortcuts, "m", 200).command == "graph", "graph completion did not survive rejected changes");
  shortcuts.set("palette", {});
  shortcuts.set("other", {"ctrl+k"});
  rejected([&] { shortcuts.reset("palette"); });
  check(shortcuts.keys("palette").empty() && shortcuts.keys("other") == std::vector<std::string>{"ctrl+k"}, "failed reset stole another binding");
  shortcuts.reset("other");
  check(shortcuts.reset("palette") && shortcuts.keys("palette") == shortcuts.defaults("palette"), "reset did not restore exact default");
  shortcuts.set("play", {});
  check(shortcuts.overridden("play") && !press(shortcuts, "space").consumed, "clear did not disable trusted default");
  check(!shortcuts.set("play", {}), "clearing empty binding was not a no-op");
  shortcuts.reset("play");
  check(press(shortcuts, "space").command == "play", "reset did not reactivate default");
}
void sequenceLifecycle() {
  Shortcuts shortcuts(definitions());
  shortcuts.set("graph", {"ctrl+alt+g", "m"});
  shortcuts.set("mixer", {"ctrl+alt+g", "p"});
  shortcuts.set("other", {"ctrl+alt+x", "a", "b", "c"});
  check(!press(shortcuts, "z").consumed, "unbound input was consumed");
  check(press(shortcuts, "ctrl+k").command == "palette", "single binding did not resolve");
  check(press(shortcuts, "ctrl+k", 10, 7, true).command.empty(), "repeat executed a single command");
  check(press(shortcuts, "ctrl+alt+g", 20, 7, true).consumed && !shortcuts.pending(), "repeat started a sequence");
  auto result = press(shortcuts, "ctrl+alt+g", 100);
  check(result.consumed && result.command.empty() && shortcuts.pending() && shortcuts.deadline() == 1600, "sequence prefix was not retained");
  const auto hint = shortcuts.hint();
  press(shortcuts, "escape", 150, 7, true);
  check(shortcuts.pending() && shortcuts.deadline() == 1600, "repeat cancelled or prolonged a sequence");
  press(shortcuts, "m", 200, 7, true);
  check(shortcuts.pending() && shortcuts.hint() == hint, "repeat completed a sequence");
  check(press(shortcuts, "p", 300).command == "mixer" && !shortcuts.pending(), "shared-prefix completion failed");
  press(shortcuts, "ctrl+alt+g", 400);
  result = press(shortcuts, "z", 401);
  check(result.consumed && result.command.empty() && !shortcuts.pending(), "unmatched suffix leaked into native/note input");
  press(shortcuts, "ctrl+alt+g", 500);
  result = press(shortcuts, "escape", 501);
  check(result.consumed && result.command.empty() && !shortcuts.pending(), "Escape ran Stop instead of cancelling prefix");
  check(press(shortcuts, "escape", 502).command == "stop", "Escape default did not run with no prefix");
  press(shortcuts, "ctrl+alt+g", 1000);
  check(!shortcuts.expire(7, 2499) && shortcuts.expire(7, 2500), "1.5-second deadline was wrong");
  check(!press(shortcuts, "m", 2501).consumed, "expired prefix captured later input");
  press(shortcuts, "ctrl+alt+g", 3000);
  check(!press(shortcuts, "m", 3001, 8).consumed && !shortcuts.pending(), "focus/context change completed old sequence");
  press(shortcuts, "ctrl+alt+x", 4000);
  press(shortcuts, "a", 5000);
  press(shortcuts, "b", 6000);
  check(press(shortcuts, "c", 7000).command == "other", "four-stroke sequence or deadline renewal failed");
  press(shortcuts, "ctrl+alt+g", 8000);
  check(shortcuts.cancel() && !shortcuts.cancel() && shortcuts.hint().empty() && shortcuts.deadline() == 0, "explicit cancellation retained prefix state");
  press(shortcuts, "ctrl+alt+g", 9000);
  rejected([&] { shortcuts.handle({"k", 8}, false, 7, 9001); });
  check(shortcuts.pending(), "invalid event changed pending input");
  shortcuts.set("graph", {"ctrl+alt+h", "m"});
  check(!shortcuts.pending(), "successful rebind retained an obsolete prefix");
}
void persistenceAndConflicts(const std::filesystem::path &base) {
  const auto path = base / "nested" / "shortcuts.json";
  Shortcuts first(definitions()), second(definitions());
  check(first.load(path) && second.load(path) && !std::filesystem::exists(path), "missing-file load created preferences");
  first.set("graph", {"ctrl+alt+g", "m"});
  const auto initial = contents(path);
  press(second, "ctrl+alt+q", 100);
  rejected([&] { second.set("mixer", {"ctrl+alt+p"}); }, -32001);
  check(second.keys("mixer").empty() && second.pending() && contents(path) == initial, "stale save modified disk, bindings or prefix");
  check(second.reload() && second.keys("graph") == first.keys("graph"), "CAS reload did not load other session");
  second.set("mixer", {"ctrl+alt+p"});
  const auto newest = contents(path);
  rejected([&] { first.reset("graph"); }, -32001);
  check(first.overridden("graph") && contents(path) == newest, "stale reset overwrote another session");
  check(first.reload(), "preferences failed to reopen");
  const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  check(handle != INVALID_HANDLE_VALUE, "cannot lock shortcut fixture");
  try {
    press(first, "ctrl+alt+g", 300);
    const auto hint = first.hint();
    failed([&] { first.set("graph", {"ctrl+alt+h", "m"}); });
    check(first.keys("graph") == std::vector<std::string>({"ctrl+alt+g", "m"}) && first.hint() == hint && contents(path) == newest,
      "failed atomic replacement changed live or disk state");
    failed([&] { first.reset("graph"); });
    check(first.overridden("graph") && first.pending() && contents(path) == newest, "failed reset changed live or disk state");
    check(!first.set("graph", {"ctrl+alt+g", "m"}), "no-op attempted a write to locked storage");
    check(first.pending(), "persistent no-op cancelled input");
  } catch(...) { CloseHandle(handle); throw; }
  CloseHandle(handle);
  for(const auto &entry : std::filesystem::directory_iterator(path.parent_path()))
    check(entry.path() == path, "failed save left a staging file");
  check(first.reset("graph") && !first.pending(), "reset after failed save did not succeed");
  Shortcuts reopened(definitions());
  check(reopened.load(path) && reopened.keys("graph").empty() && reopened.keys("mixer") == first.keys("mixer"), "reset did not persist or dropped unrelated binding");
  const auto valid = contents(path);
  write(path, valid + "\n");
  rejected([&] { first.set("other", {"ctrl+alt+x"}); }, -32001);
  check(contents(path) == valid + "\n", "external file edit was overwritten");
}
void invalidReloadAndInspection(const std::filesystem::path &base) {
  Shortcuts shortcuts(definitions());
  check(shortcuts.load({}), "inspection memory mode did not load");
  shortcuts.set("graph", {"ctrl+alt+g", "m"});
  check(shortcuts.reload() && shortcuts.overridden("graph"), "inspection reload erased in-memory override");
  const auto path = base / "reload.json";
  check(shortcuts.load(path), "new preference path failed");
  shortcuts.set("graph", {"ctrl+alt+g", "m"});
  const auto valid = contents(path);
  for(const auto &bytes : std::vector<std::string>{"{", "{}", "[]", R"({"version":2,"overrides":{}})",
      R"({"version":1.0,"overrides":{}})", R"({"version":true,"overrides":{}})",
      R"({"version":1,"overrides":{},"extra":1})", R"({"version":1,"overrides":{},"overrides":{}})",
      R"({"version":1,"overrides":{"graph":[],"graph":["ctrl+g"]}})",
      R"({"version":1,"overrides":{"missing":["ctrl+g"]}})", R"({"version":1,"overrides":{"graph":[true]}})",
      R"({"version":1,"overrides":{"graph":["g","m"]}})", R"({"version":1,"overrides":{"graph":["ctrl+k"]}})",
      R"({"version":1,"overrides":{"graph":["ctrl+alt+delete"]}})", R"({"version":1,"overrides":{"graph":["ctrl+g","alt+tab"]}})",
      R"({"version":1,"overrides":{"graph":["ctrl+g","a","ctrl+shift+esc"]}})",
      R"({"version":1,"overrides":{"graph":["ctrl+g","a","b","shift+escape"]}})",
      R"({"version":1,"overrides":{"graph":["ctrl+g","a","b","c","d"]}})",
      R"({"version":1,"overrides":{"graph":{"nested":{"too":{"far":{"away":[]}}}}}}})"}) {
    write(path, bytes);
    press(shortcuts, "ctrl+alt+g", 100);
    const auto hint = shortcuts.hint();
    check(!shortcuts.reload() && !shortcuts.diagnostic().empty(), "malformed preferences were accepted");
    check(shortcuts.keys("graph") == std::vector<std::string>({"ctrl+alt+g", "m"}) && shortcuts.hint() == hint,
      "invalid reload discarded active binding or pending input");
    rejected([&] { shortcuts.set("other", {"ctrl+alt+x"}); }, -32001);
    check(contents(path) == bytes && shortcuts.keys("other").empty(), "invalid file was silently overwritten");
    write(path, valid);
    check(shortcuts.reload() && shortcuts.diagnostic().empty(), "valid reload did not recover preferences");
    shortcuts.cancel();
  }
  write(path, std::string(Shortcuts::maximumBytes + 1, ' '));
  check(!shortcuts.reload() && shortcuts.overridden("graph"), "oversized reload replaced active preferences");
  write(path, valid);
  check(shortcuts.reload(), "bounded reload did not recover");
  write(path, R"({"version":1,"overrides":{"play":["SPACE"],"stop":["ESC"],"follow":["F"],"next":["F6"],"chord":["ALT+CTRL+Q","M"],"graph":[]}})");
  check(shortcuts.reload(), "persisted exact trusted defaults were rejected");
  for(const auto *id : {"play", "stop", "follow", "next", "chord", "graph"})
    check(!shortcuts.overridden(id) && shortcuts.keys(id) == shortcuts.defaults(id), "redundant stored default was not normalized away");
  check(std::filesystem::remove(path), "cannot remove owned preference fixture");
  check(shortcuts.reload() && !shortcuts.overridden("graph") && shortcuts.keys("graph").empty(), "missing-file reload did not restore defaults");
  check(!shortcuts.load("relative-shortcuts.json"), "relative storage path was accepted");
}
} // namespace
int main() {
  try {
    Scratch scratch;
    parserAndDefaults();
    reservedKeysAndDefaultRoundTrips();
    conflictsAndAtomicity();
    sequenceLifecycle();
    persistenceAndConflicts(scratch.path);
    invalidReloadAndInspection(scratch.path);
    std::cout << "Workspace shortcut parser, conflicts, sequences, atomic persistence and reload guards passed\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
