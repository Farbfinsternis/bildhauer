#include "app/app.h"

namespace bh::viewer {

App app;

ToolSliders& sliders(Tool tool) { return app.sliders[size_t(tool)]; }

std::array<float, 3>& paintSliders() { return app.paintSliders[size_t(app.paint.tool)]; }

float worldRadius(const Vec3& at) {
  float depth = dot(at - app.camera.eye(), app.camera.forward());
  if (app.paintMode) return paintBrushRadius(paintSliders()[0], depth, kFovSlider);
  return brushRadius(sliders(app.brush.tool).size, depth, kFovSlider);
}

}  // namespace bh::viewer
