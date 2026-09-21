cmake_minimum_required(VERSION 3.24)
project(ScreamSeqWindows LANGUAGES C CXX RC)
if(NOT WIN32)
  message(FATAL_ERROR "This frontend requires Windows. Use mac/ for the macOS frontend.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include(CTest)
option(SCREAMSEQ_BUILD_APP "Build the native Windows application when its sources exist" ON)
option(SCREAMSEQ_BUILD_RENDER_PROBE "Build the standalone native renderer probe" ON)

add_library(ScreamSeqBuildOptions INTERFACE)
target_compile_definitions(ScreamSeqBuildOptions INTERFACE
  NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(MSVC)
  target_compile_options(ScreamSeqBuildOptions INTERFACE
    "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/bigobj;/MP4>"
    "$<$<COMPILE_LANGUAGE:CXX>:/permissive-;/Zc:__cplusplus;/Zc:preprocessor>")
endif()

# Keep the engine configuration and shared editor source list aligned with mac/.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
  "${ROOT}/common/*.cpp" "${ROOT}/soundlib/*.cpp"
  "${ROOT}/soundlib/plugins/*.cpp" "${ROOT}/soundlib/plugins/dmo/*.cpp"
  "${ROOT}/sounddsp/*.cpp")
add_library(OpenMPTCore STATIC ${CORE_SOURCES} "${ROOT}/editor/Assert.cpp"
  "${ROOT}/src/openmpt/soundfile_write/wav_write.cpp"
  "${ROOT}/include/miniz/miniz.c" "${ROOT}/include/minimp3/minimp3.c"
  "${ROOT}/include/stb_vorbis/stb_vorbis.c")
target_compile_definitions(OpenMPTCore PUBLIC LIBOPENMPT_BUILD OPENMPT_EDITOR_CORE
  MPT_WITH_MINIZ MPT_WITH_MINIMP3 MPT_WITH_STBVORBIS MPT_WITH_FLAC)
target_include_directories(OpenMPTCore PUBLIC "${ROOT}" "${ROOT}/src" "${ROOT}/common" "${ROOT}/include"
  "${ROOT}/include/miniz" "${ROOT}/include/minimp3" "${ROOT}/include/stb_vorbis")

file(GLOB FLAC_SOURCES CONFIGURE_DEPENDS "${ROOT}/include/flac/src/libFLAC/*.c")
list(FILTER FLAC_SOURCES EXCLUDE REGEX "/ogg_[^/]*\\.c$")
add_library(TrackerFLAC STATIC ${FLAC_SOURCES}
  "${ROOT}/include/flac/src/share/win_utf8_io/win_utf8_io.c")
target_include_directories(TrackerFLAC PUBLIC "${ROOT}/include/flac/include"
  PRIVATE "${ROOT}/include/flac/src/libFLAC/include")
# FLAC's no-thread fallback is sufficient for shared sample import/export.
# Do not enable the macOS pthread/fseeko configuration on Windows.
target_compile_definitions(TrackerFLAC PUBLIC FLAC__NO_DLL
  PRIVATE HAVE_STDINT_H HAVE_LROUND FLAC__NO_ASM FLAC__HAS_OGG=0 PACKAGE_VERSION="1.5.0")
target_link_libraries(TrackerFLAC PRIVATE ScreamSeqBuildOptions)
target_link_libraries(OpenMPTCore PUBLIC TrackerFLAC ScreamSeqBuildOptions
  PRIVATE shlwapi ole32 rpcrt4)

set(EDITOR_SOURCES
  TrackLayout PatternCommands AutomationTools TrackerDocument PatternTools NativeSong
  ArrangementTools MixerGraph MixerRuntime NativeEffects SampleImport SongTiming
  PreciseNoteRuntime NoteRecording InstrumentEnvelopeTools EnvelopeBank
  StateVariableFilter FractionalDelay Distortion LofiMat CabinetVoicing CabinetSimulator
  Dynamics Maximizer SampleProcessing SampleWaveform SampleDocument SampleArchive
  SampleClipboard SampleSplice SampleCopy SampleDrawing SampleSnap SampleLoopCrossfade
  SignalGraph SignalRuntime)
list(TRANSFORM EDITOR_SOURCES PREPEND "${ROOT}/editor/")
list(TRANSFORM EDITOR_SOURCES APPEND ".cpp")
add_library(TrackerEditor STATIC ${EDITOR_SOURCES} "${ROOT}/tracklib/SampleEdit.cpp")
target_link_libraries(TrackerEditor PUBLIC OpenMPTCore)

include(Plugins/Provider.cmake)
add_subdirectory("${ROOT}/editor/hosted" hosted)
screamseq_windows_vst3_target(TrackerHosted)
add_library(ScreamSeqPlayback STATIC Session/HostedProject.cpp)
target_link_libraries(ScreamSeqPlayback PUBLIC TrackerHosted)
target_include_directories(ScreamSeqPlayback PUBLIC "${ROOT}/include/nlohmann-json/include")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Audio/WasapiDevice.cpp")
  add_library(ScreamSeqAudio STATIC Audio/WasapiDevice.cpp)
  target_link_libraries(ScreamSeqAudio PUBLIC ScreamSeqBuildOptions PRIVATE ole32 avrt uuid)
endif()
add_library(ScreamSeqProject STATIC Project/BinaryPlist.cpp Project/NativeMetadata.cpp Project/NativeProject.cpp
  Plugins/PluginPreset.cpp Plugins/PluginLibrary.cpp "${ROOT}/include/pugixml/src/pugixml.cpp")
target_include_directories(ScreamSeqProject PRIVATE "${ROOT}/include/pugixml/src")
target_include_directories(ScreamSeqProject PUBLIC "${ROOT}/include/nlohmann-json/include")
target_link_libraries(ScreamSeqProject PUBLIC TrackerEditor PRIVATE crypt32 bcrypt)

if(SCREAMSEQ_BUILD_APP)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/App/Main.cpp" AND TARGET ScreamSeqAudio)
    add_executable(ScreamSeq WIN32 App/Main.cpp Session/DocumentController.cpp Session/DocumentOperations.cpp Session/TimelineOperations.cpp Session/AssetOperations.cpp Session/PluginOperations.cpp Session/PatternOperations.cpp Session/GraphOperations.cpp Session/MixerOperations.cpp Session/EnvelopeOperations.cpp Api/PipeServer.cpp Resources/ScreamSeq.rc)
    if(MSVC)
      target_link_options(ScreamSeq PRIVATE /STACK:8388608)
    endif()
    target_include_directories(ScreamSeq PRIVATE Resources "${ROOT}/include/nlohmann-json/include")
    target_link_libraries(ScreamSeq PRIVATE ScreamSeqProject ScreamSeqAudio ScreamSeqPlayback
      d3d11 d2d1 dwrite dxgi dwmapi shell32 comdlg32 comctl32 advapi32)
    add_dependencies(ScreamSeq ScreamSeqVST3Scanner)
    add_custom_command(TARGET ScreamSeq POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ScreamSeq>/THIRD-PARTY-NOTICES"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ROOT}/include/pugixml/LICENSE.md"
        "$<TARGET_FILE_DIR:ScreamSeq>/THIRD-PARTY-NOTICES/pugixml-LICENSE.md")
  else()
    message(STATUS "ScreamSeq frontend sources are not present; building shared libraries/tests only")
  endif()
endif()
if(SCREAMSEQ_BUILD_RENDER_PROBE)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/App/RenderProbe.cpp")
    add_executable(renderer-probe App/RenderProbe.cpp)
    target_link_libraries(renderer-probe PRIVATE ScreamSeqBuildOptions d3d11 d2d1 dwrite dxgi)
  else()
    message(STATUS "Renderer probe source is not present; target omitted")
  endif()
endif()

if(BUILD_TESTING)
  include(cmake/PortableTests.cmake)
  foreach(case "unified-effects;UnifiedEffects" "playback-display;PlaybackDisplay")
    list(GET case 0 name)
    list(GET case 1 source)
    add_executable(${name}-tests Tests/${source}Tests.cpp)
    target_link_libraries(${name}-tests PRIVATE TrackerEditor)
    target_compile_options(${name}-tests PRIVATE "/FI${ROOT}/windows/cmake/WindowsTestPreamble.hpp")
    target_link_options(${name}-tests PRIVATE /STACK:8388608)
    add_test(NAME ${name} COMMAND ${name}-tests)
    set_tests_properties(${name} PROPERTIES LABELS "portable;functional;no-realtime-audit" TIMEOUT 120)
    add_dependencies(portable-tests ${name}-tests)
  endforeach()
  add_executable(native-project-tests Tests/ProjectNative/NativeProjectTests.cpp)
  add_executable(live-parameter-tests Tests/LiveParameterTests.cpp Tests/HostedProject/CallbackAudit.cpp)
  target_link_libraries(live-parameter-tests PRIVATE ScreamSeqProject ScreamSeqPlayback)
  target_compile_definitions(live-parameter-tests PRIVATE SCREAMSEQ_WASAPI_ALLOCATION_AUDIT=1)
  target_link_options(live-parameter-tests PRIVATE /STACK:8388608)
  add_test(NAME live-parameter-tests COMMAND live-parameter-tests)
  set_tests_properties(live-parameter-tests PROPERTIES LABELS "hosted;live-parameters;cxx-allocation-audit" TIMEOUT 120)
  add_executable(hosted-project-tests Tests/HostedProject/HostedProjectTests.cpp Tests/HostedProject/CallbackAudit.cpp)
  target_link_libraries(hosted-project-tests PRIVATE ScreamSeqProject ScreamSeqPlayback)
  target_compile_definitions(hosted-project-tests PRIVATE SCREAMSEQ_WASAPI_ALLOCATION_AUDIT=1)
  target_link_options(hosted-project-tests PRIVATE /STACK:8388608)
  add_test(NAME hosted-project-tests COMMAND hosted-project-tests)
  set_tests_properties(hosted-project-tests PROPERTIES LABELS "hosted;cxx-allocation-audit" TIMEOUT 120)
  target_link_libraries(native-project-tests PRIVATE ScreamSeqProject)
  if(MSVC)
    target_link_options(native-project-tests PRIVATE /STACK:8388608)
  endif()
  add_executable(project-preservation-tests Tests/ProjectNative/PreservationTests.cpp)
  target_include_directories(project-preservation-tests PRIVATE "${ROOT}/include/nlohmann-json/include")
  target_link_libraries(project-preservation-tests PRIVATE ScreamSeqBuildOptions)
  add_test(NAME project-preservation-tests COMMAND project-preservation-tests)
  set_tests_properties(project-preservation-tests PROPERTIES LABELS "project-native")
  add_executable(document-controller-tests Tests/DocumentControllerTests.cpp Session/DocumentController.cpp Session/DocumentOperations.cpp Session/TimelineOperations.cpp Session/AssetOperations.cpp Session/PluginOperations.cpp Session/PatternOperations.cpp Session/GraphOperations.cpp Session/MixerOperations.cpp Session/EnvelopeOperations.cpp)
  target_link_libraries(document-controller-tests PRIVATE ScreamSeqProject ScreamSeqPlayback)
  if(MSVC)
    target_link_options(document-controller-tests PRIVATE /STACK:8388608)
  endif()
  add_executable(windows-save-tests Tests/SaveTests.cpp)
  target_link_libraries(windows-save-tests PRIVATE TrackerEditor)
endif()
