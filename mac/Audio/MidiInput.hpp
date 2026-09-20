#pragma once
#include <CoreMIDI/CoreMIDI.h>
#include <array>
#include <atomic>
#include <string>
#include <vector>
namespace Tracker {
struct MidiEvent {
  uint64_t timestamp;
  uint8_t status, note, velocity;
};
struct MidiSource {
  uint32_t id;
  std::string name;
};
class MidiInput {
  struct Slot {
    std::atomic<uint32_t> sequence{};
    MidiEvent event{};
  };
  std::array<Slot, 1024> queue_{};
  std::atomic<uint32_t> head_{0};
  uint32_t tail_ = 0;
  std::atomic<uint32_t> drops_{0};
  uint32_t deliveredDrops_ = 0;
  bool discarding_ = false;
  MIDIClientRef client_ = 0;
  MIDIPortRef port_ = 0;
  MIDIEndpointRef virtual_ = 0, source_ = 0;
  static void receive(const MIDIPacketList *, void *, void *);

public:
  MidiInput();
  ~MidiInput();
  static std::vector<MidiSource> sources();
  void connect(uint32_t id);
  void push(MidiEvent) noexcept;
  std::vector<MidiEvent> drain();
  std::vector<MidiEvent> drainSafe();
  uint32_t dropped() const { return drops_.load(); }
};
} // namespace Tracker
