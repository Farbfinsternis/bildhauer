#pragma once

// Matcaps for the viewer: a few computed ones (no image assets ship with
// bildhauer), named "bildhauer:<name>", and image files (PNG, JPEG, ...) read
// through the Windows Imaging Component, so a painted .sc1 can show the
// matcap images it names where they exist on this machine.
//
// Pixels are RGB rows from the bottom up: row 0 is a view-space normal
// pointing down, so the texture coordinate is n.xy * 0.495 + 0.5.

#include <cstdint>
#include <string>
#include <vector>

namespace bh {

struct MatcapImage {
  int width = 0, height = 0;
  std::vector<uint8_t> rgb;
};

// Names of the computed matcaps, "bildhauer:clay" first.
const std::vector<std::string>& builtinMatcaps();

// A computed matcap or an image file. Gray converts as Sculptris does in
// paint mode for a matcap that is not "Colored": (2R + 3G + B) / 6.
bool loadMatcap(const std::string& name, bool gray, MatcapImage& out, std::string* error);

}  // namespace bh
