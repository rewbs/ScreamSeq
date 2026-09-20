#pragma once
#include "modcommand.h"
OPENMPT_NAMESPACE_BEGIN
// Only channel-local effects with meaningful fractional-onset semantics. Row
// navigation, tempo, delayed triggers and destructive sample commands stay in
// the main pattern. Retriggers/releases are explicit native events instead.
inline bool NativeNoteEffectSupported(uint8 command, uint8 parameter)
{
  switch(command)
  {
  case CMD_NONE: return parameter == 0;
  case CMD_ARPEGGIO: case CMD_PORTAMENTOUP: case CMD_PORTAMENTODOWN:
  case CMD_VIBRATO: case CMD_VIBRATOVOL: case CMD_TREMOLO:
  case CMD_PANNING8: case CMD_OFFSET: case CMD_VOLUMESLIDE:
  case CMD_TREMOR: case CMD_CHANNELVOLSLIDE: case CMD_FINEVIBRATO:
  case CMD_PANBRELLO: case CMD_PANNINGSLIDE: case CMD_SETENVPOSITION:
  case CMD_MIDI: case CMD_SMOOTHMIDI: return true;
  case CMD_VOLUME: case CMD_CHANNELVOLUME: return parameter <= 64;
  case CMD_MODCMDEX:
    switch(parameter >> 4) {case 1: case 2: case 3: case 4: case 5: case 7: case 8: case 10: case 11: return true;}
    return false;
  case CMD_S3MCMDEX:
    switch(parameter >> 4) {case 1: case 2: case 3: case 4: case 5: case 7: case 8: case 10: case 15: return true;}
    // Surround and sample direction are local; S9A..S9D change global modes.
    return parameter == 0x90 || parameter == 0x91 || parameter == 0x9E || parameter == 0x9F;
  default: return false;
  }
}
OPENMPT_NAMESPACE_END
