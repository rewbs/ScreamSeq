#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace OpenMPT {struct ModInstrument;}
namespace Tracker {
// Fixed generator adapters belong to the playback engine. A publication only
// changes the bindings below; vendor endpoints and their MIDI ownership remain
// separately retained by the mixer/note plans.
struct InstrumentSourceBindings {
  uint64_t revision=0;
  struct Binding {
    OpenMPT::ModInstrument *instrument=nullptr;
    uint64_t identity=0;
    uint16_t slot=0;
    uint8_t midiChannel=0;
    bool plugin=false;
  };
  std::array<Binding,256> instruments{};
  std::vector<std::string> generators; // Producer ownership/allocator snapshot.
  size_t storageBytes() const noexcept {
    size_t result=sizeof(*this)+generators.capacity()*sizeof(std::string);
    for(const auto &id:generators)result+=id.capacity();
    return result;
  }
};
}
