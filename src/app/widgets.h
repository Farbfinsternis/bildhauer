#pragma once

// The look and the icon widgets shared by the desktop and the tablet UI: the
// slate and amber theme, the tool strip buttons, the round view buttons and
// the panel title with the tool icon.

#include "app/icons.h"
#include "core/brush.h"
#include "imgui.h"

namespace bh::viewer {

// bildhauer's ImGui theme, sizes multiplied by `scale` (DPI or density).
void applyStyle(float scale);

// The paint color as an ImGui color (canvas colors are 0x00BBGGRR).
ImU32 paintColor();

// The tool icon, a larger title beside it, and a short description below.
void panelTitle(Icon icon, const char* title, const char* description);

const char* toolDescription(Tool tool);

// A tool button of the strip: the icon with a small label under it. `height`
// <= 0: the desktop height; `width` <= 0: as wide as the strip.
bool stripButton(const char* label, Icon icon, bool selected, const char* tooltip, float height = 0, float width = 0);

// A square icon button of the view bar. `side` <= 0: the desktop size.
bool viewButton(const char* id, Icon icon, bool selected, const char* tooltip, float side = 0);

// The world axes as the camera sees them, in a `size` square at `pos` (top
// left, screen pixels). Tapping or clicking an axis end looks at the model
// from that side; its name shows under the gizmo for 5 s, then fades.
void axisGizmo(ImVec2 pos, float size);
// Whether that name is still visible: a renderer drawing on demand needs
// frames until it has faded.
bool axisLabelFading();

}  // namespace bh::viewer

#include "app/tool_ring.h"

namespace bh::viewer {

// One tool of the tool ring. `key` may be null (no keyboard).
struct RingTool {
  Icon icon;
  const char* name;
  const char* key;
};

// Where the ring sits and what it needs from the frame.
struct RingPlace {
  float edge = 0;            // left edge of the 3D view, pixels
  float top = 0, bottom = 0; // the view's height range, pixels
  float pxPerMm = 1;         // screen pixels per millimeter
  bool stroke = false;       // a sculpt or paint stroke is running
  bool pressOutside = false; // a tap beside the ring that the UI did not see
};

struct RingResult {
  int picked = -1;           // tool chosen this frame, or -1
  bool region = false;       // the ring takes input in [min, max]
  ImVec2 min, max;
};

// The tool ring (tool_ring.h) at the left edge of the view. Keeps its state
// in `state`; `active` is the current tool's index in `tools`.
RingResult toolRing(RingState& state, const RingSettings& settings, const RingTool* tools, int count, int active,
                    const RingPlace& place);

}  // namespace bh::viewer
