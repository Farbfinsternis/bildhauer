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

// Fill/Clear (paint.md 3.5) with the current paint tool, as far as its
// Strength slider says: fills the color layer (or the edited layer's mask),
// or clears the bump. Inverted: the secondary color, or an empty mask.
void fillLayer(bool invert);
// What fillLayer does now, for buttons and messages.
const char* fillLabel();

// Brush mask (mask = true, made gray) or brush texture from a file dialog;
// switches it on. False if cancelled or unreadable.
bool chooseStampImage(bool mask);
// The same without the dialog.
bool loadStampImage(const std::string& path, bool mask);
// Builds the stamps from the images and options; call after any change.
void rebuildStamps();
// Whether strokes use a stamp: an enabled mask or texture that is loaded.
bool stampsActive();
// Whether sculpt strokes use the brush mask: switched on and loaded.
bool sculptMaskActive();

// A new material layer with the next built-in matcap; it becomes what Paint
// color paints. Returns its slot or -1 when all are in use.
int addMaterialLayer();
std::string nextBuiltinMatcap(const std::string& current);

}  // namespace bh::viewer
