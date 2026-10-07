#include "../../App/SampleWorkflowDraft.hpp"
#include <iostream>

using namespace ScreamSeq::SampleWorkflow;
namespace {
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
template<typename Action> void rejected(Action action) {
  try { action(); } catch(const std::exception &) { return; }
  throw std::runtime_error("Invalid loop draft was accepted");
}
Target target() { return {"document-a", "sample-a", "revision-a", 1, 256}; }
Json info() {
  return {{"id", "sample-a"}, {"index", 1}, {"frames", 256},
    {"loopStart", 16}, {"loopEnd", 128}, {"loop", true}, {"pingpong", false}, {"reverseLoop", true},
    {"sustainStart", 32}, {"sustainEnd", 96}, {"sustainLoop", true}, {"sustainPingpong", true}, {"sustainReverse", false}};
}
void completeLoopRequest() {
  const auto raw = readLoops(info());
  check(raw.normal == LoopRaw{L"16", L"128", true, false, true} &&
    raw.sustain == LoopRaw{L"32", L"96", true, true, false}, "Saved normal/sustain fields lost independent modes or bounds");
  const auto preview = loopParams(target(), raw, true);
  check(preview == Json({{"sample", 1}, {"expectedRevision", "revision-a"}, {"dryRun", true},
    {"normal", {{"start", 16}, {"end", 128}, {"enabled", true}, {"pingpong", false}, {"reverse", true}}},
    {"sustain", {{"start", 32}, {"end", 96}, {"enabled", true}, {"pingpong", true}, {"reverse", false}}}}),
    "Loop request did not contain both complete loops and the captured revision");
  auto apply = preview; apply["dryRun"] = false;
  check(loopParams(target(), raw, false) == apply, "Preview and Apply changed loop intent");
  auto disabled = raw;
  disabled.normal.enabled = false;
  disabled.sustain = {L"256", L"256", false, true, false};
  const auto request = loopParams(target(), disabled, false);
  check(request["normal"]["start"] == 16 && request["normal"]["end"] == 128 &&
    request["normal"]["reverse"] == false && request["sustain"]["start"] == 256 &&
    request["sustain"]["end"] == 256 && request["sustain"]["pingpong"] == false,
    "Disabling a loop discarded bounds or submitted active direction flags");
  check(disabled.normal.reverse && disabled.sustain.pingpong, "Building a request changed retained direction choices");
}
void strictParsing() {
  auto raw = readLoops(info());
  raw.normal.start = L" 00016\t"; raw.normal.end = L"256";
  check(loopParams(target(), raw, true)["normal"]["start"] == 16 &&
    loopParams(target(), raw, true)["normal"]["end"] == 256, "Whole-frame parsing lost padding or exclusive sample end");
  for(const auto &text : {L"", L" ", L"-1", L"+1", L"3.", L"1.5", L"1e2", L"NaN", L"inf", L"2 3",
      L"257", L"4294967296", L"18446744073709551616", L"１２", L"000000000000000000000000000000000"}) {
    auto invalid = raw; invalid.normal.start = text;
    rejected([&] { loopParams(target(), invalid, true); });
  }
  auto invalid = raw; invalid.sustain.end = L"31";
  rejected([&] { loopParams(target(), invalid, false); });
  invalid = raw; invalid.normal.start = invalid.normal.end;
  rejected([&] { loopParams(target(), invalid, false); });
  invalid = raw; invalid.normal.pingpong = true;
  rejected([&] { loopParams(target(), invalid, false); });
  invalid.normal.enabled = false;
  rejected([&] { loopParams(target(), invalid, false); });
  for(unsigned field = 0; field < 4; ++field) {
    auto invalidTarget = target();
    if(field == 0) invalidTarget.document.clear();
    else if(field == 1) invalidTarget.id.clear();
    else if(field == 2) invalidTarget.revision.clear();
    else invalidTarget.sample = 0;
    rejected([&] { loopParams(invalidTarget, raw, true); });
  }
  auto large = target(); large.frames = std::numeric_limits<unsigned>::max();
  raw.normal = {L"4294967294", L"4294967295", true, false, false};
  check(loopParams(large, raw, true)["normal"]["end"] == uint64_t(4294967295), "Valid large frame boundary narrowed before validation");
}
void retainedRawAndStaleness() {
  LoopDraft draft;
  check(!draft.loaded() && !draft.dirty(), "Empty draft began dirty");
  rejected([&] { draft.params(target(), true); });
  rejected([&] { draft.replaceRaw(readLoops(info())); });
  draft.reset(target(), info());
  check(draft.loaded() && !draft.dirty() && draft.raw() == draft.baseline(), "Reset did not install a complete clean baseline");
  auto raw = draft.raw(); raw.normal.start = L"3.";
  check(draft.replaceRaw(raw) && draft.dirty() && !draft.replaceRaw(raw), "Invalid raw input was not retained independently or equivalent update was not a no-op");
  const auto baseline = draft.baseline();
  rejected([&] { draft.params(target(), true); });
  check(draft.raw().normal.start == L"3." && draft.baseline() == baseline && draft.target() == target(), "Validation discarded or rebased raw fields");
  raw.normal.start = L"24"; draft.replaceRaw(raw);
  const auto request = draft.params(target(), true);
  for(unsigned field = 0; field < 5; ++field) {
    auto changed = target();
    if(field == 0) changed.document = "other-document";
    else if(field == 1) changed.id = "replacement-in-same-slot";
    else if(field == 2) changed.revision = "unrelated-settings-edit";
    else if(field == 3) changed.sample = 2;
    else changed.frames = 255;
    rejected([&] { draft.params(changed, true); });
    rejected([&] { draft.params(changed, false); });
    check(draft.target() == target() && draft.raw() == raw && draft.baseline() == baseline,
      "Unrelated target change modified a retained loop draft");
  }
  check(draft.params(target(), true) == request, "Rejected stale request changed the preview signature");
  auto fresh = target(); fresh.revision = "revision-b";
  draft.reset(fresh, info());
  check(draft.target() == fresh && !draft.dirty() && draft.raw() == baseline, "Explicit loop reload did not retire stale raw fields");
}
void atomicLoadAndCompletion() {
  LoopDraft draft; draft.reset(target(), info());
  auto raw = draft.raw(); raw.sustain.end = L"100"; draft.replaceRaw(raw);
  for(unsigned error = 0; error < 7; ++error) {
    auto invalid = info();
    if(error == 0) invalid["loopStart"] = -1;
    else if(error == 1) invalid["sustainEnd"] = 257;
    else if(error == 2) invalid["loopEnd"] = 2.5;
    else if(error == 3) invalid["frames"] = 255;
    else if(error == 4) invalid["id"] = "replacement";
    else if(error == 5) invalid["index"] = 2;
    else invalid.erase("sustainStart");
    rejected([&] { draft.reset(target(), invalid); });
    check(draft.raw() == raw && draft.target() == target() && draft.dirty(), "Failed reload replaced retained state");
  }
  const auto submitted = draft.raw();
  auto newer = submitted; newer.normal.end = L"144"; draft.replaceRaw(newer);
  auto committed = target(); committed.revision = "committed";
  check(draft.acceptApplied(target(), submitted, committed), "Matching Apply completion was rejected");
  check(draft.target() == committed && draft.raw() == newer && draft.baseline() == submitted && draft.dirty(),
    "Apply completion erased a newer gesture or failed to advance only its baseline");
  check(!draft.acceptApplied(target(), submitted, committed), "An old completion was accepted twice after the revision advanced");
  auto wrong = committed; wrong.id = "other-sample";
  check(!draft.acceptApplied(committed, newer, wrong), "A completion retargeted to another sample identity");
  wrong = committed; wrong.document = "other-song";
  check(!draft.acceptApplied(committed, newer, wrong), "A completion retargeted to another document");
  wrong = committed; wrong.frames = 255;
  check(!draft.acceptApplied(committed, newer, wrong), "An unrelated PCM replacement rebased the loop draft");
  wrong = committed; wrong.sample = 2;
  check(!draft.acceptApplied(committed, newer, wrong), "A completion changed the captured sample slot");
  wrong = committed; wrong.revision.clear();
  rejected([&] { draft.acceptApplied(committed, newer, wrong); });
  check(draft.target() == committed && draft.raw() == newer && draft.baseline() == submitted,
    "Rejected completion changed the active draft");
  auto next = committed; next.revision = "next";
  check(draft.acceptApplied(committed, newer, next) && !draft.dirty(), "Successful latest-gesture completion did not become clean");
  check(draft.params(next, true)["expectedRevision"] == "next", "Next request kept the old revision");
  draft.reset(target(), info());
  check(!draft.acceptApplied(committed, newer, next) && draft.target() == target(), "A completion overwrote an explicit reload");
}
} // namespace
int main() {
  try {
    completeLoopRequest(); strictParsing(); retainedRawAndStaleness(); atomicLoadAndCompletion();
    std::cout << "Sample workflow loop parsing, retained drafts and completion guards passed\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
