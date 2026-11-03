#pragma once

#include <windows.h>

namespace bh::viewer {

// Native window handles belong to the Windows host, not the shared document.
struct WindowsHost {
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
};
inline WindowsHost windowsHost;

}  // namespace bh::viewer
