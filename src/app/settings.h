#pragma once

// What bildhauer remembers between runs (Windows): a small INI file at
// %APPDATA%\bildhauer\bildhauer.ini, sections and keys in plain ASCII.

#include <string>

namespace bh::viewer {

std::string readSetting(const char* section, const char* key, const std::string& fallback);
void writeSetting(const char* section, const char* key, const std::string& value);

}  // namespace bh::viewer
