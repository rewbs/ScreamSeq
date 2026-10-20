cmake_minimum_required(VERSION 3.24)
project(ScreamSeqSnapshotTests LANGUAGES CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SCREAMSEQ_ENGINE_LIB_DIR "${ROOT}/bin/windows-workspace/Release" CACHE PATH "Matching ARM64 engine libraries")
# Compile the changed shared units from source, never rely on a stale archive.
add_executable(snapshot-tests SnapshotTests.cpp
  "${ROOT}/editor/TrackerDocument.cpp" "${ROOT}/editor/SampleArchive.cpp"
  "${ROOT}/windows/Project/NativeProject.cpp" "${ROOT}/windows/Project/NativeMetadata.cpp"
  "${ROOT}/editor/project/BinaryPlist.cpp")
add_executable(snapshot-editing-tests "${ROOT}/windows/Tests/Editing/DocumentOperationsTests.cpp"
  "${ROOT}/windows/Session/DocumentOperations.cpp"
  "${ROOT}/editor/TrackerDocument.cpp" "${ROOT}/editor/SampleArchive.cpp")
foreach(test snapshot-tests snapshot-editing-tests)
target_include_directories(${test} PRIVATE "${ROOT}" "${ROOT}/src" "${ROOT}/common" "${ROOT}/include" "${ROOT}/include/flac/include" "${ROOT}/include/nlohmann-json/include")
target_compile_definitions(${test} PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS LIBOPENMPT_BUILD OPENMPT_EDITOR_CORE MPT_WITH_MINIZ MPT_WITH_MINIMP3 MPT_WITH_STBVORBIS MPT_WITH_FLAC FLAC__NO_DLL)
target_compile_options(${test} PRIVATE /utf-8 /bigobj /permissive- /Zc:__cplusplus /Zc:preprocessor)
target_link_options(${test} PRIVATE /STACK:8388608)
target_link_libraries(${test} PRIVATE "${SCREAMSEQ_ENGINE_LIB_DIR}/TrackerEditor.lib" "${SCREAMSEQ_ENGINE_LIB_DIR}/OpenMPTCore.lib" "${SCREAMSEQ_ENGINE_LIB_DIR}/TrackerFLAC.lib" shlwapi ole32 rpcrt4)
endforeach()
enable_testing()
add_test(NAME snapshot-tests COMMAND snapshot-tests)
set(snapshot_scratch "${CMAKE_CURRENT_BINARY_DIR}/test-scratch/snapshot")
add_test(NAME snapshot-scratch COMMAND "${CMAKE_COMMAND}" "-DDIRECTORY=${snapshot_scratch}"
  -P "${ROOT}/windows/cmake/FreshDirectory.cmake")
set_tests_properties(snapshot-scratch PROPERTIES FIXTURES_SETUP snapshot-scratch)
set_tests_properties(snapshot-tests PROPERTIES FIXTURES_REQUIRED snapshot-scratch
  ENVIRONMENT "TMPDIR=${snapshot_scratch};SCREAMSEQ_SNAPSHOT_REFERENCE_PROJECT=${ROOT}/editor/Tests/Fixtures/Parity20261009/fixtures/04-complete-reference.screamseq")
add_test(NAME snapshot-editing-tests COMMAND snapshot-editing-tests)
add_test(NAME snapshot-probe COMMAND snapshot-editing-tests --snapshot-probe)
