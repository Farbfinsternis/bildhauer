#pragma once

// Commands on the model, shared by the menus, the panels and the keys: new,
// open, save, undo, mesh operations, paint mode and material layers.

#include <string>

namespace bh::viewer {

// An icosphere as the new model (full mesh, no symmetry).
void newModel();
// Points the camera at the whole model.
void frameModel();
// Loads an .sc1; a painted file opens in paint mode. On failure the current
// model stays.
bool loadModel(const std::string& path, std::string* error);
bool saveModel(const std::string& path, std::string* error);
// <name>_bildhauer.sc1 beside the loaded file, so originals stay untouched.
std::string defaultSavePath();

// The File menu. Save writes to the file chosen with Save as (asking the
// first time); New and Open ask to save unsaved changes first.
void commandNew();
void commandOpen();
void commandSave();
void commandSaveAs();
// Asks whether to save unsaved changes: false if the user cancels.
bool confirmDiscard();

void pushUndo();
void undoLast();  // the last stroke or mesh operation (paint mode: the last stroke)
void subdivideAll();
void reduceAll();
// Turns mirror symmetry on (keeps the +x side) or off (mirrors the half into
// a full mesh); undoable. Not in paint mode.
void setSymmetry(bool on);

// Freezes the mesh, builds the UV atlas and a fresh canvas. There is no way
// back: sculpting would invalidate the mapping.
void enterPaintMode();

// A new material layer with the next built-in matcap; it becomes what Paint
// color paints. Returns its slot or -1 when all are in use.
int addMaterialLayer();
std::string nextBuiltinMatcap(const std::string& current);

}  // namespace bh::viewer
