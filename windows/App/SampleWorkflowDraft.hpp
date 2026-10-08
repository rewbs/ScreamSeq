#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ScreamSeq::SampleWorkflow {
using Json = nlohmann::json;

// UI-owned values only. No HWND, document mutation, or implicit refresh: an
// unrelated successful edit must not rebase a retained loop draft.
struct Target {
  std::string document, id, revision;
  unsigned sample = 0, frames = 0;
  bool operator==(const Target &) const = default;
};
struct LoopRaw {
  std::wstring start, end;
  bool enabled = false, pingpong = false, reverse = false;
  bool operator==(const LoopRaw &) const = default;
};
struct LoopsRaw {
  LoopRaw normal, sustain;
  bool operator==(const LoopsRaw &) const = default;
};

namespace Detail {
inline void require(bool value, const char *message) {
  if(!value) throw std::runtime_error(message);
}
inline void validateTarget(const Target &target) {
  require(!target.document.empty() && !target.id.empty() && !target.revision.empty() && target.sample > 0,
    "Choose a captured sample before editing loops");
}
inline bool sameSample(const Target &left, const Target &right) {
  return left.document == right.document && left.id == right.id &&
    left.sample == right.sample && left.frames == right.frames;
}
inline unsigned frame(std::wstring_view text, unsigned maximum) {
  const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
  while(!text.empty() && space(text.front())) text.remove_prefix(1);
  while(!text.empty() && space(text.back())) text.remove_suffix(1);
  require(!text.empty() && text.size() <= 32, "Enter whole loop frame numbers inside the sample");
  uint64_t value = 0;
  for(const auto c : text) {
    require(c >= L'0' && c <= L'9', "Enter whole loop frame numbers inside the sample");
    value = value * 10 + unsigned(c - L'0');
    // Check each digit before another multiplication, including overflow-size
    // input; never narrow an unvalidated user value.
    require(value <= maximum, "Loop boundaries must be inside the sample");
  }
  return unsigned(value);
}
inline Json loop(const LoopRaw &raw, unsigned frames) {
  const auto first = frame(raw.start, frames), last = frame(raw.end, frames);
  require(first <= last && (!raw.enabled || first < last),
    "Loop end must follow its start; enabled loops must be nonempty");
  require(!raw.pingpong || !raw.reverse, "Choose either ping-pong or reverse for each loop");
  // A disabled row retains its direction choice as UI state. Only enabled
  // modes are submitted, matching the shared loop API and Mac controls.
  return {{"start", first}, {"end", last}, {"enabled", raw.enabled},
    {"pingpong", raw.enabled && raw.pingpong}, {"reverse", raw.enabled && raw.reverse}};
}
inline unsigned savedFrame(const Json &info, const char *key) {
  const auto &value = info.at(key);
  require(value.is_number_integer() && (value.is_number_unsigned() || value.get<int64_t>() >= 0),
    "Saved loop boundaries are invalid");
  const auto frame = value.get<uint64_t>();
  require(frame <= std::numeric_limits<unsigned>::max(), "Saved loop boundaries exceed the supported range");
  return unsigned(frame);
}
} // namespace Detail

inline LoopsRaw readLoops(const Json &info) {
  const auto row = [&](bool sustain) {
    return LoopRaw{std::to_wstring(Detail::savedFrame(info, sustain ? "sustainStart" : "loopStart")),
      std::to_wstring(Detail::savedFrame(info, sustain ? "sustainEnd" : "loopEnd")),
      info.value(sustain ? "sustainLoop" : "loop", false),
      info.value(sustain ? "sustainPingpong" : "pingpong", false),
      info.value(sustain ? "sustainReverse" : "reverseLoop", false)};
  };
  return {row(false), row(true)};
}
inline Json loopParams(const Target &target, const LoopsRaw &raw, bool dryRun) {
  Detail::validateTarget(target);
  return {{"sample", target.sample}, {"normal", Detail::loop(raw.normal, target.frames)},
    {"sustain", Detail::loop(raw.sustain, target.frames)}, {"dryRun", dryRun}, {"expectedRevision", target.revision}};
}

class LoopDraft {
  bool loaded_ = false;
  Target target_;
  LoopsRaw raw_, baseline_;
public:
  bool loaded() const { return loaded_; }
  const Target &target() const { return target_; }
  const LoopsRaw &raw() const { return raw_; }
  const LoopsRaw &baseline() const { return baseline_; }
  bool dirty() const { return loaded_ && raw_ != baseline_; }
  void reset(Target target, const Json &info) {
    auto raw = readLoops(info);
    (void)loopParams(target, raw, true);
    if(info.contains("frames"))
      Detail::require(Detail::savedFrame(info, "frames") == target.frames, "Sample length changed while loading loops");
    if(info.contains("id"))
      Detail::require(info.at("id") == target.id, "Sample identity changed while loading loops");
    if(info.contains("index"))
      Detail::require(info.at("index") == target.sample, "Sample slot changed while loading loops");
    auto baseline = raw; // Allocate the complete replacement before publishing.
    using std::swap;
    swap(target_, target); swap(raw_, raw); swap(baseline_, baseline); loaded_ = true;
  }
  bool replaceRaw(LoopsRaw raw) {
    Detail::require(loaded_, "Load the captured sample loops first");
    if(raw_ == raw) return false;
    using std::swap; swap(raw_, raw); return true;
  }
  Json params(const Target &current, bool dryRun) const {
    Detail::require(loaded_ && current == target_, "Sample changed; Reload loops before previewing or applying");
    return loopParams(target_, raw_, dryRun);
  }
  // A completion must belong to the exact submitted target. A newer local
  // gesture survives; only its baseline and known successful revision advance.
  bool acceptApplied(const Target &submittedTarget, const LoopsRaw &submittedRaw, const Target &updatedTarget) {
    if(!loaded_ || submittedTarget != target_ || !Detail::sameSample(submittedTarget, updatedTarget)) return false;
    (void)loopParams(updatedTarget, submittedRaw, false);
    auto target = updatedTarget;
    auto baseline = submittedRaw;
    using std::swap; swap(target_, target); swap(baseline_, baseline);
    return true;
  }
};
} // namespace ScreamSeq::SampleWorkflow
