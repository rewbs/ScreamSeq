#pragma once
#include "common/stdafx.h"
#include "soundlib/Sndfile.h"
#include <string>
#include <vector>

namespace Tracker {
struct PatternCommandInfo {
  uint8_t command = 0, mask = 0, value = 0, suggested = 0;
  std::string label, name, family, description;
  uint8_t minimum = 0, maximum = 255;
};
// Catalog data is format-dependent, not dependent on current parameter memory.
std::vector<PatternCommandInfo> patternCommands(OpenMPT::MODTYPE type, bool volume);
}
