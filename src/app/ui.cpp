#include "app/ui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "app/dialogs.h"
#include "app/document.h"
#include "app/icons.h"
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

void slider(const char* label, float& value) { ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f"); }

// Fixed layout: menu bar on top, status bar at the bottom, the tool strip and
// the brush panel on the left, the model panel on the right; the 3D view
// gets the rest.
struct Layout {
  float top, bottom, strip, panel, side;
};

Layout layout() {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float s = st.FontScaleDpi;
  // Before the first frame there is no font size yet: estimate the bars.
  const float bar = ImGui::GetFontSize() > 0 ? ImGui::GetFrameHeight() : 16 * s + 2 * st.FramePadding.y;
  return {bar, bar, 84 * s, 270 * s, 250 * s};
}

bool beginPanel(const char* id, float x, float w) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const Layout l = layout();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + x, vp->Pos.y + l.top));
  ImGui::SetNextWindowSize(ImVec2(w, vp->Size.y - l.top - l.bottom));
  return ImGui::Begin(id, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBringToFrontOnFocus);
}

// A larger title with a short description under it.
// The paint color as an ImGui color (canvas colors are 0x00BBGGRR).
ImU32 paintColor() { return app.paint.primary | 0xFF000000u; }

// The tool icon, a larger title beside it, and a short description below.
void panelTitle(Icon icon, const char* title, const char* description) {
  ImGui::PushFont(nullptr, ImGui::GetFontSize() / ImGui::GetStyle().FontScaleDpi * 1.35f);
  const float side = ImGui::GetFontSize() * 1.5f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(p.x + side / 2, p.y + side / 2), side,
           ImGui::GetColorU32(ImGuiCol_Text), paintColor());
  ImGui::Dummy(ImVec2(side, side));
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (side - ImGui::GetFontSize()) / 2);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", description);
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

const char* toolDescription(Tool tool) {
  switch (tool) {
    case Tool::Draw: return "Builds up the surface along the stroke. Clay: flatter, layered build-up.";
    case Tool::Smooth: return "Evens out the surface. Shift smooths with any tool.";
    case Tool::Grab: return "Drags the surface along with the pointer.";
    case Tool::Inflate: return "Pushes the surface out along its normals.";
    case Tool::Flatten: return "Planes the surface down to the plane under the brush.";
    case Tool::Pinch: return "Pulls the surface toward the middle of the brush.";
    case Tool::Mask: return "Protects the surface from the other brushes.";
    case Tool::Crease: return "Cuts a sharp groove; inverted, a sharp ridge.";
    case Tool::Rotate: return "Turns the area under the brush.";
    case Tool::Scale: return "Grows or shrinks the area under the brush.";
    case Tool::Reduce: return "Removes detail under the brush; inverted, adds it.";
  }
  return "";
}

const char* paintToolDescription(PaintTool tool) {
  switch (tool) {
    case PaintTool::Color: return "Paints the color layer, or the mask of the selected material layer.";
    case PaintTool::Bump: return "Paints height into the bump layer.";
    case PaintTool::FlattenBump: return "Moves the bump back toward flat.";
  }
  return "";
}

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

  // The mode in the middle. Paint mode is one way: Paint asks first, and
  // there is no way back to Sculpt.
  const float pad = ImGui::GetStyle().FramePadding.x * 3;
  const float sculptW = ImGui::CalcTextSize("Sculpt").x + 2 * pad, paintW = ImGui::CalcTextSize("Paint").x + 2 * pad;
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - sculptW - paintW) / 2);
  ImGui::BeginDisabled(app.paintMode);
  ImGui::Selectable("Sculpt", !app.paintMode, ImGuiSelectableFlags_None, ImVec2(sculptW - 2 * pad, 0));
  ImGui::EndDisabled();
  if (app.paintMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Paint mode is one way: the mesh stays as it is");
  if (ImGui::Selectable("Paint", app.paintMode, ImGuiSelectableFlags_None, ImVec2(paintW - 2 * pad, 0)) &&
      !app.paintMode)
    askEnterPaintMode();
  if (!app.paintMode && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Asks first: painting needs a fixed mesh (Ctrl+P)");
  ImGui::EndMainMenuBar();
}

void sculptMaskPanel();

// A tool button of the strip: the icon with a small label under it.
bool stripButton(const char* label, Icon icon, bool selected, const char* tooltip) {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 2.3f;
  ImGui::PushID(label);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##tool", ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiCol bg = selected ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(bg), st.FrameRounding);
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
  drawIcon(dl, icon, ImVec2(p.x + w / 2, p.y + h * 0.39f), h * 0.54f, ink, paintColor());
  ImGui::PushFont(nullptr, ImGui::GetFontSize() / st.FontScaleDpi * 0.85f);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(p.x + (w - ts.x) / 2, p.y + h - ts.y - st.FramePadding.y), ink, label);
  ImGui::PopFont();
  if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
  ImGui::PopID();
  return clicked;
}

// The tools of the current mode, one button per row.
void toolStrip() {
  const Layout l = layout();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().WindowPadding.y));
  if (beginPanel("##tools", 0, l.strip)) {
    char tip[64];
    if (!app.paintMode) {
      for (const ToolButton& b : kSculptTools) {
        std::snprintf(tip, sizeof tip, "%s (%s)", toolName(b.tool), b.key);
        if (stripButton(toolName(b.tool), toolIcon(b.tool), app.brush.tool == b.tool, tip)) app.brush.tool = b.tool;
      }
    } else {
      const PaintTool tools[] = {PaintTool::Color, PaintTool::Bump, PaintTool::FlattenBump};
      const char* labels[] = {"Color", "Bump", "Flatten"};
      const char* keys[] = {"D", "B", "F"};
      for (int i = 0; i < 3; ++i) {
        std::snprintf(tip, sizeof tip, "%s (%s)", paintToolName(tools[i]), keys[i]);
        if (stripButton(labels[i], paintToolIcon(tools[i]), app.paint.tool == tools[i], tip)) app.paint.tool = tools[i];
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

// The current sculpt brush: its sliders and options, and the brush mask.
void sculptBrushPanel() {
  const Tool tool = app.brush.tool;
  panelTitle(toolIcon(tool), toolName(tool), toolDescription(tool));
  ToolSliders& s = sliders(tool);
  ImGui::SeparatorText("Brush");
  slider("Size", s.size);
  slider("Strength", s.strength);
  auto option = [](const char* label, bool& value, const char* tip) {
    ImGui::Checkbox(label, &value);
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
  };
  switch (tool) {
    case Tool::Draw:
      option("Clay", app.brush.clay, "Flatter, layered build-up (D again)");
      ImGui::SameLine();
      option("Soft", app.brush.soft, "With Clay: softer build-up");
      break;
    case Tool::Flatten:
      option("Lock plane", app.brush.lockPlane, "Keep the plane of the stroke's first dab (F again)");
      option("Angle falloff", app.brush.angleFalloff, "Leave surface that turns away from the plane");
      break;
    case Tool::Grab:
      option("Global", app.brush.global, "Move every unmasked vertex (G again)");
      ImGui::SameLine();
      option("Limit", app.brush.limit, nullptr);
      break;
    case Tool::Rotate: option("Global", app.brush.global, "Turn every unmasked vertex (R again)"); break;
    case Tool::Scale:
      option("Global", app.brush.global, "Scale every unmasked vertex (T again)");
      ImGui::SameLine();
      option("XYZ", app.brush.xyz, nullptr);
      break;
    default: break;
  }
  ImGui::Spacing();
  if (ImGui::CollapsingHeader(sculptMaskActive() ? "Brush mask (on)###brushmask" : "Brush mask###brushmask"))
    sculptMaskPanel();
}

// What concerns the whole model in sculpt mode.
void sculptModelPanel() {
  ImGui::SeparatorText("Model");
  bool symmetric = app.mesh.symmetric;
  if (ImGui::Checkbox("Symmetry (x = 0)", &symmetric)) askSetSymmetry(symmetric);
  ImGui::Checkbox("Beautify while sculpting", &app.beautify);
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (ImGui::Button("Subdivide", ImVec2(half, 0))) subdivideAll();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Splits every triangle in four (Ctrl+D)");
  ImGui::SameLine();
  if (ImGui::Button("Reduce all", ImVec2(-1, 0))) reduceAll();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Removes detail where the surface is flat (Ctrl+Y)");

  ImGui::SeparatorText("Paint");
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("Painting needs a fixed mesh: after entering paint mode, the model can no longer be sculpted.");
  ImGui::PopStyleColor();
  if (ImGui::Button("Enter paint mode...", ImVec2(-1, 0))) askEnterPaintMode();
}

// A stamp as a texture for the preview; `which` tells the stamps apart
// (0 color, 1 bump, 2 sculpt mask).
GLuint stampPreview(const Stamp& s, int which, float& aspect) {
  static GLuint tex = 0;
  static int version = -1, shown = -1;
  if (s.empty()) return 0;
  aspect = float(s.height) / float(s.width);
  if (tex && version == app.stampsVersion && shown == which) return tex;
  if (!tex) glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.width, s.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.rgba.data());
  version = app.stampsVersion;
  shown = which;
  return tex;
}

// Fixed / Directional / Random and the angle, shared by both modes.
void stampModeControls() {
  int mode = int(app.stampMode);
  ImGui::RadioButton("Fixed", &mode, int(StampMode::Fixed));
  ImGui::SameLine();
  ImGui::RadioButton("Directional", &mode, int(StampMode::Directional));
  ImGui::SameLine();
  ImGui::RadioButton("Random", &mode, int(StampMode::Random));
  app.stampMode = StampMode(mode);
  if (app.stampMode == StampMode::Fixed) ImGui::SliderFloat("Angle", &app.stampAngle, -180, 180, "%.0f\xC2\xB0");
}

// Brush mask in sculpt mode (brushes.md 3.4): the mask image alone, read by
// Draw, Smooth and Flatten.
void sculptMaskPanel() {
  if (ImGui::Checkbox("Enable", &app.sculptMaskOn) && app.sculptMaskOn && app.maskImage.empty() &&
      !chooseStampImage(true))
    app.sculptMaskOn = false;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Draw, Smooth and Flatten work where the image is bright");
  ImGui::SameLine();
  const std::string name = app.maskPath.empty() ? std::string("Image...") : fileName(app.maskPath);
  if (ImGui::Button(name.c_str(), ImVec2(-1, 0))) chooseStampImage(true);
  if (!app.maskPath.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", app.maskPath.c_str());
  if (!app.sculptMaskOn) return;
  if (ImGui::Checkbox("Invert mask", &app.maskInvert)) rebuildStamps();
  stampModeControls();
  float aspect = 1;
  if (GLuint tex = stampPreview(app.sculptStamp, 2, aspect)) {
    const float side = ImGui::GetFrameHeight() * 4;
    ImGui::Image(ImTextureRef(ImTextureID(intptr_t(tex))), ImVec2(side, side * aspect));
  }
  const Tool t = app.brush.tool;
  if (t != Tool::Draw && t != Tool::Smooth && t != Tool::Flatten) ImGui::TextDisabled("%s ignores the mask", toolName(t));
}

// Heightmask (paint.md 3.3): Paint color only reaches the high (or, with
// Cavity, the low) parts within the dab.
void heightmaskPanel() {
  PaintSettings& p = app.paint;
  ImGui::Checkbox("Enable", &p.heightmask);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Paint color only reaches what lies above the brush area's average height.\n"
                      "Bump strokes ignore it.");
  if (!p.heightmask) return;
  ImGui::Indent();
  ImGui::Checkbox("Cavity", &p.cavity);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Paint the low parts instead");
  ImGui::TextUnformatted("Height from:");
  ImGui::Checkbox("Bumpmap", &p.heightFromBump);
  ImGui::SameLine();
  ImGui::Checkbox("Geometry", &p.heightFromGeometry);
  slider("Depth", p.heightmaskDepth);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Higher: fewer texels pass, and they get less paint");
  ImGui::Unindent();
}

// Brush mask and brush texture (paint.md 3.6) and how the stamp turns.
void stampPanel() {
  bool changed = false;
  auto imageRow = [&](const char* label, bool& on, const std::string& path, bool mask) {
    ImGui::PushID(label);
    if (ImGui::Checkbox(label, &on)) {
      const bool loaded = !(mask ? app.maskImage : app.textureImage).empty();
      if (on && !loaded && !chooseStampImage(mask)) on = false;
      changed = true;
    }
    ImGui::SameLine();
    const std::string name = path.empty() ? std::string("Image...") : fileName(path);
    if (ImGui::Button(name.c_str(), ImVec2(-1, 0))) chooseStampImage(mask);
    if (!path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", path.c_str());
    ImGui::PopID();
  };
  imageRow("Brush mask", app.maskOn, app.maskPath, true);
  if (ImGui::Checkbox("Invert mask", &app.maskInvert)) changed = true;
  imageRow("Brush texture", app.textureOn, app.texturePath, false);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Paint color paints the texture's colors, the bump tools its brightness");

  stampModeControls();
  if (changed) rebuildStamps();

  if (!stampsActive()) {
    ImGui::TextDisabled("No stamp: enable a mask or a texture");
    return;
  }
  float aspect = 1;
  const bool color = app.paint.tool == PaintTool::Color;
  if (GLuint tex = stampPreview(color ? app.stamps.color : app.stamps.bump, color ? 0 : 1, aspect)) {
    const float side = ImGui::GetFrameHeight() * 4;
    ImGui::Image(ImTextureRef(ImTextureID(intptr_t(tex))), ImVec2(side, side * aspect));
    ImGui::SameLine();
    ImGui::TextDisabled("%s stamp", app.paint.tool == PaintTool::Color ? "Color" : "Bump");
  }
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

// The current paint tool: its sliders, fill, colors, stamp and heightmask.
void paintBrushPanel() {
  const PaintTool tool = app.paint.tool;
  panelTitle(paintToolIcon(tool), paintToolName(tool), paintToolDescription(tool));
  std::array<float, 3>& ps = paintSliders();
  ImGui::SeparatorText("Brush");
  slider("Size", ps[0]);
  slider("Strength", ps[1]);
  slider("Hardness", ps[2]);
  ImGui::Checkbox("Surface angle falloff", &app.paint.angleFalloff);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Leave surface that turns away from the spot under the brush");
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

  ImGui::Spacing();
  if (ImGui::CollapsingHeader(stampsActive() ? "Stamp (on)###stamp" : "Stamp###stamp")) stampPanel();
  if (ImGui::CollapsingHeader(app.paint.heightmask ? "Heightmask (on)###heightmask" : "Heightmask###heightmask"))
    heightmaskPanel();
}

// What concerns the whole model in paint mode: material layers and the view.
void paintModelPanel() {
  materialPanel();
  ImGui::SeparatorText("View");
  ImGui::Checkbox("Bump normal map (N)", &app.showBumpNormals);
}

void panels() {
  const Layout l = layout();
  if (beginPanel("##brush", l.strip, l.panel)) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
    if (app.paintMode) paintBrushPanel();
    else sculptBrushPanel();
    ImGui::PopItemWidth();
  }
  ImGui::End();
  if (beginPanel("##model", ImGui::GetMainViewport()->Size.x - l.side, l.side)) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
    if (app.paintMode) paintModelPanel();
    else sculptModelPanel();
    ImGui::PopItemWidth();
  }
  ImGui::End();
}

// Mouse buttons and keys for what the current tool does.
std::vector<std::pair<const char*, const char*>> hints() {
  std::vector<std::pair<const char*, const char*>> h;
  if (!app.paintMode) {
    switch (app.brush.tool) {
      case Tool::Mask: h = {{"LMB", "mask"}, {"Ctrl", "unmask"}}; break;
      case Tool::Grab:
      case Tool::Rotate:
      case Tool::Scale: h = {{"LMB", "drag"}}; break;
      case Tool::Reduce: h = {{"LMB", "reduce"}, {"Ctrl", "add detail"}}; break;
      default: h = {{"LMB", "sculpt"}, {"Ctrl", "invert"}}; break;
    }
    h.push_back({"Shift", "smooth"});
  } else {
    switch (app.paint.tool) {
      case PaintTool::Color:
        h = {{"LMB", "paint"}, {"Ctrl", app.editSlot > 0 ? "erase mask" : "secondary color"}};
        break;
      case PaintTool::Bump: h = {{"LMB", "raise"}, {"Ctrl", "lower"}}; break;
      case PaintTool::FlattenBump: h = {{"LMB", "flatten"}}; break;
    }
  }
  h.insert(h.end(), {{"RMB", "orbit"}, {"MMB", "pan"}, {"Wheel", "zoom"}, {"[ ]", "size"}});
  return h;
}

void statusBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = layout().bottom;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().FramePadding.x * 2, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  ImGui::Begin("##status", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(app.status.c_str());
  const float statusEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

  // Right: the hints, keys bright and what they do dim; left of them the model.
  const float gap = ImGui::CalcTextSize("   ").x, space = ImGui::CalcTextSize(" ").x;
  const auto pairs = hints();
  float hintsW = 0;
  for (const auto& [key, what] : pairs) hintsW += ImGui::CalcTextSize(key).x + space + ImGui::CalcTextSize(what).x + gap;
  char info[256];
  std::snprintf(info, sizeof info, "%s%s   %d triangles", app.path.empty() ? "new model" : fileName(app.path).c_str(),
                app.modified ? " *" : "", app.mesh.numTriangles());
  const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
  float x = right - hintsW;
  const float infoX = x - gap - ImGui::CalcTextSize(info).x;
  const bool showHints = infoX > statusEnd + gap;
  if (showHints) {
    for (const auto& [key, what] : pairs) {
      ImGui::SameLine(x);
      ImGui::TextUnformatted(key);
      x += ImGui::CalcTextSize(key).x + space;
      ImGui::SameLine(x);
      ImGui::TextDisabled("%s", what);
      x += ImGui::CalcTextSize(what).x + gap;
    }
  }
  const float infoAt = showHints ? infoX : right - ImGui::CalcTextSize(info).x;
  if (infoAt > statusEnd + gap) {
    ImGui::SameLine(infoAt);
    ImGui::TextDisabled("%s", info);
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}

// --- view overlay -------------------------------------------------------------

constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

// The world axes as the camera sees them, top right in the view. Clicking an
// axis looks at the model from that side.
void axisGizmo() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float s = ImGui::GetStyle().FontScaleDpi, size = 92 * s, pad = 8 * s;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + app.viewX + app.viewW - size - pad, vp->Pos.y + app.viewY + pad));
  ImGui::SetNextWindowSize(ImVec2(size, size));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (ImGui::Begin("##axes", nullptr, kOverlayFlags | ImGuiWindowFlags_NoBackground)) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##axes", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered(), clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + size / 2, p.y + size / 2);
    const float reach = size * 0.33f, knob = size * 0.1f;
    const Camera& cam = app.camera;
    struct Axis {
      Vec3 d;
      ImU32 color;
      const char* label;  // null: the negative end
      float yaw, pitch;   // camera looking from this side
      const char* tip;
    };
    const ImU32 red = IM_COL32(214, 92, 92, 255), green = IM_COL32(120, 190, 92, 255), blue = IM_COL32(92, 140, 228, 255);
    const Axis axes[6] = {
        {{1, 0, 0}, red, "X", kPi / 2, 0, "View from +X (right)"},
        {{-1, 0, 0}, red, nullptr, -kPi / 2, 0, "View from -X (left)"},
        {{0, 1, 0}, green, "Y", cam.yaw, 1.5f, "View from +Y (top)"},
        {{0, -1, 0}, green, nullptr, cam.yaw, -1.5f, "View from -Y (bottom)"},
        {{0, 0, 1}, blue, "Z", 0, 0, "View from +Z (front)"},
        {{0, 0, -1}, blue, nullptr, kPi, 0, "View from -Z (back)"},
    };
    ImVec2 at[6];
    int order[6];
    for (int k = 0; k < 6; ++k) {
      at[k] = ImVec2(c.x + dot(axes[k].d, cam.right()) * reach, c.y - dot(axes[k].d, cam.up()) * reach);
      order[k] = k;
    }
    // Far ends first, so the near ones draw over them.
    std::sort(order, order + 6, [&](int a, int b) { return dot(axes[a].d, cam.forward()) > dot(axes[b].d, cam.forward()); });
    int hit = -1;
    if (hovered) {
      const ImVec2 m = ImGui::GetMousePos();
      for (int k : order) {  // the nearest end wins: it is drawn last
        const float dx = m.x - at[k].x, dy = m.y - at[k].y;
        if (dx * dx + dy * dy <= knob * knob) hit = k;
      }
      dl->AddCircleFilled(c, size * 0.48f, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.6f));
    }
    for (int k : order) {
      const Axis& a = axes[k];
      if (a.label) {
        dl->AddLine(c, at[k], a.color, 2 * s);
        dl->AddCircleFilled(at[k], knob, a.color);
        const ImVec2 ts = ImGui::CalcTextSize(a.label);
        dl->AddText(ImVec2(at[k].x - ts.x / 2, at[k].y - ts.y / 2), IM_COL32(20, 20, 24, 255), a.label);
      } else {
        dl->AddCircleFilled(at[k], knob * 0.8f, (a.color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90));
        dl->AddCircle(at[k], knob * 0.8f, a.color, 0, 1.5f * s);
      }
      if (k == hit) dl->AddCircle(at[k], knob + 2 * s, IM_COL32(255, 255, 255, 220), 0, 1.5f * s);
    }
    if (hit >= 0) {
      ImGui::SetTooltip("%s", axes[hit].tip);
      if (clicked) {
        app.camera.yaw = axes[hit].yaw;
        app.camera.pitch = axes[hit].pitch;
      }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

// An icon button for the view bar.
bool viewButton(const char* id, Icon icon, bool selected, const char* tooltip) {
  const float side = ImGui::GetFrameHeight() * 1.4f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected || hovered)
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side),
                      ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                      ImGui::GetStyle().FrameRounding);
  drawIcon(dl, icon, ImVec2(p.x + side / 2, p.y + side / 2), side * 0.6f, ImGui::GetColorU32(ImGuiCol_Text));
  if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

// A small bar at the bottom of the view: frame the model, wireframe.
void viewBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImGuiStyle& st = ImGui::GetStyle();
  const float side = ImGui::GetFrameHeight() * 1.4f, pad = 8 * st.FontScaleDpi;
  const ImVec2 padding(st.FramePadding.x, st.FramePadding.x);
  const float w = 2 * side + st.ItemSpacing.x + 2 * padding.x, h = side + 2 * padding.y;
  ImGui::SetNextWindowPos(
      ImVec2(vp->Pos.x + app.viewX + (app.viewW - w) / 2, vp->Pos.y + app.viewY + app.viewH - h - pad));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, st.FrameRounding * 2);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  if (ImGui::Begin("##viewbar", nullptr, kOverlayFlags)) {
    if (viewButton("##frame", Icon::Frame, false, "Frame the model (front view)")) frameModel();
    ImGui::SameLine();
    if (viewButton("##wire", Icon::Wireframe, app.wireframe, "Wireframe (W)")) app.wireframe = !app.wireframe;
  }
  ImGui::End();
  ImGui::PopStyleColor();
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

void layoutView(bool withUi) {
  app.viewX = app.viewY = 0;
  app.viewW = app.width;
  app.viewH = app.height;
  if (!withUi || !ImGui::GetCurrentContext()) return;
  const Layout l = layout();
  app.viewX = int(l.strip + l.panel);
  app.viewY = int(l.top);
  app.viewW = std::max(1, app.width - app.viewX - int(l.side));
  app.viewH = std::max(1, app.height - app.viewY - int(l.bottom));
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
  toolStrip();
  panels();
  axisGizmo();
  viewBar();
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
