#pragma once

// Mouse and keyboard on the 3D view: strokes, camera, shortcuts. The window
// procedure passes on only what the UI does not take.

#include <windows.h>

#include "core/brush.h"
#include "core/pick.h"

namespace bh::viewer {

// The pointer ray at window pixel (mx, my) and what it hits.
StrokeInput inputAt(int mx, int my, PickHit* hitOut = nullptr);

// Shift sculpts with Smooth at the Smooth strength and the current tool's size.
void beginStroke(int mx, int my, bool invert, bool smoothing);
// Ctrl inverts: the secondary color, lowering the bump, flattening away
// from the neutral height.
void beginPaintStroke(int mx, int my, bool invert);

// Selects a sculpt tool; selecting it again toggles its option, as in
// Sculptris (Clay, Lock plane, Global).
void setTool(Tool tool);

void onMouseDown(UINT msg, int x, int y, WPARAM flags);
void onMouseUp();
void onMouseMove(int x, int y);
void onMouseWheel(int delta);
void onKey(WPARAM key);

}  // namespace bh::viewer
