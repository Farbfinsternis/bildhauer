#include "app/document.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

#include "app/app.h"
#include "app/dialogs.h"
#include "app/images.h"
#include "app/matcaps.h"
#include "core/reduce.h"
#include "core/relax.h"
#include "core/subdivide.h"
#include "core/symmetry.h"

namespace bh::viewer {

namespace {

void makeIcosphere(Mesh& m, int levels) {
  const float t = (1 + std::sqrt(5.0f)) / 2;
  std::vector<Vec3> p = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                         {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  std::vector<std::array<int, 3>> f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                       {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                       {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (int l = 0; l < levels; ++l) {
    std::map<std::pair<int, int>, int> mid;
    auto midpoint = [&](int a, int b) {
      auto key = std::minmax(a, b);
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      p.push_back(normalize(p[a] + p[b]));
      return mid[key] = int(p.size()) - 1;
    };
    std::vector<std::array<int, 3>> next;
    for (auto& tri : f) {
      int a = midpoint(tri[0], tri[1]), b = midpoint(tri[1], tri[2]), c = midpoint(tri[2], tri[0]);
      next.push_back({tri[0], a, c});
      next.push_back({tri[1], b, a});
      next.push_back({tri[2], c, b});
      next.push_back({a, b, c});
    }
    f.swap(next);
  }
  m = Mesh{};
  for (Vec3& q : p) m.addVertex(normalize(q));
  for (auto& tri : f) m.addTriangle(tri[0], tri[1], tri[2]);
  m.updateGeometry();
  m.storeReference();
  m.clearDirty();
}

// Back to sculpt mode with an empty history, for a new or loaded model.
void resetDocument() {
  app.stroke.reset();
  app.paintStroke.reset();
  app.undo.clear();
  app.paintMode = false;
  app.canvas.reset();
  app.texmap = TextureMapping{};
  app.tangents.clear();
  app.editSlot = 0;
  app.modified = false;
  app.savePath.clear();
  app.hover = PickHit{};
}

std::string fileName(const std::string& path) { return path.substr(path.find_last_of("/\\") + 1); }

}  // namespace

void newModel() {
  resetDocument();
  makeIcosphere(app.mesh, 3);
  app.file = Sc1File{};
  app.path.clear();
  frameModel();
}

void frameModel() {
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (int v = 0; v < app.mesh.vertexSlots(); ++v) {
    const Vertex& vx = app.mesh.vertex(v);
    if (!vx.active) continue;
    lo = {std::fmin(lo.x, vx.pos.x), std::fmin(lo.y, vx.pos.y), std::fmin(lo.z, vx.pos.z)};
    hi = {std::fmax(hi.x, vx.pos.x), std::fmax(hi.y, vx.pos.y), std::fmax(hi.z, vx.pos.z)};
  }
  if (app.mesh.symmetric) lo.x = -hi.x;
  app.camera = Camera{};
  app.camera.target = (lo + hi) * 0.5f;
  app.sceneSize = std::fmax(length(hi - lo), 1e-3f);
  app.camera.distance = app.sceneSize * 1.6f;
}

bool loadModel(const std::string& path, std::string* error) {
  std::vector<uint8_t> data;
  Sc1File file;
  Mesh mesh;
  if (!readBinaryFile(path, data)) {
    if (error) *error = "cannot read the file";
    return false;
  }
  if (!readSc1(data, file, error) || !sc1ToMesh(file, mesh, error)) return false;
  resetDocument();
  app.mesh = std::move(mesh);
  app.file = std::move(file);
  app.path = path;
  app.status.clear();
  if (app.file.hasTexturing) {  // painted: open in paint mode with the stored UVs and images
    Sc1Texturing tex;
    std::unique_ptr<Canvas> canvas;
    std::string err;
    bool ok = decodeTexturing(app.file, tex, &err) && texturingToMapping(tex, app.mesh, app.texmap, &err);
    if (ok) {
      canvas = std::make_unique<Canvas>(app.mesh, app.texmap, tex.color.w, tex.color.h);
      ok = texturingToCanvas(tex, *canvas, &err);
    }
    if (ok) {
      app.canvas = std::move(canvas);
      app.tangents = computeTangents(app.mesh, app.texmap);
      app.paintMode = true;
      app.uploadAll = true;
    } else {
      app.texmap = TextureMapping{};
      app.status = "paint data not loaded: " + err;
    }
  }
  frameModel();
  return true;
}

bool saveModel(const std::string& path, std::string* error) {
  if (app.paintMode) meshToSc1(app.mesh, app.file, app.texmap, *app.canvas);
  else meshToSc1(app.mesh, app.file);
  std::vector<uint8_t> data;
  if (!writeSc1(app.file, data, error)) return false;
  if (!writeBinaryFile(path, data)) {
    if (error) *error = "cannot write " + path;
    return false;
  }
  return true;
}

std::string defaultSavePath() {
  std::string out = app.path.empty() ? std::string("untitled") : app.path;
  if (out.size() > 4 && out.substr(out.size() - 4) == ".sc1") out.resize(out.size() - 4);
  if (out.size() < 10 || out.substr(out.size() - 10) != "_bildhauer") out += "_bildhauer";
  return out + ".sc1";
}

void commandNew() {
  if (confirmDiscard()) newModel();
}

void commandOpen() {
  if (!confirmDiscard()) return;
  std::string path = openFileDialog(app.hwnd, kSc1Filter);
  if (path.empty()) return;
  std::string err;
  if (!loadModel(path, &err)) app.status = "cannot open " + fileName(path) + ": " + err;
}

void commandSaveAs() {
  std::string path = saveFileDialog(app.hwnd, kSc1Filter, app.savePath.empty() ? defaultSavePath() : app.savePath, "sc1");
  if (path.empty()) return;
  std::string err;
  if (saveModel(path, &err)) {
    app.savePath = path;
    app.modified = false;
    app.status = "saved " + fileName(path);
  } else {
    app.status = "save failed: " + err;
  }
}

void commandSave() {
  if (app.savePath.empty()) {
    commandSaveAs();
    return;
  }
  std::string err;
  if (saveModel(app.savePath, &err)) {
    app.modified = false;
    app.status = "saved " + fileName(app.savePath);
  } else {
    app.status = "save failed: " + err;
  }
}

bool confirmDiscard() {
  if (!app.modified) return true;
  std::string name = app.path.empty() ? "the new model" : fileName(app.path);
  int answer = MessageBoxA(app.hwnd, ("Save changes to " + name + "?").c_str(), "bildhauer",
                           MB_YESNOCANCEL | MB_ICONQUESTION);
  if (answer == IDCANCEL) return false;
  if (answer == IDYES) {
    commandSave();
    return !app.modified;  // still modified: the save was cancelled or failed
  }
  return true;
}

void pushUndo() {
  app.undo.push_back(app.mesh);
  if (app.undo.size() > 20) app.undo.pop_front();
}

void undoLast() {
  if (app.paintMode) {
    if (!app.canvas->undo()) app.status = "nothing to undo";
    return;
  }
  if (app.undo.empty()) {
    app.status = "nothing to undo";
    return;
  }
  app.mesh = std::move(app.undo.back());
  app.undo.pop_back();
}

void subdivideAll() {
  if (app.paintMode) return;
  pushUndo();
  subdivide(app.mesh, {true, app.mesh.symmetric});
  app.modified = true;
}

void reduceAll() {
  if (app.paintMode) return;
  pushUndo();
  reduceMesh(app.mesh);
  app.modified = true;
}

void setSymmetry(bool on) {
  if (app.paintMode || app.mesh.symmetric == on) return;
  app.stroke.reset();
  pushUndo();
  if (on) {
    const int converted = symmetryOn(app.mesh);
    app.status = "symmetry on";
    if (converted > 0) app.status += ", " + std::to_string(converted) + " quads turned into triangles";
  } else if (symmetryOff(app.mesh)) {
    app.status = "symmetry off";
  } else {
    app.mesh = std::move(app.undo.back());
    app.undo.pop_back();
    app.status = "symmetry off failed: a vertex on the plane has too many edges";
    return;
  }
  app.hover = PickHit{};
  app.modified = true;
}

void enterPaintMode() {
  if (app.paintMode) return;
  app.stroke.reset();
  for (int i = 0; i < 200 && !app.mesh.dirtyVertices.empty(); ++i) beautifyPass(app.mesh);
  app.mesh.updateGeometry();
  app.mesh.clearDirty();
  app.undo.clear();
  app.texmap = buildAtlas(app.mesh, {kPaintTextureSize});
  app.canvas = std::make_unique<Canvas>(app.mesh, app.texmap, kPaintTextureSize, kPaintTextureSize);
  app.canvas->material(0).matcap = builtinMatcaps()[0];
  app.tangents = computeTangents(app.mesh, app.texmap);
  app.editSlot = 0;
  app.paintMode = true;
  app.uploadAll = true;
  app.modified = true;
  char msg[128];
  std::snprintf(msg, sizeof msg, "paint mode: %d charts, texel coverage %d%%", app.texmap.numCharts,
                int(100.0 * app.canvas->texels().covered / (kPaintTextureSize * kPaintTextureSize)));
  app.status = msg;
}

void fillLayer(bool invert) {
  if (!app.paintMode) return;
  app.paintStroke.reset();
  PaintSettings s = app.paint;
  s.target = app.editSlot;
  app.canvas->fill(s, paintSliders()[1], invert);
  app.modified = true;
  app.status = std::string(fillLabel()) + (invert ? ", inverted" : "");
}

const char* fillLabel() {
  if (app.paint.tool != PaintTool::Color) return "Clear bump";
  return app.editSlot > 0 ? "Fill layer mask" : "Fill color";
}

bool chooseStampImage(bool mask) {
  std::string path = openFileDialog(app.hwnd, kImageFilter);
  return !path.empty() && loadStampImage(path, mask);
}

bool loadStampImage(const std::string& path, bool mask) {
  Image image;
  std::string err;
  if (!loadImage(path, image, &err)) {
    app.status = err;
    return false;
  }
  Stamp stamp{image.width, image.height, std::move(image.rgba)};
  if (mask) {
    app.maskImage = grayStamp(stamp);
    app.maskPath = path;
    app.maskOn = true;
  } else {
    app.textureImage = std::move(stamp);
    app.texturePath = path;
    app.textureOn = true;
  }
  rebuildStamps();
  return true;
}

void rebuildStamps() {
  const bool mask = app.maskOn && !app.maskImage.empty(), texture = app.textureOn && !app.textureImage.empty();
  app.stamps = buildStamps(texture ? &app.textureImage : nullptr, mask ? &app.maskImage : nullptr, app.maskInvert);
  ++app.stampsVersion;
}

bool stampsActive() {
  return (app.maskOn && !app.maskImage.empty()) || (app.textureOn && !app.textureImage.empty());
}

std::string nextBuiltinMatcap(const std::string& current) {
  const std::vector<std::string>& names = builtinMatcaps();
  auto it = std::find(names.begin(), names.end(), current);
  return it == names.end() || it + 1 == names.end() ? names[0] : *(it + 1);
}

int addMaterialLayer() {
  Canvas& cv = *app.canvas;
  int last = 0;
  for (int s = 0; s < kMaxMaterials; ++s)
    if (cv.material(s).used) last = s;
  const int slot = cv.addLayer(nextBuiltinMatcap(cv.material(last).matcap));
  if (slot < 0) {
    app.status = "all 16 material slots are in use";
    return -1;
  }
  cv.material(slot).colored = true;
  app.editSlot = slot;
  app.modified = true;
  return slot;
}

}  // namespace bh::viewer
