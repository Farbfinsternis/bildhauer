#pragma once

// Tool icons, drawn as simple vector shapes with ImGui's draw list (no image
// files). The sculpt brushes show a surface in profile and what they do to
// it; the paint tools a drop of paint and the bump. Two more serve the view
// buttons.

#include "core/brush.h"
#include "core/paint.h"
#include "imgui.h"

namespace bh::viewer {

enum class Icon {
  Draw, Smooth, Grab, Inflate, Flatten, Pinch, Mask, Crease, Rotate, Scale, Reduce,
  PaintColor, PaintBump, PaintFlatten,
  Frame, Wireframe,  // view buttons
};

Icon toolIcon(Tool tool);
Icon paintToolIcon(PaintTool tool);

// Draws `icon` into a square of `size` around `center`: lines in `ink`, the
// sculpted material as a fill of `ink` at low alpha. `paint` colors the drop
// of the Paint color icon (0xAABBGGRR, ImGui's order).
void drawIcon(ImDrawList* list, Icon icon, ImVec2 center, float size, ImU32 ink, ImU32 paint = 0);

}  // namespace bh::viewer
