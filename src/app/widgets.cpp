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

namespace bh::viewer {

namespace {

constexpr float kDeg = 3.14159265f / 180;

// ImGui's angles start at +x; the menu's at the top.
float imAngle(float menuDegrees) { return (menuDegrees - 90) * kDeg; }

ImVec2 onCircle(ImVec2 c, float radius, float menuDegrees) {
  const float a = imAngle(menuDegrees);
  return ImVec2(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
}

// A band of the ring from `from` to `to` degrees (either order).
void fillBand(ImDrawList* dl, ImVec2 c, float r0, float r1, float from, float to, ImU32 color) {
  const float a0 = imAngle(std::min(from, to)), a1 = imAngle(std::max(from, to));
  dl->PathArcTo(c, r1, a0, a1, 32);
  dl->PathArcTo(c, r0, a1, a0, 32);
  dl->PathFillConcave(color);
}

ImU32 withAlpha(ImGuiCol col, float alpha) {
  ImVec4 v = ImGui::GetStyleColorVec4(col);
  v.w *= alpha;
  return ImGui::GetColorU32(v);
}

void centeredText(ImDrawList* dl, ImVec2 at, ImU32 color, const char* text) {
  const ImVec2 ts = ImGui::CalcTextSize(text);
  dl->AddText(ImVec2(at.x - ts.x / 2, at.y - ts.y / 2), color, text);
}

}  // namespace

int brushMenu(BrushMenu& menu, const char* title, Icon icon, const char* description, float* size, float sizeMin,
              float* strength, const MenuSegment* segments, int count, float scale, void (*flyout)(int segment)) {
  if (!menu.open) return -1;
  // The tool may have changed under an open panel (keys).
  if (menu.flyout >= count || (menu.flyout >= 0 && !segments[menu.flyout].flyout)) menu.flyout = -1;
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 padding = ImGui::GetStyle().WindowPadding;
  menu.alpha = std::min(1.0f, menu.alpha + io.DeltaTime / 0.1f);
  menu.held += io.DeltaTime;
  const float full = menu.alpha;
  MenuRadii r;
  r.inner = 70 * scale;
  r.outer = 165 * scale;
  const ImVec2 c = menu.center;
  int clicked = -1;

  // The window covers the whole screen while the menu is open, so a click
  // beside the ring closes it instead of starting a stroke.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  if (menu.raise) ImGui::SetNextWindowFocus();
  menu.raise = false;
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin("##brushmenu", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::SetCursorScreenPos(vp->Pos);
  ImGui::InvisibleButton("##menuarea", vp->Size);
  const ImVec2 m = io.MousePos;
  const MenuHit hit = menuHit(m.x - c.x, m.y - c.y, r, count, 6 * scale);
  if (ImGui::IsItemActivated()) {
    menu.pressed = hit;
    // Using anything else on the ring closes an open panel; a segment with a
    // panel switches to its own on release.
    if (hit.zone != MenuZone::Segment || hit.segment >= count || !segments[hit.segment].flyout) menu.flyout = -1;
    if (hit.zone == MenuZone::Size || hit.zone == MenuZone::Strength) menu.drag = hit.zone;
  }
  if (menu.drag != MenuZone::None && ImGui::IsItemActive()) {
    const bool left = menu.drag == MenuZone::Size;
    const float v = arcValue(menuAngle(m.x - c.x, m.y - c.y), left);
    if (left) *size = sizeMin + v * (1 - sizeMin);
    else *strength = v;
  }
  if (ImGui::IsItemDeactivated()) {
    // A click counts where it started and ended: on a segment it switches or
    // presses it, beside the ring or in its free middle it closes the menu.
    if (menu.drag == MenuZone::None && hit.zone == menu.pressed.zone) {
      if (hit.zone == MenuZone::Segment && hit.segment == menu.pressed.segment && hit.segment < count) {
        const MenuSegment& s = segments[hit.segment];
        if (s.flyout) {
          // Its panel opens beside the ring (a second click closes it); from
          // now on the menu stays open when Space is released.
          menu.flyout = menu.flyout == hit.segment ? -1 : hit.segment;
          menu.sticky = true;
        } else if (s.toggle) {
          *s.toggle = !*s.toggle;
        } else {
          clicked = hit.segment;
        }
      } else if (hit.zone == MenuZone::Outside || hit.zone == MenuZone::Center) {
        menu.open = false;
      }
    }
    menu.drag = MenuZone::None;
  }

  // While Size is pointed at or dragged, the ring turns see-through except for
  // the Size arc, so the brush circle on the model shows its new size.
  const MenuZone over = menu.drag != MenuZone::None ? menu.drag : hit.zone;
  const bool preview = over == MenuZone::Size;
  const float seeStep = io.DeltaTime / 0.12f;
  menu.see = preview ? std::max(0.25f, menu.see - seeStep) : std::min(1.0f, menu.see + seeStep);
  const float alpha = full * menu.see;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiStyle& st = ImGui::GetStyle();
  const float band = r.outer - r.inner, mid = (r.inner + r.outer) / 2, line = std::max(1.0f, scale);
  const ImU32 ink = withAlpha(ImGuiCol_Text, alpha), dim = withAlpha(ImGuiCol_TextDisabled, alpha);
  const ImU32 border = withAlpha(ImGuiCol_Border, alpha);
  ImVec4 bgv = st.Colors[ImGuiCol_PopupBg];
  bgv.w = 0.94f * alpha;
  dl->AddCircle(c, mid, ImGui::GetColorU32(bgv), 96, band);
  dl->AddCircle(c, r.outer, border, 96, line);
  dl->AddCircle(c, r.inner, border, 96, line);
  menu.titleHover = over == MenuZone::Title && ImGui::IsItemHovered() ? menu.titleHover + io.DeltaTime : 0;
  if (description && *description && menu.titleHover > 0.4f) ImGui::SetTooltip("%s", description);

  // Title.
  fillBand(dl, c, r.inner, r.outer, -kMenuTitleHalf, kMenuTitleHalf, withAlpha(ImGuiCol_ButtonActive, alpha));
  {
    ImGui::PushFont(nullptr, ImGui::GetFontSize() / st.FontScaleDpi * 1.15f);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    const float iconSize = ts.y * 1.3f, w = iconSize + st.ItemInnerSpacing.x + ts.x;
    const ImVec2 at(c.x - w / 2, c.y - mid);
    drawIcon(dl, icon, ImVec2(at.x + iconSize / 2, at.y), iconSize, ink, paintColor());
    dl->AddText(ImVec2(at.x + iconSize + st.ItemInnerSpacing.x, at.y - ts.y / 2), ink, title);
    ImGui::PopFont();
  }

  // Size and Strength: a track near the outer edge, filled from the bottom,
  // with a knob; name and value inside.
  const float track = r.outer - 20 * scale, thick = 7 * scale;
  for (const bool left : {true, false}) {
    const float v = left ? (*size - sizeMin) / (1 - sizeMin) : *strength;
    const float sign = left ? -1.0f : 1.0f;
    const float a = left ? full : alpha;  // Size stays solid while the rest fades
    if (over == (left ? MenuZone::Size : MenuZone::Strength))
      fillBand(dl, c, r.inner, r.outer, sign * kMenuArcFrom, sign * kMenuArcTo,
               withAlpha(ImGuiCol_ButtonHovered, a * 0.6f));
    auto stroke = [&](float from, float to, ImU32 color) {
      dl->PathArcTo(c, track, imAngle(std::min(from, to)), imAngle(std::max(from, to)), 32);
      dl->PathStroke(color, ImDrawFlags_None, thick);
    };
    stroke(sign * kMenuArcFrom, sign * kMenuArcTo, withAlpha(ImGuiCol_FrameBg, a));
    const float at = arcAngle(v, left);
    if (v > 0) stroke(sign * kMenuArcTo, at, withAlpha(ImGuiCol_SliderGrabActive, a));
    dl->AddCircleFilled(onCircle(c, track, at), thick * 1.3f, withAlpha(ImGuiCol_Text, a), 24);
    char value[16];
    std::snprintf(value, sizeof value, "%.2f", left ? *size : *strength);
    const ImVec2 p = onCircle(c, (r.inner + track) / 2 - 4 * scale, sign * 75);
    const float lh = ImGui::GetTextLineHeight();
    centeredText(dl, ImVec2(p.x, p.y - lh / 2), withAlpha(ImGuiCol_TextDisabled, a), left ? "Size" : "Strength");
    centeredText(dl, ImVec2(p.x, p.y + lh / 2), withAlpha(ImGuiCol_Text, a), value);
  }

  // Segments: name, and a box for switches.
  for (int i = 0; i < count; ++i) {
    float from, to;
    segmentAngles(i, count, from, to);
    const MenuSegment& seg = segments[i];
    if (menu.flyout == i)
      fillBand(dl, c, r.inner, r.outer, from, to, withAlpha(ImGuiCol_ButtonActive, alpha * 0.7f));
    else if (over == MenuZone::Segment && hit.segment == i)
      fillBand(dl, c, r.inner, r.outer, from, to, withAlpha(ImGuiCol_ButtonHovered, alpha * 0.8f));
    if (i > 0) dl->AddLine(onCircle(c, r.inner, from), onCircle(c, r.outer, from), border, line);
    const ImVec2 p = onCircle(c, mid, (from + to) / 2);
    ImGui::PushFont(nullptr, ImGui::GetFontSize() / st.FontScaleDpi * 0.9f);
    const float lh = ImGui::GetTextLineHeight();
    if (seg.flyout) {
      // The name, under it the colors or a small arrow toward the panel.
      centeredText(dl, ImVec2(p.x, p.y - lh * 0.6f), ink, seg.label);
      const float b = lh * 0.9f, y = p.y + lh * 0.25f;
      if (seg.swatches > 0) {
        const float w = seg.swatches * b + (seg.swatches - 1) * b * 0.3f;
        for (int k = 0; k < seg.swatches; ++k) {
          const float x = p.x - w / 2 + k * b * 1.3f;
          ImU32 col = seg.colors[k];
          col = (col & 0x00FFFFFF) | (ImU32(((col >> 24) & 0xFF) * alpha) << 24);
          dl->AddRectFilled(ImVec2(x, y), ImVec2(x + b, y + b), col, st.FrameRounding * 0.6f);
          dl->AddRect(ImVec2(x, y), ImVec2(x + b, y + b), border, st.FrameRounding * 0.6f, 0, line);
        }
      } else {
        const float h = b * 0.45f, cy = y + b / 2;
        dl->AddTriangleFilled(ImVec2(p.x - h * 0.6f, cy - h), ImVec2(p.x - h * 0.6f, cy + h), ImVec2(p.x + h, cy),
                              menu.flyout == i ? ink : dim);
      }
    } else if (bool* t = seg.toggle) {
      centeredText(dl, ImVec2(p.x, p.y - lh * 0.6f), ink, segments[i].label);
      const float b = lh * 0.9f;
      const ImVec2 b0(p.x - b / 2, p.y + lh * 0.25f), b1(p.x + b / 2, p.y + lh * 0.25f + b);
      dl->AddRectFilled(b0, b1, withAlpha(*t ? ImGuiCol_CheckMark : ImGuiCol_FrameBg, alpha), st.FrameRounding * 0.6f);
      if (*t) {
        const ImVec2 pts[] = {{b0.x + b * 0.22f, b0.y + b * 0.52f}, {b0.x + b * 0.42f, b0.y + b * 0.72f},
                              {b0.x + b * 0.78f, b0.y + b * 0.3f}};
        dl->AddPolyline(pts, 3, withAlpha(ImGuiCol_WindowBg, alpha), ImDrawFlags_None, 2 * scale);
      }
    } else {
      centeredText(dl, p, ink, segments[i].label);
    }
    ImGui::PopFont();
  }
  // The gaps between the sectors.
  for (const float a : {kMenuTitleHalf, kMenuArcFrom, kMenuArcTo, kMenuBottomFrom}) {
    for (const float s : {-1.0f, 1.0f})
      dl->AddLine(onCircle(c, r.inner, s * a), onCircle(c, r.outer, s * a), border, line);
  }

  // The open segment's panel beside the ring, on the side with room. A child
  // of the menu window, so it lies above the area that closes the menu.
  if (menu.flyout >= 0 && flyout) {
    const float width = 280 * scale, gap = 10 * scale;
    float x = c.x + r.outer + gap;
    if (x + width > vp->Pos.x + vp->Size.x - gap) x = c.x - r.outer - gap - width;
    const float h = menu.flyoutHeight > 0 ? menu.flyoutHeight : 200 * scale;
    const float y = std::clamp(c.y - h / 2, vp->Pos.y + gap, std::max(vp->Pos.y + gap, vp->Pos.y + vp->Size.y - h - gap));
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, st.FrameRounding * 2);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, st.Colors[ImGuiCol_PopupBg]);
    ImGui::BeginChild("##flyout", ImVec2(width, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
    ImGui::SeparatorText(segments[menu.flyout].label);
    flyout(menu.flyout);
    ImGui::PopItemWidth();
    ImGui::EndChild();
    menu.flyoutHeight = ImGui::GetItemRectSize().y;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  return clicked;
}

}  // namespace bh::viewer
