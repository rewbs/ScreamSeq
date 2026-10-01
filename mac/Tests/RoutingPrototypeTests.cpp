// Qualification prototype: prove that native buses can receive tracker voices,
// including NNA tails, without replacing the established sample mixer.
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/plugins/PlugInterface.h"
#include "soundlib/plugins/PluginManager.h"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
namespace {
VSTPluginLib &factory() { static VSTPluginLib result(nullptr, false, {}, {}); return result; }
struct Capture {
  std::vector<std::array<float, 8192>> buffers;
  std::vector<float> gains, peaks;
  explicit Capture(size_t tracks) : buffers(tracks), gains(tracks, 1), peaks(tracks, 0) {}
};
class Tap final : public IMixPlugin {
  Capture &capture_;
  size_t track_;
public:
  Tap(CSoundFile &song, SNDMIXPLUGIN &slot, Capture &capture, size_t track)
    : IMixPlugin(factory(), song, slot), capture_(capture), track_(track) { m_isResumed = true; }
  int32 GetUID() const override { return 0x52544f55; }
  int32 GetVersion() const override { return 1; }
  void Idle() override {}
  uint32 GetLatency() const override { return 0; }
  int32 GetNumPrograms() const override { return 0; }
  int32 GetCurrentProgram() override { return 0; }
  void SetCurrentProgram(int32) override {}
  PlugParamIndex GetNumParameters() const override { return 0; }
  PlugParamValue GetParameter(PlugParamIndex) override { return 0; }
  void SetParameter(PlugParamIndex, PlugParamValue, PlayState *, CHANNELINDEX) override {}
  void Resume() override { m_isResumed = true; }
  void Suspend() override { m_isResumed = false; }
  void PositionChanged() override {}
  bool IsInstrument() const override { return false; }
  bool CanRecieveMidiEvents() override { return false; }
  bool ShouldProcessSilence() override { return true; }
  int GetNumInputChannels() const override { return 2; }
  int GetNumOutputChannels() const override { return 2; }
  void Process(float *left, float *right, uint32 count) override {
    const auto *inL = m_mixBuffer.GetInputBuffer(0), *inR = m_mixBuffer.GetInputBuffer(1);
    if (track_ < capture_.buffers.size()) {
      auto &buffer = capture_.buffers[track_];
      auto gain = capture_.gains[track_];
      float peak = 0;
      for (uint32_t n = 0; n < count; ++n) {
        buffer[n * 2] = inL[n] * gain; buffer[n * 2 + 1] = inR[n] * gain;
        peak = std::max({peak, std::abs(inL[n]), std::abs(inR[n])});
      }
      capture_.peaks[track_] = std::max(capture_.peaks[track_], peak);
    } else {
      for (uint32_t n = 0; n < count; ++n) {
        float a = inL[n], b = inR[n];
        for (const auto &buffer : capture_.buffers) { a += buffer[n * 2]; b += buffer[n * 2 + 1]; }
        left[n] += a; right[n] += b;
      }
    }
  }
};
void attach(Renderer &renderer, Capture &capture) {
  auto &song = renderer.song();
  for (size_t track = 0; track <= capture.buffers.size(); ++track) {
    auto &slot = song.m_MixPlugins[track];
    slot.Info = {}; slot.fDryRatio = 0;
    slot.pMixPlugin = new Tap(song, slot, capture, track);
    if (track == capture.buffers.size()) slot.SetMasterEffect();
    else song.ChnSettings[track].nMixPlugin = PLUGINDEX(track + 1);
  }
}
std::vector<float> render(Document &doc, uint32_t block, bool routed, bool silenceFirst, uint32_t rate) {
  Capture capture(doc.song().GetNumChannels());
  if (silenceFirst) capture.gains[0] = 0;
  Renderer renderer(doc.serialize(), rate);
  if (routed) attach(renderer, capture);
  std::vector<float> out(rate * 4);
  for (uint32_t pos = 0; pos < rate * 2; pos += block) {
    uint32_t count = std::min(block, rate * 2 - pos);
    uint64_t a, f, l; tracker_audit_begin();
    renderer.render(out.data() + pos * 2, count);
    tracker_audit_end(&a, &f, &l);
    check(a + f + l == 0 && !renderer.faulted(), "Track capture has no realtime allocation, lock or capacity fault");
  }
  if (routed) check(capture.peaks[0] > 0 && capture.peaks[1] > 0, "Track meters receive separate audible voices");
  return out;
}
double error(const std::vector<float> &a, const std::vector<float> &b) {
  double maximum = 0;
  for (size_t i = 0; i < a.size(); ++i) maximum = std::max(maximum, std::abs(double(a[i]) - b[i]));
  return maximum;
}
}
int main() {
  try {
    auto doc = Document::demo();
    doc->transaction([](CSoundFile &song) {
      song.m_nInstruments = 4;
      for (int n = 1; n <= 4; ++n) {
        song.Instruments[n] = new ModInstrument(SAMPLEINDEX(n));
        song.Instruments[n]->nNNA = NewNoteAction::Continue;
      }
    });
    for (uint32_t rate : {44100, 48000, 96000}) {
      auto plain = render(*doc, 128, false, false, rate);
      auto routed = render(*doc, 128, true, false, rate);
      const auto maximum = error(plain, routed);
      std::cout << "Routing unity at " << rate << " Hz, max difference " << maximum << '\n';
      check(maximum < 2e-7, "Track capture preserves summed sample/NNA output within float conversion precision");
      check(error(routed, render(*doc, 17, true, false, rate)) < 2e-7 &&
            error(routed, render(*doc, 4096, true, false, rate)) < 2e-7, "Track routing is callback-size independent");
      auto muted = render(*doc, 128, true, true, rate);
      doc->transaction([](CSoundFile &song) {
        for (auto &pattern : song.Patterns) if (pattern.IsValid())
          for (ROWINDEX row = 0; row < pattern.GetNumRows(); ++row) *pattern.GetpModCommand(row, 0) = {};
      });
      check(error(muted, render(*doc, 128, false, false, rate)) < 2e-7, "Bus gain silences only its own notes and NNA tails");
      doc->undo();
    }
    std::cout << "PASS native track routing prototype: independent stems/meters, sample/NNA fidelity, silent bus and realtime audit\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
