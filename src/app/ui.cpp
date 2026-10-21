#include "app/ui.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

#include "app/app.h"
#include "app/dialogs.h"
#include "app/document.h"
#include "app/input.h"
#include "app/matcaps.h"
#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

namespace bh::viewer {

namespace {

bool paintModeQuestion = false;  // open the "Enter paint mode?" dialog next frame
int symmetryQuestion = -1;       // open the symmetry dialog next frame: 1 on, 0 off
bool showKeys = false;

struct ToolButton {
  Tool tool;
  const char* key;
};
const ToolButton kSculptTools[] = {
    {Tool::Draw, "D"},    {Tool::Smooth, "B"}, {Tool::Grab, "G"},   {Tool::Inflate, "C"},
    {Tool::Flatten, "F"}, {Tool::Pinch, "V"},  {Tool::Mask, "M"},   {Tool::Crease, "E"},
    {Tool::Rotate, "R"},  {Tool::Scale, "T"},  {Tool::Reduce, "Y"},
};

std::string fileName(const std::string& path) { return path.substr(path.find_last_of("/\\") + 1); }

// A matcap's short name: "gold" for "bildhauer:gold", the file name of an image.
std::string matcapLabel(const std::string& matcap) {
  std::string s = matcap.substr(matcap.find_last_of(":/\\") + 1);
  return s.empty() ? std::string("(none)") : s;
}

// Canvas colors are 0x00BBGGRR.
bool colorEdit(const char* label, uint32_t& color) {
  float c[3] = {(color & 0xFF) / 255.0f, (color >> 8 & 0xFF) / 255.0f, (color >> 16 & 0xFF) / 255.0f};
  if (!ImGui::ColorEdit3(label, c, ImGuiColorEditFlags_NoInputs)) return false;
  auto byte = [](float v) { return uint32_t(v * 255 + 0.5f); };
  color = byte(c[0]) | byte(c[1]) << 8 | byte(c[2]) << 16;
  return true;
}

// A row of equally wide selectable buttons.
bool toolButton(const char* label, bool selected, float width, const char* tooltip) {
  if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  bool clicked = ImGui::Button(label, ImVec2(width, 0));
  if (selected) ImGui::PopStyleColor();
  if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

void slider(const char* label, float& value) { ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f"); }

void menuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New", "Ctrl+N")) commandNew();
    if (ImGui::MenuItem("Open...", "Ctrl+O")) commandOpen();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S")) commandSave();
    if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) commandSaveAs();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) PostMessageA(app.hwnd, WM_CLOSE, 0, 0);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z")) undoLast();
    ImGui::Separator();
    if (ImGui::MenuItem("Subdivide", "Ctrl+D", false, !app.paintMode)) subdivideAll();
    if (ImGui::MenuItem("Reduce all", "Ctrl+Y", false, !app.paintMode)) reduceAll();
    if (ImGui::MenuItem("Symmetry", nullptr, app.mesh.symmetric, !app.paintMode)) askSetSymmetry(!app.mesh.symmetric);
    ImGui::Separator();
    if (ImGui::MenuItem("Enter paint mode...", "Ctrl+P", false, !app.paintMode)) askEnterPaintMode();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Wireframe", "W", &app.wireframe);
    ImGui::MenuItem("Bump normal map", "N", &app.showBumpNormals, app.paintMode);
    if (ImGui::MenuItem("Frame model")) frameModel();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    if (ImGui::MenuItem("Mouse and keys")) showKeys = true;
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void sculptPanel(float width) {
  ImGui::SeparatorText("Brush");
  const float column = (width - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
  int i = 0;
  for (const ToolButton& b : kSculptTools) {
    char tip[64];
    std::snprintf(tip, sizeof tip, "%s (%s)", toolName(b.tool), b.key);
    if (i % 3) ImGui::SameLine();
    if (toolButton(toolName(b.tool), app.brush.tool == b.tool, column, tip)) app.brush.tool = b.tool;
    ++i;
  }
  ToolSliders& s = sliders(app.brush.tool);
  ImGui::Spacing();
  slider("Size", s.size);
  slider("Strength", s.strength);
  switch (app.brush.tool) {
    case Tool::Draw:
      ImGui::Checkbox("Clay", &app.brush.clay);
      ImGui::SameLine();
      ImGui::Checkbox("Soft", &app.brush.soft);
      break;
    case Tool::Flatten:
      ImGui::Checkbox("Lock plane", &app.brush.lockPlane);
      ImGui::SameLine();
      ImGui::Checkbox("Angle falloff", &app.brush.angleFalloff);
      break;
    case Tool::Grab:
      ImGui::Checkbox("Global", &app.brush.global);
      ImGui::SameLine();
      ImGui::Checkbox("Limit", &app.brush.limit);
      break;
    case Tool::Rotate: ImGui::Checkbox("Global", &app.brush.global); break;
    case Tool::Scale:
      ImGui::Checkbox("Global", &app.brush.global);
      ImGui::SameLine();
      ImGui::Checkbox("XYZ", &app.brush.xyz);
      break;
    default: break;
  }
  ImGui::TextDisabled("Ctrl inverts, Shift smooths");

  ImGui::SeparatorText("Mesh");
  ImGui::Checkbox("Beautify while sculpting", &app.beautify);
  bool symmetric = app.mesh.symmetric;
  if (ImGui::Checkbox("Symmetry (mirror at x = 0)", &symmetric)) askSetSymmetry(symmetric);
  if (ImGui::Button("Subdivide", ImVec2(width / 2 - 4, 0))) subdivideAll();
  ImGui::SameLine();
  if (ImGui::Button("Reduce all", ImVec2(-1, 0))) reduceAll();
  ImGui::Spacing();
  if (ImGui::Button("Enter paint mode...", ImVec2(-1, 0))) askEnterPaintMode();
}

void materialPanel() {
  Canvas& cv = *app.canvas;
  ImGui::SeparatorText("Materials");
  const float thumb = ImGui::GetFrameHeight() * 1.4f;
  for (int slot = 0; slot < kMaxMaterials; ++slot) {
    const Material& m = cv.material(slot);
    if (!m.used) continue;
    ImGui::PushID(slot);
    GLuint tex = app.slotTex[slot].matcap;
    if (tex) ImGui::Image(ImTextureRef(ImTextureID(intptr_t(tex))), ImVec2(thumb, thumb), ImVec2(0, 1), ImVec2(1, 0));
    else ImGui::Dummy(ImVec2(thumb, thumb));
    ImGui::SameLine();
    char label[128];
    std::snprintf(label, sizeof label, "%s  %s%s", slot == 0 ? "Base" : ("Layer " + std::to_string(slot)).c_str(),
                  matcapLabel(m.matcap).c_str(), m.hidden ? "  (hidden)" : "");
    if (ImGui::Selectable(label, app.editSlot == slot, 0, ImVec2(0, thumb))) app.editSlot = slot;
    ImGui::PopID();
  }
  if (ImGui::Button("Add layer (L)", ImVec2(-1, 0))) addMaterialLayer();

  Material& m = cv.material(app.editSlot);
  ImGui::Spacing();
  ImGui::Text("%s", app.editSlot == 0 ? "Base material" : ("Layer " + std::to_string(app.editSlot)).c_str());
  if (ImGui::BeginCombo("Matcap", matcapLabel(m.matcap).c_str())) {
    for (const std::string& name : builtinMatcaps())
      if (ImGui::Selectable(matcapLabel(name).c_str(), name == m.matcap)) {
        m.matcap = name;
        app.modified = true;
      }
    ImGui::Separator();
    if (ImGui::Selectable("Image file...")) {
      std::string file = openFileDialog(app.hwnd, kImageFilter);
      if (!file.empty()) {
        m.matcap = file;
        app.modified = true;
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::Checkbox("Colored", &m.colored)) app.modified = true;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Keep the matcap's colors; off: gray, so the paint shows through");
  if (app.editSlot > 0) {
    ImGui::SameLine();
    if (ImGui::Checkbox("Additive", &m.additive)) app.modified = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Hide", &m.hidden)) app.modified = true;
    if (ImGui::Button("Delete layer", ImVec2(-1, 0))) {
      cv.material(app.editSlot) = Material{};
      app.editSlot = 0;
      app.modified = true;
    }
  }
  ImGui::TextDisabled(app.editSlot == 0 ? "Paint color paints the color layer" : "Paint color paints this layer's mask");
}

void paintPanel(float width) {
  ImGui::SeparatorText("Brush");
  const PaintTool tools[] = {PaintTool::Color, PaintTool::Bump, PaintTool::FlattenBump};
  const char* labels[] = {"Color", "Bump", "Flatten"};
  const char* tips[] = {"Paint color (D)", "Paint bump (B)", "Flatten bump (F)"};
  const float column = (width - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    if (i) ImGui::SameLine();
    if (toolButton(labels[i], app.paint.tool == tools[i], column, tips[i])) app.paint.tool = tools[i];
  }
  std::array<float, 3>& ps = paintSliders();
  ImGui::Spacing();
  slider("Size", ps[0]);
  slider("Strength", ps[1]);
  slider("Hardness", ps[2]);
  ImGui::Checkbox("Surface angle falloff", &app.paint.angleFalloff);
  ImGui::TextDisabled("Ctrl inverts");
  char fill[64];
  std::snprintf(fill, sizeof fill, "%s (Ctrl+F)", fillLabel());
  if (ImGui::Button(fill, ImVec2(-1, 0))) fillLayer(ImGui::GetIO().KeyShift);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("The whole layer at once, as far as Strength says.\n"
                      "With Shift (Ctrl+Shift+F): the secondary color, or an empty mask.");

  ImGui::SeparatorText("Colors");
  colorEdit("Primary", app.paint.primary);
  ImGui::SameLine();
  colorEdit("Secondary", app.paint.secondary);
  ImGui::SameLine();
  if (ImGui::SmallButton("Swap")) std::swap(app.paint.primary, app.paint.secondary);

  materialPanel();

  ImGui::SeparatorText("View");
  ImGui::Checkbox("Bump normal map (N)", &app.showBumpNormals);
}

void toolPanel() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float top = ImGui::GetFrameHeight(), bottom = ImGui::GetFrameHeight();
  const float width = 280 * ImGui::GetStyle().FontScaleDpi;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + top));
  ImGui::SetNextWindowSize(ImVec2(width, vp->Size.y - top - bottom));
  ImGui::Begin(app.paintMode ? "Paint" : "Sculpt", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoSavedSettings);
  const float inner = ImGui::GetContentRegionAvail().x;
  ImGui::PushItemWidth(inner * 0.62f);
  if (app.paintMode) paintPanel(inner);
  else sculptPanel(inner);
  ImGui::PopItemWidth();
  ImGui::End();
}

void statusBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().FramePadding.x * 2, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##status", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(app.status.c_str());
  char right[256];
  std::snprintf(right, sizeof right, "%s%s   %d triangles   %s", app.path.empty() ? "new model" : fileName(app.path).c_str(),
                app.modified ? " *" : "", app.mesh.numTriangles(), app.paintMode ? "paint mode" : "sculpt mode");
  ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right).x - ImGui::GetStyle().FramePadding.x * 2);
  ImGui::TextDisabled("%s", right);
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void dialogs() {
  if (paintModeQuestion) {
    ImGui::OpenPopup("Enter paint mode?");
    paintModeQuestion = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Enter paint mode?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Painting needs a fixed mesh: after this, the model can no longer be sculpted.");
    ImGui::TextUnformatted("bildhauer builds a texture atlas and starts with an empty canvas.");
    ImGui::Spacing();
    if (ImGui::Button("Enter paint mode", ImVec2(160 * ImGui::GetStyle().FontScaleDpi, 0))) {
      enterPaintMode();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (symmetryQuestion >= 0) {
    ImGui::OpenPopup(symmetryQuestion ? "Turn symmetry on?" : "Turn symmetry off?");
    symmetryQuestion = -1;
  }
  for (bool on : {true, false}) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(on ? "Turn symmetry on?" : "Turn symmetry off?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
      continue;
    if (on) {
      ImGui::TextUnformatted("The +x side is kept and mirrored to the -x side.");
      ImGui::TextUnformatted("Whatever differs on the -x side is lost.");
    } else {
      ImGui::TextUnformatted("The mirrored half becomes part of the mesh, and both sides can differ.");
      ImGui::TextUnformatted("Turning symmetry on again later cuts the -x side away.");
    }
    ImGui::TextDisabled("Undo (Ctrl+Z) brings the mesh back. The mask is cleared.");
    ImGui::Spacing();
    if (ImGui::Button(on ? "Turn on" : "Turn off", ImVec2(120 * ImGui::GetStyle().FontScaleDpi, 0))) {
      setSymmetry(on);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (showKeys) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Mouse and keys", &showKeys, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(
        "Left button: sculpt / paint (Ctrl inverts; sculpt: Shift smooths)\n"
        "Right button or Alt+left: orbit    Middle button: pan    Wheel: zoom\n"
        "\n"
        "[ ] or , .   size;  with Ctrl: strength (also - =);  paint: with Shift hardness\n"
        "W wireframe    Ctrl+Z undo    Ctrl+N new    Ctrl+O open    Ctrl+S save\n"
        "\n"
        "Sculpt: D Draw, B Smooth, G Grab, C Inflate, F Flatten, V Pinch, M Mask,\n"
        "  E Crease, R Rotate, T Scale, Y Reduce; the tool key again toggles its option\n"
        "  Ctrl+D subdivide, Ctrl+Y reduce all, Ctrl+P paint mode\n"
        "\n"
        "Paint: D color, B bump, F flatten bump, N bump normal map\n"
        "  Ctrl+F fill color / layer mask or clear bump (with Shift: inverted)\n"
        "  L add layer, 0-9 paint target (0 color, n mask of layer n),\n"
        "  M next matcap, O matcap image, C colored, A additive, H hide");
    ImGui::End();
  }
}

void setStyle(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0;
  s.FrameRounding = 4;
  s.GrabRounding = 4;
  s.PopupRounding = 4;
  s.WindowBorderSize = 0;
  s.FramePadding = ImVec2(8, 4);
  s.ItemSpacing = ImVec2(8, 6);
  ImVec4* c = s.Colors;
  const ImVec4 accent(0.86f, 0.62f, 0.25f, 1.0f);
  c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.115f, 0.13f, 0.94f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.095f, 0.11f, 1.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.14f, 0.98f);
  c[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.2f, 0.23f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.26f, 0.3f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.31f, 0.35f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.2f, 0.21f, 0.24f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.29f, 0.33f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.4f, 0.17f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.55f, 0.4f, 0.17f, 0.55f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.4f, 0.17f, 0.75f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.4f, 0.17f, 1.0f);
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.75f, 0.35f, 1.0f);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.095f, 0.11f, 1.0f);
  c[ImGuiCol_Separator] = ImVec4(0.3f, 0.31f, 0.35f, 1.0f);
  c[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.88f, 1.0f);
  s.ScaleAllSizes(scale);
  s.FontScaleDpi = scale;
}

}  // namespace

void initUi(HWND hwnd) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // fixed layout, nothing to remember
  char fonts[MAX_PATH];
  if (GetWindowsDirectoryA(fonts, MAX_PATH)) {
    std::string font = std::string(fonts) + "\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES) io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
  }
  setStyle(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplOpenGL2_Init();
}

void shutdownUi() {
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
}

bool uiHandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp) != 0;
}

void drawUi() {
  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  menuBar();
  toolPanel();
  statusBar();
  dialogs();
  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

bool uiWantsMouse() { return ImGui::GetIO().WantCaptureMouse; }

bool uiWantsKeyboard() { return ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId); }

void askEnterPaintMode() {
  if (!app.paintMode) paintModeQuestion = true;
}

void askSetSymmetry(bool on) {
  if (!app.paintMode && app.mesh.symmetric != on) symmetryQuestion = on ? 1 : 0;
}

}  // namespace bh::viewer
