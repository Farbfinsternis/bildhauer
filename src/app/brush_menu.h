#pragma once

// The brush menu: a ring around the pointer that holds the current tool's
// settings, opened with Space (prototype, desktop). Top: the tool's name.
// Left and right: Size and Strength as arcs that fill from the bottom.
// Bottom: the tool's switches and buttons as segments. The middle stays
// free, so the model and the brush circle under the pointer stay visible.
//
// This part has no ImGui: which zone a point lies in and the arcs' values.
// Angles are in degrees, 0 at the top, growing clockwise (screen y down).

namespace bh::viewer {

struct MenuRadii {
  float inner = 70, outer = 165;
};

// The sectors, in the angle convention above.
constexpr float kMenuTitleHalf = 30;                    // title: -30..30
constexpr float kMenuArcFrom = 40, kMenuArcTo = 110;    // right arc 40..110, left -110..-40
constexpr float kMenuBottomFrom = 120;                  // segments: 120..240 (through 180)

enum class MenuZone { None, Center, Title, Size, Strength, Segment, Outside };

struct MenuHit {
  MenuZone zone = MenuZone::None;
  int segment = -1;  // with Segment: 0 = leftmost
};

// The angle of the offset (dx, dy) from the center, -180..180.
float menuAngle(float dx, float dy);

// Where the offset (dx, dy) from the center falls, with `segments` segments
// at the bottom. A margin of `slack` beyond the outer radius still counts.
MenuHit menuHit(float dx, float dy, const MenuRadii& r, int segments, float slack = 0);

// The value 0..1 of the Size (left) or Strength (right) arc at `angle`,
// clamped to the arc: 0 at its bottom end, 1 at its top end. Past the
// bottom of the ring it stays 0, so dragging around the bottom never jumps
// to the other end.
float arcValue(float angle, bool left);
// The angle of `value` on that arc.
float arcAngle(float value, bool left);

// Segment `index` of `count`: its start and end angle (left to right on
// screen, so from 240 down to 120; as -180..180 they may wrap).
void segmentAngles(int index, int count, float& from, float& to);

}  // namespace bh::viewer
