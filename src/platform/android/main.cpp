#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/document.h"
#include "app/icons.h"
#include "app/input.h"
#include "app/matcaps.h"
#include "app/widgets.h"
#include "core/relax.h"
#include "io/obj.h"
#include "platform/android/render.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

namespace bh::viewer {
namespace {

bool initialized = false, uiPointer = false, invert = false, smoothing = false, pressureOn = true;
// Pen contact that started beside the model orbits the view, as in Sculptris.
bool penOrbit = false;
float orbitX = 0, orbitY = 0;
bool savePending = false;
float density = 1;
std::string recovery;
std::chrono::steady_clock::time_point lastFrame;

std::string text(JNIEnv* env, jstring value) {
  const char* utf = env->GetStringUTFChars(value, nullptr);
  std::string result(utf);
  env->ReleaseStringUTFChars(value, utf);
  return result;
}

bool saveRecovery() {
  if (recovery.empty()) return false;
  std::string error;
  const std::string temporary = recovery + ".tmp";
  if (!saveModel(temporary, &error) || std::rename(temporary.c_str(), recovery.c_str()) != 0) {
    app.status = "Recovery save failed: " + error;
    __android_log_print(ANDROID_LOG_ERROR, "bildhauer", "%s", app.status.c_str());
    return false;
  }
  savePending = false;
  return true;
}

// "bildhauer:redwax" -> "Red wax".
std::string materialLabel(const std::string& name) {
  std::string s = name.substr(name.find(':') + 1);
  if (s == "redwax") s = "red wax";
  if (!s.empty()) s[0] = char(std::toupper(s[0]));
  return s;
}

// The Sculptris material panel: one sphere per matcap, a tap picks it.
void materialPanel() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
  if (!ImGui::BeginPopupModal("Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  const float size = 96 * density;
  const std::vector<std::string>& names = builtinMatcaps();
  for (int i = 0; i < mobileMatcapCount(); ++i) {
    if (i % 3) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##matcap", {size,size})) { setMobileMatcap(i); ImGui::CloseCurrentPopup(); }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Matcap rows run bottom-up; flip V so the sphere is lit from above.
    draw->AddImageRounded(ImTextureRef(ImTextureID(mobileMatcapTexture(i))), p, {p.x+size,p.y+size}, {0,1}, {1,0},
                          IM_COL32_WHITE, size / 2);
    if (i == mobileMatcap())
      draw->AddCircle({p.x+size/2,p.y+size/2}, size/2 + 2*density, ImGui::GetColorU32(ImGuiCol_CheckMark), 0, 2*density);
    const std::string label = materialLabel(names[size_t(i)]);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (size - ImGui::CalcTextSize(label.c_str()).x) / 2);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopID();
    ImGui::EndGroup();
  }
  if (ImGui::Button("Cancel", {-1,0})) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// Where the UI takes pointer input, for the pen here and for fingers in Java
// (read on the UI thread, hence atomic): the tool strip left of the view, the
// top bar and the view bar over it, and everything while a popup is open.
struct UiRect {
  std::atomic<float> x0{0}, y0{0}, x1{0}, y1{0};
  void set(ImVec2 p, ImVec2 size) { x0 = p.x; y0 = p.y; x1 = p.x + size.x; y1 = p.y + size.y; }
  bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};
struct UiRegions {
  std::atomic<float> viewX{0};
  UiRect top, tab, bar, axes;
  std::atomic<bool> modal{false};
} ui;

bool uiAt(float x, float y) {
  return ui.modal || x < ui.viewX || ui.top.contains(x, y) || ui.tab.contains(x, y) || ui.bar.contains(x, y) ||
         ui.axes.contains(x, y);
}

// The top bar shows its second row (options and pen switches) unless folded.
bool barOpen = true;
bool askNewSphere = false;

constexpr ImGuiWindowFlags kFixed = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

void beginFixed(const char* id, ImVec2 pos, ImVec2 size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin(id, nullptr, kFixed);
}

// The tools in one column of icon buttons, as on the desktop; the buttons
// shrink to fit all eleven into the tablet's height.
void toolStrip(float w, float h) {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float gap = 4 * density;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, st.Colors[ImGuiCol_MenuBarBg]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {st.FramePadding.x, st.WindowPadding.y});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {st.ItemSpacing.x, gap});
  beginFixed("##tools", {0,0}, {w,h});
  const int n = int(Tool::Reduce) + 1;
  const float button = std::min(ImGui::GetFrameHeight() * 2.3f, (ImGui::GetContentRegionAvail().y - (n - 1) * gap) / n);
  for (int i = 0; i < n; ++i) {
    const Tool tool = Tool(i);
    if (stripButton(toolName(tool), toolIcon(tool), app.brush.tool == tool, nullptr, button)) app.brush.tool = tool;
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

// A square button of the frame height with the menu's three lines, drawn
// (the font has no symbols).
bool menuButton(const char* id) {
  const float side = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, {side,side});
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, {p.x+side,p.y+side}, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_Button),
                    ImGui::GetStyle().FrameRounding);
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
  const float c = side / 2, s = side * 0.22f;
  for (float dy : {-s, 0.0f, s}) dl->AddLine({p.x+c-s,p.y+c+dy}, {p.x+c+s,p.y+c+dy}, ink, 2 * density);
  return clicked;
}

// A tab hanging centered under the top bar, in the bar's color, with a
// chevron: up folds the bar's second row away, down brings it back. Placed
// there, it reads as the handle of the bar.
void foldTab(float barBottom) {
  const float w = 72 * density, h = 26 * density;
  const ImVec2 pos(app.viewX + (app.viewW - w) / 2, barBottom);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize({w,h});
  ImGui::Begin("##fold", nullptr, kFixed | ImGuiWindowFlags_NoBackground);
  if (ImGui::InvisibleButton("##tab", {w,h})) barOpen = !barOpen;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_PopupBg]; bg.w = 0.85f;
  if (ImGui::IsItemActive()) bg = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
  dl->AddRectFilled(pos, {pos.x+w,pos.y+h}, ImGui::GetColorU32(bg), h / 2, ImDrawFlags_RoundCornersBottom);
  const float c = pos.x + w / 2, m = pos.y + h / 2, s = 8 * density, d = (barOpen ? -s : s) / 2;
  const ImVec2 pts[] = {{c-s,m-d}, {c,m+d}, {c+s,m-d}};
  dl->AddPolyline(pts, 3, ImGui::GetColorU32(ImGuiCol_Text), ImDrawFlags_None, 2 * density);
  ImGui::End();
  ImGui::PopStyleVar();
  ui.tab.set(pos, {w,h});
}

// Sculptris' layout: the brush settings in a bar along the top of the view,
// drawn over it. Row 1: the tool, Size, Strength, Detail, the material, the
// menu. Row 2 (unless folded with the tab below the bar): the tool's options, the pen
// switches and symmetry. Seldom-used commands are in the menu.
int topBar(float x, float w) {
  int action = 0;
  const ImGuiStyle& st = ImGui::GetStyle();
  const float row = ImGui::GetFrameHeight(), rows = barOpen ? 2.0f : 1.0f;
  const ImVec2 pos(x, 0), size(w, 2 * st.WindowPadding.y + rows * row + (rows - 1) * st.ItemSpacing.y);
  ImVec4 bg = st.Colors[ImGuiCol_PopupBg]; bg.w = 0.85f;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  beginFixed("##top", pos, size);
  ui.top.set(pos, size);

  const Tool tool = app.brush.tool;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  drawIcon(ImGui::GetWindowDrawList(), toolIcon(tool), {p.x+row/2,p.y+row/2}, row, ImGui::GetColorU32(ImGuiCol_Text), paintColor());
  ImGui::Dummy({row,row});
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(toolName(tool));
  ImGui::SameLine(row + ImGui::CalcTextSize("Flatten").x + 2 * st.ItemSpacing.x + st.WindowPadding.x);
  const float slider = 150 * density;
  ToolSliders& s = sliders(tool);
  ImGui::SetNextItemWidth(slider); ImGui::SliderFloat("##size", &s.size, 0.02f, 1, "Size %.2f");
  ImGui::SameLine(); ImGui::SetNextItemWidth(slider); ImGui::SliderFloat("##strength", &s.strength, 0, 1, "Strength %.2f");
  ImGui::SameLine(); ImGui::SetNextItemWidth(slider); ImGui::SliderFloat("##detail", &app.brush.detailFactor, 0.15f, 2, "Detail %.2f");

  // Right end: material sphere, menu.
  ImGui::SameLine(w - st.WindowPadding.x - 2 * row - st.ItemSpacing.x);
  const ImVec2 m = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("##material", {row,row})) ImGui::OpenPopup("Material");
  ImGui::GetWindowDrawList()->AddImageRounded(ImTextureRef(ImTextureID(mobileMatcapTexture(mobileMatcap()))), m,
      {m.x+row,m.y+row}, {0,1}, {1,0}, IM_COL32_WHITE, row / 2);
  materialPanel();
  ImGui::SameLine();
  if (menuButton("##menubutton")) ImGui::OpenPopup("##menu");
  if (ImGui::BeginPopup("##menu")) {
    if (ImGui::Selectable("Open...")) action = 1;
    if (ImGui::Selectable("Save...")) action = 2;
    if (ImGui::Selectable("Export OBJ...")) action = 3;
    ImGui::Separator();
    if (ImGui::Selectable("New sphere...")) askNewSphere = true;
    ImGui::EndPopup();
  }
  if (askNewSphere) { ImGui::OpenPopup("New sphere?"); askNewSphere = false; }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
  if (ImGui::BeginPopupModal("New sphere?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Replace the current model? Save a copy first.");
    if (ImGui::Button("Replace")) { newModel(); app.modified = true; savePending = true; invalidateMobileGeometry(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (barOpen) {
    bool options = true;
    switch (tool) {
      case Tool::Draw:
        ImGui::Checkbox("Clay", &app.brush.clay); ImGui::SameLine(); ImGui::Checkbox("Soft", &app.brush.soft); break;
      case Tool::Flatten:
        ImGui::Checkbox("Lock plane", &app.brush.lockPlane); ImGui::SameLine();
        ImGui::Checkbox("Angle falloff", &app.brush.angleFalloff); break;
      case Tool::Grab:
        ImGui::Checkbox("Global", &app.brush.global); ImGui::SameLine(); ImGui::Checkbox("Limit", &app.brush.limit); break;
      case Tool::Rotate: ImGui::Checkbox("Global", &app.brush.global); break;
      case Tool::Scale:
        ImGui::Checkbox("Global", &app.brush.global); ImGui::SameLine(); ImGui::Checkbox("XYZ", &app.brush.xyz); break;
      default: options = false; break;
    }
    if (options) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
    ImGui::Checkbox("Invert", &invert); ImGui::SameLine();
    ImGui::Checkbox("Smooth", &smoothing); ImGui::SameLine();
    ImGui::Checkbox("Pressure", &pressureOn); ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    bool symmetric = app.mesh.symmetric;
    if (ImGui::Checkbox("Symmetry", &symmetric)) ImGui::OpenPopup("Change symmetry?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
    if (ImGui::BeginPopupModal("Change symmetry?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(app.mesh.symmetric ? "Mirror the half into a full mesh?" : "Keep the +x side and mirror it?");
      if (ImGui::Button("Apply")) { setSymmetry(!app.mesh.symmetric); savePending = true; invalidateMobileGeometry(); ImGui::CloseCurrentPopup(); }
      ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  return action;
}

// Floating at the bottom of the view, as on the desktop, with Undo added:
// the tablet has no keyboard for Ctrl+Z.
void viewBar() {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float side = ImGui::GetFrameHeight() * 1.5f, pad = 10 * density;
  const ImVec2 padding(st.FramePadding.x, st.FramePadding.x);
  const float w = 3 * side + 2 * st.ItemSpacing.x + 2 * padding.x, h = side + 2 * padding.y;
  const ImVec2 pos(app.viewX + (app.viewW - w) / 2, app.viewY + app.viewH - h - pad);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, st.FrameRounding * 2);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, st.Colors[ImGuiCol_PopupBg]);
  beginFixed("##viewbar", pos, {w,h});
  if (viewButton("##undo", Icon::Undo, false, nullptr, side) && !app.stroke) {
    undoLast(); app.modified = true; savePending = true; invalidateMobileGeometry();
  }
  ImGui::SameLine();
  if (viewButton("##frame", Icon::Frame, false, nullptr, side)) frameModel();
  ImGui::SameLine();
  if (viewButton("##wire", Icon::Wireframe, app.wireframe, nullptr, side)) app.wireframe = !app.wireframe;
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  ui.bar.set(pos, {w,h});
}

// Like Sculptris: the triangle count, and any message, as dim text in the
// lower left corner of the view, not a bar of its own.
void statusText() {
  const float pad = 12 * density, line = ImGui::GetTextLineHeightWithSpacing();
  char info[64];
  std::snprintf(info, sizeof info, "%d triangles", app.mesh.numTriangles());
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  dl->AddText({app.viewX + pad, app.viewH - pad - line}, dim, info);
  if (!app.status.empty()) dl->AddText({app.viewX + pad, app.viewH - pad - 2 * line}, dim, app.status.c_str());
}

int mobileUi() {
  const float strip = 76 * density;
  app.viewX = int(strip); app.viewY = 0;
  app.viewW = std::max(1, app.width - app.viewX); app.viewH = std::max(1, app.height);
  ui.viewX = float(app.viewX);
  toolStrip(strip, float(app.height));
  const int action = topBar(strip, float(app.viewW));
  foldTab(ui.top.y1);
  // The axes top right under the bar, as on the desktop; a bit larger for fingers.
  const float axes = 110 * density, pad = 10 * density;
  const ImVec2 axesAt(app.viewX + app.viewW - axes - pad, ui.top.y1 + pad);
  axisGizmo(axesAt, axes);
  ui.axes.set(axesAt, {axes,axes});
  viewBar();
  statusText();
  ui.modal = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
  return action;
}

}  // namespace
}  // namespace bh::viewer

using namespace bh;
using namespace bh::viewer;

extern "C" JNIEXPORT jboolean JNICALL
Java_org_bildhauer_app_MainActivity_nativeInit(JNIEnv* env, jclass, jstring directory, jfloat scale, jboolean restore) {
  try {
    if (ImGui::GetCurrentContext()) { ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); }
    density = scale;
    setMobileDensity(scale);
    recovery = text(env,directory) + "/recovery.sc1";
    if (!initialized) {
      for (int i=0;i<int(app.sliders.size());++i) app.sliders[i]=defaultSliders(Tool(i));
      app.undoBudget = 256 * 1024 * 1024;
      newModel();
      std::string error;
      std::vector<uint8_t> bytes;
      // Like Sculptris, a launch starts with the sphere. Only an activity that
      // Android killed in the background (e.g. behind the file picker) and now
      // recreates gets its session back.
      if (restore && readBinaryFile(recovery,bytes)) {
        if (loadModel(recovery,&error)) { app.path.clear(); app.modified = true; app.status = "Recovered the last session"; }
        else app.status = "Recovery could not be loaded: " + error;
      }
      initialized = true;
    }
    initMobileRendering();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    applyStyle(density);
    // Touch: taller rows and grips than with a mouse.
    ImGuiStyle& style=ImGui::GetStyle();
    style.FramePadding.y=7*density; style.GrabMinSize=20*density; style.ScrollbarSize=14*density;
    // The style's density scale (FontScaleDpi) sizes the font, as on the desktop.
    if (!io.Fonts->AddFontFromFileTTF("/system/fonts/Roboto-Regular.ttf",16))
      io.Fonts->AddFontDefault();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    lastFrame=std::chrono::steady_clock::now();
    __android_log_print(ANDROID_LOG_INFO,"bildhauer","Ready: %d triangles, undo budget 256 MiB",app.mesh.numTriangles());
    return JNI_TRUE;
  } catch (const std::exception& e) {
    __android_log_print(ANDROID_LOG_ERROR,"bildhauer","Init failed: %s",e.what());
    return JNI_FALSE;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_org_bildhauer_app_MainActivity_nativeResize(JNIEnv*,jclass,jint width,jint height) {
  app.width=width; app.height=height;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_bildhauer_app_MainActivity_nativeFrame(JNIEnv*,jclass) {
  auto now=std::chrono::steady_clock::now();
  ImGuiIO& io=ImGui::GetIO(); io.DisplaySize={float(app.width),float(app.height)};
  if (flushPendingStroke()) invalidateMobileGeometry();
  io.DeltaTime=std::clamp(std::chrono::duration<float>(now-lastFrame).count(),0.001f,0.1f); lastFrame=now;
  if (!app.mesh.dirtyVertices.empty()) { beautifyPass(app.mesh); invalidateMobileGeometry(); }
  if (savePending && !app.stroke && app.mesh.dirtyVertices.empty()) saveRecovery();
  ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
  int action=mobileUi();
  renderMobileScene();
  ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // Bit 8 asks Java for another frame: stroke, mesh cleanup, save pending or
  // the view name under the axes still fading.
  const bool busy=app.stroke || app.pendingGrab.valid || !app.mesh.dirtyVertices.empty() || savePending ||
                  axisLabelFading();
  return action | (busy ? 0x100 : 0);
}

extern "C" JNIEXPORT void JNICALL
Java_org_bildhauer_app_MainActivity_nativePointer(JNIEnv*,jclass,jint action,jfloat x,jfloat y,jfloat pressure,jboolean button) {
  if (!ImGui::GetCurrentContext()) return;
  ImGuiIO& io=ImGui::GetIO(); io.AddMousePosEvent(x,y);
  const float strength=pressureOn ? std::clamp(pressure,0.0f,1.0f) : 1;
  if (action==0) {
    uiPointer = uiAt(x,y) || ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId);
    if (uiPointer) io.AddMouseButtonEvent(0,true);
    else if (!inputAt(int(x),int(y)).hit) { penOrbit=true; orbitX=x; orbitY=y; }
    else beginStroke(int(x),int(y),invert || button,smoothing,strength);
  } else if (action==1 && penOrbit) {
    app.camera.yaw-=(x-orbitX)*0.008f/density;
    app.camera.pitch=std::clamp(app.camera.pitch+(y-orbitY)*0.008f/density,-1.5f,1.5f);
    orbitX=x; orbitY=y; app.hover={};
  } else if (action==1) {
    if (!uiPointer && x >= app.viewX && !deferGrabMove(int(x),int(y),strength))
      moveStroke(int(x),int(y),strength);
  } else if (action==4) {
    // Hover has no contact pressure and cannot update a contacting stroke.
    if (!app.stroke && !uiPointer && !uiAt(x,y)) moveStroke(int(x),int(y));
    else if (!app.stroke) app.hover={};
  } else if (action==5) {
    // Leaving hover is separate from ACTION_CANCEL (action 3).
    if (!app.stroke && !uiPointer) app.hover={};
  } else if (action==2 || action==3) {
    if (uiPointer) io.AddMouseButtonEvent(0,false);
    // A lifted finger leaves no pointer behind: without this, ImGui would keep
    // hovering the last touch point (the pen's hover events set it again).
    io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);
    if (app.stroke) {
      flushPendingStroke();
      const DabTimes& t=app.stroke->times();
      __android_log_print(ANDROID_LOG_INFO,"bildhauer",
          "%s: %d updates, tess %.1f / tool %.1f / geometry %.1f ms, worst %.1f ms",
          toolName(app.stroke->tool()),app.stroke->dabCount(),t.tessellate,t.tool,t.geometry,t.worstDab);
      endStroke(); savePending = true; invalidateMobileGeometry();
    }
    uiPointer=false; penOrbit=false;
    if (action==3) app.hover={};
  }
  if (app.stroke) invalidateMobileGeometry();
}

extern "C" JNIEXPORT void JNICALL
Java_org_bildhauer_app_MainActivity_nativeCamera(JNIEnv*,jclass,jfloat dx,jfloat dy,jfloat zoom,jboolean pan) {
  if (app.stroke) return;
  if (pan) {
    float scale=app.camera.distance*2*std::tan(kFov/2)/std::max(1,app.viewH);
    app.camera.target+=(app.camera.right()*-dx+app.camera.up()*dy)*scale;
  } else {
    app.camera.yaw-=dx*0.008f/density;
    app.camera.pitch=std::clamp(app.camera.pitch+dy*0.008f/density,-1.5f,1.5f);
  }
  app.camera.distance=std::clamp(app.camera.distance*zoom,app.sceneSize*0.15f,app.sceneSize*30);
  app.hover={};
}

extern "C" JNIEXPORT void JNICALL
Java_org_bildhauer_app_MainActivity_nativePause(JNIEnv*,jclass) {
  if (flushPendingStroke()) invalidateMobileGeometry();
  endStroke(); uiPointer=false; penOrbit=false;
  if (ImGui::GetCurrentContext()) ImGui::GetIO().AddMouseButtonEvent(0,false);
  if (app.modified) saveRecovery();
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_bildhauer_app_MainActivity_nativeFile(JNIEnv* env,jclass,jint action,jstring filename) {
  if (flushPendingStroke()) invalidateMobileGeometry();
  endStroke();
  std::string path=text(env,filename),error;
  bool ok=false;
  if (action==1) {
    std::vector<uint8_t> bytes; Sc1File file;
    if (!readBinaryFile(path,bytes)) error="Cannot read the selected file";
    else if (readSc1(bytes,file,&error)) {
      if (file.hasTexturing) error="Painted files will be supported in a later Android version";
      else if ((ok=loadModel(path,&error))) { app.path.clear(); saveRecovery(); invalidateMobileGeometry(); }
    }
  } else if (action==2) ok=saveModel(path,&error);
  else if (action==3) {
    ObjExport options; options.scale=app.file.extraA; options.offset=app.file.extraVec;
    ok=writeObj(path,app.mesh,options,&error);
  }
  app.status=ok ? (action==1 ? "Opened model" : "Snapshot ready to save") : error;
  return env->NewStringUTF(ok ? "" : error.c_str());
}

// For finger input on the UI thread: whether (x, y) belongs to the UI.
extern "C" JNIEXPORT jboolean JNICALL
Java_org_bildhauer_app_MainActivity_nativeUiAt(JNIEnv*,jclass,jfloat x,jfloat y) {
  return uiAt(x,y) ? JNI_TRUE : JNI_FALSE;
}
