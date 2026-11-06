set(BH_ROOT ${CMAKE_SOURCE_DIR})
add_library(imgui_android STATIC
  ${BH_ROOT}/third_party/imgui/imgui.cpp
  ${BH_ROOT}/third_party/imgui/imgui_draw.cpp
  ${BH_ROOT}/third_party/imgui/imgui_tables.cpp
  ${BH_ROOT}/third_party/imgui/imgui_widgets.cpp
  ${BH_ROOT}/third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_android PUBLIC
  ${BH_ROOT}/third_party/imgui ${BH_ROOT}/third_party/imgui/backends)
target_compile_definitions(imgui_android PRIVATE IMGUI_IMPL_OPENGL_ES3)

add_library(bildhauer_android SHARED
  ${BH_ROOT}/src/app/app.cpp
  ${BH_ROOT}/src/app/document.cpp
  ${BH_ROOT}/src/app/gpu_mesh.cpp
  ${BH_ROOT}/src/app/interaction.cpp
  ${BH_ROOT}/src/app/matcaps.cpp
  ${BH_ROOT}/src/app/icons.cpp
  ${BH_ROOT}/src/app/widgets.cpp
  ${BH_ROOT}/src/platform/android/images.cpp
  ${BH_ROOT}/src/platform/android/render.cpp
  ${BH_ROOT}/src/platform/android/main.cpp
)
target_link_libraries(bildhauer_android PRIVATE bildhauer_core imgui_android GLESv3 log)
target_link_options(bildhauer_android PRIVATE -Wl,--no-undefined)
