cmake_minimum_required(VERSION 3.25)
project(bildhauer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

add_library(bildhauer_core
  src/core/mesh.cpp
  src/core/reduce.cpp
  src/core/relax.cpp
  src/core/subdivide.cpp
  src/core/symmetry.cpp
  src/core/tessellate.cpp
  src/io/sc1.cpp
)
target_include_directories(bildhauer_core PUBLIC src)

# Tests run against .sc1 files that are not part of the repository (Sculptris
# samples and local test files). Pass them via BILDHAUER_SC1_SAMPLES, or point
# BILDHAUER_SCULPTRIS_DIR at a Sculptris installation.
set(BILDHAUER_SCULPTRIS_DIR "F:/Grafik/Sculptris Alpha 6" CACHE PATH "Sculptris installation with sample .sc1 files")
set(BILDHAUER_SC1_SAMPLES "" CACHE STRING "Semicolon-separated list of .sc1 files for the tests")

if(NOT BILDHAUER_SC1_SAMPLES)
  file(GLOB _samples
    "${BILDHAUER_SCULPTRIS_DIR}/data/*.sc1"
    "${BILDHAUER_SCULPTRIS_DIR}/models/*.sc1"
    "${CMAKE_SOURCE_DIR}/re/_work/models/*.sc1")
  set(BILDHAUER_SC1_SAMPLES ${_samples})
endif()

enable_testing()
add_executable(sc1_test tests/sc1_test.cpp)
target_link_libraries(sc1_test PRIVATE bildhauer_core)
add_test(NAME sc1 COMMAND sc1_test ${BILDHAUER_SC1_SAMPLES})

add_executable(ops_test tests/ops_test.cpp)
target_link_libraries(ops_test PRIVATE bildhauer_core)
add_test(NAME ops COMMAND ops_test ${BILDHAUER_SC1_SAMPLES})
