#include "app/icons.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bh::viewer {

namespace {

constexpr float kPi = 3.14159265f;

// A raised cosine: 1 at t = 0, 0 from |t| = 1 on.
float hill(float t) { return std::fabs(t) < 1 ? (1 + std::cos(kPi * t)) / 2 : 0; }

// Icon coordinates: x and y in [-1, 1], y up.
struct Pen {
  ImDrawList* list;
  ImVec2 c;
  float h;     // half the icon size in pixels
  float line;  // line width
  ImU32 ink, fill;

  ImVec2 at(float x, float y) const { return ImVec2(c.x + x * h, c.y - y * h); }

  // A surface in profile, y = f(x), filled down to the bottom of the icon.
  template <class F>
  void profile(F f, ImU32 stroke, bool filled = true) const {
    std::vector<ImVec2> top;
    for (int i = 0; i <= 32; ++i) {
      const float x = -0.95f + 1.9f * i / 32;
      top.push_back(at(x, f(x)));
    }
    if (filled) {
      std::vector<ImVec2> area = top;
      area.push_back(at(0.95f, -0.9f));
      area.push_back(at(-0.95f, -0.9f));
      list->AddConcavePolyFilled(area.data(), int(area.size()), fill);
    }
    list->AddPolyline(top.data(), int(top.size()), stroke, ImDrawFlags_None, line);
  }

  void segment(float x0, float y0, float x1, float y1) const { list->AddLine(at(x0, y0), at(x1, y1), ink, line); }

  void arrow(float x0, float y0, float x1, float y1) const {
    segment(x0, y0, x1, y1);
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    const float ux = dx / len, uy = dy / len, s = 0.22f;
    segment(x1, y1, x1 - s * (ux + 0.7f * uy), y1 - s * (uy - 0.7f * ux));
    segment(x1, y1, x1 - s * (ux - 0.7f * uy), y1 - s * (uy + 0.7f * ux));
  }

  void dot(float x, float y, float r) const { list->AddCircleFilled(at(x, y), r * h, ink); }
};

}  // namespace

Icon toolIcon(Tool tool) { return Icon(int(tool)); }  // same order as Tool

Icon paintToolIcon(PaintTool tool) {
  switch (tool) {
    case PaintTool::Color: return Icon::PaintColor;
    case PaintTool::Bump: return Icon::PaintBump;
    case PaintTool::FlattenBump: return Icon::PaintFlatten;
  }
  return Icon::PaintColor;
}

void drawIcon(ImDrawList* list, Icon icon, ImVec2 center, float size, ImU32 ink, ImU32 paint) {
  const ImU32 fill = (ink & ~IM_COL32_A_MASK) | (((ink >> IM_COL32_A_SHIFT) & 0xFF) * 3 / 10) << IM_COL32_A_SHIFT;
  const ImU32 faint = (ink & ~IM_COL32_A_MASK) | (((ink >> IM_COL32_A_SHIFT) & 0xFF) * 4 / 10) << IM_COL32_A_SHIFT;
  const Pen p{list, center, size / 2, std::max(1.0f, size / 16), ink, fill};
  switch (icon) {
    case Icon::Draw:  // a hill built up, with an arrow up
      p.profile([](float x) { return -0.55f + 0.6f * hill(x / 0.75f); }, ink);
      p.arrow(0, 0.3f, 0, 0.9f);
      break;
    case Icon::Smooth:  // ripples settling into a flat surface
      p.profile([](float x) { return -0.2f + 0.3f * std::sin(5 * kPi * x) * std::clamp((0.3f - x) / 1.25f, 0.0f, 1.0f); },
                ink);
      break;
    case Icon::Grab:  // the surface pulled up at a grip point
      p.profile([](float x) { return -0.55f + 0.75f * hill((x + 0.2f) / 0.6f); }, ink);
      p.dot(-0.2f, 0.2f, 0.12f);
      p.arrow(-0.05f, 0.35f, 0.6f, 0.9f);
      break;
    case Icon::Inflate:  // a body with arrows pushing outward
      list->AddCircleFilled(p.at(0, 0), 0.42f * p.h, fill);
      list->AddCircle(p.at(0, 0), 0.42f * p.h, ink, 0, p.line);
      for (int k = 0; k < 4; ++k) {
        const float a = kPi / 4 + k * kPi / 2, cx = std::cos(a), cy = std::sin(a);
        p.arrow(0.55f * cx, 0.55f * cy, 0.95f * cx, 0.95f * cy);
      }
      break;
    case Icon::Flatten:  // a hill cut off at the brush plane
      p.profile([](float x) { return -0.55f + std::min(0.75f * hill(x / 0.85f), 0.45f); }, ink);
      p.segment(-0.95f, -0.1f, -0.6f, -0.1f);
      p.segment(0.6f, -0.1f, 0.95f, -0.1f);
      p.arrow(0, 0.85f, 0, 0.25f);
      break;
    case Icon::Pinch:  // a ridge pressed together from both sides
      p.profile([](float x) { return -0.55f + 0.6f * hill(x / 0.3f); }, ink);
      p.arrow(-0.95f, 0.45f, -0.35f, 0.45f);
      p.arrow(0.95f, 0.45f, 0.35f, 0.45f);
      break;
    case Icon::Mask: {  // half protected: one half filled, the other open
      const ImVec2 c = p.at(0, 0);
      const float r = 0.75f * p.h;
      list->PathArcTo(c, r, kPi / 2, 3 * kPi / 2);
      list->PathFillConvex(ink);
      list->AddCircle(c, r, ink, 0, p.line);
      break;
    }
    case Icon::Crease:  // a sharp groove cut into the surface
      p.profile([](float x) { return -0.05f - 0.6f * std::max(0.0f, 1 - std::fabs(x) / 0.4f); }, ink);
      break;
    case Icon::Rotate: {  // a turning arrow
      const ImVec2 c = p.at(0, 0);
      const float r = 0.65f * p.h, a0 = -0.35f * kPi, a1 = 1.25f * kPi;
      list->PathArcTo(c, r, a0, a1, 24);
      list->PathStroke(ink, ImDrawFlags_None, p.line);
      // Arrowhead at the end of the arc (screen angle a1, y down).
      const float ex = 0.65f * std::cos(a1), ey = -0.65f * std::sin(a1);
      const float tx = -std::sin(a1), ty = -std::cos(a1);  // direction of travel, icon coordinates
      p.arrow(ex - 0.05f * tx, ey - 0.05f * ty, ex, ey);
      break;
    }
    case Icon::Scale:  // a small box grown into a large one
      list->AddRectFilled(p.at(-0.85f, -0.15f), p.at(-0.15f, -0.85f), fill);
      list->AddRect(p.at(-0.85f, -0.15f), p.at(-0.15f, -0.85f), ink, 0, 0, p.line);
      list->AddRect(p.at(-0.85f, 0.85f), p.at(0.85f, -0.85f), ink, 0, 0, p.line);
      p.arrow(-0.1f, -0.1f, 0.55f, 0.55f);
      break;
    case Icon::Reduce: {  // a coarse triangle where finer ones were
      const float xs[3] = {-0.8f, 0.8f, 0}, ys[3] = {-0.65f, -0.65f, 0.75f};
      for (int k = 0; k < 3; ++k) {
        const int n = (k + 1) % 3;
        list->AddLine(p.at((xs[k] + xs[n]) / 2, (ys[k] + ys[n]) / 2), p.at((xs[n] + xs[(n + 1) % 3]) / 2, (ys[n] + ys[(n + 1) % 3]) / 2),
                      faint, p.line);
      }
      list->AddTriangle(p.at(xs[0], ys[0]), p.at(xs[1], ys[1]), p.at(xs[2], ys[2]), ink, p.line);
      for (int k = 0; k < 3; ++k) p.dot(xs[k], ys[k], 0.1f);
      break;
    }
    case Icon::PaintColor: {  // a drop of paint
      const ImVec2 c = p.at(0, -0.25f);
      const float r = 0.55f * p.h;
      list->PathArcTo(c, r, -0.25f * kPi, 1.25f * kPi, 20);
      list->PathLineTo(p.at(0, 0.9f));
      list->PathFillConvex(paint ? paint : fill);
      list->PathArcTo(c, r, -0.25f * kPi, 1.25f * kPi, 20);
      list->PathLineTo(p.at(0, 0.9f));
      list->PathStroke(ink, ImDrawFlags_Closed, p.line);
      break;
    }
    case Icon::PaintBump:  // small bumps raised on the surface
      p.profile([](float x) { return -0.45f + 0.35f * std::max(0.0f, std::sin(3 * kPi * (x + 0.95f) / 1.9f)); },
                ink);
      p.arrow(0, 0.35f, 0, 0.9f);
      break;
    case Icon::PaintFlatten:  // the bumps (faint) pressed back to flat
      p.profile([](float x) { return -0.45f + 0.35f * std::max(0.0f, std::sin(3 * kPi * (x + 0.95f) / 1.9f)); }, faint,
                false);
      p.profile([](float) { return -0.45f; }, ink);
      p.arrow(0, 0.85f, 0, 0.25f);
      break;
    case Icon::Frame:  // a viewfinder: four corners around the model
      for (float sx : {-1.0f, 1.0f})
        for (float sy : {-1.0f, 1.0f}) {
          p.segment(0.85f * sx, 0.85f * sy, 0.4f * sx, 0.85f * sy);
          p.segment(0.85f * sx, 0.85f * sy, 0.85f * sx, 0.4f * sy);
        }
      list->AddCircleFilled(p.at(0, 0), 0.3f * p.h, fill);
      list->AddCircle(p.at(0, 0), 0.3f * p.h, ink, 0, p.line);
      break;
    case Icon::Wireframe:  // a cube in wireframe
      list->AddRect(p.at(-0.8f, 0.4f), p.at(0.4f, -0.8f), ink, 0, 0, p.line);
      list->AddRect(p.at(-0.4f, 0.8f), p.at(0.8f, -0.4f), faint, 0, 0, p.line);
      p.segment(-0.8f, 0.4f, -0.4f, 0.8f);
      p.segment(0.4f, 0.4f, 0.8f, 0.8f);
      p.segment(0.4f, -0.8f, 0.8f, -0.4f);
      list->AddLine(p.at(-0.8f, -0.8f), p.at(-0.4f, -0.4f), faint, p.line);
      break;
  }
}

}  // namespace bh::viewer
