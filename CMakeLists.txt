cmake_minimum_required(VERSION 3.25)
# The version of a release: bump it here (and versionName/versionCode in
# android/app/build.gradle). The Windows app compares it with GitHub's latest
# release to tell the user about updates.
project(bildhauer VERSION 0.1.4 LANGUAGES CXX)
configure_file(src/app/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ANDROID)
  option(BILDHAUER_ANDROID_OPTIMIZE_DEBUG "Optimize native Android debug code" OFF)
  if(BILDHAUER_ANDROID_OPTIMIZE_DEBUG)
    add_compile_options("$<$<CONFIG:Debug>:-O2>")
  endif()
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

add_library(bildhauer_core
  src/core/brush.cpp
  src/core/mesh.cpp
  src/core/mesh_tree.cpp
  src/core/paint.cpp
  src/core/pick.cpp
  src/core/reduce.cpp
  src/core/relax.cpp
  src/core/subdivide.cpp
  src/core/symmetry.cpp
  src/core/tessellate.cpp
  src/core/texmap.cpp
  src/io/obj.cpp
  src/io/sc1.cpp
)
target_include_directories(bildhauer_core PUBLIC src)

if(WIN32)
  # Dear ImGui (MIT, third_party/imgui), with its Win32 and OpenGL 2 backends.
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_opengl2.cpp
  )
  target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
  if(MSVC)
    target_compile_options(imgui PRIVATE /W3)
  endif()

  add_executable(bildhauer WIN32
    src/app/main.cpp
    src/app/app.cpp
    src/app/dialogs.cpp
    src/app/document.cpp
    src/platform/windows/document_commands.cpp
    src/app/gl2.cpp
    src/app/gpu_mesh.cpp
    src/app/icons.cpp
    src/app/images.cpp
    src/app/input.cpp
    src/app/interaction.cpp
    src/app/matcaps.cpp
    src/app/render.cpp
    src/app/snapshot.cpp
    src/app/settings.cpp
    src/app/ui.cpp
    src/app/update.cpp
    src/app/widgets.cpp
    src/app/bildhauer.rc
  )
  target_include_directories(bildhauer PRIVATE ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(bildhauer PRIVATE bildhauer_core imgui opengl32 comdlg32 windowscodecs ole32 dwmapi winhttp shell32)
  target_compile_definitions(bildhauer PRIVATE WIN32_LEAN_AND_MEAN)
endif()

if(ANDROID)
  option(BILDHAUER_BUILD_BENCHMARKS "Build native device benchmarks" OFF)
  if(BILDHAUER_BUILD_BENCHMARKS)
    add_executable(grab_bench tests/grab_bench.cpp)
    target_link_libraries(grab_bench PRIVATE bildhauer_core)
    add_executable(perf_bench tests/perf_bench.cpp)
    target_link_libraries(perf_bench PRIVATE bildhauer_core)
  endif()
  add_subdirectory(android/native)
  return() # Desktop test executables are built/run on the host.
endif()

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
add_executable(interaction_test
  tests/interaction_test.cpp src/app/app.cpp src/app/document.cpp
  src/app/interaction.cpp src/app/matcaps.cpp)
target_link_libraries(interaction_test PRIVATE bildhauer_core)
add_test(NAME interaction COMMAND interaction_test)
add_executable(gpu_mesh_test tests/gpu_mesh_test.cpp src/app/gpu_mesh.cpp)
target_link_libraries(gpu_mesh_test PRIVATE bildhauer_core)
add_test(NAME gpu_mesh COMMAND gpu_mesh_test)
add_executable(mesh_tree_test tests/mesh_tree_test.cpp)
target_link_libraries(mesh_tree_test PRIVATE bildhauer_core)
add_test(NAME mesh_tree COMMAND mesh_tree_test)
add_executable(grab_test tests/grab_test.cpp)
target_link_libraries(grab_test PRIVATE bildhauer_core)
add_test(NAME grab COMMAND grab_test)
add_executable(release_test tests/release_test.cpp)
target_include_directories(release_test PRIVATE src)
add_test(NAME release COMMAND release_test)

add_executable(sc1_test tests/sc1_test.cpp)
target_link_libraries(sc1_test PRIVATE bildhauer_core)
add_test(NAME sc1 COMMAND sc1_test ${BILDHAUER_SC1_SAMPLES})

add_executable(ops_test tests/ops_test.cpp)
target_link_libraries(ops_test PRIVATE bildhauer_core)
add_test(NAME ops COMMAND ops_test ${BILDHAUER_SC1_SAMPLES})

add_executable(brush_test tests/brush_test.cpp)
target_link_libraries(brush_test PRIVATE bildhauer_core)
add_test(NAME brush COMMAND brush_test ${BILDHAUER_SC1_SAMPLES})

add_executable(texmap_test tests/texmap_test.cpp)
target_link_libraries(texmap_test PRIVATE bildhauer_core)
add_test(NAME texmap COMMAND texmap_test ${BILDHAUER_SC1_SAMPLES})

add_executable(paint_test tests/paint_test.cpp)
target_link_libraries(paint_test PRIVATE bildhauer_core)
add_test(NAME paint COMMAND paint_test ${BILDHAUER_SC1_SAMPLES})

add_executable(obj_test tests/obj_test.cpp)
target_link_libraries(obj_test PRIVATE bildhauer_core)
add_test(NAME obj COMMAND obj_test ${BILDHAUER_SC1_SAMPLES})

# Not a test: times strokes phase by phase (brush_bench <file.sc1>).
add_executable(brush_bench tests/brush_bench.cpp)
target_link_libraries(brush_bench PRIVATE bildhauer_core)

# Not a test: what sculpting costs on a device (tools/perf-bench.ps1).
add_executable(perf_bench tests/perf_bench.cpp)
target_link_libraries(perf_bench PRIVATE bildhauer_core)
