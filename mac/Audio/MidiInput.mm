#include "MidiInput.hpp"
#import <Foundation/Foundation.h>
#include <mach/mach_time.h>
#include <stdexcept>
namespace Tracker {
MidiInput::MidiInput() {
  for (uint32_t i = 0; i < queue_.size(); ++i)
    queue_[i].sequence = i;
  if (MIDIClientCreate(CFSTR("ScreamSeq"), nullptr, nullptr, &client_))
    return;
  MIDIInputPortCreate(client_, CFSTR("Tracker notes"), receive, this, &port_);
  MIDIDestinationCreate(client_, CFSTR("ScreamSeq MIDI In"), receive, this, &virtual_);
}
MidiInput::~MidiInput() {
  if (client_)
    MIDIClientDispose(client_);
}
std::vector<MidiSource> MidiInput::sources() {
  std::vector<MidiSource> out;
  for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i) {
    auto source = MIDIGetSource(i);
    CFStringRef name = nullptr;
    MIDIObjectGetStringProperty(source, kMIDIPropertyDisplayName, &name);
    out.push_back({source, name ? std::string([(__bridge NSString *)name UTF8String]) : "MIDI input"});
    if (name)
      CFRelease(name);
  }
  return out;
}
void MidiInput::connect(uint32_t id) {
  if (!port_)
    throw std::runtime_error("CoreMIDI is unavailable.");
  if (source_)
    MIDIPortDisconnectSource(port_, source_);
  source_ = 0;
  if (id) {
    if (MIDIPortConnectSource(port_, id, nullptr))
      throw std::runtime_error("Cannot connect MIDI source.");
    source_ = id;
  }
}
void MidiInput::push(MidiEvent event) noexcept {
  uint32_t position = head_.load(std::memory_order_relaxed);
  for (int attempts = 0; attempts < 8; ++attempts) {
    auto &slot = queue_[position % queue_.size()];
    auto sequence = slot.sequence.load(std::memory_order_acquire);
    int32_t difference = int32_t(sequence - position);
    if (difference == 0) {
      if (head_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
        slot.event = event;
        slot.sequence.store(position + 1, std::memory_order_release);
        return;
      }
    } else if (difference < 0)
      break;
    else
      position = head_.load(std::memory_order_relaxed);
  }
  drops_.fetch_add(1, std::memory_order_relaxed);
}
std::vector<MidiEvent> MidiInput::drain() {
  std::vector<MidiEvent> result;
  result.reserve(128);
  for (int i = 0; i < 1024; ++i) {
    auto &slot = queue_[tail_ % queue_.size()];
    if (slot.sequence.load(std::memory_order_acquire) != tail_ + 1)
      break;
    result.push_back(slot.event);
    slot.sequence.store(tail_ + uint32_t(queue_.size()), std::memory_order_release);
    ++tail_;
  }
  return result;
}
std::vector<MidiEvent> MidiInput::drainSafe() {
  auto result = drain();
  const auto drops = drops_.load(std::memory_order_acquire);
  discarding_ = discarding_ || drops != deliveredDrops_;
  deliveredDrops_ = drops;
  if (discarding_) {
    // A lost release invalidates the whole pending batch. Emitting panic before
    // queued note-ons would immediately restart notes with no matching release.
    result.clear();
    result.push_back({0, 0xb0, 123, 0});
    // A producer may have reserved a slot without publishing it yet. Quarantine
    // those older events too, until every outstanding reservation is drained.
    if (tail_ == head_.load(std::memory_order_acquire))
      discarding_ = false;
  }
  return result;
}
void MidiInput::receive(const MIDIPacketList *list, void *ref, void *) {
  auto &self = *static_cast<MidiInput *>(ref);
  auto packet = &list->packet[0];
  for (UInt32 p = 0; p < list->numPackets; ++p) {
    uint8_t status = 0, data[2]{};
    int count = 0;
    for (UInt16 i = 0; i < packet->length; ++i) {
      auto value = packet->data[i];
      if (value >= 0xf8)
        continue;
      if (value & 0x80) {
        status = value;
        count = 0;
        continue;
      }
      if (status < 0x80 || status >= 0xf0)
        continue;
      data[count++] = value;
      int needed = (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 1 : 2;
      if (count == needed) {
        if ((status & 0xf0) == 0x80 || (status & 0xf0) == 0x90 || (status & 0xf0) == 0xb0)
          self.push({packet->timeStamp ? packet->timeStamp : mach_absolute_time(), status, data[0], data[1]});
        count = 0;
      }
    }
    packet = MIDIPacketNext(packet);
  }
}
} // namespace Tracker
