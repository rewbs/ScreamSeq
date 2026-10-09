#include "HostedAudio.hpp"
#include "editor/NativeSong.hpp"
#include "soundlib/Sndfile.h"
#include "soundlib/ModInstrument.h"
namespace Tracker {
bool PluginChain::patternPitchSource(const OpenMPT::CSoundFile &song,uint16_t voice,NoteSource &source,uint8_t &midiChannel) const noexcept {
  if(!noteLedger_||!activeNoteRouting_||voice>=song.m_PlayState.Chn.size())return false;
  const auto &channel=song.m_PlayState.Chn[voice];const auto *instrument=channel.pModInstrument;
  if(!instrument)return false;
  // Native sample bends still use the phase-ratio path. A channel cable must
  // never turn those sample-instrument commands into plugin MIDI.
  bool pluginInstrument=false;
  for(size_t i=1;i<activeNoteRouting_->pluginInstruments.size()&&i<=song.GetNumInstruments();++i)
    if(song.Instruments[i]==instrument){pluginInstrument=activeNoteRouting_->pluginInstruments[i];break;}
  if(!pluginInstrument)return false;
  const void *origin=instrument;
  if(instrument->nMixPlug&&instrument->nMixPlug<=song.m_MixPlugins.size())origin=song.m_MixPlugins[instrument->nMixPlug-1].pMixPlugin;
  source=noteSource(origin,song,voice,instrument);
  midiChannel=instrument->GetMIDIChannel(channel,voice);return true;
}
bool PluginChain::schedulePitchMIDI(const NoteSource &source,uint8_t midiChannel,uint16_t wheel,uint64_t frame) noexcept {
  const bool ok=noteLedger_->scheduleControl(source,uint8_t(0xe0|midiChannel),uint8_t(wheel&127),uint8_t(wheel>>7),frame);
  if(!ok)failed_=true;return ok;
}
}
