# Builds Core Engine binaries and registers isolated CTest cases.
cmake_minimum_required(VERSION 3.16)

project(CoreEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(CTest)

add_library(core_engine_lib
            src/cli.cpp
            src/cluster.cpp
            src/codec.cpp
            src/config.cpp
            src/error.cpp
            src/generator.cpp
            src/hash.cpp
            src/mock_network.cpp
            src/node.cpp
            src/packed_record.cpp
            src/parser.cpp
            src/report.cpp
            src/schema.cpp
            src/stats.cpp
            src/store.cpp)
target_include_directories(core_engine_lib PUBLIC include)
core_engine_set_warnings(core_engine_lib)
core_engine_enable_sanitizers(core_engine_lib)

add_executable(core_engine src/main.cpp)
target_link_libraries(core_engine PRIVATE core_engine_lib)
core_engine_set_warnings(core_engine)
core_engine_enable_sanitizers(core_engine)

# Optional: an import-able C++23 module re-exporting the same API, built additively
# alongside the primary C++17 header build when the toolchain supports named modules.
if(CMAKE_GENERATOR STREQUAL "Ninja" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.28 AND
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 18)
  set(CMAKE_CXX_SCAN_FOR_MODULES ON)

  add_library(core_engine_module)
  target_sources(core_engine_module PUBLIC FILE_SET CXX_MODULES FILES modules/core_engine.cppm)
  target_include_directories(core_engine_module PUBLIC include modules)
  target_compile_features(core_engine_module PRIVATE cxx_std_23)
  target_link_libraries(core_engine_module PRIVATE core_engine_lib)
  core_engine_enable_sanitizers(core_engine_module)

  add_executable(core_engine_module_smoke_test modules/smoke_test.cpp)
  target_compile_features(core_engine_module_smoke_test PRIVATE cxx_std_23)
  target_link_libraries(core_engine_module_smoke_test PRIVATE core_engine_module)
  core_engine_enable_sanitizers(core_engine_module_smoke_test)

  if(BUILD_TESTING)
    add_test(NAME "MODULE-SMOKE" COMMAND core_engine_module_smoke_test)
  endif()
else()
  message(STATUS "core_engine: optional C++23 module skipped "
                  "(needs Ninja + CMake >= 3.28 + Clang >= 18; primary C++17 build is unaffected)")
endif()

if(BUILD_TESTING)
  add_executable(core_engine_tests tests/test_cli.cpp tests/test_cluster.cpp tests/test_codec.cpp
                                   tests/test_config.cpp tests/test_generator.cpp tests/test_hash.cpp
                                   tests/test_main.cpp tests/test_network.cpp tests/test_parser.cpp
                                   tests/test_queue.cpp tests/test_reference_model.cpp
                                   tests/test_report.cpp tests/test_schema.cpp tests/test_stats.cpp
                                   tests/test_store.cpp)
  target_link_libraries(core_engine_tests PRIVATE core_engine_lib)
  core_engine_set_warnings(core_engine_tests)
  core_engine_enable_sanitizers(core_engine_tests)

  function(core_engine_add_test_id test_id)
    add_test(NAME "${test_id}" COMMAND core_engine_tests --run "${test_id}")
    set_tests_properties("${test_id}" PROPERTIES TIMEOUT 10)
  endfunction()

  core_engine_add_test_id("FRAMEWORK-01")
  core_engine_add_test_id("E2E-17")
  core_engine_add_test_id("CFG-01")
  core_engine_add_test_id("CFG-02")
  core_engine_add_test_id("CFG-03")
  core_engine_add_test_id("CFG-04")
  core_engine_add_test_id("CFG-05")
  core_engine_add_test_id("CFG-06")
  core_engine_add_test_id("CFG-07")
  core_engine_add_test_id("SCH-01")
  core_engine_add_test_id("SCH-02")
  core_engine_add_test_id("SCH-03")
  core_engine_add_test_id("QUE-01")
  core_engine_add_test_id("STATS-01")
  core_engine_add_test_id("HSH-01")
  core_engine_add_test_id("HSH-02")
  core_engine_add_test_id("HSH-03")
  core_engine_add_test_id("HSH-04")
  core_engine_add_test_id("HSH-05")
  core_engine_add_test_id("HSH-06")
  core_engine_add_test_id("PAR-01")
  core_engine_add_test_id("PAR-02")
  core_engine_add_test_id("PAR-03")
  core_engine_add_test_id("PAR-04")
  core_engine_add_test_id("PAR-05")
  core_engine_add_test_id("PAR-06")
  core_engine_add_test_id("PAR-07")
  core_engine_add_test_id("PAR-08")
  core_engine_add_test_id("PAR-09")
  core_engine_add_test_id("PAR-10")
  core_engine_add_test_id("PAR-11")
  core_engine_add_test_id("PAR-12")
  core_engine_add_test_id("COD-01")
  core_engine_add_test_id("COD-02")
  core_engine_add_test_id("COD-03")
  core_engine_add_test_id("COD-04")
  core_engine_add_test_id("COD-05")
  core_engine_add_test_id("COD-06")

  add_test(NAME "COD-07" COMMAND core_engine_tests --run "COD-07")
  set_tests_properties("COD-07" PROPERTIES TIMEOUT 30)
  core_engine_add_test_id("COD-08")
  core_engine_add_test_id("REF-01")
  core_engine_add_test_id("E2E-01")
  core_engine_add_test_id("E2E-02")
  core_engine_add_test_id("E2E-03")
  core_engine_add_test_id("E2E-04")
  core_engine_add_test_id("E2E-05")
  core_engine_add_test_id("E2E-06")
  core_engine_add_test_id("E2E-07")
  core_engine_add_test_id("E2E-08")
  core_engine_add_test_id("E2E-09")
  core_engine_add_test_id("E2E-10")
  core_engine_add_test_id("E2E-11")
  add_test(NAME "E2E-12" COMMAND core_engine_tests --run "E2E-12")
  set_tests_properties("E2E-12" PROPERTIES TIMEOUT 60)
  core_engine_add_test_id("E2E-13")
  core_engine_add_test_id("E2E-15")
  core_engine_add_test_id("NET-07-CLUSTER")
  core_engine_add_test_id("RPT-01")
  core_engine_add_test_id("RPT-02")
  core_engine_add_test_id("RPT-03")
  core_engine_add_test_id("RPT-04")
  core_engine_add_test_id("RPT-05")
  core_engine_add_test_id("RPT-06")
  core_engine_add_test_id("RPT-07")

  core_engine_add_test_id("GEN-01")
  core_engine_add_test_id("GEN-02")
  core_engine_add_test_id("GEN-03")
  core_engine_add_test_id("GEN-04")
  core_engine_add_test_id("GEN-05")
  core_engine_add_test_id("GEN-06")
  core_engine_add_test_id("STO-01")
  core_engine_add_test_id("STO-02")
  core_engine_add_test_id("STO-03")
  core_engine_add_test_id("STO-04")
  core_engine_add_test_id("STO-05")
  core_engine_add_test_id("STO-06")
  core_engine_add_test_id("STO-07")
  core_engine_add_test_id("NET-01")
  core_engine_add_test_id("NET-02")
  core_engine_add_test_id("NET-03")
  core_engine_add_test_id("NET-04")
  core_engine_add_test_id("NET-05")
  core_engine_add_test_id("NET-06")
  core_engine_add_test_id("NET-07")
  core_engine_add_test_id("NET-08")
  core_engine_add_test_id("NET-09")
  core_engine_add_test_id("NET-11")

  add_test(NAME "NET-10" COMMAND core_engine_tests --run "NET-10")
  set_tests_properties("NET-10" PROPERTIES TIMEOUT 30)

  add_test(NAME "E2E-14" COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_e2e.sh"
                                  "$<TARGET_FILE:core_engine>" "E2E-14")
  set_tests_properties("E2E-14" PROPERTIES TIMEOUT 120)
  add_test(NAME "E2E-16" COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_e2e.sh"
                                  "$<TARGET_FILE:core_engine>" "E2E-16")
  set_tests_properties("E2E-16" PROPERTIES TIMEOUT 60)
  add_test(NAME "E2E-FAILURES"
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_e2e.sh"
                   "$<TARGET_FILE:core_engine>" "E2E-FAILURES")
  set_tests_properties("E2E-FAILURES" PROPERTIES TIMEOUT 30)
endif()
