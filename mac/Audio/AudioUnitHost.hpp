#pragma once
// Compatibility include. Shared callers should include editor/hosted/HostedAudio.hpp.
#include "../../editor/hosted/HostedAudio.hpp"
#ifdef __APPLE__
// Preserve historical transitive SDK declarations for Mac clients.
#include <AudioToolbox/AudioToolbox.h>
#else
// Existing portable PluginAssignments.cpp compares this persisted FourCC.
inline constexpr uint32_t kAudioUnitType_MusicDevice = Tracker::audioUnitMusicDeviceType;
#endif
