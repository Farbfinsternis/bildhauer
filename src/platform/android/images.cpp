#include "app/images.h"

namespace bh::viewer {

bool loadImage(const std::string&, Image&, std::string* error) {
  if (error) *error = "Image import is not available in this Android prototype";
  return false;
}

}  // namespace bh::viewer
