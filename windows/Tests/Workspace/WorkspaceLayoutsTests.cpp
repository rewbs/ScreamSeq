#include "../../App/WorkspaceLayouts.hpp"
#include <fstream>
#include <iostream>

using ScreamSeq::Api::ApiError;
using ScreamSeq::Api::Json;
using ScreamSeq::WorkspaceLayouts;
namespace {
void check(bool condition, const char *message) {
  if(!condition) throw std::runtime_error(message);
}
template<typename Action> void rejected(Action action, const char *message) {
  try { action(); } catch(const std::exception &) { return; }
  throw std::runtime_error(message);
}
template<typename Action> void conflict(Action action) {
  try { action(); } catch(const ApiError &error) {
    check(error.code == -32001, "stale catalogue has the wrong error code");
    return;
  }
  throw std::runtime_error("stale catalogue silently overwrote another session");
}
struct Scratch {
  std::filesystem::path path;
  Scratch() {
    const auto stem = L"screamseq-layout-tests-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(GetTickCount64());
    path = std::filesystem::temp_directory_path() / stem;
    check(std::filesystem::create_directory(path), "cannot reserve test scratch directory");
  }
  ~Scratch() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
Json configuration(int width = 440) {
  return {{"layout", "Compose"}, {"active", "notes"}, {"rightWidth", width},
    {"lowerHeight", 300}, {"lowerEditor", "graph"}, {"lowerVisible", true}, {"hidden", {false, false}}};
}
std::string contents(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  check(bool(stream), "cannot write test fixture");
}
void memoryAndNames() {
  WorkspaceLayouts layouts;
  check(layouts.load({}) && layouts.list().empty(), "in-memory catalogue did not start empty");
  const std::string unicode = "Mix \xF0\x9F\x8E\xB9 \xC3\xA9";
  layouts.save(unicode, configuration());
  check(layouts.get(unicode) && *layouts.get(unicode) == configuration(), "Unicode name lost configuration");
  check(layouts.reload() && layouts.get(unicode) && *layouts.get(unicode) == configuration(),
    "refresh erased the in-memory inspection catalogue");
  check(!layouts.get("missing") && !layouts.remove("missing"), "missing layout unexpectedly exists");
  std::string sixtyFour;
  for(unsigned i = 0; i < 64; ++i) sixtyFour += "\xF0\x9F\x8E\xB9";
  WorkspaceLayouts::validateName(sixtyFour);
  rejected([&] { WorkspaceLayouts::validateName(sixtyFour + "x"); }, "accepted 65 Unicode characters");
  for(const auto &invalid : std::vector<std::string>{"", " padded", "padded ", "line\nfeed", "tab\tname",
      std::string("nul\0name", 8), "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x80\xA8", "\xC2\xA0"})
    rejected([&] { layouts.save(invalid, configuration()); }, "accepted an invalid layout name");
  check(layouts.list().size() == 1, "invalid name modified catalogue");
  rejected([&] { layouts.save("array", Json::array()); }, "accepted non-object configuration");
  layouts.save("Mix", configuration());
  layouts.save("mix", configuration(500));
  check(layouts.get("Mix") && layouts.get("mix"), "case-sensitive names were merged");
  for(size_t i = layouts.list().size(); i < WorkspaceLayouts::maximumLayouts; ++i)
    layouts.save("Layout " + std::to_string(i), configuration());
  rejected([&] { layouts.save("overflow", configuration()); }, "accepted a 25th layout");
  layouts.save("Mix", configuration(520));
  check(layouts.get("Mix")->at("rightWidth") == 520, "cannot replace existing layout at capacity");
  check(layouts.remove("Mix"), "cannot remove existing layout");
  layouts.save("replacement", configuration());
}
void persistenceAndConflicts(const std::filesystem::path &base) {
  const auto path = base / "nested" / "layouts.json";
  WorkspaceLayouts first, second;
  check(first.load(path) && second.load(path), "missing layout storage failed to load");
  check(!std::filesystem::exists(path), "read-only first load created storage");
  first.save("Compose", configuration());
  const auto initial = contents(path);
  conflict([&] { second.save("Mix", configuration(500)); });
  check(second.list().empty() && contents(path) == initial, "stale initial save changed disk or memory");
  check(second.reload(), "saved layout failed to reload after conflict");
  second.save("Mix", configuration(500));
  WorkspaceLayouts observer;
  check(observer.load(path) && observer.get("Compose") && observer.get("Mix"),
    "reload and retry dropped the other session's layout");
  const auto next = contents(path);
  conflict([&] { first.remove("Compose"); });
  check(first.get("Compose") && contents(path) == next, "stale delete changed disk or memory");
  check(first.reload() && first.list().size() == 2, "reload omitted another session's layout");
  check(*first.get("Mix") == configuration(500), "reopened configuration changed");
  const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  check(handle != INVALID_HANDLE_VALUE, "cannot lock destination fixture");
  try {
    rejected([&] { first.save("Mix", configuration(600)); }, "locked destination unexpectedly replaced");
    check(*first.get("Mix") == configuration(500) && contents(path) == next, "failed save changed disk or memory");
    rejected([&] { first.remove("Mix"); }, "locked destination unexpectedly deleted");
    check(first.get("Mix") && contents(path) == next, "failed delete changed disk or memory");
  } catch(...) { CloseHandle(handle); throw; }
  CloseHandle(handle);
  for(const auto &entry : std::filesystem::directory_iterator(path.parent_path()))
    check(entry.path() == path, "failed atomic save left a staging file");
  check(first.remove("Mix"), "retry after failed delete did not succeed");
  check(second.reload() && second.list() == std::vector<std::string>{"Compose"}, "deletion was not persisted");
  write(path, initial + "\n");
  conflict([&] { second.save("Mix", configuration()); });
  check(contents(path) == initial + "\n", "external edit was not preserved");
}
void malformedAndBounded(const std::filesystem::path &base) {
  const auto path = base / "invalid.json";
  WorkspaceLayouts layouts;
  for(const auto &bytes : std::vector<std::string>{"{", "{}", "[]",
      R"({"version":2,"layouts":{}})", R"({"version":1.0,"layouts":{}})",
      R"({"version":1,"layouts":{},"extra":1})", R"({"version":1,"layouts":{},"layouts":{}})",
      R"({"version":1,"layouts":{"bad ":{}}})", R"({"version":1,"layouts":{"A":[],"B":{}}})",
      R"({"version":1,"layouts":{"A":{"layout":"Compose","layout":"Mix"}}})"}) {
    write(path, bytes);
    check(!layouts.load(path) && !layouts.diagnostic().empty(), "invalid storage was silently accepted");
    check(layouts.list().empty() && contents(path) == bytes, "invalid storage was changed or partially loaded");
  }
  layouts.save("Recovered", configuration());
  check(layouts.diagnostic().empty() && layouts.load(path), "explicit save failed to replace safely read corruption");
  const auto valid = contents(path);
  auto oversized = configuration();
  oversized["oversized"] = std::string(WorkspaceLayouts::maximumBytes, 'x');
  rejected([&] { layouts.save("Too big", oversized); }, "oversized save was accepted");
  auto nested = Json::object();
  for(unsigned i = 0; i < 20; ++i) nested = {{"nested", nested}};
  rejected([&] { layouts.save("Too deep", nested); }, "deeply nested save was accepted");
  auto many = configuration();
  many["many"] = Json::array();
  for(unsigned i = 0; i < 40000; ++i) many["many"].push_back(0);
  rejected([&] { layouts.save("Too many", many); }, "structurally oversized save was accepted");
  check(layouts.list() == std::vector<std::string>{"Recovered"} && contents(path) == valid,
    "failed bounded save changed catalogue");
  write(path, std::string(WorkspaceLayouts::maximumBytes + 1, ' '));
  check(!layouts.load(path), "oversized file was accepted");
  conflict([&] { layouts.save("Recovered", configuration()); });
  check(contents(path).size() == WorkspaceLayouts::maximumBytes + 1, "unreadable storage was overwritten");
}
} // namespace
int main() {
  try {
    Scratch scratch;
    memoryAndNames();
    persistenceAndConflicts(scratch.path);
    malformedAndBounded(scratch.path);
    std::cout << "Workspace layout storage: Unicode bounds, atomic rollback, corruption and conflicts passed\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
