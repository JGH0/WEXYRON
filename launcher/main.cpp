// WEXYRON Auto-Update Launcher
// Checks GitHub releases, downloads/applies updates, then launches game.exe
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// UNICODE/_UNICODE are injected via -DUNICODE -D_UNICODE compiler flags;
// defining them here too would cause a redefinition warning on GCC/MinGW.
#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <fstream>
#include <thread>
#include <filesystem>
// Libraries linked via -l flags in Makefile (pragma comment is MSVC-only)

static const wchar_t* API_HOST = L"api.github.com";
static const wchar_t* API_PATH = L"/repos/JGH0/WEXYRON/releases/latest";

#define IDC_STATUS   101
#define IDC_PROGRESS 102

// Messages posted from the update thread to the UI thread
#define WM_SET_STATUS   (WM_USER + 1) // lParam = heap wstring* (we delete it)
#define WM_SET_PROGRESS (WM_USER + 2) // wParam = 0-100 to show, -1 to hide
#define WM_DO_LAUNCH    (WM_USER + 3) // wParam = 0:launch+exit, 1:just exit

static HWND g_hwnd, g_hStatus, g_hProgress;
static HBRUSH g_bgBrush;

// ---- Utilities ----

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    return p.substr(0, p.rfind(L'\\'));
}

static std::string ReadVersionFile(const std::wstring& dir) {
    std::ifstream f((dir + L"\\version.txt").c_str());
    std::string v;
    if (f) std::getline(f, v);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
        v.pop_back();
    return v.empty() ? "v0.0.0" : v;
}

// Returns true when `latest` is semantically newer than `cur` (both "vX.Y.Z")
static bool IsNewer(const std::string& cur, const std::string& latest) {
    auto parse = [](const std::string& s) {
        const char* p = s.c_str();
        if (*p == 'v') p++;
        int a = 0, b = 0, c = 0;
        sscanf_s(p, "%d.%d.%d", &a, &b, &c);
        return a * 1000000 + b * 1000 + c;
    };
    return parse(latest) > parse(cur);
}

// Extract a JSON string field: "key":"value" → value
static std::string JsonStr(const std::string& j, const std::string& key) {
    std::string k = "\"" + key + "\":\"";
    auto p = j.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    auto e = j.find('"', p);
    return e == std::string::npos ? std::string{} : j.substr(p, e - p);
}

// Find the browser_download_url for the Windows zip in the assets JSON array
static std::string FindWinZipUrl(const std::string& j) {
    const std::string needle = "windows-x86_64.zip";
    auto pos = j.find(needle);
    if (pos == std::string::npos) return {};
    const std::string key = "\"browser_download_url\":\"";
    auto kp = j.rfind(key, pos);
    if (kp == std::string::npos) return {};
    kp += key.size();
    auto ep = j.find('"', kp);
    return ep == std::string::npos ? std::string{} : j.substr(kp, ep - kp);
}

static std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// ---- WinHTTP helpers ----

static HINTERNET OpenSession() {
    HINTERNET h = WinHttpOpen(L"WEXYRON-Launcher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (h) {
        DWORD rp = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(h, WINHTTP_OPTION_REDIRECT_POLICY, &rp, sizeof rp);
    }
    return h;
}

static std::string HttpGet(const wchar_t* host, const wchar_t* path) {
    HINTERNET hs = OpenSession();
    if (!hs) return {};
    HINTERNET hc = WinHttpConnect(hs, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hr = hc ? WinHttpOpenRequest(hc, L"GET", path,
        nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;
    std::string body;
    if (hr) {
        WinHttpAddRequestHeaders(hr,
            L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28",
            (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(hr, nullptr, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(hr, nullptr)) {
            DWORD av = 0;
            while (WinHttpQueryDataAvailable(hr, &av) && av > 0) {
                std::string chunk(av, 0); DWORD rd = 0;
                WinHttpReadData(hr, chunk.data(), av, &rd);
                body.append(chunk.data(), rd);
            }
        }
        WinHttpCloseHandle(hr);
    }
    if (hc) WinHttpCloseHandle(hc);
    WinHttpCloseHandle(hs);
    return body;
}

// Downloads url → dest, posting WM_SET_PROGRESS messages to hwnd
static bool HttpDownload(const std::wstring& url, const std::wstring& dest, HWND hwnd) {
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[512] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hs = OpenSession();
    if (!hs) return false;
    HINTERNET hc = WinHttpConnect(hs, host, uc.nPort, 0);
    HINTERNET hr = hc ? WinHttpOpenRequest(hc, L"GET", path, nullptr, nullptr, nullptr,
        (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0) : nullptr;

    bool ok = false;
    if (hr && WinHttpSendRequest(hr, nullptr, 0, nullptr, 0, 0, 0) &&
              WinHttpReceiveResponse(hr, nullptr)) {
        DWORD clen = 0, csz = sizeof clen;
        WinHttpQueryHeaders(hr, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &clen, &csz, nullptr);

        std::ofstream out(dest.c_str(), std::ios::binary);
        if (out) {
            DWORD downloaded = 0, av = 0;
            char buf[65536];
            while (WinHttpQueryDataAvailable(hr, &av) && av > 0) {
                DWORD chunk = std::min(av, (DWORD)sizeof(buf));
                DWORD rd = 0;
                if (!WinHttpReadData(hr, buf, chunk, &rd)) break;
                out.write(buf, rd);
                downloaded += rd;
                if (clen > 0)
                    PostMessage(hwnd, WM_SET_PROGRESS,
                        (WPARAM)(int)(100.0 * downloaded / clen), 0);
            }
            ok = (downloaded > 0) && out.good();
        }
    }
    if (hr) WinHttpCloseHandle(hr);
    if (hc) WinHttpCloseHandle(hc);
    WinHttpCloseHandle(hs);
    return ok;
}

// ---- Update & launch ----

// Writes a PowerShell script to %TEMP% and runs it detached so it can
// replace launcher.exe after this process exits, then restart the launcher.
static bool SpawnUpdateScript(const std::wstring& zip, const std::wstring& dir,
                               const std::wstring& ver) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring script = std::wstring(tmp) + L"wexyron_upd.ps1";

    std::wofstream f(script.c_str());
    if (!f) return false;
    f << L"$g='" << dir << L"'\n"
      << L"$z='" << zip << L"'\n"
      << L"$v='" << ver << L"'\n"
      << L"$t=$env:TEMP+'\\WEXYRON_upd'\n"
      << L"Start-Sleep 2\n"
      << L"Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue\n"
      << L"Expand-Archive $z $t -Force\n"
      << L"$src=Join-Path $t 'wexyron'\n"
      << L"if(!(Test-Path $src)){$src=$t}\n"
      << L"Copy-Item \"$src\\*\" $g -Recurse -Force\n"
      << L"Set-Content (Join-Path $g 'version.txt') $v\n"
      << L"Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue\n"
      << L"Remove-Item $z -Force -ErrorAction SilentlyContinue\n"
      << L"Start-Process (Join-Path $g 'launcher.exe')\n";
    f.close();

    std::wstring cmd = L"powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File \""
                     + script + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool DoLaunchGame(const std::wstring& dir) {
    std::wstring exe = dir + L"\\game.exe";
    if (!std::filesystem::exists(exe)) return false;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ---- Update thread (runs off the UI thread) ----

static void UpdateThread(HWND hwnd, std::wstring gameDir) {
    auto setStatus = [&](std::wstring s) {
        PostMessage(hwnd, WM_SET_STATUS, 0, (LPARAM)new std::wstring(std::move(s)));
    };

    std::string cur = ReadVersionFile(gameDir);
    setStatus(L"Checking for updates...");

    std::string json = HttpGet(API_HOST, API_PATH);
    if (json.empty()) {
        setStatus(L"No connection — launching game.");
        Sleep(1200);
        PostMessage(hwnd, WM_DO_LAUNCH, 0, 0);
        return;
    }

    std::string tag = JsonStr(json, "tag_name");
    std::string url = FindWinZipUrl(json);

    if (!tag.empty() && IsNewer(cur, tag) && !url.empty()) {
        setStatus(L"Downloading update " + W(tag) + L"...");
        PostMessage(hwnd, WM_SET_PROGRESS, 0, 0); // show progress bar at 0

        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring zip = std::wstring(tmp) + L"WEXYRON_update.zip";

        if (HttpDownload(W(url), zip, hwnd)) {
            setStatus(L"Applying update — restarting launcher...");
            PostMessage(hwnd, WM_SET_PROGRESS, 100, 0);
            Sleep(600);
            if (SpawnUpdateScript(zip, gameDir, W(tag))) {
                PostMessage(hwnd, WM_DO_LAUNCH, 1, 0); // exit only; PS will relaunch
                return;
            }
        }
        setStatus(L"Update failed — launching current version.");
        Sleep(1800);
    } else {
        std::wstring msg = tag.empty()
            ? L"Could not check for updates — launching."
            : L"Up to date (" + W(cur) + L") — launching...";
        setStatus(msg);
        Sleep(1200);
    }
    PostMessage(hwnd, WM_DO_LAUNCH, 0, 0);
}

// ---- WndProc ----

static std::wstring g_gameDir;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_gameDir  = GetExeDir();
        g_bgBrush  = CreateSolidBrush(RGB(18, 18, 30));

        {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
            InitCommonControlsEx(&icc);
        }

        g_hStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 56, 360, 22, hwnd, (HMENU)IDC_STATUS, nullptr, nullptr);

        g_hProgress = CreateWindowExW(0, PROGRESS_CLASS, nullptr,
            WS_CHILD | PBS_SMOOTH,
            10, 86, 360, 12, hwnd, (HMENU)IDC_PROGRESS, nullptr, nullptr);
        SendMessage(g_hProgress, PBM_SETRANGE,  0, MAKELPARAM(0, 100));
        SendMessage(g_hProgress, PBM_SETBARCOLOR, 0, (LPARAM)RGB(60, 140, 255));
        SendMessage(g_hProgress, PBM_SETBKCOLOR,  0, (LPARAM)RGB(35, 35, 55));

        std::thread(UpdateThread, hwnd, g_gameDir).detach();
        return 0;

    case WM_ERASEBKGND:
        { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)wp, &rc, g_bgBrush); }
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bgBrush);

        // "WEXYRON" title
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 255));
        HFONT fnt = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ old = SelectObject(hdc, fnt);
        RECT tr = { 0, 10, rc.right, 50 };
        DrawTextW(hdc, L"WEXYRON", -1, &tr, DT_CENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(fnt);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(155, 155, 200));
        return (LRESULT)g_bgBrush;

    case WM_SET_STATUS: {
        auto* s = reinterpret_cast<std::wstring*>(lp);
        SetWindowTextW(g_hStatus, s->c_str());
        delete s;
        return 0;
    }

    case WM_SET_PROGRESS: {
        int pct = (int)(DWORD)wp;
        if (pct < 0) {
            ShowWindow(g_hProgress, SW_HIDE);
        } else {
            ShowWindow(g_hProgress, SW_SHOW);
            SendMessage(g_hProgress, PBM_SETPOS, pct, 0);
        }
        return 0;
    }

    case WM_DO_LAUNCH:
        if (wp == 0) DoLaunchGame(g_gameDir);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        DeleteObject(g_bgBrush);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---- WinMain ----

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Single-instance guard
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"WEXYRON_Launcher_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mtx); return 0; }

    WNDCLASSEXW wc    = { sizeof(wc) };
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = CreateSolidBrush(RGB(18, 18, 30));
    wc.lpszClassName  = L"WEXYRONLauncher";
    RegisterClassExW(&wc);

    // Size the window so the client area is exactly 380 × 110
    RECT wr = { 0, 0, 380, 110 };
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"WEXYRONLauncher", L"WEXYRON Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sx - ww) / 2, (sy - wh) / 2, ww, wh,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ReleaseMutex(mtx);
    CloseHandle(mtx);
    return (int)msg.wParam;
}
