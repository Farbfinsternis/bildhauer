#pragma once

// Windows file dialogs.

#include <windows.h>

#include <string>

namespace bh::viewer {

constexpr const char* kSc1Filter = "Sculptris models (*.sc1)\0*.sc1\0All files\0*.*\0";
constexpr const char* kImageFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif\0All files\0*.*\0";

// The chosen file, or an empty string if the dialog was cancelled.
std::string openFileDialog(HWND owner, const char* filter);
std::string saveFileDialog(HWND owner, const char* filter, const std::string& suggested, const char* extension);

}  // namespace bh::viewer
