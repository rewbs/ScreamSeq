#include "../Project/RecoveryStore.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

using ScreamSeq::Project::RecoveryEntry;
using ScreamSeq::Project::RecoveryStore;
namespace fs = std::filesystem;
namespace {
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
template<typename Action> void rejected(Action action, const char *message) {
  try { action(); } catch(const std::exception &) { return; }
  throw std::runtime_error(message);
}
struct Handle {
  HANDLE value;
  explicit Handle(HANDLE value) : value(value) { check(value != INVALID_HANDLE_VALUE, "cannot open test fixture"); }
  ~Handle() { CloseHandle(value); }
  Handle(const Handle &) = delete;
};
struct Scratch {
  fs::path path = fs::temp_directory_path() / ("screamseq-recovery-tests-" + RecoveryStore::newSessionID());
  Scratch() { check(fs::create_directory(path), "cannot reserve scratch directory"); }
  ~Scratch() { std::error_code ignored; fs::remove_all(path, ignored); }
};
const std::vector<std::byte> payload{std::byte{0x00}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff}};
void write(const fs::path &path, const std::string &value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  check(bool(output), "cannot write fixture");
}
std::string contents(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "cannot read fixture");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
fs::path sidecar(fs::path path) { path += L".json"; return path; }
std::string identifier(const std::string &session, std::string time = "1000000") {
  return session + "-" + time + "-" + RecoveryStore::newSessionID() + ".screamseq";
}
void noPendingFiles(const fs::path &directory) {
  for(const auto &entry : fs::directory_iterator(directory))
    check(!entry.path().filename().wstring().starts_with(L".pending-"), "failed write left a staging file");
}
void makeSizedFile(const fs::path &path, std::size_t size) {
  Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  DWORD returned = 0;
  check(DeviceIoControl(file.value, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr), "cannot make sparse fixture");
  LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>(size);
  check(SetFilePointerEx(file.value, offset, nullptr, FILE_BEGIN) && SetEndOfFile(file.value), "cannot size sparse fixture");
}
void junction(const fs::path &path, const fs::path &target) {
  check(fs::create_directory(path), "cannot reserve junction fixture");
  Handle directory(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  struct MountPoint {
    DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    WORD length = 0, reserved = 0, substituteOffset = 0, substituteLength = 0, printOffset = 0, printLength = 0;
    wchar_t names[1024]{};
  } buffer;
  const auto substitute = L"\\??\\" + target.native(), print = target.native();
  check(substitute.size() + print.size() + 2 <= std::size(buffer.names), "junction fixture path too long");
  std::copy(substitute.begin(), substitute.end(), buffer.names);
  std::copy(print.begin(), print.end(), buffer.names + substitute.size() + 1);
  buffer.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
  buffer.printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
  buffer.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
  buffer.length = static_cast<WORD>(8 + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
  DWORD returned = 0;
  check(DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, &buffer, 8 + buffer.length,
                        nullptr, 0, &returned, nullptr), "cannot create junction fixture");
}
void markReparseFile(const fs::path &path) {
  // A private non-Microsoft tag needs no symbolic-link privilege, and lets this
  // regression exercise a reparse *file* on ordinary developer machines.
  Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  struct Tagged {
    DWORD tag = 0x00000042;
    WORD length = 1, reserved = 0;
    GUID guid{0x56d612db, 0x1da8, 0x4f75, {0x9d, 0x6c, 0xf0, 0x31, 0xef, 0x47, 0x06, 0xbb}};
    BYTE value = 1;
  } tagged;
  DWORD returned = 0;
  check(DeviceIoControl(file.value, FSCTL_SET_REPARSE_POINT, &tagged, 25, nullptr, 0, &returned, nullptr),
        "cannot create private reparse-file fixture");
}
void roundTripAndValidation(const fs::path &base) {
  const auto directory = base / "round-trip" / "store";
  RecoveryStore store(directory);
  const auto session = RecoveryStore::newSessionID(), other = RecoveryStore::newSessionID();
  check(session.size() == 36 && session != other && session.find('{') == std::string::npos, "invalid generated session identity");
  check(store.entries().empty() && !fs::exists(directory), "listing a missing store wrote to disk");
  store.clear(session);
  check(!fs::exists(directory), "clearing a missing store wrote to disk");
  const auto source = base / L"caf\u00e9.screamseq";
  const auto saved = store.save(session, payload, "Caf\xc3\xa9 \xf0\x9f\x8e\xb9", source, true);
  check(saved.document == session && saved.source == source && saved.hasRecording, "save lost metadata");
  check(saved.id.starts_with(session + "-") && saved.id.ends_with(".screamseq"), "save identifier is not opaque/session-scoped");
  const auto listed = store.entries();
  check(listed.size() == 1 && listed[0].dictionary() == saved.dictionary(), "list differs from saved entry");
  check(saved.savedAt.size() == 27 && saved.savedAt[10] == 'T' && saved.savedAt.back() == 'Z', "save date is not UTC ISO8601");
  check(store.read(saved.id) == payload, "project bytes changed");
  check(saved.dictionary().at("source").get<std::string>().find("caf\xc3\xa9") != std::string::npos, "source path is not UTF8");
  const auto before = contents(sidecar(directory / saved.id));
  rejected([&] { store.save("../bad", payload); }, "accepted invalid session");
  rejected([&] { store.clear("../bad"); }, "accepted invalid clear session");
  rejected([&] { store.save(session, {}); }, "accepted empty project");
  rejected([&] { store.save(session, payload, std::string("a\0b", 3)); }, "accepted NUL title");
  rejected([&] { store.save(session, payload, "valid", fs::path(std::wstring(L"a\0b", 3))); }, "accepted NUL source");
  rejected([&] { store.save(session, payload, std::string(RecoveryStore::maximumMetadataBytes, 'x')); }, "accepted oversized metadata");
  rejected([&] { store.save(session, payload, "\xc0\xaf"); }, "accepted invalid UTF8 metadata");
  check(store.entries().size() == 1 && contents(sidecar(directory / saved.id)) == before, "invalid save modified prior recovery");
  for(const auto &invalid : std::vector<std::string>{"", "../" + saved.id, "..\\" + saved.id,
      (directory / saved.id).string(), saved.id + ":stream", std::string("\0", 1) + saved.id,
      identifier(session, "0"), identifier(session, "01"), identifier(session, "-1"),
      identifier(session, "253402300800000000"), identifier(session, "18446744073709551616"),
      identifier("bad-session"), saved.id + "extra", saved.id.substr(0, saved.id.size() - 1)})
    rejected([&] { store.read(invalid); }, "accepted invalid recovery identifier");
  for(const auto &invalid : std::vector<fs::path>{fs::path{}, fs::path("relative"), directory / ".." / "escape",
      directory / "." / "alias", directory / "NUL", directory / "con.txt", directory / "x.",
      directory / "x ", directory / "x:stream", directory.root_path(), fs::path(std::wstring(L"C:\\bad\0path", 11)),
      fs::path(L"\\\\?\\C:\\store")})
    rejected([&] { RecoveryStore invalidStore(invalid); }, "accepted an unsafe store directory");
  noPendingFiles(directory);
}
void retentionAndSessionIsolation(const fs::path &base) {
  const auto directory = base / "retention";
  RecoveryStore store(directory);
  const auto first = RecoveryStore::newSessionID(), second = RecoveryStore::newSessionID();
  const auto other = store.save(second, payload, "Other session");
  std::vector<RecoveryEntry> saves;
  for(unsigned i = 0; i < 13; ++i) saves.push_back(store.save(first, payload, "Generation " + std::to_string(i)));
  const auto listed = store.entries();
  check(listed.size() == 11 && listed.front().id == saves.back().id && listed.back().id == other.id, "retention/order/cross-session failure");
  for(std::size_t i = 0; i < 10; ++i) check(listed[i].id == saves[12 - i].id, "generation order changed");
  for(std::size_t i = 0; i < 3; ++i)
    check(!fs::exists(directory / saves[i].id) && !fs::exists(sidecar(directory / saves[i].id)), "retention left an old generation/sidecar");
  check(store.read(other.id) == payload, "retention damaged another session");
  std::string uppercase = first;
  for(auto &c : uppercase) if(c >= 'a' && c <= 'f') c -= 'a' - 'A';
  store.clear(uppercase);
  check(store.entries().size() == 1 && store.entries()[0].id == other.id, "clear crossed a session boundary");
  store.clear(second);
  check(store.entries().empty() && fs::is_empty(directory), "clear left session files");
}
void damagedAndIncomplete(const fs::path &base) {
  const auto directory = base / "damaged";
  RecoveryStore store(directory);
  const auto session = RecoveryStore::newSessionID();
  const auto saved = store.save(session, payload, "Title", base / "source.screamseq", true);
  const auto metadata = sidecar(directory / saved.id);
  for(const auto &bad : std::vector<std::string>{"{", "[]", std::string(RecoveryStore::maximumMetadataBytes + 1, ' '),
      R"({"title":4,"source":7,"hasRecording":"true"})", R"({"title":"x\u0000y","source":"x\u0000y"})",
      std::string(1000, '[') + "0" + std::string(1000, ']')}) {
    write(metadata, bad);
    const auto listed = store.entries();
    check(listed.size() == 1 && listed[0].title == "Recovered song" && !listed[0].source && !listed[0].hasRecording,
          "bad metadata prevented recovery or leaked invalid fields");
    check(store.read(saved.id) == payload, "bad metadata prevented byte recovery");
  }
  fs::remove(metadata);
  check(store.entries().size() == 1 && store.entries()[0].title == "Recovered song", "missing metadata hid a complete project");
  write(metadata, R"({"title":"Partial","source":null,"hasRecording":true})");
  check(store.entries()[0].title == "Partial" && store.entries()[0].hasRecording, "valid metadata fields did not survive");
  const auto empty = identifier(session), oversized = identifier(session), folder = identifier(session), orphan = identifier(session);
  write(directory / empty, "");
  makeSizedFile(directory / oversized, RecoveryStore::maximumBytes + 1);
  fs::create_directory(directory / folder);
  write(sidecar(directory / orphan), R"({"title":"Never published"})");
  write(directory / ".pending-unpublished.screamseq", "incomplete");
  write(directory / "not-a-recovery-name", "incomplete");
  check(store.entries().size() == 1, "incomplete/nonregular/oversized files became recoveries");
  rejected([&] { store.read(empty); }, "read accepted empty project");
  rejected([&] { store.read(oversized); }, "read accepted oversized project");
  rejected([&] { store.read(folder); }, "read accepted a directory");
  rejected([&] { store.read(orphan); }, "read accepted a metadata-only publication");
  store.clear(session);
  check(!fs::exists(directory / saved.id) && fs::exists(directory / oversized) && fs::exists(directory / ".pending-unpublished.screamseq"),
        "clear touched an incomplete/unvalidated entry");
}
void reparseAndAncestorPinning(const fs::path &base) {
  const auto target = base / "junction-target", link = base / "junction-link";
  fs::create_directory(target);
  write(target / "sentinel", "untouched");
  junction(link, target);
  const auto session = RecoveryStore::newSessionID();
  RecoveryStore linked(link / "nested");
  rejected([&] { linked.entries(); }, "listing followed a reparse ancestor");
  rejected([&] { linked.save(session, payload); }, "save followed a reparse ancestor");
  rejected([&] { linked.read(identifier(session)); }, "read followed a reparse ancestor");
  rejected([&] { linked.clear(session); }, "clear followed a reparse ancestor");
  check(contents(target / "sentinel") == "untouched" && !fs::exists(target / "nested"), "reparse ancestor changed target");
  check(RemoveDirectoryW(link.c_str()), "cannot remove junction fixture");
  const auto directory = base / "reparse-entries";
  RecoveryStore store(directory);
  const auto saved = store.save(session, payload);
  const auto reparse = directory / identifier(session), junctionEntry = directory / identifier(session);
  write(reparse, "outside-data"); markReparseFile(reparse);
  junction(junctionEntry, target);
  check(store.entries().size() == 1, "reparse entry became a recovery");
  rejected([&] { store.read(reparse.filename().string()); }, "read accepted reparse file");
  rejected([&] { store.read(junctionEntry.filename().string()); }, "read followed junction entry");
  const auto metadata = sidecar(directory / saved.id);
  fs::remove(metadata); write(metadata, "x"); markReparseFile(metadata);
  check(store.entries().size() == 1 && store.entries()[0].title == "Recovered song", "reparse sidecar hid complete project");
  check(store.read(saved.id) == payload, "reparse sidecar interfered with safe project read");
  store.clear(session);
  check(GetFileAttributesW(reparse.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT, "clear deleted a reparse entry");
  check(contents(target / "sentinel") == "untouched", "clear changed junction target");
  check(RemoveDirectoryW(junctionEntry.c_str()), "cannot remove junction-entry fixture");
  check(DeleteFileW(reparse.c_str()) && DeleteFileW(metadata.c_str()), "cannot remove private reparse fixtures");

  bool checked = false;
  const auto pinned = base / "pinned" / "store", renamed = base / "renamed";
  RecoveryStore pinnedStore(pinned, [&](RecoveryStore::FaultPoint point, const fs::path &) {
    if(point != RecoveryStore::FaultPoint::projectPublish) return;
    checked = true;
    check(!MoveFileExW(pinned.parent_path().c_str(), renamed.c_str(), 0), "an ancestor moved during publication");
    check(GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_ACCESS_DENIED, "ancestor lock failed unexpectedly");
  });
  const auto published = pinnedStore.save(session, payload);
  check(checked && pinnedStore.read(published.id) == payload && !fs::exists(renamed), "pinning invalidated publication");
}
void publicationFailures(const fs::path &base) {
  const auto directory = base / "failures";
  const auto session = RecoveryStore::newSessionID();
  RecoveryStore baseline(directory);
  const auto prior = baseline.save(session, payload, "Prior durable copy");
  for(const auto injected : {RecoveryStore::FaultPoint::projectFlush, RecoveryStore::FaultPoint::metadataFlush,
      RecoveryStore::FaultPoint::metadataPublish, RecoveryStore::FaultPoint::projectPublish}) {
    bool observed = false, metadataBeforeProject = false;
    RecoveryStore store(directory, [&](RecoveryStore::FaultPoint point, const fs::path &destination) {
      if(point != injected) return;
      observed = true;
      if(point == RecoveryStore::FaultPoint::projectPublish) {
        metadataBeforeProject = fs::is_regular_file(sidecar(destination)) && !fs::exists(destination) && baseline.entries().size() == 1;
      }
      throw std::runtime_error("injected publication failure");
    });
    rejected([&] { store.save(session, payload, "Must not publish"); }, "publication fault did not fail save");
    check(observed && baseline.entries().size() == 1 && baseline.read(prior.id) == payload, "failed publication damaged existing recovery");
    check(injected != RecoveryStore::FaultPoint::projectPublish || metadataBeforeProject, "project publication ordering/visibility failed");
    check(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 2, "failed publication left a project/sidecar");
    noPendingFiles(directory);
  }
  fs::path collision;
  RecoveryStore colliding(directory, [&](RecoveryStore::FaultPoint point, const fs::path &destination) {
    if(point == RecoveryStore::FaultPoint::projectPublish) { collision = destination; write(destination, "do not replace"); }
  });
  rejected([&] { colliding.save(session, payload); }, "publication replaced a colliding project");
  check(contents(collision) == "do not replace" && !fs::exists(sidecar(collision)), "collision rollback erased/replaced another file");
  fs::remove(collision);
  noPendingFiles(directory);
}
void cleanupDoesNotRevoke(const fs::path &base) {
  const auto directory = base / "cleanup";
  const auto session = RecoveryStore::newSessionID(), other = RecoveryStore::newSessionID();
  RecoveryStore baseline(directory);
  const auto foreign = baseline.save(other, payload, "Other session");
  std::vector<RecoveryEntry> prior;
  for(unsigned i = 0; i < 10; ++i) prior.push_back(baseline.save(session, payload));
  bool faultObserved = false;
  RecoveryStore faulting(directory, [&](RecoveryStore::FaultPoint point, const fs::path &) {
    if(point == RecoveryStore::FaultPoint::cleanup) { faultObserved = true; throw std::runtime_error("cleanup unavailable"); }
  });
  const auto saved = faulting.save(session, payload, "Durable despite cleanup");
  check(faultObserved && baseline.entries().size() == 12 && baseline.read(saved.id) == payload,
        "cleanup failure invalidated a durable save");
  const auto next = baseline.save(session, payload);
  check(baseline.entries().size() == 11 && baseline.read(next.id) == payload && baseline.read(foreign.id) == payload,
        "retry did not restore retention/preserve other session");
  const auto oldest = prior[2];
  {
    Handle locked(CreateFileW((directory / oldest.id).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    const auto afterLock = baseline.save(session, payload);
    check(baseline.read(afterLock.id) == payload && fs::exists(sidecar(directory / oldest.id)), "locked cleanup revoked durable copy/sidecar");
    rejected([&] { baseline.clear(session); }, "locked project clear unexpectedly succeeded");
    check(fs::exists(directory / oldest.id) && fs::exists(sidecar(directory / oldest.id)), "failed clear removed locked project metadata");
  }
  baseline.clear(session);
  check(baseline.entries().size() == 1 && baseline.read(foreign.id) == payload, "clear retry touched other session");
  const auto exclusive = baseline.save(session, payload, "Exclusively locked copy");
  {
    Handle locked(CreateFileW((directory / exclusive.id).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    rejected([&] { baseline.clear(session); }, "exclusive lock made clear falsely report success");
    check(fs::exists(directory / exclusive.id) && fs::exists(sidecar(directory / exclusive.id)), "exclusive clear failure lost a copy/sidecar");
  }
  check(baseline.read(exclusive.id) == payload, "exclusive clear failure damaged bytes");
  {
    Handle lockedOther(CreateFileW((directory / foreign.id).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    baseline.clear(session);
    check(!fs::exists(directory / exclusive.id) && fs::exists(directory / foreign.id), "another session's lock blocked or redirected clear");
  }
  check(baseline.entries().size() == 1 && baseline.read(foreign.id) == payload, "clear retry damaged the other session");
}
} // namespace
int main() {
  try {
    Scratch scratch;
    roundTripAndValidation(scratch.path);
    retentionAndSessionIsolation(scratch.path);
    damagedAndIncomplete(scratch.path);
    reparseAndAncestorPinning(scratch.path);
    publicationFailures(scratch.path);
    cleanupDoesNotRevoke(scratch.path);
    std::cout << "Recovery store: bounds, retention, isolation, reparse rejection and durable publication passed\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << "Recovery store test failed: " << error.what() << '\n';
    return 1;
  }
}
