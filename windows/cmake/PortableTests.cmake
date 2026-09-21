# Functional regressions reused verbatim from mac/Tests. No Objective-C++,
# CoreAudio device tests, or Darwin malloc/pthread interposer are linked here.
function(screamseq_portable_test target source name)
  add_executable(${target} "${ROOT}/mac/Tests/${source}.cpp")
  target_link_libraries(${target} PRIVATE TrackerEditor)
  if(MSVC)
    target_compile_options(${target} PRIVATE "/FI${ROOT}/windows/cmake/WindowsTestPreamble.hpp")
    # Existing fixtures put several large Renderer objects on the stack. Match
    # the macOS test process's 8 MiB reserve; production renderers are heap-owned.
    target_link_options(${target} PRIVATE /STACK:8388608)
  endif()
  add_test(NAME ${name} COMMAND ${target} ${ARGN})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${ROOT}" LABELS "portable;functional")
  set_property(GLOBAL APPEND PROPERTY SCREAMSEQ_PORTABLE_TEST_TARGETS ${target})
endfunction()

screamseq_portable_test(tracker-core-tests CoreTests tracker-core "${ROOT}")
screamseq_portable_test(instrument-envelope-tests InstrumentEnvelopeTests instrument-envelopes)
screamseq_portable_test(sample-crossfade-tests SampleCrossfadeTests sample-crossfade)
screamseq_portable_test(sample-snap-tests SampleSnapTests sample-snap)
screamseq_portable_test(sample-drawing-tests SampleDrawingTests sample-drawing)
screamseq_portable_test(sample-copy-tests SampleCopyTests sample-copy)
screamseq_portable_test(sample-clipboard-tests SampleClipboardTests sample-clipboard)
screamseq_portable_test(sample-archive-tests SampleArchiveTests sample-archive)
screamseq_portable_test(sample-processing-tests SampleProcessingTests sample-processing)
screamseq_portable_test(automation-tools-tests AutomationToolsTests automation-tools)
screamseq_portable_test(pattern-tools-tests PatternToolsTests pattern-tools)
screamseq_portable_test(pattern-commands-tests PatternCommandsTests pattern-commands)
screamseq_portable_test(mixer-graph-tests MixerGraphTests mixer-graph)
screamseq_portable_test(signal-graph-tests SignalGraphTests signal-graph)

# These existing tests already provide a functional-only path for sanitizer
# builds. Reuse it to disable *only* the unavailable Darwin realtime interposer.
# TRACKER_SANITIZER here does not mean that sanitizers are enabled. Passing these
# targets proves neither allocation/lock freedom nor sanitizer cleanliness.
screamseq_portable_test(song-timing-tests SongTimingTests song-timing)
screamseq_portable_test(native-reverse-loop-tests NativeReverseLoopTests native-reverse-loops)
screamseq_portable_test(filter-tests FilterTests filters)
screamseq_portable_test(oversampling-tests OversamplingTests oversampling)
screamseq_portable_test(routing-prototype-tests RoutingPrototypeTests routing-prototype)
screamseq_portable_test(mixer-runtime-tests MixerRuntimeTests mixer-runtime)
screamseq_portable_test(sidechain-tests SidechainTests sidechains)
screamseq_portable_test(effect-auxiliary-tests EffectAuxiliaryTests effect-auxiliary)
screamseq_portable_test(playback-region-tests PlaybackRegionTests playback-regions)
screamseq_portable_test(curve-formula-tests CurveFormulaTests curve-formulas)
foreach(target song-timing-tests native-reverse-loop-tests filter-tests oversampling-tests
    routing-prototype-tests mixer-runtime-tests sidechain-tests effect-auxiliary-tests
    playback-region-tests curve-formula-tests)
  target_compile_definitions(${target} PRIVATE TRACKER_SANITIZER)
endforeach()
set_tests_properties(song-timing native-reverse-loops filters oversampling routing-prototype
  mixer-runtime sidechains effect-auxiliary playback-regions curve-formulas
  PROPERTIES LABELS "portable;functional;no-realtime-audit")
message(STATUS "Windows tests are functional only: Darwin realtime audit and sanitizers are NOT enabled")
get_property(portable_targets GLOBAL PROPERTY SCREAMSEQ_PORTABLE_TEST_TARGETS)
add_custom_target(portable-tests DEPENDS ${portable_targets})
