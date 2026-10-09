#pragma once
#include <windows.h>
#include <cstdint>

namespace Tracker::WindowsVST3 {
// A module must match this process, not the OS architecture (an x64 process on
// ARM64 Windows still loads x64 DLLs). Keep incompatible caches separate.
#if defined(_M_ARM64EC)
#error The native VST3 provider does not support ARM64EC.
#elif defined(_M_ARM64) || defined(__aarch64__)
inline constexpr uint16_t nativeMachine=IMAGE_FILE_MACHINE_ARM64;
inline constexpr const wchar_t *nativeBundleDirectory=L"arm64-win";
inline constexpr const wchar_t *nativeCacheName=L"vst3-arm64-cache.json";
#elif defined(_M_X64) || defined(__x86_64__)
inline constexpr uint16_t nativeMachine=IMAGE_FILE_MACHINE_AMD64;
inline constexpr const wchar_t *nativeBundleDirectory=L"x86_64-win";
inline constexpr const wchar_t *nativeCacheName=L"vst3-x64-cache.json";
#else
#error The native VST3 provider requires an ARM64 or x64 build.
#endif
}
