#include "../Audio/AudioUnitHost.hpp"
#import <Foundation/Foundation.h>
#include <cerrno>
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
  @autoreleasepool {
    // Third-party plugins sometimes log to stdout. Reserve the original pipe
    // for our JSON protocol and send plugin diagnostics to stderr instead.
    const int protocol = dup(STDOUT_FILENO);
    if (protocol < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
      return 1;
    auto respond = [&](NSData *data) {
      const auto *bytes = static_cast<const char *>(data.bytes);
      size_t remaining = data.length;
      while (remaining) {
        auto sent = write(protocol, bytes, remaining);
        if (sent < 0 && errno == EINTR)
          continue;
        if (sent <= 0)
          throw std::runtime_error("Cannot return scanner result");
        bytes += sent;
        remaining -= sent;
      }
    };
    try {
      if ((argc == 2 && std::string(argv[1]) == "--list") || (argc == 3 && std::string(argv[1]) == "--list-vst3")) {
        auto list = [NSMutableArray array];
        for (auto &plugin :
             (argc == 3 ? Tracker::NativePlugin::discoverVST3(argv[2]) : Tracker::NativePlugin::discover()))
          [list addObject:@{
            @"type" : @(plugin.type),
            @"subtype" : @(plugin.subtype),
            @"manufacturer" : @(plugin.manufacturer),
            @"name" : @(plugin.name.c_str()),
            @"format" : @(plugin.format.c_str()),
            @"path" : @(plugin.path.c_str()),
            @"classID" : @(plugin.classID.c_str()),
            @"isInstrument" : @(plugin.instrument)
          }];
        auto data = [NSJSONSerialization dataWithJSONObject:list options:0 error:nil];
        respond(data);
        return 0;
      }
      if (argc == 5 && (std::string(argv[1]) == "--validate" || std::string(argv[1]) == "--validate-vst3")) {
        Tracker::PluginState state;
        if (std::string(argv[1]) == "--validate-vst3") {
          state.descriptor.format = "VST3";
          state.descriptor.path = argv[2];
          state.descriptor.classID = argv[3];
          state.descriptor.instrument = std::string(argv[4]) == "1";
        } else
          state.descriptor = {uint32_t(std::stoul(argv[2])), uint32_t(std::stoul(argv[3])),
                              uint32_t(std::stoul(argv[4])), "Candidate"};
        Tracker::NativePlugin unit(state, 48000);
        std::array<float, 512> data{};
        data[0] = data[1] = 0.1f;
        if (unit.isInstrument() && !unit.midi(0x90, 60, 100))
          throw std::runtime_error("Instrument rejected a note");
        for (int i = 0; i < 32; ++i)
          if (!unit.process(data.data(), 256, i * 256))
            throw std::runtime_error("Plugin failed its render probe");
        if (unit.isInstrument())
          unit.midi(0x80, 60, 0);
        auto saved = unit.state();
        Tracker::NativePlugin restored(saved, 48000);
        auto parameters = restored.parameters();
        respond([NSJSONSerialization dataWithJSONObject:@{
          @"valid" : @YES,
          @"parameters" : @(parameters.size()),
          @"latency" : @(restored.latency())
        }
                                                options:0
                                                  error:nil]);
        return 0;
      }
      return 2;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
}
