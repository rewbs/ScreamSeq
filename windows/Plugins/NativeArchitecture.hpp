#pragma once
#include <windows.h>
#include <cstdint>

namespace Tracker::WindowsVST3 {
// A module must match this process, not the OS architecture (an x64 process on
// ARM64 Windows still loads x64 DLLs). Keep incompatible caches separate.
#if defined(_M_ARM64EC)
// Final ARM64EC images use the AMD64 PE machine and the x64-compatible ABI.
// Windows executes EC host code natively and emulates x64 plugin code in-process.
inline constexpr uint16_t nativeMachine=IMAGE_FILE_MACHINE_AMD64;
inline constexpr const wchar_t *nativeBundleDirectory=L"x86_64-win";
inline constexpr const wchar_t *nativeCacheName=L"vst3-arm64ec-cache.json";
#elif defined(_M_ARM64) || defined(__aarch64__)
inline constexpr uint16_t nativeMachine=IMAGE_FILE_MACHINE_ARM64;
inline constexpr const wchar_t *nativeBundleDirectory=L"arm64-win";
inline constexpr const wchar_t *nativeCacheName=L"vst3-arm64-cache.json";
#elif defined(_M_X64) || defined(__x86_64__)
inline constexpr uint16_t nativeMachine=IMAGE_FILE_MACHINE_AMD64;
inline constexpr const wchar_t *nativeBundleDirectory=L"x86_64-win";
inline constexpr const wchar_t *nativeCacheName=L"vst3-x64-cache.json";
#else
#error The native VST3 provider requires an ARM64EC, ARM64 or x64 build.
#endif
}
