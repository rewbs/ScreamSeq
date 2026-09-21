cmake_minimum_required(VERSION 3.24)
project(ScreamSeqPluginRegistryReview LANGUAGES CXX)
include(CTest)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
# Optional frozen backend snapshot avoids another agent's lifecycle edits. Registry,
# Module, Scanner and SDK sources always compile fresh, never from a stale archive.
set(SCREAMSEQ_VST3_BACKEND_DIR "${ROOT}/windows/Plugins" CACHE PATH "Backend source/header snapshot")
set(SCREAMSEQ_VST3_FIXTURE "" CACHE FILEPATH "Existing actual ARM64 fixture DLL (not rebuilt here)")
set(SCREAMSEQ_REGISTRY_TEST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/disposable" CACHE PATH "Private disposable test data")
if(NOT EXISTS "${SCREAMSEQ_VST3_FIXTURE}")
  message(FATAL_ERROR "Provide SCREAMSEQ_VST3_FIXTURE with an existing real ARM64 fixture DLL")
endif()
include("${ROOT}/windows/Plugins/Provider.cmake")
add_executable(RegistryReview RegistryTests.cpp
  "${ROOT}/windows/Plugins/Module.cpp" "${ROOT}/windows/Plugins/Scanner.cpp"
  "${SCREAMSEQ_VST3_BACKEND_DIR}/WindowsVST3.cpp"
  "${ROOT}/windows/Project/BinaryPlist.cpp" ${SCREAMSEQ_VST3_SDK_SOURCES})
target_include_directories(RegistryReview PRIVATE "${ROOT}" "${ROOT}/src" "${ROOT}/common" "${ROOT}/include" "${ROOT}/include/nlohmann-json/include" "${SCREAMSEQ_VST3_SDK}")
target_link_libraries(RegistryReview PRIVATE bcrypt user32 ole32)
target_link_options(RegistryReview PRIVATE /STACK:8388608)
foreach(case capacity identity invariants write short-write flush rename locked stage concurrent retarget)
  add_test(NAME registry-${case} COMMAND RegistryReview ${case} "$<TARGET_FILE:ScreamSeqVST3Scanner>" "${SCREAMSEQ_VST3_FIXTURE}" "${SCREAMSEQ_REGISTRY_TEST_ROOT}/${case}")
  set_tests_properties(registry-${case} PROPERTIES TIMEOUT 60)
endforeach()
