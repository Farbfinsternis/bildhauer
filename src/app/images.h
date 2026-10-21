#pragma once

// Image files (PNG, JPEG, BMP, ...) through the Windows Imaging Component.

#include <cstdint>
#include <string>
#include <vector>

namespace bh::viewer {

struct Image {
  int width = 0, height = 0;
  std::vector<uint32_t> rgba;  // rows from the top, 0xAABBGGRR
};

// Needs COM initialized on the calling thread.
bool loadImage(const std::string& path, Image& out, std::string* error);

}  // namespace bh::viewer
