cmake_minimum_required(VERSION 3.24)
project(ScreamSeqPersistenceRegression LANGUAGES CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SCREAMSEQ_ENGINE_LIB_DIR "${ROOT}/bin/windows-snapshot-fix/Release" CACHE PATH "Matching ARM64 engine libraries")
# Compile state/snapshot units fresh; never qualify them from an older archive.
add_library(persistence-project STATIC
  "${ROOT}/editor/TrackerDocument.cpp" "${ROOT}/editor/SampleArchive.cpp"
  "${ROOT}/windows/Project/NativeProject.cpp" "${ROOT}/windows/Project/NativeMetadata.cpp"
  "${ROOT}/windows/Project/BinaryPlist.cpp")
target_include_directories(persistence-project PUBLIC "${ROOT}/windows/Project" "${ROOT}" "${ROOT}/src" "${ROOT}/common" "${ROOT}/include" "${ROOT}/include/flac/include" "${ROOT}/include/nlohmann-json/include")
target_compile_definitions(persistence-project PUBLIC NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS LIBOPENMPT_BUILD OPENMPT_EDITOR_CORE MPT_WITH_MINIZ MPT_WITH_MINIMP3 MPT_WITH_STBVORBIS MPT_WITH_FLAC FLAC__NO_DLL)
target_compile_options(persistence-project PUBLIC /utf-8 /bigobj /permissive- /Zc:__cplusplus /Zc:preprocessor)
target_link_libraries(persistence-project PUBLIC "${SCREAMSEQ_ENGINE_LIB_DIR}/TrackerEditor.lib" "${SCREAMSEQ_ENGINE_LIB_DIR}/OpenMPTCore.lib" "${SCREAMSEQ_ENGINE_LIB_DIR}/TrackerFLAC.lib" shlwapi ole32 rpcrt4)
add_executable(persistence-regression PersistenceTests.cpp)
target_link_libraries(persistence-regression PRIVATE persistence-project)
target_link_options(persistence-regression PRIVATE /STACK:8388608)
foreach(test PreservationTests NativeMetadataTests BinaryPlistTests NativeProjectTests ProjectIOTests)
  add_executable(${test} "${ROOT}/windows/Tests/ProjectNative/${test}.cpp")
  target_link_libraries(${test} PRIVATE persistence-project)
  target_link_options(${test} PRIVATE /STACK:8388608)
endforeach()
enable_testing()
add_test(NAME preservation COMMAND PreservationTests)
add_test(NAME metadata COMMAND NativeMetadataTests)
add_test(NAME plist COMMAND BinaryPlistTests)
# Fixture-writing tests are invoked explicitly with isolated scratch paths.
