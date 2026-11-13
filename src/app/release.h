#pragma once

// Release versions: comparing them and reading the latest one from GitHub's
// answer. Plain functions without network or Windows, so the tests can check
// them (tests/release_test.cpp).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace bh::viewer {

// "v0.1.10" -> {0, 1, 10}: the numbers between the dots, after an optional
// "v"; stops at the first part that is not a number ("1.2-beta" -> {1, 2}).
inline std::vector<int> versionParts(std::string_view v) {
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.remove_prefix(1);
  std::vector<int> parts;
  size_t i = 0;
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
    int n = 0;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9' && n < 100000) n = n * 10 + (v[i++] - '0');
    parts.push_back(n);
    if (i >= v.size() || v[i] != '.') break;
    ++i;
  }
  return parts;
}

// True if `candidate` is a later version than `current` (missing parts
// count as 0: "0.2" equals "0.2.0"). An unreadable candidate is never newer.
inline bool newerVersion(std::string_view candidate, std::string_view current) {
  const std::vector<int> a = versionParts(candidate), b = versionParts(current);
  if (a.empty()) return false;
  for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
    const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
    if (x != y) return x > y;
  }
  return false;
}

// The "tag_name" of a GitHub release (the JSON of /releases/latest), or ""
// if there is none. The tag goes into a URL, so only plain tags are taken:
// letters, digits, '.', '-', '_'.
inline std::string releaseTag(std::string_view json) {
  const std::string_view key = "\"tag_name\"";
  size_t i = json.find(key);
  if (i == std::string_view::npos) return {};
  i += key.size();
  auto skipSpace = [&] {
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
  };
  skipSpace();
  if (i >= json.size() || json[i] != ':') return {};
  ++i;
  skipSpace();
  if (i >= json.size() || json[i] != '"') return {};
  const size_t start = ++i;
  while (i < json.size() && json[i] != '"') {
    const char c = json[i];
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                       c == '-' || c == '_';
    if (!plain) return {};
    ++i;
  }
  if (i >= json.size() || i == start || i - start > 64) return {};
  return std::string(json.substr(start, i - start));
}

}  // namespace bh::viewer
