#include "app/update.h"

#include <winhttp.h>
#include <shellapi.h>

#include <memory>
#include <mutex>
#include <thread>

#include "app/release.h"
#include "app/settings.h"
#include "version.h"

namespace bh::viewer {

namespace {

constexpr const wchar_t* kApiHost = L"api.github.com";
constexpr const wchar_t* kLatestPath = L"/repos/Farbfinsternis/bildhauer/releases/latest";
constexpr const char* kReleasePage = "https://github.com/Farbfinsternis/bildhauer/releases/tag/";

// Shared with the checking thread. The thread holds its own reference, so a
// check still running when bildhauer closes never touches freed memory.
struct Shared {
  std::mutex mutex;
  UpdateStatus status;
};
std::shared_ptr<Shared> shared = std::make_shared<Shared>();

struct InternetClose {
  void operator()(HINTERNET h) const { WinHttpCloseHandle(h); }
};
using Internet = std::unique_ptr<void, InternetClose>;

// GET /releases/latest; the answer's text, or "" if anything goes wrong.
std::string fetchLatestRelease() {
  const std::string version = BILDHAUER_VERSION;
  const std::wstring agent = L"bildhauer/" + std::wstring(version.begin(), version.end());
  Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) return {};
  WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 10000);
  Internet connection(WinHttpConnect(session.get(), kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) return {};
  Internet request(WinHttpOpenRequest(connection.get(), L"GET", kLatestPath, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) return {};
  const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(request.get(), headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr))
    return {};
  DWORD code = 0, size = sizeof code;
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) ||
      code != 200)
    return {};
  std::string body;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) return {};
    if (available == 0) break;
    if (body.size() + available > (1 << 20)) return {};  // a release answer is a few kB
    const size_t at = body.size();
    body.resize(at + available);
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), body.data() + at, available, &read)) return {};
    body.resize(at + read);
  }
  return body;
}

}  // namespace

const char* appVersion() { return BILDHAUER_VERSION; }

void startUpdateCheck(HWND notify, bool manual) {
  {
    std::lock_guard lock(shared->mutex);
    if (shared->status.state == UpdateState::Checking) {
      shared->status.manual |= manual;
      return;
    }
    shared->status = {UpdateState::Checking, {}, manual};
  }
  std::thread([state = shared, notify] {
    const std::string tag = releaseTag(fetchLatestRelease());
    {
      std::lock_guard lock(state->mutex);
      state->status.latest = tag;
      state->status.state = tag.empty()                         ? UpdateState::Failed
                            : newerVersion(tag, BILDHAUER_VERSION) ? UpdateState::Available
                                                                   : UpdateState::UpToDate;
    }
    PostMessageW(notify, kUpdateCheckedMessage, 0, 0);
  }).detach();
}

UpdateStatus updateStatus() {
  std::lock_guard lock(shared->mutex);
  return shared->status;
}

std::string releaseUrl(const std::string& tag) { return kReleasePage + tag; }

void openInBrowser(const std::string& url) {
  ShellExecuteW(nullptr, L"open", std::wstring(url.begin(), url.end()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool checkUpdatesOnStart() { return readSetting("update", "check_on_start", "1") != "0"; }
void setCheckUpdatesOnStart(bool on) { writeSetting("update", "check_on_start", on ? "1" : "0"); }
std::string skippedRelease() { return readSetting("update", "skipped", ""); }
void skipRelease(const std::string& tag) { writeSetting("update", "skipped", tag); }

}  // namespace bh::viewer
