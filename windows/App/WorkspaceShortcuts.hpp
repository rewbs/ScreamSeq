#pragma once

#include "../Api/SessionAdapter.hpp"
#include "../Project/ProjectIO.hpp"
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace ScreamSeq {

// Application command bindings only. The caller gives local editors, text
// input, open selectors and note releases first refusal before handle().
// All methods belong to the UI owner thread; this class installs no hooks.
class WorkspaceShortcuts {
public:
  using Json = Api::Json;
  enum Modifier : unsigned { control = 1, alt = 2, shift = 4 };
  struct Stroke {
    std::string key;
    unsigned modifiers = 0;
    bool operator==(const Stroke &) const = default;
    std::string encoded() const {
      std::string result;
      if(modifiers & control) result += "ctrl+";
      if(modifiers & alt) result += "alt+";
      if(modifiers & shift) result += "shift+";
      return result + key;
    }
  };
  struct Definition { std::string id; std::vector<std::string> defaultKeys; };
  struct Decision { bool consumed = false; std::string command; };
  using Sequence = std::vector<Stroke>;
  static constexpr size_t maximumCommands = 512;
  static constexpr size_t maximumBytes = 256u * 1024u;
  static constexpr uint64_t timeoutMilliseconds = 1500;

private:
  using Bindings = std::map<std::string, Sequence>;
  Bindings defaults_, overrides_, active_;
  std::filesystem::path path_;
  std::optional<std::vector<std::byte>> diskBytes_;
  std::string diagnostic_;
  bool readable_ = true;
  Sequence prefix_;
  uint64_t context_ = 0, deadline_ = 0;

  static void need(bool value, const char *message) {
    if(!value) throw Api::ApiError(-32602, message);
  }
  static std::string folded(std::string_view value) {
    std::string result(value);
    for(auto &c : result) if(c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return result;
  }
  static std::vector<std::string> encoded(const Sequence &sequence) {
    std::vector<std::string> result;
    result.reserve(sequence.size());
    for(const auto &stroke : sequence) result.push_back(stroke.encoded());
    return result;
  }
  static void validateCustomFirst(const Sequence &sequence) {
    if(!sequence.empty())
      need((sequence.front().modifiers & (control | alt)) != 0,
        "The first shortcut stroke needs Ctrl or Alt to preserve note entry");
  }
  static Sequence parse(const std::vector<std::string> &keys) {
    need(keys.size() <= 4, "Use at most four shortcut strokes");
    Sequence result;
    result.reserve(keys.size());
    for(const auto &key : keys) {
      auto stroke = parseStroke(key);
      need(result.empty() || stroke.key != "escape", "Escape cancels a pending sequence and cannot continue one");
      result.push_back(std::move(stroke));
    }
    return result;
  }
  static bool startsWith(const Sequence &sequence, const Sequence &prefix) {
    return prefix.size() <= sequence.size() &&
      std::equal(prefix.begin(), prefix.end(), sequence.begin());
  }
  static void validateId(const std::string &id) {
    need(!id.empty() && id.size() <= 1024 && id.find('\0') == std::string::npos,
      "Command IDs must contain 1 to 1024 UTF-8 bytes");
    need(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, id.data(), int(id.size()), nullptr, 0) > 0,
      "Command ID is not valid UTF-8");
    need(std::none_of(id.begin(), id.end(), [](unsigned char c) { return c < 32 || c == 127; }),
      "Command IDs cannot contain control characters");
  }
  const Sequence &known(const std::string &id) const {
    const auto found = defaults_.find(id);
    if(found == defaults_.end()) throw Api::ApiError(-32602, "Choose a known command ID");
    return found->second;
  }
  Bindings effective(const Bindings &overrides) const {
    auto result = defaults_;
    for(const auto &[id, keys] : overrides) {
      (void)known(id);
      result.at(id) = keys;
    }
    for(auto a = result.begin(); a != result.end(); ++a) {
      if(a->second.empty()) continue;
      for(auto b = std::next(a); b != result.end(); ++b) {
        if(b->second.empty()) continue;
        if(startsWith(a->second, b->second) || startsWith(b->second, a->second))
          throw Api::ApiError(-32602, "Shortcut prefix conflict between " + a->first + " and " + b->first);
      }
    }
    return result;
  }
  class FileLock {
    HANDLE mutex_ = nullptr;
  public:
    explicit FileLock(const std::filesystem::path &path) {
      const auto canonical = std::filesystem::weakly_canonical(path).native();
      std::wstring normalized(canonical.size(), L'\0');
      need(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, canonical.data(),
        int(canonical.size()), normalized.data(), int(normalized.size()), nullptr, nullptr, 0) > 0,
        "Cannot normalize shortcut preference path");
      uint64_t hash = 14695981039346656037ull;
      for(const auto c : normalized) { hash ^= uint16_t(c); hash *= 1099511628211ull; }
      const auto name = L"Global\\org.resonance.tracker.workspace-shortcuts-v1." + std::to_wstring(hash);
      mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
      if(!mutex_) Project::FileDetail::fail("Cannot open shortcut preference lock");
      const auto result = WaitForSingleObject(mutex_, 0);
      if(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
      const auto error = GetLastError();
      CloseHandle(mutex_); mutex_ = nullptr;
      if(result == WAIT_TIMEOUT)
        throw Api::ApiError(-32002, "Shortcuts are being changed by another session; retry shortly");
      throw std::system_error(error, std::system_category(), "Cannot lock shortcut preferences");
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
      throw std::system_error(error, std::system_category(), "Cannot inspect shortcut preferences");
    }
    need(!(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)),
      "Shortcut preferences must be a regular file");
    return Project::readProjectBytes(path, maximumBytes);
  }
  Bindings decode(std::span<const std::byte> bytes) const {
    std::vector<std::set<std::string>> objectKeys;
    size_t events = 0;
    auto check = [&](int depth, Json::parse_event_t event, Json &value) {
      need(++events <= 16384 && depth <= 5, "Shortcut preferences exceed structural limits");
      if(event == Json::parse_event_t::object_start) objectKeys.emplace_back();
      else if(event == Json::parse_event_t::object_end) objectKeys.pop_back();
      else if(event == Json::parse_event_t::key)
        need(!objectKeys.empty() && objectKeys.back().insert(value.get<std::string>()).second,
          "Shortcut preferences contain a duplicate field");
      return true;
    };
    const auto root = Json::parse(bytes.begin(), bytes.end(), check);
    need(root.is_object() && root.size() == 2 && root.contains("version") && root.contains("overrides"),
      "Shortcut preferences must contain version and overrides");
    need(root.at("version").is_number_integer() && root.at("version") == 1,
      "Unsupported shortcut preference version");
    const auto &values = root.at("overrides");
    need(values.is_object() && values.size() <= maximumCommands, "Too many shortcut overrides");
    Bindings result;
    for(auto entry = values.begin(); entry != values.end(); ++entry) {
      (void)known(entry.key());
      need(entry.value().is_array() && entry.value().size() <= 4, "Shortcut keys must be an array of up to four strokes");
      std::vector<std::string> keys;
      for(const auto &key : entry.value()) {
        need(key.is_string(), "Shortcut strokes must be strings");
        keys.push_back(key.get<std::string>());
      }
      auto sequence = parse(keys);
      // Writing the trusted default is equivalent to removing its override.
      if(sequence != known(entry.key())) {
        validateCustomFirst(sequence);
        result.emplace(entry.key(), std::move(sequence));
      }
    }
    (void)effective(result); // Reject the entire file on a conflict, never partially apply it.
    return result;
  }
  void publish(Bindings next) {
    auto active = effective(next);
    Json overrides = Json::object();
    for(const auto &[id, keys] : next) overrides[id] = encoded(keys);
    const auto bytes = Json{{"version", 1}, {"overrides", std::move(overrides)}}.dump();
    need(bytes.size() <= maximumBytes, "Shortcut preferences exceed the 256 KiB storage limit");
    const auto span = std::as_bytes(std::span(bytes.data(), bytes.size()));
    (void)decode(span);
    if(!path_.empty()) {
      if(!readable_) throw Api::ApiError(-32001, "Shortcut preferences could not be read; reload before changing them");
      FileLock lock(path_);
      if(readDisk(path_) != diskBytes_)
        throw Api::ApiError(-32001, "Shortcuts changed in another session; reload before changing them");
      // Allocate before publishing. A failed write preserves both live bindings
      // and disk contents, and cannot erase a prefix the user is entering.
      std::vector<std::byte> nextBytes(span.begin(), span.end());
      std::filesystem::create_directories(path_.parent_path());
      Project::writeProjectFile(path_, span, true);
      diskBytes_ = std::move(nextBytes);
    }
    overrides_.swap(next);
    active_.swap(active);
    diagnostic_.clear();
    cancel();
  }

public:
  static Stroke parseStroke(std::string_view value) {
    need(!value.empty() && value.size() <= 64, "Shortcut strokes must contain 1 to 64 characters");
    auto text = folded(value);
    Stroke result;
    size_t start = 0;
    while(true) {
      const auto split = text.find('+', start);
      if(split == std::string::npos) { result.key = text.substr(start); break; }
      const auto modifier = text.substr(start, split - start);
      unsigned flag = 0;
      if(modifier == "ctrl") flag = control;
      else if(modifier == "alt") flag = alt;
      else if(modifier == "shift") flag = shift;
      else throw Api::ApiError(-32602, "Windows shortcuts use Ctrl, Alt and Shift modifiers");
      need(!(result.modifiers & flag), "A shortcut modifier cannot appear twice");
      result.modifiers |= flag;
      start = split + 1;
    }
    if(result.key == "enter") result.key = "return";
    else if(result.key == "esc") result.key = "escape";
    else if(result.key == "pgup") result.key = "pageup";
    else if(result.key == "pgdn") result.key = "pagedown";
    bool valid = result.key.size() == 1 && result.key[0] >= '!' && result.key[0] <= '~' && result.key[0] != '+';
    static constexpr std::string_view names[] = {"space", "tab", "return", "escape", "backspace", "delete", "insert",
      "home", "end", "left", "right", "up", "down", "pageup", "pagedown", "plus"};
    for(const auto name : names) if(result.key == name) valid = true;
    if(result.key.size() >= 2 && result.key[0] == 'f')
      for(unsigned i = 1; i <= 24; ++i) if(result.key == "f" + std::to_string(i)) valid = true;
    need(valid, "Use one printable ASCII key or a named Windows key such as space, return, left or f6");
    const bool ctrl = (result.modifiers & control) != 0, option = (result.modifiers & alt) != 0;
    need(!(ctrl && option && result.key == "delete") &&
      !(ctrl && result.key == "escape") && !(option && (result.key == "tab" || result.key == "escape")),
      "This shortcut is reserved by Windows");
    return result;
  }
  explicit WorkspaceShortcuts(std::vector<Definition> definitions = {}) {
    need(definitions.size() <= maximumCommands, "Too many configurable commands");
    for(const auto &definition : definitions) {
      validateId(definition.id);
      need(defaults_.emplace(definition.id, parse(definition.defaultKeys)).second, "Duplicate command ID");
    }
    active_ = effective({});
  }
  std::vector<std::string> keys(const std::string &id) const {
    (void)known(id);
    return encoded(active_.at(id));
  }
  std::vector<std::string> defaults(const std::string &id) const { return encoded(known(id)); }
  bool overridden(const std::string &id) const { (void)known(id); return overrides_.contains(id); }
  bool set(const std::string &id, const std::vector<std::string> &keys) {
    const auto &original = known(id);
    auto parsed = parse(keys);
    if(parsed != original) validateCustomFirst(parsed);
    if(parsed == active_.at(id)) return false;
    auto next = overrides_;
    if(parsed == original) next.erase(id); else next[id] = std::move(parsed);
    publish(std::move(next));
    return true;
  }
  bool reset(const std::string &id) {
    (void)known(id);
    if(!overrides_.contains(id)) return false;
    auto next = overrides_;
    next.erase(id);
    publish(std::move(next));
    return true;
  }
  // An empty path keeps inspection edits entirely in memory. Invalid loads keep
  // the current bindings, but block writes until a valid explicit reload.
  bool load(const std::filesystem::path &path) noexcept {
    try {
      if(path.empty()) { path_.clear(); diskBytes_.reset(); readable_ = true; diagnostic_.clear(); return true; }
      need(path.is_absolute() && !path.filename().empty(), "Shortcut storage requires an absolute file path");
      // Copy before changing path_: reload passes an independent path copy.
      path_ = path;
      readable_ = false;
      FileLock lock(path_);
      auto bytes = readDisk(path_);
      auto next = bytes ? decode(*bytes) : Bindings{};
      auto active = effective(next);
      const bool changed = active != active_;
      overrides_.swap(next); active_.swap(active);
      diskBytes_ = std::move(bytes); readable_ = true; diagnostic_.clear();
      if(changed) cancel();
      return true;
    } catch(const std::exception &error) {
      try { diagnostic_ = std::string("Shortcut preferences were not loaded: ") + error.what(); }
      catch(...) { diagnostic_.clear(); }
      return false;
    } catch(...) { return false; }
  }
  bool reload() noexcept {
    if(path_.empty()) return true;
    try { const auto path = path_; return load(path); } catch(...) { return false; }
  }
  const std::string &diagnostic() const { return diagnostic_; }
  bool pending() const { return !prefix_.empty(); }
  uint64_t deadline() const { return deadline_; }
  std::string hint() const {
    std::string text;
    for(const auto &stroke : prefix_) { if(!text.empty()) text += " -> "; text += stroke.encoded(); }
    if(!text.empty()) text += " ...";
    return text;
  }
  bool cancel() {
    const bool changed = pending();
    prefix_.clear(); context_ = 0; deadline_ = 0;
    return changed;
  }
  bool expire(uint64_t context, uint64_t nowMilliseconds) {
    return pending() && (context != context_ || nowMilliseconds >= deadline_) ? cancel() : false;
  }
  Decision handle(const Stroke &stroke, bool repeat, uint64_t context, uint64_t nowMilliseconds) {
    need((stroke.modifiers & ~(control | alt | shift)) == 0, "Unknown Windows shortcut modifier");
    const auto normalized = parseStroke(stroke.encoded());
    expire(context, nowMilliseconds);
    if(repeat) {
      if(pending()) return {true, {}};
      for(const auto &[id, sequence] : active_)
        if(!sequence.empty() && sequence.front() == normalized) return {true, {}};
      return {};
    }
    if(pending() && normalized.key == "escape") { cancel(); return {true, {}}; }
    auto candidate = prefix_;
    candidate.push_back(normalized);
    bool matches = false;
    for(const auto &[id, sequence] : active_) if(startsWith(sequence, candidate)) {
      matches = true;
      if(sequence.size() == candidate.size()) { cancel(); return {true, id}; }
    }
    if(!matches) { const bool consumed = pending(); cancel(); return {consumed, {}}; }
    prefix_.swap(candidate); context_ = context;
    deadline_ = nowMilliseconds > std::numeric_limits<uint64_t>::max() - timeoutMilliseconds ?
      std::numeric_limits<uint64_t>::max() : nowMilliseconds + timeoutMilliseconds;
    return {true, {}};
  }
};

} // namespace ScreamSeq
