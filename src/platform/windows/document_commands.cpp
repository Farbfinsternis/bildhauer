#include "app/document.h"
#include "app/app.h"
#include "app/dialogs.h"
#include "platform/windows/host.h"
#include "io/obj.h"

namespace bh::viewer {
namespace {
std::string fileName(const std::string& path) { return path.substr(path.find_last_of("/\\") + 1); }
}

void commandNew() {
  if (confirmDiscard()) newModel();
}

void commandOpen() {
  if (!confirmDiscard()) return;
  std::string path = openFileDialog(windowsHost.hwnd, kSc1Filter);
  if (path.empty()) return;
  std::string err;
  if (!loadModel(path, &err)) app.status = "cannot open " + fileName(path) + ": " + err;
}

void commandSaveAs() {
  std::string path = saveFileDialog(windowsHost.hwnd, kSc1Filter, app.savePath.empty() ? defaultSavePath() : app.savePath, "sc1");
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

void commandExportObj() {
  std::string base = app.savePath.empty() ? defaultSavePath() : app.savePath;
  if (base.size() > 4 && base.substr(base.size() - 4) == ".sc1") base.resize(base.size() - 4);
  std::string path = saveFileDialog(windowsHost.hwnd, kObjFilter, base + ".obj", "obj");
  if (path.empty()) return;
  ObjExport options;
  options.scale = app.file.extraA;
  options.offset = app.file.extraVec;
  options.normals = app.exportNormals;
  if (app.paintMode) options.uvs = &app.texmap;
  std::string err;
  app.status = writeObj(path, app.mesh, options, &err) ? "exported " + fileName(path) : "export failed: " + err;
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
  int answer = MessageBoxA(windowsHost.hwnd, ("Save changes to " + name + "?").c_str(), "bildhauer",
                           MB_YESNOCANCEL | MB_ICONQUESTION);
  if (answer == IDCANCEL) return false;
  if (answer == IDYES) {
    commandSave();
    return !app.modified;  // still modified: the save was cancelled or failed
  }
  return true;
}

bool chooseStampImage(bool mask) {
  std::string path = openFileDialog(windowsHost.hwnd, kImageFilter);
  return !path.empty() && loadStampImage(path, mask);
}

}  // namespace bh::viewer
