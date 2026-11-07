# CMakeLists.txt - the Wiz / CWiz toolchain (native C++17).
#
# Configure + build from the repository root:
#
#     cmake -S compiler -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#     cmake --build build
#
# ...or simply run ./build.sh. The tools land in ./build:
#
#     build/wizc           the Wiz / CWiz compiler driver
#     build/wizrun         build + run a program in the simulator
#     build/customromrun   run a ROM image on a CasioEmuMsvc model
#     build/patch_menu     patch a program into an fx-580VN X firmware dump
#     build/cwmode         add modes to fx-580VN X / fx-880BTG firmware
#     build/wizlint        lint .wz / .cwiz sources (gcc-style, --json)
#     build/wiztests       the toolchain test suite
#     build/cwmode_tests   cwmode ROM fixture (also run by ctest)
#     build/cwapplink_tests .cwapplink project fixture (also run by ctest)
cmake_minimum_required(VERSION 3.16)
project(wiz CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra)
endif()

# ------------------------------------------------------------------ core library
add_library(wizcore STATIC
  src/common.cpp
  src/diag.cpp
  src/targets.cpp
  src/asm.cpp
  src/sim.cpp
  src/wiz.cpp
  src/cwiz.cpp
  src/directives.cpp
  src/cpre.cpp
  src/build.cpp
  src/builtin_rom.cpp
  src/patch.cpp
  src/rop.cpp
  src/lint.cpp
  src/cwmode.cpp
  src/cwlink.cpp
  src/cwicon.cpp
)
target_include_directories(wizcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ------------------------------------------------------------------ tools
add_executable(wizc        src/main_wizc.cpp)
add_executable(wizrun      src/main_wizrun.cpp)
add_executable(customromrun src/main_customromrun.cpp)
add_executable(patch_menu  src/main_patch_menu.cpp)
add_executable(cwmode      src/main_cwmode.cpp)
add_executable(wizlint     src/main_wizlint.cpp)
add_executable(cwicon      src/main_cwicon.cpp)

foreach(tool wizc wizrun customromrun patch_menu cwmode wizlint cwicon)
  target_link_libraries(${tool} PRIVATE wizcore)
endforeach()

# ------------------------------------------------------------------ tests
set(WIZ_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
add_executable(wiztests
  ${WIZ_TESTS_DIR}/main.cpp
  ${WIZ_TESTS_DIR}/test_cw.cpp
  ${WIZ_TESTS_DIR}/test_v02.cpp
  ${WIZ_TESTS_DIR}/test_v03.cpp
  ${WIZ_TESTS_DIR}/test_v03b.cpp
  ${WIZ_TESTS_DIR}/test_cwapp.cpp
  ${WIZ_TESTS_DIR}/test_cwicon.cpp
  ${WIZ_TESTS_DIR}/test_preserve.cpp
  ${WIZ_TESTS_DIR}/test_cwiz.cpp
  ${WIZ_TESTS_DIR}/test_cpre.cpp
  ${WIZ_TESTS_DIR}/test_lint.cpp
  ${WIZ_TESTS_DIR}/test_conformance.cpp
  ${WIZ_TESTS_DIR}/test_fuzz.cpp
  ${WIZ_TESTS_DIR}/test_rop.cpp
  ${WIZ_TESTS_DIR}/test_cwmode.cpp
  ${WIZ_TESTS_DIR}/test_cwlink.cpp
)
target_include_directories(wiztests PRIVATE "${WIZ_TESTS_DIR}")
target_link_libraries(wiztests PRIVATE wizcore)
add_dependencies(wiztests wizc patch_menu)

# Standalone CTest entry for the cwmode ROM patcher fixture.
add_executable(cwmode_tests ${WIZ_TESTS_DIR}/test_cwmode.cpp ${WIZ_TESTS_DIR}/test_cwmode_main.cpp)
target_include_directories(cwmode_tests PRIVATE "${WIZ_TESTS_DIR}")
target_link_libraries(cwmode_tests PRIVATE wizcore)
add_dependencies(cwmode_tests cwmode)

enable_testing()
add_test(NAME toolchain COMMAND wiztests)
add_test(NAME cwmode COMMAND cwmode_tests)

add_executable(cwapplink_tests ${WIZ_TESTS_DIR}/test_cwlink.cpp ${WIZ_TESTS_DIR}/test_cwlink_main.cpp)
target_include_directories(cwapplink_tests PRIVATE "${WIZ_TESTS_DIR}")
target_link_libraries(cwapplink_tests PRIVATE wizcore)
add_dependencies(cwapplink_tests wizc)
add_test(NAME cwapplink COMMAND cwapplink_tests)

# Compile the ROM agent's CWiz corpus.  Reports go under <repo>/temp/corpus.
add_executable(corpus_check src/main_corpus.cpp)
target_link_libraries(corpus_check PRIVATE wizcore)
add_dependencies(corpus_check wizc)

# Report stale vendored ROP copies; does not modify the emulator tree.
add_custom_target(vendored_check
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sync_vendored_rop.sh" --check
  COMMENT "Checking vendored ROP copies")
