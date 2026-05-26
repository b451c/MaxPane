// updater.cpp — see updater.h for rationale.

#include "updater.h"
#include "config.h"
#include "swell_cocoa_helpers.h"  // OpenUrlPlatform on every platform

#include <cstdio>
#include <cstring>
#include <string>

#ifdef __APPLE__
// FetchUrlSync declared in swell_cocoa_helpers.h, implemented in .mm.
extern std::string FetchUrlSyncMacOS(const char* url, int timeoutSec);
#elif defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <cstdlib>
#endif

namespace Updater {

namespace {

constexpr const char* kIndexUrl =
    "https://raw.githubusercontent.com/b451c/MaxPane/main/index.xml";

bool ParseVersion(const char* s, int* major, int* minor, int* patch)
{
  if (!s) return false;
  if (s[0] == 'v' || s[0] == 'V') s++;
  *major = *minor = *patch = 0;
  return std::sscanf(s, "%d.%d.%d", major, minor, patch) == 3;
}

bool IsStrictlyNewer(int aMaj, int aMin, int aPat,
                     int bMaj, int bMin, int bPat)
{
  if (aMaj != bMaj) return aMaj > bMaj;
  if (aMin != bMin) return aMin > bMin;
  return aPat > bPat;
}

// Find first `<version name="…">` substring, extract the tag value.
bool ExtractLatestVersion(const std::string& xml, std::string* out)
{
  const char* needle = "<version name=\"";
  const auto pos = xml.find(needle);
  if (pos == std::string::npos) return false;
  const auto start = pos + std::strlen(needle);
  const auto end = xml.find('"', start);
  if (end == std::string::npos) return false;
  *out = xml.substr(start, end - start);
  return true;
}

std::string HttpGetSync(const char* url)
{
#if defined(__APPLE__)
  return FetchUrlSyncMacOS(url, 10);
#elif defined(_WIN32)
  // WinHTTP — pull the URL into a single std::string. Returns empty on
  // any failure (CrackUrl, Open, Connect, OpenRequest, Send, Receive).
  wchar_t wurl[2048] = {0};
  MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048);

  URL_COMPONENTSW comps = {};
  comps.dwStructSize = sizeof(URL_COMPONENTSW);
  wchar_t host[256] = {0};
  wchar_t path[1024] = {0};
  comps.lpszHostName = host;     comps.dwHostNameLength = 256;
  comps.lpszUrlPath  = path;     comps.dwUrlPathLength  = 1024;
  if (!WinHttpCrackUrl(wurl, 0, 0, &comps)) return std::string();

  HINTERNET hSession = WinHttpOpen(L"MaxPane/2.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return std::string();

  // 10s connect / send / receive timeouts.
  WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

  HINTERNET hConnect = WinHttpConnect(hSession, host,
      INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return std::string(); }

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
      NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE);

  std::string result;
  if (hRequest &&
      WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hRequest, NULL))
  {
    DWORD avail = 0;
    do {
      avail = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
      if (avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(hRequest, &chunk[0], avail, &read)) break;
      chunk.resize(read);
      result += chunk;
    } while (avail > 0);
  }

  if (hRequest) WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return result;
#else
  // Linux — shell out to curl. curl is universally present on REAPER
  // Linux machines (ReaPack itself depends on it). Avoids linking
  // libcurl as a new build dependency. -s silent, -L follow redirects,
  // --max-time 10 hard timeout.
  std::string cmd = "curl -sL --max-time 10 ";
  cmd += "\"";
  cmd += url;
  cmd += "\"";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return std::string();
  std::string result;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
    result.append(buf, n);
  }
  pclose(pipe);
  return result;
#endif
}

}  // anon namespace

void CheckForUpdatesNow(HWND parent, bool showIfUpToDate)
{
  const std::string xml = HttpGetSync(kIndexUrl);
  if (xml.empty()) {
    if (showIfUpToDate) {
      MessageBox(parent,
          "Could not reach github.com to check for updates.\n"
          "Check your internet connection and try again.",
          "MaxPane -- Update check failed", MB_OK);
    }
    return;
  }

  std::string latestTag;
  if (!ExtractLatestVersion(xml, &latestTag)) {
    if (showIfUpToDate) {
      MessageBox(parent,
          "Could not parse the version manifest from github.com.\n"
          "Please try again later or check the Releases page manually.",
          "MaxPane -- Update check failed", MB_OK);
    }
    return;
  }

  int lMaj = 0, lMin = 0, lPat = 0;
  int cMaj = 0, cMin = 0, cPat = 0;
  if (!ParseVersion(latestTag.c_str(), &lMaj, &lMin, &lPat) ||
      !ParseVersion(MAXPANE_VERSION_STRING, &cMaj, &cMin, &cPat)) {
    if (showIfUpToDate) {
      MessageBox(parent,
          "Could not interpret the latest version string.",
          "MaxPane -- Update check failed", MB_OK);
    }
    return;
  }

  if (IsStrictlyNewer(lMaj, lMin, lPat, cMaj, cMin, cPat)) {
    char body[512];
    std::snprintf(body, sizeof(body),
        "A newer version of MaxPane is available.\n\n"
        "    Current: %s\n"
        "    Latest:  %s\n\n"
        "Open the Releases page now?",
        MAXPANE_VERSION_STRING, latestTag.c_str());
    const int res = MessageBox(parent, body,
        "MaxPane -- Update available", MB_YESNO);
    if (res == IDYES) {
      OpenUrlPlatform("https://github.com/b451c/MaxPane/releases/latest");
    }
  } else if (showIfUpToDate) {
    char body[256];
    std::snprintf(body, sizeof(body),
        "MaxPane is up to date.\n\nInstalled: %s",
        MAXPANE_VERSION_STRING);
    MessageBox(parent, body, "MaxPane -- Up to date", MB_OK);
  }
}

}  // namespace Updater
