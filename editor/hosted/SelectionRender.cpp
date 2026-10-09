#include "SelectionRender.hpp"
#include "HostedAudio.hpp"
#include <cmath>

namespace Tracker {
SelectionRenderResult renderPatternSelection(const PreparedSamplingSelection &selection,
                                             const std::vector<PluginState> &states,
                                             const std::vector<ParameterChange> &automation,
                                             double tailSeconds, uint32_t blockFrames) {
  if(!std::isfinite(tailSeconds) || tailSeconds < 0 || tailSeconds > 60)
    throw std::invalid_argument("Selection tail must be between 0 and 60 seconds");
  if(!blockFrames || blockFrames > 4096) throw std::invalid_argument("Invalid offline render block size");
  const auto &s = selection.selection;
  // Document queues have small worker stacks. Renderer owns large fixed audio
  // state, so keep it (and the hosted graph) off that stack, as in full export.
  auto rendererStorage = std::make_unique<Renderer>(selection.module, selectionRenderRate, selection.order, false,
                    std::string{}, selection.sequence,
                    PlaybackRegion{s.pattern, 0, uint32_t(s.lastRow)+1, 0, false, false}, &selection.native);
  auto &renderer = *rendererStorage;
  SelectionRenderResult result;
  result.automationStartFrame = renderer.telemetry().frames;
  auto effectsStorage = std::make_unique<PluginChain>(states, selectionRenderRate, true, automation, result.automationStartFrame);
  auto &effects = *effectsStorage;
  // Source isolation must not disable global tempo/row effects under the S3M
  // "ignore muted channels" compatibility rule. This private renderer retains
  // those effects while preventing the excluded channel's sample/MIDI source.
  renderer.song().m_playBehaviour.reset(kST3NoMutedChannels);
  renderer.applyColumnMutes(selection.native, renderer.song());
  for(uint32_t channel = 0; channel < renderer.song().GetNumChannels(); ++channel)
    if(channel < s.firstChannel || channel > s.lastChannel) renderer.mute(channel, true);
  effects.attachInstruments(renderer, &selection.native);
  effects.attachMusicalAutomation(renderer, selection.native);
  SamplingRenderWindow window(renderer, s.firstRow);
  const double latency = effects.latency();
  if(!std::isfinite(latency) || latency < 0 || latency > 60) throw std::runtime_error("Plugin latency exceeds the selection render limit");
  result.latencyFrames = uint64_t(std::ceil(latency*selectionRenderRate));
  result.tailFrames = uint64_t(std::llround(tailSeconds*selectionRenderRate));
  std::vector<float> buffer(blockFrames*2);
  uint64_t position = 0, end = UINT64_MAX;
  bool ended = false;
  while(position < end) {
    if(position >= uint64_t(selectionRenderRate)*300) throw std::runtime_error("Selection render exceeded its 300-second total audio limit (including warm-up and tail)");
    if(effects.latencyChangePending()) throw std::runtime_error("A plugin changed latency during selection rendering; no sample was added");
    effects.beginRenderBlock(); effects.syncTransport(renderer);
    const uint64_t remainingBudget = uint64_t(selectionRenderRate)*300-position;
    uint32_t count = uint32_t(std::min<uint64_t>(blockFrames, remainingBudget));
    const uint32_t requested = count;
    if(!ended) {
      count = renderer.render(buffer.data(), count);
      if(renderer.faulted()) throw std::runtime_error("Selection rendering exceeded the engine capacity");
    } else {
      count = uint32_t(std::min<uint64_t>(count, end-position));
      std::fill(buffer.begin(), buffer.end(), 0.f);
    }
    if(count && !effects.process(buffer.data(), count)) throw std::runtime_error("A plugin failed during selection rendering; no sample was added");
    if(effects.latencyChangePending()) throw std::runtime_error("A plugin changed latency during selection rendering; no sample was added");
    if(!ended && count < requested) {
      ended = true;
      if(window.firstFrame() == UINT64_MAX || window.frames() <= window.firstFrame()) throw std::runtime_error("Playback could not reach the complete selected range");
      result.preRollFrames = window.firstFrame();
      result.selectedFrames = window.frames()-window.firstFrame();
      if(result.selectedFrames + result.tailFrames > maximumRecordedSampleFrames) throw std::invalid_argument("Rendered selection exceeds the 16777216-frame sample limit");
      end = window.frames()+result.latencyFrames+result.tailFrames;
      effects.endNotes();
    }
    if(window.firstFrame() != UINT64_MAX) {
      const uint64_t first = std::max(position, window.firstFrame()+result.latencyFrames);
      const uint64_t last = std::min(position+count, end);
      if(last > first) {
        if(result.pcm.size()/2 + last-first > maximumRecordedSampleFrames) throw std::invalid_argument("Rendered selection exceeds the 16777216-frame sample limit");
        result.pcm.insert(result.pcm.end(), buffer.begin()+2*(first-position), buffer.begin()+2*(last-position));
      }
    }
    position += count;
  }
  if(result.pcm.size()/2 != result.selectedFrames+result.tailFrames) throw std::runtime_error("Selection rendering produced an incomplete range");
  return result;
}
}
