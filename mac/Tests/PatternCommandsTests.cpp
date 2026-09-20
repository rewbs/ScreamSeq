#include "editor/PatternCommands.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/mod_specifications.h"
#include <iostream>
#include <stdexcept>
#include <set>
using namespace Tracker;
using namespace OpenMPT;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    size_t count = 0;
    for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
      const auto &spec = CSoundFile::GetModSpecifications(type);
      for (bool volume : {false, true}) {
        const auto catalog = patternCommands(type, volume);
        std::set<int> supported; std::set<std::pair<int,int>> keys;
        for (const auto &c : catalog) {
          ++count;
          check(keys.emplace(c.command, c.value).second, "Duplicate/ambiguous command entry");
          check(c.command == 0 || (volume ? spec.HasVolCommand(VolumeCommand(c.command)) : spec.HasCommand(EffectCommand(c.command))), "Catalog exposes unsupported command");
          check(!c.name.empty() && !c.description.empty() && !c.family.empty(), "All catalog entries have useful metadata");
          check(c.suggested >= c.minimum && c.suggested <= c.maximum && (c.suggested & c.mask) == c.value, "Suggested value is valid for its command");
          if (c.command) check(c.label[0] == (volume ? spec.GetVolEffectLetter(VolumeCommand(c.command)) : spec.GetEffectLetter(EffectCommand(c.command))), "Catalog uses source-format letters");
          supported.insert(c.command);
        }
        for (int command = 1; command < (volume ? int(MAX_VOLCMDS) : int(MAX_EFFECTS)); ++command)
          if (volume ? spec.HasVolCommand(VolumeCommand(command)) : spec.HasCommand(EffectCommand(command)))
            if (!supported.contains(command)) throw std::runtime_error("Missing command " + std::to_string(command) + " format " + std::to_string(uint32_t(type)) + " volume " + std::to_string(volume));
      }
      const auto catalog = patternCommands(type, false);
      const auto find = [&](int code, int parameter) -> const PatternCommandInfo & {
        for (const auto &entry : catalog) if (entry.command == code && (parameter & entry.mask) == entry.value) return entry;
        throw std::runtime_error("Missing expected format-specific entry");
      };
      if (type == MOD_TYPE_MOD) check(find(CMD_MODCMDEX, 0xF3).name == "Invert Loop", "MOD EFx is sample inversion");
      if (type == MOD_TYPE_XM) check(find(CMD_MODCMDEX, 0xF3).name == "Set Active Macro", "XM EFx is macro selection");
      if (type == MOD_TYPE_IT || type == MOD_TYPE_MPT) {
        check(find(CMD_S3MCMDEX, 0xD3).name == "Note Delay", "SDx selects note delay");
        check(find(CMD_S3MCMDEX, 0x9F).description.find("backward") != std::string::npos, "Reverse playback is discoverable through sound control");
      }
    }
    std::cout << "PASS " << count << " format-filtered command entries; complete effect/volume coverage, prefixes, suggestions and MOD/XM/IT distinctions\n";
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
