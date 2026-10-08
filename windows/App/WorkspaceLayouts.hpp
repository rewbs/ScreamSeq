#pragma once

#include "../Api/SessionAdapter.hpp"
#include "../Project/ProjectIO.hpp"
#include <optional>
#include <set>

namespace ScreamSeq {

// Presentation-only named layouts. The caller validates the panel identifiers
// and geometry before applying a configuration to retained panel instances.
// This catalogue never owns song state, panel drafts, pins or inspected targets.
// Use only on the UI owner thread, alongside the workspace it describes.
class WorkspaceLayouts {
public:
  using Json = Api::Json;
  static constexpr size_t maximumLayouts = 24;
  static constexpr size_t maximumNameCharacters = 64;
  static constexpr size_t maximumBytes = 256u * 1024u;

private:
  std::filesystem::path path_;
  Json layouts_ = Json::object();
  std::string diagnostic_;
  std::optional<std::vector<std::byte>> diskBytes_;
  bool readable_ = true;

  static void need(bool condition, const char *message) {
    if(!condition) throw Api::ApiError(-32602, message);
  }
  static void validateConfiguration(const Json &config) {
    need(config.is_object(), "Workspace layout configuration must be an object");
    // Keep parsing/serialization bounded even if a future caller forgets to
    // reject an unknown, recursively nested presentation field.
    auto visit = [](auto &&self, const Json &value, unsigned depth) -> void {
      need(depth <= 8, "Workspace layout configuration is nested too deeply");
      if(value.is_structured()) for(const auto &child : value) self(self, child, depth + 1);
    };
    visit(visit, config, 0);
  }
  // Cooperating application instances lock the same normalized preference path.
  // The byte comparison below additionally detects non-cooperating file edits.
  class FileLock {
    HANDLE mutex_ = nullptr;
  public:
    explicit FileLock(const std::filesystem::path &path) {
      const auto canonical = std::filesystem::weakly_canonical(path).native();
      std::wstring folded(canonical.size(), L'\0');
      need(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, canonical.data(),
        static_cast<int>(canonical.size()), folded.data(), static_cast<int>(folded.size()),
        nullptr, nullptr, 0) > 0, "Cannot normalize workspace layout storage path");
      // Hash collisions only serialize two unrelated catalogues; they cannot
      // cause one file's contents to be read from or written to the other.
      uint64_t hash = 14695981039346656037ull;
      for(const auto c : folded) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ull; }
      const auto name = L"Global\\org.resonance.tracker.workspace-layout-v1." + std::to_wstring(hash);
      mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
      if(!mutex_) Project::FileDetail::fail("Cannot open workspace layout storage lock");
      const auto result = WaitForSingleObject(mutex_, 0);
      if(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
      const auto error = GetLastError();
      CloseHandle(mutex_);
      mutex_ = nullptr;
      if(result == WAIT_TIMEOUT)
        throw Api::ApiError(-32002, "Saved workspace layouts are being changed by another session; retry shortly");
      throw std::system_error(error, std::system_category(), "Cannot lock workspace layout storage");
    }
    ~FileLock() { if(mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); } }
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
  };
  static std::optional<std::vector<std::byte>> readDisk(const std::filesystem::path &path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if(attributes == INVALID_FILE_ATTRIBUTES) {
      const auto error = GetLastError();
      if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt;
      throw std::system_error(error, std::system_category(), "Cannot inspect workspace layout file");
    }
    need(!(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)),
      "Workspace layout storage must be a regular file");
    return Project::readProjectBytes(path, maximumBytes);
  }
  static Json decode(std::span<const std::byte> bytes) {
    std::vector<std::set<std::string>> keys;
    size_t events = 0;
    auto check = [&](int depth, Json::parse_event_t event, Json &value) {
      need(++events <= 32768, "Workspace layout file exceeds structural limits");
      need(depth <= 12, "Workspace layout file is nested too deeply");
      if(event == Json::parse_event_t::object_start) keys.emplace_back();
      else if(event == Json::parse_event_t::object_end) keys.pop_back();
      else if(event == Json::parse_event_t::key) {
        need(!keys.empty() && keys.back().insert(value.get<std::string>()).second,
          "Workspace layout file contains a duplicate field");
      }
      return true;
    };
    const auto root = Json::parse(bytes.begin(), bytes.end(), check);
    need(root.is_object() && root.size() == 2 && root.contains("version") && root.contains("layouts"),
      "Workspace layout file must contain version and layouts");
    need(root.at("version").is_number_integer() && root.at("version") == 1,
      "Unsupported workspace layout file version");
    const auto &layouts = root.at("layouts");
    need(layouts.is_object() && layouts.size() <= maximumLayouts,
      "Workspace layout file exceeds 24 saved layouts");
    for(auto entry = layouts.begin(); entry != layouts.end(); ++entry) {
      validateName(entry.key());
      validateConfiguration(entry.value());
    }
    return layouts;
  }
  void publish(Json next) {
    const auto bytes = Json{{"version", 1}, {"layouts", next}}.dump();
    need(bytes.size() <= maximumBytes, "Workspace layouts exceed the 256 KiB storage limit");
    const auto span = std::as_bytes(std::span(bytes.data(), bytes.size()));
    (void)decode(span); // Never publish a catalogue our bounded reader cannot reopen.
    if(!path_.empty()) {
      if(!readable_) throw Api::ApiError(-32001,
        "Saved workspace layouts could not be read; reload layouts before saving or deleting");
      FileLock lock(path_);
      if(readDisk(path_) != diskBytes_) throw Api::ApiError(-32001,
        "Saved workspace layouts changed in another session; reload layouts before saving or deleting");
      // Allocate the next snapshot before touching the destination so an
      // allocation failure cannot report a failed save after publishing it.
      std::vector<std::byte> nextBytes(span.begin(), span.end());
      std::filesystem::create_directories(path_.parent_path());
      // Existing preference/catalogue saves share this staged, flushed atomic
      // replacement. A failed write keeps both the old file and in-memory data.
      Project::writeProjectFile(path_, span, true);
      diskBytes_ = std::move(nextBytes);
    }
    layouts_.swap(next);
    diagnostic_.clear();
  }

public:
  static void validateName(const std::string &name) {
    need(!name.empty() && name.size() <= maximumNameCharacters * 4,
      "Layout names must contain 1 to 64 characters");
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      name.data(), static_cast<int>(name.size()), nullptr, 0);
    need(length > 0, "Layout name is not valid UTF-8");
    std::wstring wide(static_cast<size_t>(length), L'\0');
    need(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
      static_cast<int>(name.size()), wide.data(), length) == length, "Cannot decode layout name");
    size_t characters = 0;
    for(size_t i = 0; i < wide.size(); ++i) {
      const wchar_t c = wide[i];
      WORD kind = 0;
      need(GetStringTypeW(CT_CTYPE1, &c, 1, &kind) != 0, "Cannot validate layout name");
      need(!(kind & C1_CNTRL) && c != 0x2028 && c != 0x2029,
        "Layout names cannot contain control characters");
      if(i == 0 || i + 1 == wide.size())
        need(!(kind & C1_SPACE), "Layout names cannot start or end with whitespace");
      ++characters;
      if(c >= 0xD800 && c <= 0xDBFF) ++i; // UTF-8 decoding already validates the pair.
    }
    need(characters <= maximumNameCharacters, "Layout names must contain 1 to 64 characters");
  }

  // An empty path intentionally creates an inspection-only in-memory catalogue.
  // Missing files are normal on first launch. Invalid files are left untouched,
  // ignored as a whole, and reported without preventing the app from opening.
  bool load(const std::filesystem::path &path) noexcept {
    layouts_.clear();
    path_.clear();
    diagnostic_.clear();
    diskBytes_.reset();
    readable_ = true;
    try {
      if(path.empty()) return true;
      need(path.is_absolute() && !path.filename().empty(),
        "Workspace layout storage requires an absolute file path");
      path_ = path;
      readable_ = false;
      FileLock lock(path_);
      diskBytes_ = readDisk(path);
      readable_ = true;
      if(diskBytes_) layouts_ = decode(*diskBytes_);
      return true;
    } catch(const std::exception &error) {
      try { diagnostic_ = std::string("Saved workspace layouts were ignored: ") + error.what(); }
      catch(...) { diagnostic_.clear(); }
      return false;
    } catch(...) {
      return false;
    }
  }

  bool reload() noexcept {
    // Inspection catalogues have no backing file; refreshing preserves their
    // named layouts just as it preserves the currently displayed workspace.
    if(path_.empty()) return true;
    try {
      const auto path = path_; // load clears path_, so never pass it by alias.
      return load(path);
    } catch(...) {
      return false;
    }
  }

  const std::string &diagnostic() const { return diagnostic_; }
  std::vector<std::string> list() const {
    std::vector<std::string> names;
    names.reserve(layouts_.size());
    for(auto entry = layouts_.begin(); entry != layouts_.end(); ++entry) names.push_back(entry.key());
    return names;
  }
  const Json *get(const std::string &name) const {
    const auto entry = layouts_.find(name);
    return entry == layouts_.end() ? nullptr : &entry.value();
  }
  void save(const std::string &name, const Json &configuration) {
    validateName(name);
    validateConfiguration(configuration);
    need(layouts_.contains(name) || layouts_.size() < maximumLayouts,
      "At most 24 workspace layouts can be saved; replace or delete an existing layout");
    auto next = layouts_;
    next[name] = configuration;
    publish(std::move(next));
  }
  bool remove(const std::string &name) {
    validateName(name);
    if(!layouts_.contains(name)) return false;
    auto next = layouts_;
    next.erase(name);
    publish(std::move(next));
    return true;
  }
};

} // namespace ScreamSeq
