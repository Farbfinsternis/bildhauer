#pragma once

// The user interface (Dear ImGui), drawn over the 3D view: menu bar, a tool
// panel for sculpt and paint mode with the material layers, a status bar.

#include <windows.h>

namespace bh::viewer {

// Context, Win32 and OpenGL backends, font and style for the window's DPI.
void initUi(HWND hwnd);
void shutdownUi();

// Feeds a window message to the UI; true if the UI consumed it entirely.
bool uiHandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// One UI frame over what is in the back buffer.
void drawUi();

bool uiWantsMouse();     // the pointer is over the UI or a widget is held
bool uiWantsKeyboard();  // a text field has the keyboard

// Paint mode is one way, so entering it asks first (Ctrl+P, menu, button).
void askEnterPaintMode();
// Switching symmetry changes the mesh, so it asks first.
void askSetSymmetry(bool on);

}  // namespace bh::viewer
