#include "app/dialogs.h"

#include <commdlg.h>

namespace bh::viewer {

std::string openFileDialog(HWND owner, const char* filter) {
  char file[MAX_PATH] = {};
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = file;
  ofn.nMaxFile = sizeof file;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameA(&ofn) ? std::string(file) : std::string();
}

std::string saveFileDialog(HWND owner, const char* filter, const std::string& suggested, const char* extension) {
  char file[MAX_PATH] = {};
  suggested.copy(file, sizeof file - 1);
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = file;
  ofn.nMaxFile = sizeof file;
  ofn.lpstrDefExt = extension;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  return GetSaveFileNameA(&ofn) ? std::string(file) : std::string();
}

}  // namespace bh::viewer
