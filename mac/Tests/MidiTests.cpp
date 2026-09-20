#include "../Audio/MidiInput.hpp"
#include <chrono>
#include <iostream>
#include <mach/mach_time.h>
#include <stdexcept>
#include <thread>

static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

int main() {
  MIDIClientRef client = 0;
  try {
    require(MIDIClientCreate(CFSTR("Resonance loopback test"), nullptr, nullptr, &client) == noErr,
            "Create test MIDI client");
    MIDIEndpointRef source = 0;
    require(MIDISourceCreate(client, CFSTR("Resonance test source"), &source) == noErr, "Create test MIDI source");
    {
      Tracker::MidiInput input;
      input.connect(source);
      // Send through the real CoreMIDI service, not the queue's test-facing push
      // method. Running status, interleaved clock and zero-velocity releases
      // exercise parsing as well as endpoint connection and callback delivery.
      const Byte bytes[] = {0x91, 60, 100, 0xf8, 64, 90, 0x81, 60, 0, 0x91, 64, 0, 0xb1, 123, 0};
      MIDIPacketList packets{};
      const auto timestamp = mach_absolute_time();
      auto *packet = MIDIPacketListInit(&packets);
      require(MIDIPacketListAdd(&packets, sizeof(packets), packet, timestamp, sizeof(bytes), bytes) != nullptr,
              "Build MIDI packet");
      require(MIDIReceived(source, &packets) == noErr, "Send CoreMIDI test packet");
      std::vector<Tracker::MidiEvent> received;
      for (int attempt = 0; attempt < 200 && received.size() < 5; ++attempt) {
        auto batch = input.drainSafe();
        received.insert(received.end(), batch.begin(), batch.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      require(received.size() == 5, "Receive all five MIDI events");
      require(received[0].status == 0x91 && received[0].note == 60 && received[0].velocity == 100,
              "Note-on channel and velocity");
      require(received[1].status == 0x91 && received[1].note == 64 && received[1].velocity == 90,
              "Running status survives interleaved real-time clock");
      require(received[2].status == 0x81 && received[2].note == 60, "Explicit note-off");
      require(received[3].status == 0x91 && received[3].velocity == 0, "Zero-velocity note-on release");
      require(received[4].status == 0xb1 && received[4].note == 123, "All-notes-off control");
      for (const auto &event : received)
        require(event.timestamp == timestamp, "CoreMIDI timestamp preserved");
      input.connect(0);
      require(MIDIReceived(source, &packets) == noErr, "Send after disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      require(input.drainSafe().empty(), "Disconnected source delivers no further notes");
    }
    MIDIClientDispose(client);
    std::cout << "PASS CoreMIDI loopback, running status, clock interleave, note releases, timestamps and disconnect\n";
    return 0;
  } catch (const std::exception &error) {
    if (client)
      MIDIClientDispose(client);
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
