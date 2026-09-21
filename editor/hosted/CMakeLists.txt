# Include after defining TrackerEditor. This target owns ONE copy of the host
# orchestration on all platforms. Existing portable source files retain their
# Mac paths to keep the extraction diff narrow; no algorithms are forked.
get_filename_component(HOSTED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(TrackerHosted STATIC
  HostedAudio.cpp PluginScheduling.cpp PluginChain.cpp
  "${HOSTED_ROOT}/mac/Audio/PluginAssignments.cpp"
  "${HOSTED_ROOT}/mac/Audio/NativeInstrument.cpp"
  "${HOSTED_ROOT}/mac/Audio/NativeSignalGraph.cpp"
  "${HOSTED_ROOT}/mac/Audio/PatternCommandRuntime.cpp"
  "${HOSTED_ROOT}/mac/Audio/PatternPitchRuntime.cpp")
target_link_libraries(TrackerHosted PUBLIC TrackerEditor)
target_compile_features(TrackerHosted PUBLIC cxx_std_20)
target_include_directories(TrackerHosted PUBLIC "${HOSTED_ROOT}")
# A provider defines platformPluginBackendFactory(), with no SDK types leaking
# into PluginBackend.hpp. Mac supplies AU+VST3; Windows supplies native VST3.
# Without a provider source list, standalone portable hosts reject both formats.
if(TRACKER_HOSTED_BACKEND_SOURCES)
  target_sources(TrackerHosted PRIVATE ${TRACKER_HOSTED_BACKEND_SOURCES})
else()
  target_sources(TrackerHosted PRIVATE UnavailableBackend.cpp)
endif()
