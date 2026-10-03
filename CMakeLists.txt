# ADLIB: the 1996 behaviour language of Rosen & Duisberg (Wizbang!), reconstructed.
#   libadlib     runtime VM, PBN reader/writer, vocabulary, host API, and the adlibc compiler
#   adlib_tools  decompiler, graphs, lint, compare, trace sink (+ the `adlib` CLI)
#   adlibc       the compiler CLI (byte-identical to the 1996 ADLIB compiler)
# Host-agnostic C++17, no dependencies. See README.md and docs/.
cmake_minimum_required(VERSION 3.20)
project(adlib VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "ADLIB behaviour language: runtime, compiler and authoring tools"
        HOMEPAGE_URL "https://github.com/adlib-reconstruction/adlib")

set(ADLIB_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(ADLIB_TOP_LEVEL ON)
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(ADLIB_BUILD_TOOLS "Build adlibc, the adlib CLI and the adlib_tools library" ON)
option(ADLIB_BUILD_EXAMPLES "Build the examples (vending machine)" ${ADLIB_TOP_LEVEL})
option(ADLIB_BUILD_TESTS "Build the test suite (ctest)" ${ADLIB_TOP_LEVEL})
option(ADLIB_INSTALL "Generate install rules" ${ADLIB_TOP_LEVEL})
if(EMSCRIPTEN)
  set(ADLIB_NODE_LINK -sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
endif()

include(GNUInstallDirs)

# ---- version header ----
configure_file(cmake/version.h.in ${PROJECT_BINARY_DIR}/generated/include/adlib/version.h @ONLY)

# ---- libadlib ----
add_library(adlib STATIC
  src/vocabulary.cpp src/pbn.cpp src/pbn_writer.cpp src/runtime.cpp
  # adlibc, the compiler (docs/adlibc.md)
  src/compiler/numbers.cpp src/compiler/symbols.cpp src/compiler/compiler.cpp src/compiler/strict.cpp)
add_library(adlib::adlib ALIAS adlib)
target_include_directories(adlib PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(adlib PUBLIC cxx_std_17)
target_compile_options(adlib PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Lockstep determinism (the runtime does a little float math, e.g. id_SET_InteractorLatency):
# no FMA contraction, no fast-math, SSE on x86. Hosts that need bit-identical simulation across
# builds should use the same flags.
target_compile_options(adlib PRIVATE -ffp-contract=off -fno-fast-math)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT EMSCRIPTEN)
  target_compile_options(adlib PRIVATE -mfpmath=sse -msse2)
endif()
set_target_properties(adlib PROPERTIES VERSION ${PROJECT_VERSION} EXPORT_NAME adlib)

# ---- adlib-tools: decompiler, graphs, inspect/lint, compare, trace sink (docs/tools.md) ----
if(ADLIB_BUILD_TOOLS)
  add_library(adlib_tools STATIC
    src/tools/symbols.cpp src/tools/decompile.cpp src/tools/graph.cpp src/tools/inspect.cpp
    src/tools/trace.cpp)
  add_library(adlib::tools ALIAS adlib_tools)
  target_link_libraries(adlib_tools PUBLIC adlib)
  target_compile_options(adlib_tools PRIVATE -Wall -Wextra -Wno-unused-parameter)
  set_target_properties(adlib_tools PROPERTIES EXPORT_NAME tools)

  add_executable(adlibc tools/adlibc.cpp)
  target_link_libraries(adlibc PRIVATE adlib adlib_tools) # --strict runs tools::lintPlan
  target_compile_definitions(adlibc PRIVATE ADLIBC_LINT=1)
  add_executable(adlib_cli tools/adlib-cli.cpp)
  target_link_libraries(adlib_cli PRIVATE adlib_tools)
  set_target_properties(adlib_cli PROPERTIES OUTPUT_NAME adlib)
  if(EMSCRIPTEN)
    target_link_options(adlibc PRIVATE ${ADLIB_NODE_LINK})
    target_link_options(adlib_cli PRIVATE ${ADLIB_NODE_LINK})
  endif()
endif()

# ---- examples: the vending machine (plans compiled from ADLIB source by adlibc at build time) ----
if((ADLIB_BUILD_EXAMPLES OR ADLIB_BUILD_TESTS) AND TARGET adlibc)
  set(VENDING_SRC ${PROJECT_SOURCE_DIR}/examples/vending_machine)
  set(VENDING_PBN ${PROJECT_BINARY_DIR}/vending_plans)
  foreach(plan VENDING CUSTOMER)
    add_custom_command(OUTPUT ${VENDING_PBN}/${plan}.PBN
      COMMAND ${CMAKE_COMMAND} -E make_directory ${VENDING_PBN}
      COMMAND adlibc --enumids ${VENDING_SRC}/VENDING.H --texts ${VENDING_SRC}/VENDTEXT.LST
              --strict -q -o ${VENDING_PBN}/${plan}.PBN ${VENDING_SRC}/${plan}.txt
      DEPENDS adlibc ${VENDING_SRC}/${plan}.txt ${VENDING_SRC}/VENDING.H ${VENDING_SRC}/VENDTEXT.LST
      COMMENT "adlibc ${plan}.txt"
      VERBATIM)
    list(APPEND VENDING_PBNS ${VENDING_PBN}/${plan}.PBN)
  endforeach()
  add_custom_target(vending_plans ALL DEPENDS ${VENDING_PBNS})
  add_executable(vending_machine examples/vending_machine/main.cpp)
  target_link_libraries(vending_machine PRIVATE adlib adlib_tools) # adlib_tools: --trace FILTER
  target_compile_definitions(vending_machine PRIVATE
    VENDING_DIR="${VENDING_SRC}" VENDING_PBN_DIR="${VENDING_PBN}")
  add_dependencies(vending_machine vending_plans)
  if(EMSCRIPTEN)
    target_link_options(vending_machine PRIVATE ${ADLIB_NODE_LINK})
  endif()
endif()

# ---- tests ----
if(ADLIB_BUILD_TESTS)
  if(NOT TARGET adlibc)
    message(FATAL_ERROR "ADLIB_BUILD_TESTS needs ADLIB_BUILD_TOOLS=ON")
  endif()
  enable_testing()
  set(ADLIB_FIXTURES ${PROJECT_SOURCE_DIR}/fixtures)
  add_executable(adlib_test tests/adlib_test.cpp)
  target_link_libraries(adlib_test PRIVATE adlib)
  add_executable(adlibc_test tests/adlibc_test.cpp)
  target_link_libraries(adlibc_test PRIVATE adlib)
  target_compile_definitions(adlibc_test PRIVATE
    VENDING_DIR="${VENDING_SRC}" VENDING_PBN_DIR="${VENDING_PBN}")
  add_dependencies(adlibc_test vending_plans)
  add_executable(adlib_tools_test tests/adlib_tools_test.cpp)
  target_link_libraries(adlib_tools_test PRIVATE adlib_tools)
  target_compile_definitions(adlib_tools_test PRIVATE VENDING_DIR="${VENDING_SRC}")
  if(EMSCRIPTEN)
    foreach(t adlib_test adlibc_test adlib_tools_test)
      target_link_options(${t} PRIVATE ${ADLIB_NODE_LINK})
    endforeach()
  endif()

  # The test suite needs no external data.
  add_test(NAME adlib_test COMMAND adlib_test)
  add_test(NAME adlibc_test COMMAND adlibc_test ${ADLIB_FIXTURES})
  add_test(NAME adlib_tools_test COMMAND adlib_tools_test)
  add_test(NAME vending_machine COMMAND vending_machine)
  add_test(NAME cli_version COMMAND adlibc --version)
  set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "adlibc ${PROJECT_VERSION}")
  add_test(NAME cli_graph_mermaid COMMAND adlib_cli --enumids ${VENDING_SRC}/VENDING.H graph --mermaid
           ${VENDING_PBN}/VENDING.PBN)
  set_tests_properties(cli_graph_mermaid PROPERTIES PASS_REGULAR_EXPRESSION "stateDiagram|flowchart|graph")
  set_tests_properties(adlib_test adlibc_test adlib_tools_test vending_machine cli_version cli_graph_mermaid
                       PROPERTIES LABELS core)

endif()

# ---- install + CMake package ----
if(ADLIB_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_targets adlib)
  if(TARGET adlib_tools)
    list(APPEND _targets adlib_tools)
    install(TARGETS adlibc adlib_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  install(TARGETS ${_targets} EXPORT adlibTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/adlib DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES ${PROJECT_BINARY_DIR}/generated/include/adlib/version.h
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/adlib)
  set(_cfgdir ${CMAKE_INSTALL_LIBDIR}/cmake/adlib)
  install(EXPORT adlibTargets NAMESPACE adlib:: DESTINATION ${_cfgdir})
  configure_package_config_file(cmake/adlibConfig.cmake.in ${PROJECT_BINARY_DIR}/adlibConfig.cmake
                                INSTALL_DESTINATION ${_cfgdir})
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/adlibConfigVersion.cmake
                                   COMPATIBILITY SameMinorVersion)
  install(FILES ${PROJECT_BINARY_DIR}/adlibConfig.cmake ${PROJECT_BINARY_DIR}/adlibConfigVersion.cmake
          DESTINATION ${_cfgdir})
  install(FILES LICENSE NOTICE.md README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
