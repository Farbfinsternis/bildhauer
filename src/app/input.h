#pragma once

// Mouse and keyboard on the 3D view: strokes, camera, shortcuts. The window
// procedure passes on only what the UI does not take.

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/brush.h"
#include "core/pick.h"

namespace bh::viewer {

// The pointer ray at window pixel (mx, my) and what it hits.
StrokeInput inputAt(int mx, int my, PickHit* hitOut = nullptr);

// Shift sculpts with Smooth at the Smooth strength and the current tool's size.
void beginStroke(int mx, int my, bool invert, bool smoothing, float pressure = 1.0f);
// Ctrl inverts: the secondary color, lowering the bump, flattening away
// from the neutral height.
void beginPaintStroke(int mx, int my, bool invert, float pressure = 1.0f);

// Selects a sculpt tool; selecting it again toggles its option, as in
// Sculptris (Clay, Lock plane, Global).
void setTool(Tool tool);

void moveStroke(int x, int y, float pressure = 1.0f);
// Mobile Grab: accept raw input cheaply, apply the latest target per frame.
// Other tools keep their full movement history.
bool deferGrabMove(int x, int y, float pressure);
bool flushPendingStroke();
void endStroke();

#ifdef _WIN32
// `pressure` is the pen's (0..1), 1 for the mouse.
void onMouseDown(UINT msg, int x, int y, WPARAM flags, float pressure);
void onMouseUp();
void onMouseMove(int x, int y, float pressure);
void onMouseWheel(int delta);
void onKey(WPARAM key);
#endif

}  // namespace bh::viewer
