#include "app/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

#include "app/app.h"
#include "app/document.h"

namespace bh::viewer {

void applyStyle(float scale) {
  // From the defaults each time: ScaleAllSizes multiplies, and the scale
  // changes when the window moves to a monitor with other DPI.
  ImGuiStyle& s = ImGui::GetStyle();
  s = ImGuiStyle();
  ImGui::StyleColorsDark(&s);
  s.WindowRounding = 0;
  s.ChildRounding = s.FrameRounding = s.GrabRounding = s.PopupRounding = 5;
  s.ScrollbarRounding = 5;
  s.WindowBorderSize = 1;
  s.PopupBorderSize = 1;
  s.WindowPadding = ImVec2(12, 10);
  s.FramePadding = ImVec2(8, 4);
  s.ItemSpacing = ImVec2(8, 7);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.IndentSpacing = 16;
  s.ScrollbarSize = 10;
  s.GrabMinSize = 10;
  s.SeparatorTextBorderSize = 1;
  s.SeparatorTextPadding = ImVec2(0, 4);
  ImVec4* c = s.Colors;
  // Neutral slate grays; amber marks what is selected or active (the tool,
  // the mode, the material layer) and the values of sliders and checkboxes.
  const ImVec4 accent(0.86f, 0.62f, 0.25f, 1.0f), accentDim(0.55f, 0.4f, 0.17f, 1.0f);
  c[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.88f, 1.0f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.6f, 1.0f);
  c[ImGuiCol_WindowBg] = ImVec4(0.115f, 0.12f, 0.135f, 1.0f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.085f, 0.09f, 0.1f, 1.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.135f, 0.155f, 0.98f);
  c[ImGuiCol_Border] = ImVec4(0.06f, 0.065f, 0.075f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.215f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.24f, 0.275f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.28f, 0.32f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.19f, 0.2f, 0.23f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.27f, 0.31f, 1.0f);
  c[ImGuiCol_ButtonActive] = accentDim;
  c[ImGuiCol_Header] = ImVec4(0.17f, 0.18f, 0.205f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.24f, 0.275f, 1.0f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.27f, 0.28f, 0.32f, 1.0f);
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.75f, 0.35f, 1.0f);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_MenuBarBg];
  c[ImGuiCol_Separator] = ImVec4(0.24f, 0.25f, 0.28f, 1.0f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.25f, 0.28f, 1.0f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.31f, 0.35f, 1.0f);
  c[ImGuiCol_ScrollbarGrabActive] = accentDim;
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  s.ScaleAllSizes(scale);
  s.FontScaleDpi = scale;
}

ImU32 paintColor() { return app.paint.primary | 0xFF000000u; }

void panelTitle(Icon icon, const char* title, const char* description) {
  ImGui::PushFont(nullptr, ImGui::GetFontSize() / ImGui::GetStyle().FontScaleDpi * 1.6f);
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

bool stripButton(const char* label, Icon icon, bool selected, const char* tooltip, float height, float width) {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
  const float h = height > 0 ? height : ImGui::GetFrameHeight() * 2.3f;
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

bool viewButton(const char* id, Icon icon, bool selected, const char* tooltip, float side) {
  if (side <= 0) side = ImGui::GetFrameHeight() * 1.4f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected || hovered)
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side),
                      ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                      ImGui::GetStyle().FrameRounding);
  drawIcon(dl, icon, ImVec2(p.x + side / 2, p.y + side / 2), side * 0.6f, ImGui::GetColorU32(ImGuiCol_Text));
  if (tooltip && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

namespace {
// After a side is chosen on the axis gizmo, the name of the view the camera
// actually shows (axisViewName) stands under it for 5 s, then fades out. It
// goes at once when the camera turns off the axis.
double viewChosenAt = -1e9;
constexpr double kLabelShow = 5.0, kLabelFade = 0.8;

float labelAlpha() {
  if (!axisViewName(app.camera)) return 0;
  const double age = ImGui::GetTime() - viewChosenAt;
  return age < kLabelShow ? 1.0f : float(std::max(0.0, 1 - (age - kLabelShow) / kLabelFade));
}
}  // namespace

void axisGizmo(ImVec2 pos, float size) {
  const float s = ImGui::GetStyle().FontScaleDpi;
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(ImVec2(size, size));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  if (ImGui::Begin("##axes", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground)) {
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
    };
    const ImU32 red = IM_COL32(214, 92, 92, 255), green = IM_COL32(120, 190, 92, 255), blue = IM_COL32(92, 140, 228, 255);
    const Axis axes[6] = {
        {{1, 0, 0}, red, "X", kPi / 2, 0},
        {{-1, 0, 0}, red, nullptr, -kPi / 2, 0},
        {{0, 1, 0}, green, "Y", cam.yaw, 1.5f},
        {{0, -1, 0}, green, nullptr, cam.yaw, -1.5f},
        {{0, 0, 1}, blue, "Z", 0, 0},
        {{0, 0, -1}, blue, nullptr, kPi, 0},
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
      // The nearest end within reach of a fingertip; ties go to the end
      // drawn last (nearest the viewer).
      const ImVec2 m = ImGui::GetMousePos();
      float best = 1.6f * knob * 1.6f * knob;
      for (int k : order) {
        const float dx = m.x - at[k].x, dy = m.y - at[k].y;
        if (dx * dx + dy * dy <= best) { best = dx * dx + dy * dy; hit = k; }
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
    if (hit >= 0 && clicked) {
      app.camera.yaw = axes[hit].yaw;
      app.camera.pitch = axes[hit].pitch;
      viewChosenAt = ImGui::GetTime();
    }
    // Under the gizmo, not over it (a tooltip would cover it under a finger or
    // pen). Never the hovered end: after the jump the gizmo has turned, and
    // another end lies under the pointer than the one that was chosen.
    const char* label = axisViewName(app.camera);
    const float alpha = labelAlpha();
    if (label && alpha > 0) {
      const ImVec2 ts = ImGui::CalcTextSize(label);
      const float x = std::min(c.x - ts.x / 2, p.x + size - ts.x);
      ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, p.y + size), ImGui::GetColorU32(ImGuiCol_Text, alpha), label);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

bool axisLabelFading() { return labelAlpha() > 0; }

}  // namespace bh::viewer

namespace bh::viewer {

RingResult toolRing(RingState& state, const RingSettings& settings, const RingTool* tools, int count, int active,
                    const RingPlace& place) {
  RingResult result;
  if (count <= 0) return result;
  const ImGuiIO& io = ImGui::GetIO();
  const float mm = place.pxPerMm;
  const RingSize size = ringSize((place.bottom - place.top) / mm, count);
  RingGeometry g;
  g.count = count;
  g.edge = place.edge;
  g.centerY = (place.top + place.bottom) / 2;
  g.diameter = size.diameter * mm;
  g.gap = size.gap * mm;
  g.visible = ringVisible(settings, size.diameter);
  g.margin = 1.5f * mm;
  const float d = g.diameter, radius = d / 2;

  // What the pointer does, measured against the geometry of the last frame's
  // progress (what the user saw).
  std::vector<RingPoint> points = ringPoints(g, state.eased(), state.reference());
  const bool pointer = ImGui::IsMousePosValid();
  const ImVec2 m = io.MousePos;
  auto distance = [&](const RingPoint& p) { return std::hypot(m.x - p.x, m.y - p.y); };
  const RingPoint closedAt = ringPoints(g, 0, active)[size_t(active)];
  RingInput in;
  in.dt = io.DeltaTime;
  in.active = active;
  in.stroke = place.stroke;
  in.pointer = pointer;
  in.overIcon = pointer && m.x >= g.edge && distance(closedAt) <= radius;
  in.nearIcon = pointer && distance(closedAt) <= radius + d;
  // The box of all icons the user sees, with half a diameter around it.
  float x0 = g.edge, y0 = FLT_MAX, x1 = g.edge, y1 = -FLT_MAX;
  const float shown = state.closed() ? 0 : std::min(1.0f, state.progress() * 4);
  for (int i = 0; i < count; ++i) {
    if (i != state.reference() && shown <= 0) continue;
    const RingPoint& p = points[size_t(i)];
    x1 = std::max(x1, p.x + radius);
    y0 = std::min(y0, p.y - radius);
    y1 = std::max(y1, p.y + radius);
  }
  in.insideColumn = pointer && m.x >= g.edge && m.x <= x1 + radius && m.y >= y0 - radius && m.y <= y1 + radius;
  in.pressOutside = place.pressOutside || (ImGui::IsMouseClicked(0) && !in.insideColumn);

  // The window takes the clicks over the visible icons; skipped while the
  // icon is faded out for a stroke, so the stroke goes on beneath it.
  const bool visible = state.iconAlpha() > 0 && !(state.closed() && state.hidden());
  if (visible && x1 > x0 && y1 > y0) {
    const ImVec2 pos(x0, y0), extent(x1 - x0, y1 - y0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(extent);
    ImGui::Begin("##toolring", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse);
    result.region = true;
    result.min = pos;
    result.max = ImVec2(x1, y1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();
    int hovered = -1;
    for (int i = 0; i < count; ++i) {
      const bool reference = i == state.reference();
      const float alpha = (reference ? 1.0f : shown) * state.iconAlpha();
      if (alpha <= 0) continue;
      const RingPoint& p = points[size_t(i)];
      ImGui::SetCursorScreenPos(ImVec2(p.x - radius, p.y - radius));
      ImGui::PushID(i);
      const bool clicked = ImGui::InvisibleButton("##ringtool", ImVec2(d, d));
      const bool over = ImGui::IsItemHovered();
      ImGui::PopID();
      if (state.open()) {
        if (over) hovered = i;
        if (clicked) in.picked = i;
      } else if (reference && clicked && state.closed()) {
        in.tapIcon = true;
      }
      const bool selected = i == active;
      ImVec4 fill = st.Colors[selected ? ImGuiCol_ButtonActive : (over && state.open()) ? ImGuiCol_ButtonHovered
                                                                                     : ImGuiCol_Button];
      ImVec4 back = st.Colors[ImGuiCol_PopupBg];
      back.w = alpha;
      fill.w *= alpha;
      ImVec4 border = st.Colors[ImGuiCol_Border];
      border.w = alpha;
      ImVec4 ink = st.Colors[ImGuiCol_Text];
      ink.w = alpha;
      const ImVec2 c(p.x, p.y);
      dl->AddCircleFilled(c, radius, ImGui::GetColorU32(back), 48);
      dl->AddCircleFilled(c, radius, ImGui::GetColorU32(fill), 48);
      dl->AddCircle(c, radius, ImGui::GetColorU32(border), 48, std::max(1.0f, mm * 0.2f));
      ImU32 paint = paintColor();
      paint = (paint & 0x00FFFFFF) | (ImU32(((paint >> 24) & 0xFF) * alpha) << 24);
      drawIcon(dl, tools[i].icon, c, d * 0.62f, ImGui::GetColorU32(ink), paint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    // The name of the icon under the pointer beside the column, at once:
    // the icons carry no labels.
    if (hovered >= 0) {
      char label[64];
      if (tools[hovered].key) std::snprintf(label, sizeof label, "%s (%s)", tools[hovered].name, tools[hovered].key);
      else std::snprintf(label, sizeof label, "%s", tools[hovered].name);
      const RingPoint& p = points[size_t(hovered)];
      const ImVec2 ts = ImGui::CalcTextSize(label), pad = st.FramePadding;
      const ImVec2 at(p.x + radius + 2 * mm, p.y - ts.y / 2 - pad.y);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(at, ImVec2(at.x + ts.x + 2 * pad.x, at.y + ts.y + 2 * pad.y),
                        ImGui::GetColorU32(ImGuiCol_PopupBg), st.FrameRounding);
      fg->AddText(ImVec2(at.x + pad.x, at.y + pad.y), ImGui::GetColorU32(ImGuiCol_Text), label);
    }
  }
  result.picked = state.update(settings, in);
  return result;
}

}  // namespace bh::viewer
