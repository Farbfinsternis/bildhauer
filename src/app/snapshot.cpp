#include "app/snapshot.h"

#include <windows.h>

#include <GL/gl.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "app/app.h"
#include "app/document.h"
#include "app/input.h"
#include "app/render.h"
#include "app/ui.h"

namespace bh::viewer {

namespace {

uint32_t crc32(const unsigned char* d, size_t n, uint32_t c = 0xffffffffu) {
  for (size_t i = 0; i < n; ++i) {
    c ^= d[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return c;
}

// PNG with stored (uncompressed) deflate blocks; no zlib needed.
bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgbBottomUp) {
  std::vector<unsigned char> raw;
  for (int y = h - 1; y >= 0; --y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgbBottomUp.begin() + size_t(y) * w * 3, rgbBottomUp.begin() + size_t(y + 1) * w * 3);
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
    size_t len = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + len >= raw.size() ? 1 : 0);
    z.insert(z.end(), {(unsigned char)(len & 255), (unsigned char)(len >> 8), (unsigned char)(~len & 255),
                       (unsigned char)((~len >> 8) & 255)});
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
    if (pos + len >= raw.size()) break;
  }
  uint32_t adler = (b << 16) | a;
  z.insert(z.end(), {(unsigned char)(adler >> 24), (unsigned char)(adler >> 16), (unsigned char)(adler >> 8),
                     (unsigned char)adler});
  std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
    uint32_t n = uint32_t(data.size());
    out.insert(out.end(), {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n});
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    uint32_t c = ~crc32(td.data(), td.size());
    out.insert(out.end(), {(unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c});
  };
  std::vector<unsigned char> ihdr = {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8),
                                     (unsigned char)w, (unsigned char)(h >> 24), (unsigned char)(h >> 16),
                                     (unsigned char)(h >> 8), (unsigned char)h, 8, 2, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return writeBinaryFile(path, out);
}

// A few strokes across the front, in screen space: Draw, inverted Draw, Mask.
void runDemo() {
  auto strokeAcross = [&](Tool tool, bool invert, int y0) {
    app.brush.tool = tool;
    beginStroke(app.width / 2 - 130, y0, invert, false);
    for (int i = 0; i <= 40; ++i)
      if (app.stroke) app.stroke->moveTo(inputAt(app.width / 2 - 130 + i * 6, y0 + int(20 * std::sin(i * 0.3))));
    app.stroke.reset();
  };
  sliders(Tool::Draw) = sliders(Tool::Mask) = {0.4f, 0.5f};
  strokeAcross(Tool::Draw, false, app.height / 2 - 90);
  strokeAcross(Tool::Draw, true, app.height / 2);
  strokeAcross(Tool::Mask, false, app.height / 2 + 90);
  app.brush.tool = Tool::Draw;
}

// Paint strokes across the front: color, secondary color, bump.
void runPaintDemo() {
  auto strokeAcross = [&](PaintTool tool, bool invert, int y0) {
    app.paint.tool = tool;
    beginPaintStroke(app.width / 2 - 70, y0, invert);
    for (int i = 0; i <= 40; ++i) {
      if (!app.paintStroke) break;
      StrokeInput in = inputAt(app.width / 2 - 70 + i * 4, y0 + int(20 * std::sin(i * 0.3)));
      app.paintStroke->moveTo(in.point, in.hit);
    }
    app.paintStroke.reset();
  };
  strokeAcross(PaintTool::Color, false, app.height / 2 - 110);
  strokeAcross(PaintTool::Color, true, app.height / 2 - 30);
  strokeAcross(PaintTool::Bump, false, app.height / 2 + 50);
  // A gold layer, painted in across the forehead.
  app.editSlot = app.canvas->addLayer("bildhauer:gold");
  app.canvas->material(app.editSlot).colored = true;
  strokeAcross(PaintTool::Color, false, app.height / 2 - 170);
  app.paint.tool = PaintTool::Color;
  app.editSlot = 0;
}

}  // namespace

int runSnapshot(const SnapshotOptions& o) {
  RECT rc;
  GetClientRect(app.hwnd, &rc);
  app.width = rc.right;
  app.height = rc.bottom;
  if (o.paint) enterPaintMode();
  if (o.demo) app.paintMode ? runPaintDemo() : runDemo();
  if (o.save) {
    std::string err;
    if (saveModel(defaultSavePath(), &err)) app.modified = false;
    else app.status = "save failed: " + err;
  }
  if (o.frames > 0) {  // average frame time, written next to the snapshot
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < o.frames; ++i) {
      renderScene();
      glFinish();
    }
    QueryPerformanceCounter(&t1);
    double ms = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(f.QuadPart) / o.frames;
    FILE* out = std::fopen((o.path + ".txt").c_str(), "w");
    if (out) {
      std::fprintf(out, "%d triangles, %.2f ms per frame\n", app.mesh.numTriangles(), ms);
      std::fclose(out);
    }
  }
  // The UI settles over a few frames (window sizes, fonts); the last one counts.
  for (int i = 0; i < (o.ui ? 3 : 1); ++i) {
    renderScene();
    if (o.ui) drawUi();
  }
  std::vector<unsigned char> px(size_t(app.width) * app.height * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, app.width, app.height, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  return writePng(o.path, app.width, app.height, px) ? 0 : 1;
}

}  // namespace bh::viewer
