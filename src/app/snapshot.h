#pragma once

// Snapshot mode, for checks without interaction: optional demo strokes, a
// save, a frame-time measurement, then one frame written as PNG.

#include <string>

namespace bh::viewer {

struct SnapshotOptions {
  std::string path;   // PNG to write
  bool demo = false;  // a few strokes across the front (paint strokes and a gold layer in paint mode)
  bool symmetryOff = false;  // turn symmetry off first
  bool paint = false; // enter paint mode first
  bool save = false;  // save as <name>_bildhauer.sc1 before rendering
  bool ui = true;     // draw the UI into the snapshot
  std::string brushMask, brushTexture;  // stamp images for the paint strokes
  int frames = 0;     // > 0: write the mean frame time to <path>.txt
};

int runSnapshot(const SnapshotOptions& options);

}  // namespace bh::viewer
