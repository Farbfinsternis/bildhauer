#pragma once

// Image decoding supplied by the platform host (Windows: WIC).

#include <cstdint>
#include <string>
#include <vector>

namespace bh::viewer {

struct Image {
  int width = 0, height = 0;
  std::vector<uint32_t> rgba;  // rows from the top, 0xAABBGGRR
};

// On Windows, needs COM initialized on the calling thread.
bool loadImage(const std::string& path, Image& out, std::string* error);

}  // namespace bh::viewer
