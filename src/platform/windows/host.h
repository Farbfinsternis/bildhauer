#pragma once

#include <windows.h>

namespace bh::viewer {

// Native window handles belong to the Windows host, not the shared document.
struct WindowsHost {
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  float dpiScale = 1;  // the window's monitor: 1 at 96 DPI (100 %), 1.5 at 150 %
};
inline WindowsHost windowsHost;

}  // namespace bh::viewer
