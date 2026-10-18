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
#     build/wiztests       the toolchain test suite
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
  src/cpre.cpp
  src/build.cpp
)
target_include_directories(wizcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ------------------------------------------------------------------ tools
add_executable(wizc        src/main_wizc.cpp)
add_executable(wizrun      src/main_wizrun.cpp)
add_executable(customromrun src/main_customromrun.cpp)
add_executable(patch_menu  src/main_patch_menu.cpp)

foreach(tool wizc wizrun customromrun patch_menu)
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
  ${WIZ_TESTS_DIR}/test_cwiz.cpp
  ${WIZ_TESTS_DIR}/test_cpre.cpp
  ${WIZ_TESTS_DIR}/test_fuzz.cpp
)
target_include_directories(wiztests PRIVATE "${WIZ_TESTS_DIR}")
target_link_libraries(wiztests PRIVATE wizcore)

enable_testing()
add_test(NAME toolchain COMMAND wiztests)
