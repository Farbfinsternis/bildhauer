// Version comparison and reading the tag from GitHub's release JSON
// (src/app/release.h), as the Windows update check uses them.

#include <cstdio>

#include "app/release.h"

using namespace bh::viewer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("    %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int main() {
  std::printf("versions\n");
  check(newerVersion("v0.1.4", "0.1.3"), "0.1.4 is newer than 0.1.3");
  check(newerVersion("v0.1.10", "0.1.9"), "numbers, not text: 0.1.10 > 0.1.9");
  check(newerVersion("v0.2", "0.1.9"), "0.2 > 0.1.9");
  check(newerVersion("v1.0.0", "0.9.9"), "1.0.0 > 0.9.9");
  check(!newerVersion("v0.1.3", "0.1.3"), "the same version is not newer");
  check(!newerVersion("v0.2", "0.2.0"), "0.2 equals 0.2.0");
  check(!newerVersion("v0.1.2", "0.1.3"), "an older release is not newer");
  check(!newerVersion("", "0.1.3"), "no tag is not newer");
  check(!newerVersion("latest", "0.1.3"), "a tag without numbers is not newer");
  check(newerVersion("v0.1.4-beta", "0.1.3"), "a suffix is ignored");

  std::printf("release JSON\n");
  check(releaseTag(R"({"url":"x","tag_name":"v0.1.4","name":"bildhauer 0.1.4"})") == "v0.1.4", "reads tag_name");
  check(releaseTag("{\n  \"tag_name\" :\n  \"v0.2.0\",\n}") == "v0.2.0", "spaces and line breaks");
  check(releaseTag(R"({"message":"Not Found"})").empty(), "no tag_name: empty");
  check(releaseTag(R"({"tag_name":"v1/../../x"})").empty(), "refuses a tag that is not plain");
  check(releaseTag(R"({"tag_name":"v1 x"})").empty(), "refuses spaces");
  check(releaseTag(R"({"tag_name":""})").empty(), "refuses an empty tag");
  check(releaseTag(R"({"tag_name":"v0.1.4)").empty(), "refuses a cut-off answer");
  check(releaseTag(R"({"tag_name":null})").empty(), "refuses null");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
