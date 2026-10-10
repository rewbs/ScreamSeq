#pragma once
#include "editor/TrackerDocument.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/plugins/PlugInterface.h"
#include <algorithm>
#include <array>
#include <cmath>

// Included by native provider suites with check() and their realtime audit.
// The caller supplies the actual deterministic VST3 instrument on its platform.
static void columnMuteOwnershipChecks(const Tracker::PluginDescriptor &descriptor,
  uint32_t rate, uint32_t block, bool sample, bool samePitch) {
  using namespace Tracker;
  using namespace OpenMPT;
  check(block > 0 && block <= 512, "Column mute fixture block exceeds scratch capacity");
  auto source = Document::demo();
  source->transaction([&](CSoundFile &song) {
    for (auto &pattern : song.Patterns) if (pattern.IsValid()) for (auto &cell : pattern) cell.Clear();
    song.m_nInstruments = 1;
    song.Instruments[1] = new ModInstrument(SAMPLEINDEX(1));
    song.Instruments[1]->nNNA = NewNoteAction::Continue;
    auto &wave = song.GetSample(1); wave.SetLoop(0, wave.nLength, true, false, song);
    for (unsigned c = 0; c < 2; ++c) for (unsigned row = 0; row < 3; ++row) {
      auto &cell = *song.Patterns[0].GetpModCommand(row, c);
      cell.note = uint8_t(49 + row + (samePitch ? 0 : c * 7)); cell.instr = 1;
    }
  });
  const auto original = source->native(); const auto bytes = source->snapshotData();
  PluginState instrument{descriptor}; instrument.instrument = 1; instrument.instanceID = "queued-column-synth";
  auto renderer = std::make_unique<Renderer>(bytes, rate);
  auto chain = std::make_unique<PluginChain>(sample ? std::vector<PluginState>{} : std::vector<PluginState>{instrument}, rate, true);
  chain->attachInstruments(*renderer, &source->native());
  std::array<float, 1024> pcm{};
  const auto render = [&] {
    uint64_t allocations, frees, locks;
    tracker_audit_begin();
    chain->beginRenderBlock(); renderer->render(pcm.data(), block);
    const bool okay = chain->process(pcm.data(), block);
    tracker_audit_end(&allocations, &frees, &locks);
    check(okay && !renderer->faulted() && allocations + frees + locks == 0,
      "Queued column mute failed rendering or native host realtime audit");
    for (unsigned i = 0; i < block * 2; ++i) check(std::isfinite(pcm[i]), "Nonfinite queued mute PCM");
  };
  for (unsigned elapsed = 0; elapsed < rate / 3; elapsed += block) render();
  auto *plugin = renderer->song().m_MixPlugins[0].pMixPlugin;
  check(sample || plugin, "Instrument fixture did not attach to the renderer");
  const auto owned = [&](unsigned parent) {
    size_t count = 0;
    for (CHANNELINDEX c = renderer->song().GetNumChannels(); c < MAX_CHANNELS; ++c) {
      const auto &voice = renderer->song().m_PlayState.Chn[c];
      if (voice.nMasterChn == parent && (sample ? bool(voice.nLength) : plugin->IsNotePlaying(voice.lastMidiNoteWithoutArp, c))) ++count;
    }
    return count;
  };
  check(owned(1) > 0 && owned(2) > 0, "Both fixture columns must own sustained NNA voices");
  auto native = original;
  const auto mute = [&](unsigned channel, bool value) {
    native.columnMutes[native.tracks.at(channel).id] = value;
    auto frame = renderer->prepareColumnMuteUpdate(native);
    check(frame && renderer->publishColumnMuteUpdate(frame) && !frame,
      "Complete column mute frame was not admitted");
  };
  mute(0, true); render();
  for (CHANNELINDEX c = 0; c < MAX_CHANNELS; ++c) {
    const auto &voice = renderer->song().m_PlayState.Chn[c];
    const bool first = c == 0 || (c >= renderer->song().GetNumChannels() && voice.nMasterChn == 1);
    const bool second = c == 1 || (c >= renderer->song().GetNumChannels() && voice.nMasterChn == 2);
    if (sample && voice.nLength && (first || second))
      check(bool(voice.dwFlags[CHN_MUTE]) == first, "Queued sample mute changed the wrong foreground/NNA owner");
    if (!sample && first)
      check(!plugin->IsNotePlaying(voice.lastMidiNoteWithoutArp, c), "Queued mute left an owned plugin voice playing");
  }
  check(owned(2) > 0, "Muting one column released another column's sustained notes");
  if (!sample) check(std::abs(pcm[block * 2 - 1]) > .01f, "Other column's shared plugin became silent");
  if (sample) {
    mute(0, false); render();
    check(owned(1) > 0, "Unmute lost surviving sample NNA voices");
    for (const auto &voice : renderer->song().m_PlayState.BackgroundChannels(renderer->song()))
      if (voice.nMasterChn == 1 && voice.nLength) check(!voice.dwFlags[CHN_MUTE], "Unmute did not restore its sample NNA voices");
    mute(0, true); render();
  }
  mute(1, true);
  for (unsigned elapsed = 0; elapsed < 2048; elapsed += block) render();
  if (!sample) check(std::abs(pcm[block * 2 - 1]) < 1e-8f, "Muted columns left stuck plugin notes");
  // A manually auditioned voice has independent ownership from pattern columns.
  check(renderer->preview({61, 1, 100, true}), "Manual audition was refused"); render();
  mute(0, false); render(); mute(0, true); render();
  if (!sample) check(std::abs(pcm[block * 2 - 1]) > .01f, "Column mute released an independent audition voice");
  else check(std::any_of(pcm.begin(), pcm.begin() + block * 2, [](float value) { return std::abs(value) > 1e-7f; }),
    "Column mute silenced an independent sample audition voice");
  check(renderer->preview({61, 1, 0, false}), "Manual audition release was refused");
  for (unsigned elapsed = 0; elapsed < 2048; elapsed += block) render();
  if (!sample) check(std::abs(pcm[block * 2 - 1]) < 1e-8f, "Audition release left stuck plugin notes");
  check(source->native() == original && source->snapshotData() == bytes, "Live publication changed the source document");
}
