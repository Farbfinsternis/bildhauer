#pragma once

// The tool ring (idea and geometry from Paintling's spec 007): closed, only
// the active tool shows as a circle cut by the left edge of the 3D view; the
// cut hints that more lies behind it. Hovering over it (or tapping it) rolls
// the ring out into an upright column of all tools; after a choice it rolls
// back in around the new tool. The column takes no room while it is closed.
//
// This part has no ImGui: the sizes, where each icon sits at a given progress,
// and the states. widgets.h draws it.

#include <vector>

namespace bh::viewer {

struct RingSettings {
  float visible = 0.76f;     // share of the diameter that shows while closed
  float dwell = 0.3f;        // s of hovering over the icon before it opens
  float closeDelay = 0.45f;  // s outside the open column before it closes
  float duration = 0.4f;     // s to roll out or in
  float fade = 0.1f;         // s to fade the icon out or in near a stroke
};

// Icon diameter and the gap between icons in millimeters, from the height of
// the view: the column of `count` icons fits with 2 mm above and below, the
// diameter stays within 7..10 mm, the gap is a quarter of it.
struct RingSize {
  float diameter, gap;
};
RingSize ringSize(float viewHeightMm, int count);
// The visible share while closed: at least `minTargetMm` (a safe pen target).
float ringVisible(const RingSettings& s, float diameterMm, float minTargetMm = 6);

// Where the icon centers are, in pixels. `edge` is the left edge of the
// view, `centerY` its vertical middle, `margin` the space between the edge
// and the open column.
struct RingGeometry {
  int count = 0;
  float edge = 0, centerY = 0, diameter = 0, gap = 0, visible = 0.76f, margin = 0;
};
struct RingPoint {
  float x, y;
};
// Progress 0 = closed ring, 1 = open column (already eased); `reference` is
// the icon at which the curve touches the upright line.
std::vector<RingPoint> ringPoints(const RingGeometry& g, float progress, int reference);

// What the widget saw this frame.
struct RingInput {
  float dt = 0;
  int active = 0;            // the current tool's index
  bool overIcon = false;     // pointer over the visible part of the active icon
  bool tapIcon = false;      // a tap or click on it
  bool insideColumn = false; // pointer over the open column (with a margin)
  bool pressOutside = false; // a tap or click beside the open column
  int picked = -1;           // icon tapped in the open column
  bool stroke = false;       // a sculpt or paint stroke is running
  bool nearIcon = false;     // pointer within the near distance of the icon
  bool pointer = true;       // a pointer is there (a lifted finger is not)
};

class RingState {
 public:
  // Advances timers and the animation. Returns the tool picked (or -1).
  int update(const RingSettings& s, const RingInput& in);

  float progress() const { return progress_; }  // linear, 0..1
  float eased() const;                           // smooth in and out
  int reference() const { return reference_; }
  bool open() const { return progress_ >= 1 && want_; }     // icons can be picked
  bool closed() const { return progress_ <= 0 && !want_; }
  float iconAlpha() const { return iconAlpha_; }            // < 1 near a stroke
  bool hidden() const { return hidden_; }
  // Frames are needed: animation, fading or a timer running.
  bool busy() const;

  // Closes at once without animation (mode switch, for example).
  void reset(int active);
  // Holds the ring at `progress` around `reference` (snapshots).
  void show(float progress, int reference);

 private:
  float progress_ = 0;
  bool want_ = false;   // rolling out (or open)
  // Opened by a tap: while no pointer is there (a lifted finger), it waits
  // for a pick or a tap beside it instead of closing.
  bool sticky_ = false;
  int reference_ = 0;
  float hover_ = 0, away_ = 0;
  bool hidden_ = false;
  float iconAlpha_ = 1;
};

}  // namespace bh::viewer
