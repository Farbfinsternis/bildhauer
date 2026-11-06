#pragma once

namespace bh::viewer {
// Called on the GLSurfaceView render thread with a current ES 3 context.
void initMobileRendering();
void renderMobileScene();
void invalidateMobileGeometry();
// Brings the graphics card up to date; drawing does it by itself.
void syncMobileGeometry();
// Screen density in pixels per dp, for sizes such as the symmetry line.
void setMobileDensity(float pixelsPerDp);
// Sculpt materials: the built-in matcaps, red wax by default.
int mobileMatcapCount();
unsigned mobileMatcapTexture(int index);
int mobileMatcap();
void setMobileMatcap(int index);
}
