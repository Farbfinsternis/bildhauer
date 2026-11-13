#include "app/settings.h"

#include <windows.h>
#include <shlobj.h>

namespace bh::viewer {

namespace {

// Wide strings, since the user's profile path may hold any characters.
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::wstring settingsFile(bool create) {
  PWSTR roaming = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) {
    CoTaskMemFree(roaming);
    return {};
  }
  std::wstring dir = std::wstring(roaming) + L"\\bildhauer";
  CoTaskMemFree(roaming);
  if (create) CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\bildhauer.ini";
}

}  // namespace

std::string readSetting(const char* section, const char* key, const std::string& fallback) {
  const std::wstring file = settingsFile(false);
  if (file.empty()) return fallback;
  wchar_t value[256];
  GetPrivateProfileStringW(widen(section).c_str(), widen(key).c_str(), widen(fallback).c_str(), value, 256,
                           file.c_str());
  std::string out;
  for (const wchar_t* c = value; *c; ++c) out += *c < 128 ? char(*c) : '?';
  return out;
}

void writeSetting(const char* section, const char* key, const std::string& value) {
  const std::wstring file = settingsFile(true);
  if (!file.empty())
    WritePrivateProfileStringW(widen(section).c_str(), widen(key).c_str(), widen(value).c_str(), file.c_str());
}

}  // namespace bh::viewer
