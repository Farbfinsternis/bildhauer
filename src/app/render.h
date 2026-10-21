#pragma once

// Drawing the model: matcap shading in sculpt mode; in paint mode the color
// and bump maps with the material layers (GLSL when OpenGL 2.0 is there).

namespace bh::viewer {

// Matcap texture and paint shader; needs the GL context.
void initRendering();

// The 3D view over the whole window, with the brush cursor. The UI is drawn
// over it afterwards.
void renderScene();

}  // namespace bh::viewer
