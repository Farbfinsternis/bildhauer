#pragma once

// Tells the user about a new release (Windows): asks GitHub for bildhauer's
// latest release on a background thread and compares it with the version
// built into the exe. Nothing is downloaded; the dialog leads to the release
// page. The only data sent is the request itself, with "bildhauer/<version>"
// as the user agent, as GitHub's API asks.

#include <windows.h>

#include <string>

namespace bh::viewer {

// Posted to the window when a check has finished; then see updateStatus().
inline constexpr UINT kUpdateCheckedMessage = WM_APP + 1;

enum class UpdateState { Idle, Checking, Available, UpToDate, Failed };

struct UpdateStatus {
  UpdateState state = UpdateState::Idle;
  std::string latest;   // the release's tag, e.g. "v0.1.4"
  bool manual = false;  // asked for in the Help menu: report every outcome
};

const char* appVersion();  // "0.1.3"

// Starts a check unless one is running (a manual request still marks it).
void startUpdateCheck(HWND notify, bool manual);
UpdateStatus updateStatus();

// The release page of a tag from releaseTag(), opened in the browser.
std::string releaseUrl(const std::string& tag);
void openInBrowser(const std::string& url);

// Remembered in the settings file.
bool checkUpdatesOnStart();
void setCheckUpdatesOnStart(bool on);
std::string skippedRelease();  // the tag the user chose to skip, or ""
void skipRelease(const std::string& tag);

}  // namespace bh::viewer
