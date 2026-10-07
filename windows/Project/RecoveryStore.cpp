#include "RecoveryStore.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace ScreamSeq::Project {
namespace {
using Json = nlohmann::json;
constexpr std::uint64_t epochMicroseconds = 11644473600000000ull;
constexpr std::uint64_t maximumTimestamp = 253402300799999999ull; // end of year 9999

[[noreturn]] void fail(const char *operation) {
  throw std::system_error(GetLastError(), std::system_category(), operation);
}
void require(bool value, const char *message) {
  if(!value) throw std::invalid_argument(message);
}
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
  ~Handle() { if(value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  void close() {
    const auto handle = std::exchange(value, INVALID_HANDLE_VALUE);
    if(handle != INVALID_HANDLE_VALUE && !CloseHandle(handle)) fail("Cannot close recovery file");
  }
};
std::string utf8(const std::filesystem::path &path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}
std::string uuid(const std::string &value) {
  require(value.size() == 36, "Recovery session must be a UUID without braces");
  std::string result = value;
  for(std::size_t i = 0; i < result.size(); ++i) {
    if(i == 8 || i == 13 || i == 18 || i == 23) require(result[i] == '-', "Invalid recovery UUID");
    else {
      if(result[i] >= 'A' && result[i] <= 'F') result[i] += 'a' - 'A';
      require((result[i] >= '0' && result[i] <= '9') || (result[i] >= 'a' && result[i] <= 'f'), "Invalid recovery UUID");
    }
  }
  return result;
}
struct Name {
  std::string id, session;
  std::uint64_t timestamp = 0;
};
Name parseName(const std::string &id) {
  require(id.size() >= 85 && id.size() <= 104 && id.ends_with(".screamseq"), "Invalid recovery identifier");
  const auto split = id.find('-', 37);
  require(id[36] == '-' && split != std::string::npos && split > 37 && id.size() == split + 47, "Invalid recovery identifier");
  Name result{id, uuid(id.substr(0, 36)), 0};
  uuid(id.substr(split + 1, 36));
  require(id[37] != '0', "Invalid recovery timestamp");
  const auto number = std::from_chars(id.data() + 37, id.data() + split, result.timestamp);
  require(number.ec == std::errc{} && number.ptr == id.data() + split && result.timestamp <= maximumTimestamp,
          "Invalid recovery timestamp");
  return result;
}
std::wstring uppercase(std::wstring value) {
  for(auto &character : value) if(character >= L'a' && character <= L'z') character -= L'a' - L'A';
  return value;
}
std::filesystem::path validateDirectory(std::filesystem::path directory) {
  const auto &raw = directory.native();
  require(!raw.empty() && raw.find(L'\0') == std::wstring::npos && directory.is_absolute(), "Recovery directory requires an absolute path without NUL");
  require(!raw.starts_with(L"\\\\?\\") && !raw.starts_with(L"\\\\.\\"), "Device paths are not recovery directories");
  require(directory != directory.root_path(), "Choose a dedicated recovery directory");
  for(const auto &component : directory.relative_path()) {
    const auto value = component.native();
    if(value.empty()) continue;
    require(value != L"." && value != L".." && value.back() != L'.' && value.back() != L' ', "Recovery path cannot escape or normalize to another directory");
    require(value.find_first_of(L":*?\"<>|") == std::wstring::npos, "Invalid recovery directory component");
    const auto stem = uppercase(value.substr(0, value.find(L'.')));
    const bool device = stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
      (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'1' && stem[3] <= L'9');
    require(!device, "Device names are not recovery directories");
  }
  return directory.lexically_normal();
}
BY_HANDLE_FILE_INFORMATION information(HANDLE file) {
  BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(file, &info)) fail("Cannot inspect recovery file");
  require(GetFileType(file) == FILE_TYPE_DISK && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
          "Recovery storage cannot use reparse points or non-disk files");
  return info;
}
struct DirectoryLease {
  std::vector<Handle> ancestors;
  bool exists = true;
  DirectoryLease(const std::filesystem::path &directory, bool create) {
    auto current = directory.root_path();
    const auto open = [&](const std::filesystem::path &path) {
      // Holding every ancestor without DELETE sharing prevents a checked
      // directory from being exchanged for a junction during an operation.
      Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      if(handle.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if(!create && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) { exists = false; return; }
        fail("Cannot open recovery directory");
      }
      require(information(handle.value).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY, "Recovery ancestor is not a directory");
      ancestors.push_back(std::move(handle));
    };
    open(current);
    for(const auto &component : directory.relative_path()) {
      if(!exists) break;
      if(component.empty()) continue;
      current /= component;
      if(create && !CreateDirectoryW(current.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        fail("Cannot create recovery directory");
      open(current);
    }
  }
};
Handle openRegular(const std::filesystem::path &path, bool deleting = false) {
  Handle file(CreateFileW(path.c_str(), GENERIC_READ | (deleting ? DELETE : 0), FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  if(file.value == INVALID_HANDLE_VALUE) fail("Cannot open recovery file");
  require(!(information(file.value).dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "Recovery entry is not a regular file");
  return file;
}
std::size_t length(HANDLE file, std::size_t maximum, bool nonempty) {
  LARGE_INTEGER value{};
  if(!GetFileSizeEx(file, &value)) fail("Cannot size recovery file");
  require(value.QuadPart >= (nonempty ? 1 : 0) && static_cast<std::uint64_t>(value.QuadPart) <= maximum,
          "Recovery file is empty or exceeds the size limit");
  return static_cast<std::size_t>(value.QuadPart);
}
std::vector<std::byte> bytes(const std::filesystem::path &path, std::size_t maximum, bool nonempty = true) {
  auto file = openRegular(path);
  std::vector<std::byte> result(length(file.value, maximum, nonempty));
  std::size_t offset = 0;
  while(offset < result.size()) {
    DWORD received = 0;
    const auto count = static_cast<DWORD>(std::min<std::size_t>(result.size() - offset, 1024u * 1024u));
    if(!ReadFile(file.value, result.data() + offset, count, &received, nullptr)) fail("Cannot read recovery file");
    require(received != 0, "Recovery file was truncated while reading");
    offset += received;
  }
  return result;
}
std::filesystem::path sidecar(const std::filesystem::path &path) { auto result = path; result += L".json"; return result; }
std::string date(std::uint64_t timestamp) {
  const auto ticks = (timestamp + epochMicroseconds) * 10;
  FILETIME file{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  SYSTEMTIME time{};
  require(FileTimeToSystemTime(&file, &time), "Invalid recovery date");
  char result[40]{};
  std::snprintf(result, sizeof(result), "%04u-%02u-%02uT%02u:%02u:%02u.%06uZ", unsigned(time.wYear), unsigned(time.wMonth),
                unsigned(time.wDay), unsigned(time.wHour), unsigned(time.wMinute), unsigned(time.wSecond), unsigned(timestamp % 1000000));
  return result;
}
std::uint64_t timestamp() {
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  const auto value = ((std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime) / 10 - epochMicroseconds;
  static std::atomic<std::uint64_t> previous{0};
  auto before = previous.load();
  for(;;) { const auto next = std::max(value, before + 1); if(previous.compare_exchange_weak(before, next)) return next; }
}
std::vector<Name> candidates(const std::filesystem::path &directory, const std::string *clearingSession = nullptr) {
  std::vector<Name> result;
  for(const auto &entry : std::filesystem::directory_iterator(directory)) {
    std::optional<Name> name;
    try {
      name = parseName(utf8(entry.path().filename()));
      if(clearingSession && name->session != *clearingSession) continue;
      auto file = openRegular(entry.path());
      length(file.value, RecoveryStore::maximumBytes, true);
      result.push_back(std::move(*name));
    } catch(const std::invalid_argument &) { } catch(const std::system_error &) {
      // Listing cannot offer files it cannot validate. Explicit cleanup must
      // report an inaccessible own copy, rather than claim it was cleared.
      if(clearingSession && name && name->session == *clearingSession) throw;
    }
  }
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.timestamp == b.timestamp ? a.id > b.id : a.timestamp > b.timestamp;
  });
  return result;
}
RecoveryEntry entry(const std::filesystem::path &directory, const Name &name) {
  RecoveryEntry result{name.id, name.session, date(name.timestamp), "Recovered song", {}, false};
  try {
    const auto content = bytes(sidecar(directory / name.id), RecoveryStore::maximumMetadataBytes);
    const auto bounded = [](int depth, Json::parse_event_t, Json &) {
      require(depth <= 8, "Recovery metadata is too deeply nested");
      return true;
    };
    const auto metadata = Json::parse(reinterpret_cast<const char *>(content.data()),
      reinterpret_cast<const char *>(content.data() + content.size()), bounded);
    if(!metadata.is_object()) return result;
    if(metadata.contains("title") && metadata["title"].is_string()) {
      const auto title = metadata["title"].get<std::string>();
      if(title.find('\0') == std::string::npos) result.title = title;
    }
    if(metadata.contains("source") && metadata["source"].is_string()) {
      const auto source = metadata["source"].get<std::string>();
      if(source.find('\0') == std::string::npos) result.source = std::filesystem::u8path(source);
    }
    if(metadata.contains("hasRecording") && metadata["hasRecording"].is_boolean()) result.hasRecording = metadata["hasRecording"].get<bool>();
  } catch(const std::exception &) { }
  return result;
}
void removeRegular(const std::filesystem::path &path) {
  auto file = openRegular(path, true);
  FILE_DISPOSITION_INFO remove{TRUE};
  if(!SetFileInformationByHandle(file.value, FileDispositionInfo, &remove, sizeof(remove))) fail("Cannot remove recovery file");
}
void removeQuietly(const std::filesystem::path &path) noexcept { try { removeRegular(path); } catch(...) { } }
void writeStaging(const std::filesystem::path &path, std::span<const std::byte> content,
                  const RecoveryStore::FaultHook &fault, RecoveryStore::FaultPoint point, bool &created) {
  Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if(file.value == INVALID_HANDLE_VALUE) fail("Cannot reserve recovery staging file");
  created = true;
  std::size_t offset = 0;
  while(offset < content.size()) {
    DWORD written = 0;
    const auto count = static_cast<DWORD>(std::min<std::size_t>(content.size() - offset, 1024u * 1024u));
    if(!WriteFile(file.value, content.data() + offset, count, &written, nullptr)) fail("Cannot write recovery staging file");
    require(written != 0, "Recovery write made no progress");
    offset += written;
  }
  if(fault) fault(point, path);
  if(!FlushFileBuffers(file.value)) fail("Cannot flush recovery staging file");
  file.close();
}
void publish(const std::filesystem::path &source, const std::filesystem::path &destination) {
  if(!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) fail("Cannot publish recovery file");
}
} // namespace

nlohmann::json RecoveryEntry::dictionary() const {
  return {{"id", id}, {"document", document}, {"savedAt", savedAt}, {"title", title},
          {"source", source ? Json(utf8(*source)) : Json(nullptr)}, {"hasRecording", hasRecording}};
}
RecoveryStore::RecoveryStore(std::filesystem::path directory, FaultHook fault)
  : directory_(validateDirectory(std::move(directory))), fault_(std::move(fault)) {}
std::string RecoveryStore::newSessionID() {
  GUID id{};
  if(FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot generate recovery session identity");
  char result[37]{};
  std::snprintf(result, sizeof(result), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", unsigned(id.Data1), unsigned(id.Data2),
                unsigned(id.Data3), unsigned(id.Data4[0]), unsigned(id.Data4[1]), unsigned(id.Data4[2]), unsigned(id.Data4[3]),
                unsigned(id.Data4[4]), unsigned(id.Data4[5]), unsigned(id.Data4[6]), unsigned(id.Data4[7]));
  return result;
}
std::vector<RecoveryEntry> RecoveryStore::entries() const {
  DirectoryLease lease(directory_, false);
  if(!lease.exists) return {};
  std::vector<RecoveryEntry> result;
  for(const auto &name : candidates(directory_)) result.push_back(entry(directory_, name));
  return result;
}
RecoveryEntry RecoveryStore::save(const std::string &sessionUUID, std::span<const std::byte> content, std::string title,
                                   std::optional<std::filesystem::path> source, bool hasRecording) {
  const auto session = uuid(sessionUUID);
  require(!content.empty() && content.size() <= maximumBytes, "Recovery project is empty or exceeds the size limit");
  require(title.find('\0') == std::string::npos, "Recovery title contains NUL");
  if(source) require(source->native().find(L'\0') == std::wstring::npos, "Recovery source contains NUL");
  const Json metadata{{"title", title}, {"source", source ? Json(utf8(*source)) : Json(nullptr)}, {"hasRecording", hasRecording}};
  const auto json = metadata.dump();
  require(json.size() <= maximumMetadataBytes, "Recovery metadata exceeds 64 KiB");
  const auto unique = newSessionID();
  const Name name{session + "-" + std::to_string(timestamp()) + "-" + unique + ".screamseq", session, 0};
  const auto parsed = parseName(name.id);
  const auto destination = directory_ / name.id, metadataPath = sidecar(destination);
  const auto staging = directory_ / (".pending-" + unique + ".screamseq"), metadataStaging = directory_ / (".pending-" + unique + ".json");
  DirectoryLease lease(directory_, true);
  bool metadataPublished = false, stagingCreated = false, metadataStagingCreated = false;
  try {
    writeStaging(staging, content, fault_, FaultPoint::projectFlush, stagingCreated);
    writeStaging(metadataStaging, std::as_bytes(std::span(json.data(), json.size())), fault_, FaultPoint::metadataFlush, metadataStagingCreated);
    if(fault_) fault_(FaultPoint::metadataPublish, metadataPath);
    publish(metadataStaging, metadataPath); metadataPublished = true;
    if(fault_) fault_(FaultPoint::projectPublish, destination);
    publish(staging, destination);
  } catch(...) {
    if(stagingCreated) removeQuietly(staging);
    if(metadataStagingCreated) removeQuietly(metadataStaging);
    if(metadataPublished) removeQuietly(metadataPath);
    throw;
  }
  // Publication succeeded. Cleanup is best effort and cannot revoke that result.
  try {
    std::size_t own = 0;
    for(const auto &candidate : candidates(directory_)) if(candidate.session == session && ++own > generations) {
      if(fault_) fault_(FaultPoint::cleanup, directory_ / candidate.id);
      try { removeRegular(directory_ / candidate.id); removeQuietly(sidecar(directory_ / candidate.id)); } catch(...) { }
    }
  } catch(...) { }
  return {name.id, session, date(parsed.timestamp), std::move(title), std::move(source), hasRecording};
}
std::vector<std::byte> RecoveryStore::read(const std::string &id) const {
  parseName(id);
  DirectoryLease lease(directory_, false);
  require(lease.exists, "Recovery directory does not exist");
  return bytes(directory_ / id, maximumBytes);
}
void RecoveryStore::clear(const std::string &sessionUUID) {
  const auto session = uuid(sessionUUID);
  DirectoryLease lease(directory_, false);
  if(!lease.exists) return;
  for(const auto &candidate : candidates(directory_, &session)) {
    removeRegular(directory_ / candidate.id);
    removeQuietly(sidecar(directory_ / candidate.id));
  }
}
} // namespace ScreamSeq::Project
