#pragma once

// Drawing the model: matcap shading in sculpt mode; in paint mode the color
// and bump maps with the material layers (GLSL when OpenGL 2.0 is there).

#include <string>

namespace bh::viewer {

// Paint shader; needs the GL context.
void initRendering();

// The 3D view over the whole window, with the brush cursor. The UI is drawn
// over it afterwards.
void renderScene();

// Texture of a built-in matcap, made on first use; for previews in the UI.
unsigned int builtinMatcapTexture(const std::string& name);

}  // namespace bh::viewer
