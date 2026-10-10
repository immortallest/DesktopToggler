// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Desktop Toggler contributors
//
// This file was written by an AI assistant (Claude, by Anthropic) in a
// conversation with a human maintainer, who tested it on real Windows.
// Please review it like any other third-party code before you trust it.
//
// ============================================================================
//  Desktop Toggler  v2.0   (complete rewrite)
// ============================================================================
//  Double-click an EMPTY spot on the desktop:
//      clean mode ON  -> desktop icons hidden  + taskbar auto-hide ON
//      clean mode OFF -> desktop icons shown   + taskbar auto-hide restored
//
//  * No window, no taskbar button, no tray icon.
//  * Right-click the desktop -> "Enable/Disable Desktop Toggler" turns the
//    double-click feature on/off. The choice is stored in the registry.
//  * No administrator rights needed (everything lives under HKCU / LocalAppData).
//
//  One EXE does everything:
//      DesktopToggler.exe                 install (or reinstall) + start
//      DesktopToggler.exe --install       same, explicit
//      DesktopToggler.exe --uninstall     remove EVERY version of this app
//      DesktopToggler-Uninstall.exe       same EXE under another name:
//                                         a file name containing "uninstall"
//                                         always means uninstall
//      add  --silent  to suppress all dialogs
//      (internal) --toggle-enabled        used by the desktop context menu
//
//  The uninstaller removes, for ALL versions (anything named DesktopToggle*):
//      running processes, autostart entries, Task-Manager startup flags,
//      desktop context-menu entries, settings keys, "Apps & features" entries
//      and the install folders in %LocalAppData%.
//
//  BUILD (mingw-w64, cross-compile from Linux; see build.sh and the README):
//      cd src
//      x86_64-w64-mingw32-windres -O coff -i DesktopToggler.rc -o ../build/res.o
//      x86_64-w64-mingw32-g++ -O2 -municode -mwindows -std=c++17 -static
//          -o ../build/DesktopToggler.exe DesktopToggler.cpp ../build/res.o
//          -luser32 -lshell32 -ladvapi32 -lole32 -lshlwapi
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cwchar>

// ---------------------------------------------------------------------------
//  Identity
// ---------------------------------------------------------------------------
#define APP_VERSION L"2.0.0"
static const wchar_t* const APP_TITLE     = L"Desktop Toggler";
static const wchar_t* const APP_ID        = L"DesktopToggler";
static const wchar_t* const EXE_FILE      = L"DesktopToggler.exe";
// Everything ever shipped by this app starts with this prefix
// (DesktopToggle, DesktopToggler, DesktopToggleHelper, ...).
static const wchar_t* const TRACE_PREFIX  = L"DesktopToggle";

static const wchar_t* const WND_CLASS     = L"DesktopToggler_Wnd_7C1E5A";
static const wchar_t* const OLD_WND_CLASS = L"DesktopToggle_Hidden_Wnd_9F3B2A"; // v1.x
static const wchar_t* const MUTEX_NAME    = L"Local\\DesktopToggler_Single_7C1E5A";
static const wchar_t* const MSG_NAME      = L"DesktopToggler_ToggleEnabled_7C1E5A";

#define REG_SETTINGS   L"Software\\DesktopToggler"
#define REG_RUN        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REG_STARTAPPR  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define REG_ADVANCED   L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
#define REG_VERBS      L"Software\\Classes\\DesktopBackground\\Shell"
#define REG_VERB       REG_VERBS L"\\DesktopToggler"
#define REG_UNINST     L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"

static const DWORD DOUBLECLICK_COOLDOWN_MS = 700;

// ---------------------------------------------------------------------------
//  Globals
// ---------------------------------------------------------------------------
static HINSTANCE        g_hInst = NULL;
static HWND             g_hWnd = NULL;
static HHOOK            g_hook = NULL;
static UINT             g_msgToggleEnabled = 0;
static bool             g_silent = false;

static bool             g_enabled = true;          // hook thread / main thread only
static volatile LONG    g_busy = 0;                // one toggle at a time
static DWORD            g_lastClickTime = 0;       // hook thread only
static POINT            g_lastClickPos = { -32000, -32000 };
static DWORD            g_cooldownUntil = 0;       // hook thread only

static bool             g_clean = false;           // worker thread (serialised by g_busy)
static bool             g_readsReliable = false;   // do OS reads of icon state track reality?

static bool             g_logOn = false;
static CRITICAL_SECTION g_logLock;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
static bool StartsWithI(const std::wstring& s, const wchar_t* prefix) {
    size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}
static bool ContainsI(const std::wstring& s, const wchar_t* needle) {
    return StrStrIW(s.c_str(), needle) != NULL;
}
static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
static bool SamePath(const std::wstring& a, const std::wstring& b) {
    return Lower(a) == Lower(b);
}
static bool Exists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static std::wstring GetExePath() {
    std::vector<wchar_t> buf(2048);
    DWORD n = GetModuleFileNameW(NULL, buf.data(), (DWORD)buf.size());
    return std::wstring(buf.data(), n);
}
static std::wstring LocalAppData() {
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf)))
        return buf;
    return L"";
}
static std::wstring InstallDir() {
    std::wstring l = LocalAppData();
    return l.empty() ? L"" : l + L"\\" + APP_ID;
}
static std::wstring InstallExe() {
    std::wstring d = InstallDir();
    return d.empty() ? L"" : d + L"\\" + EXE_FILE;
}
static int Msg(const wchar_t* text, UINT flags, int silentResult = IDOK) {
    if (g_silent) return silentResult;
    return MessageBoxW(NULL, text, APP_TITLE, flags | MB_SETFOREGROUND);
}

// ---------------------------------------------------------------------------
//  Diagnostic log  (%LocalAppData%\DesktopToggler\log.txt, capped at 128 KB)
//  Only the listener writes it, and only into an already existing folder.
// ---------------------------------------------------------------------------
static void Logf(const wchar_t* fmt, ...) {
    if (!g_logOn) return;
    wchar_t msg[700];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(msg, 699, fmt, ap);
    va_end(ap);
    msg[699] = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[820];
    _snwprintf(line, 819, L"%04d-%02d-%02d %02d:%02d:%02d.%03d  %ls\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds, msg);
    line[819] = 0;

    char utf8[2600];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL);
    if (n <= 1) return;

    std::wstring path = InstallDir() + L"\\log.txt";
    EnterCriticalSection(&g_logLock);
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart > 128 * 1024) {
            CloseHandle(h);
            h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, utf8, (DWORD)(n - 1), &w, NULL);
            CloseHandle(h);
        }
    }
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
//  Registry helpers (all HKCU)
// ---------------------------------------------------------------------------
static bool ReadDword(const wchar_t* sub, const wchar_t* name, DWORD& out) {
    DWORD size = sizeof(DWORD), type = 0, v = 0;
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, sub, name, RRF_RT_REG_DWORD, &type, &v, &size);
    if (r != ERROR_SUCCESS) return false;
    out = v;
    return true;
}
static void WriteDword(const wchar_t* sub, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}
static void WriteString(const std::wstring& sub, const wchar_t* name, const std::wstring& v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
static std::vector<std::wstring> EnumSubKeys(const wchar_t* sub) {
    std::vector<std::wstring> out;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD len = 512;
        LSTATUS r = RegEnumKeyExW(k, i, name, &len, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r == ERROR_SUCCESS) out.push_back(name);
    }
    RegCloseKey(k);
    return out;
}
struct RegValue { std::wstring name; std::wstring data; };
static std::vector<RegValue> EnumValues(const wchar_t* sub) {
    std::vector<RegValue> out;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[1024];
        DWORD nlen = 1024, type = 0, dlen = 8192;
        std::vector<BYTE> data(8192);
        LSTATUS r = RegEnumValueW(k, i, name, &nlen, NULL, &type, data.data(), &dlen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) continue;
        RegValue v;
        v.name = name;
        if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
            v.data.assign((const wchar_t*)data.data(), dlen / sizeof(wchar_t));
        out.push_back(v);
    }
    RegCloseKey(k);
    return out;
}
static std::wstring ReadDefaultString(const std::wstring& sub) {
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, sub.c_str(), NULL, RRF_RT_REG_SZ, NULL, buf, &size) == ERROR_SUCCESS)
        return buf;
    return L"";
}

// ---------------------------------------------------------------------------
//  Taskbar auto-hide (documented AppBar API; primary + secondary monitors)
// ---------------------------------------------------------------------------
static bool GetTaskbarAutoHide() {
    APPBARDATA abd;
    ZeroMemory(&abd, sizeof(abd));
    abd.cbSize = sizeof(abd);
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!abd.hWnd) return false;
    return (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) != 0;
}
static void SetAutoHideOne(HWND tray, bool on) {
    if (!tray) return;
    APPBARDATA abd;
    ZeroMemory(&abd, sizeof(abd));
    abd.cbSize = sizeof(abd);
    abd.hWnd = tray;
    LRESULT cur = SHAppBarMessage(ABM_GETSTATE, &abd);
    LRESULT target = on ? (cur | ABS_AUTOHIDE) : (cur & ~(LRESULT)ABS_AUTOHIDE);
    abd.lParam = target;
    SHAppBarMessage(ABM_SETSTATE, &abd);
    LRESULT verify = SHAppBarMessage(ABM_GETSTATE, &abd);   // known quirk: retry once
    if ((verify & ABS_AUTOHIDE) != (target & ABS_AUTOHIDE)) {
        abd.lParam = target;
        SHAppBarMessage(ABM_SETSTATE, &abd);
    }
}
static void SetTaskbarAutoHide(bool on) {
    SetAutoHideOne(FindWindowW(L"Shell_TrayWnd", NULL), on);
    HWND h = NULL;
    while ((h = FindWindowExW(NULL, h, L"Shell_SecondaryTrayWnd", NULL)) != NULL)
        SetAutoHideOne(h, on);
}

// ---------------------------------------------------------------------------
//  Desktop icons
//
//  The real Explorer command behind  View > "Show desktop icons"  is
//  WM_COMMAND with command id 0x7402, delivered to the desktop's
//  SHELLDLL_DefView window.  (0x7402 is a COMMAND ID, not a message number.)
//  Hiding makes the desktop's SysListView32 invisible, so its visibility is
//  also how we read the current state.  If the command ever fails to produce
//  an observable change, the ListView is shown/hidden directly as a fallback,
//  so the visible result is always right.
// ---------------------------------------------------------------------------
static HWND FindDefView() {
    HWND prog = FindWindowW(L"Progman", NULL);
    HWND dv = prog ? FindWindowExW(prog, NULL, L"SHELLDLL_DefView", NULL) : NULL;
    if (!dv) {                                  // Win10/11: DefView may live under a WorkerW
        HWND w = NULL;
        while ((w = FindWindowExW(NULL, w, L"WorkerW", NULL)) != NULL) {
            dv = FindWindowExW(w, NULL, L"SHELLDLL_DefView", NULL);
            if (dv) break;
        }
    }
    return dv;
}
static HWND FindDesktopListView() {
    HWND dv = FindDefView();
    return dv ? FindWindowExW(dv, NULL, L"SysListView32", NULL) : NULL;
}
static bool AreIconsHidden() {
    HWND lv = FindDesktopListView();
    if (lv) return !IsWindowVisible(lv);
    DWORD v = 0;                                // Explorer not ready yet: use its setting
    return ReadDword(REG_ADVANCED, L"HideIcons", v) && v == 1;
}
static bool SendIconsCommand(HWND target) {
    if (!target) return false;
    DWORD_PTR res = 0;
    return SendMessageTimeoutW(target, WM_COMMAND, 0x7402, 0,
                               SMTO_ABORTIFHUNG, 1500, &res) != 0;
}
static bool WaitIconsState(bool wantHidden, DWORD ms) {
    for (DWORD t = 0; t <= ms; t += 40) {
        if (AreIconsHidden() == wantHidden) return true;
        Sleep(40);
    }
    return false;
}
static bool SetIconsHidden(bool wantHidden) {
    if (AreIconsHidden() == wantHidden) {
        Logf(L"icons already %ls", wantHidden ? L"hidden" : L"shown");
        return true;
    }
    HWND targets[2] = { FindDefView(), FindWindowW(L"Progman", NULL) };
    const wchar_t* names[2] = { L"DefView", L"Progman" };
    for (int i = 0; i < 2; ++i) {
        if (!targets[i]) continue;
        bool sent = SendIconsCommand(targets[i]);
        bool ok = WaitIconsState(wantHidden, 800);
        Logf(L"icons: WM_COMMAND 0x7402 -> %ls (sent=%d) observed=%d", names[i], sent, ok);
        if (ok) {
            if (i == 0) g_readsReliable = true;
            return true;
        }
    }
    HWND lv = FindDesktopListView();
    if (lv) {
        ShowWindow(lv, wantHidden ? SW_HIDE : SW_SHOW);
        g_readsReliable = false;
        bool ok = (AreIconsHidden() == wantHidden);
        Logf(L"icons: fallback ShowWindow(%ls) ok=%d", wantHidden ? L"HIDE" : L"SHOW", ok);
        return ok;
    }
    Logf(L"icons: desktop list view not found");
    return false;
}

// ---------------------------------------------------------------------------
//  Clean mode
// ---------------------------------------------------------------------------
static void ToggleCleanMode() {
    bool cur = g_readsReliable ? AreIconsHidden() : g_clean;
    bool goClean = !cur;
    Logf(L"toggle: %ls -> %ls (reliableReads=%d)", cur ? L"clean" : L"normal",
         goClean ? L"clean" : L"normal", g_readsReliable);

    DWORD prevAuto = 0;
    if (goClean) {
        prevAuto = GetTaskbarAutoHide() ? 1 : 0;
        WriteDword(REG_SETTINGS, L"PrevAutoHide", prevAuto);
    } else {
        ReadDword(REG_SETTINGS, L"PrevAutoHide", prevAuto);
    }

    SetIconsHidden(goClean);
    SetTaskbarAutoHide(goClean ? true : (prevAuto != 0));
    g_clean = goClean;
    WriteDword(REG_SETTINGS, L"CleanActive", goClean ? 1 : 0);
    Logf(L"toggle done: clean=%d taskbarAutoHide=%d", g_clean, GetTaskbarAutoHide());
}

// ---------------------------------------------------------------------------
//  Double-click handling (runs on a pool thread, never inside the hook)
// ---------------------------------------------------------------------------
static std::wstring ClassOf(HWND h) {
    wchar_t c[128] = { 0 };
    if (h) GetClassNameW(h, c, 127);
    return c;
}

// 1 = on an icon, 0 = empty space, -1 = could not tell
static int HitTestDesktopIcon(HWND lv, POINT screenPt) {
    POINT cp = screenPt;
    ScreenToClient(lv, &cp);
    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    if (!pid) return -1;
    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return -1;
    int result = -1;
    LPVOID remote = VirtualAllocEx(proc, NULL, sizeof(LVHITTESTINFO), MEM_COMMIT, PAGE_READWRITE);
    if (remote) {
        LVHITTESTINFO hti;
        ZeroMemory(&hti, sizeof(hti));
        hti.pt = cp;
        DWORD_PTR r = 0;
        if (WriteProcessMemory(proc, remote, &hti, sizeof(hti), NULL) &&
            SendMessageTimeoutW(lv, LVM_HITTEST, 0, (LPARAM)remote, SMTO_ABORTIFHUNG, 1000, &r) &&
            ReadProcessMemory(proc, remote, &hti, sizeof(hti), NULL)) {
            result = (hti.iItem != -1) ? 1 : 0;
        }
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    }
    CloseHandle(proc);
    return result;
}

static void HandleDoubleClick(POINT pt) {
    HWND hit = WindowFromPoint(pt);
    if (!hit) return;
    std::wstring rootCls = ClassOf(GetAncestor(hit, GA_ROOT));
    if (rootCls != L"Progman" && rootCls != L"WorkerW") return;     // not the desktop

    std::wstring hitCls = ClassOf(hit);
    Logf(L"double-click on desktop at (%ld,%ld) hit=%ls root=%ls", pt.x, pt.y,
         hitCls.c_str(), rootCls.c_str());

    // While icons are hidden the ListView is invisible, so the click lands on
    // DefView/Progman/WorkerW instead: that is empty space by definition.
    if (hitCls == L"SysListView32") {
        int r = HitTestDesktopIcon(hit, pt);
        Logf(L"hit test: %d (1=icon 0=empty -1=unknown)", r);
        if (r != 0) return;
    }
    ToggleCleanMode();
}

static DWORD WINAPI WorkerProc(LPVOID param) {
    POINT pt = *(POINT*)param;
    delete (POINT*)param;
    HandleDoubleClick(pt);
    InterlockedExchange(&g_busy, 0);
    return 0;
}

// ---------------------------------------------------------------------------
//  Low-level mouse hook: stays tiny and fast (Windows drops slow hooks).
//  It only recognises a double click (same rule as Windows) and hands off.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN && g_enabled) {
        const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lParam;
        DWORD now = ms->time;
        POINT pt = ms->pt;

        // Cooldown after an acted-on double-click so an extra fast click
        // (or switch bounce) can't fire a second toggle that undoes the first.
        if (g_cooldownUntil != 0 && (LONG)(now - g_cooldownUntil) < 0)
            return CallNextHookEx(g_hook, nCode, wParam, lParam);

        int halfW = std::max(2, GetSystemMetrics(SM_CXDOUBLECLK) / 2);
        int halfH = std::max(2, GetSystemMetrics(SM_CYDOUBLECLK) / 2);
        bool isDouble = false;
        if (g_lastClickTime != 0 && (now - g_lastClickTime) <= GetDoubleClickTime() &&
            abs((int)(pt.x - g_lastClickPos.x)) <= halfW &&
            abs((int)(pt.y - g_lastClickPos.y)) <= halfH) {
            isDouble = true;
            g_lastClickTime = 0;
        } else {
            g_lastClickTime = now;
            g_lastClickPos = pt;
        }

        if (isDouble && InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
            g_cooldownUntil = now + DOUBLECLICK_COOLDOWN_MS;
            POINT* p = new (std::nothrow) POINT(pt);
            if (!p || !QueueUserWorkItem(WorkerProc, p, WT_EXECUTELONGFUNCTION)) {
                delete p;
                InterlockedExchange(&g_busy, 0);
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}
static void InstallHook() {
    if (!g_hook && g_enabled) {
        g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, g_hInst, 0);
        Logf(L"mouse hook %ls", g_hook ? L"installed" : L"FAILED");
    }
}
static void RemoveHook() {
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
        Logf(L"mouse hook removed");
    }
}

// ---------------------------------------------------------------------------
//  Context menu entry + enable/disable
// ---------------------------------------------------------------------------
static void UpdateContextMenu(bool enabled, const std::wstring& exe) {
    WriteString(REG_VERB, NULL, enabled ? L"Disable Desktop Toggler" : L"Enable Desktop Toggler");
    WriteString(std::wstring(REG_VERB) + L"\\command", NULL, L"\"" + exe + L"\" --toggle-enabled");
}
static bool LoadEnabled() {
    DWORD v = 1;
    ReadDword(REG_SETTINGS, L"Enabled", v);
    return v != 0;
}
static void ToggleEnabledFeature() {
    g_enabled = !g_enabled;
    WriteDword(REG_SETTINGS, L"Enabled", g_enabled ? 1 : 0);
    UpdateContextMenu(g_enabled, GetExePath());
    Logf(L"feature %ls", g_enabled ? L"ENABLED" : L"DISABLED");
    if (g_enabled) InstallHook(); else RemoveHook();
}
static HWND FindListenerWindow() {
    HWND h = FindWindowExW(HWND_MESSAGE, NULL, WND_CLASS, NULL);
    return h ? h : FindWindowExW(NULL, NULL, WND_CLASS, NULL);
}
static void HelperToggleEnabled() {          // run by Explorer from the context menu
    UINT m = RegisterWindowMessageW(MSG_NAME);
    HWND h = FindListenerWindow();
    if (h) { PostMessageW(h, m, 0, 0); return; }
    bool en = !LoadEnabled();                // listener not running: flip setting, restart it
    WriteDword(REG_SETTINGS, L"Enabled", en ? 1 : 0);
    std::wstring exe = InstallExe();
    UpdateContextMenu(en, exe);
    if (Exists(exe)) ShellExecuteW(NULL, L"open", exe.c_str(), NULL, InstallDir().c_str(), SW_HIDE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_msgToggleEnabled && m == g_msgToggleEnabled) { ToggleEnabledFeature(); return 0; }
    if (m == WM_CLOSE)   { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static int RunListener() {
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        Msg(L"Desktop Toggler is already running in the background.\n\n"
            L"Right-click the desktop for the Enable/Disable option.", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    g_logOn = true;
    g_msgToggleEnabled = RegisterWindowMessageW(MSG_NAME);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = WND_CLASS;
    RegisterClassW(&wc);
    // Message-only window: it can never be shown, has no taskbar button and no
    // tray icon exists anywhere in this program. It only receives our own
    // "toggle enable/disable" message from the context-menu command.
    g_hWnd = CreateWindowExW(0, WND_CLASS, APP_TITLE, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, g_hInst, NULL);
    Logf(L"control window created: hwnd=%p error=%lu", (void*)g_hWnd, g_hWnd ? 0UL : GetLastError());

    g_enabled = LoadEnabled();
    g_clean = AreIconsHidden();
    UpdateContextMenu(g_enabled, GetExePath());
    Logf(L"==== Desktop Toggler %ls started: enabled=%d iconsHidden=%d taskbarAutoHide=%d ====",
         APP_VERSION, g_enabled, g_clean, GetTaskbarAutoHide());
    InstallHook();

    MSG msg;
    BOOL r;
    while ((r = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (r == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    RemoveHook();
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ===========================================================================
//  INSTALLER / UNINSTALLER
// ===========================================================================
static std::vector<std::wstring> FindTraceFolders() {
    std::vector<std::wstring> out;
    std::wstring base = LocalAppData();
    if (base.empty()) return out;
    WIN32_FIND_DATAW fd;
    std::wstring pat = base + L"\\" + TRACE_PREFIX + L"*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            out.push_back(base + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
static std::vector<std::wstring> FindTraceSoftwareKeys() {
    std::vector<std::wstring> out;
    for (auto& s : EnumSubKeys(L"Software"))
        if (StartsWithI(s, TRACE_PREFIX)) out.push_back(L"Software\\" + s);
    return out;
}
static std::vector<std::wstring> FindTraceVerbKeys() {
    std::vector<std::wstring> out;
    for (auto& s : EnumSubKeys(REG_VERBS)) {
        std::wstring path = std::wstring(REG_VERBS) + L"\\" + s;
        if (StartsWithI(s, TRACE_PREFIX) || ContainsI(ReadDefaultString(path + L"\\command"), TRACE_PREFIX))
            out.push_back(path);
    }
    return out;
}
static std::vector<std::wstring> FindTraceUninstallKeys() {
    std::vector<std::wstring> out;
    for (auto& s : EnumSubKeys(REG_UNINST))
        if (StartsWithI(s, TRACE_PREFIX)) out.push_back(std::wstring(REG_UNINST) + L"\\" + s);
    return out;
}
static std::vector<std::wstring> FindTraceRunValues() {
    std::vector<std::wstring> out;
    for (auto& v : EnumValues(REG_RUN))
        if (StartsWithI(v.name, TRACE_PREFIX) || ContainsI(v.data, TRACE_PREFIX)) out.push_back(v.name);
    return out;
}
static std::vector<std::wstring> FindTraceApprovedValues() {
    std::vector<std::wstring> out;
    for (auto& v : EnumValues(REG_STARTAPPR))
        if (StartsWithI(v.name, TRACE_PREFIX)) out.push_back(v.name);
    return out;
}
static std::vector<HWND> FindTraceWindows() {
    std::vector<HWND> out;
    const wchar_t* classes[2] = { WND_CLASS, OLD_WND_CLASS };
    HWND parents[2] = { NULL, HWND_MESSAGE };   // normal top-level and message-only
    for (auto c : classes) {
        for (HWND parent : parents) {
            HWND h = NULL;
            while ((h = FindWindowExW(parent, h, c, NULL)) != NULL) out.push_back(h);
        }
    }
    return out;
}
static std::vector<DWORD> FindTracePids() {
    std::vector<DWORD> out;
    DWORD self = GetCurrentProcessId();
    auto add = [&](DWORD pid) {
        if (pid && pid != self && std::find(out.begin(), out.end(), pid) == out.end()) out.push_back(pid);
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (StartsWithI(pe.szExeFile, TRACE_PREFIX)) add(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    for (HWND w : FindTraceWindows()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        add(pid);
    }
    return out;
}
static bool AnyTraceExists() {
    return !FindTraceFolders().empty() || !FindTraceSoftwareKeys().empty() ||
           !FindTraceVerbKeys().empty() || !FindTraceUninstallKeys().empty() ||
           !FindTraceRunValues().empty() || !FindTracePids().empty();
}

static void StopAllInstances() {
    for (HWND w : FindTraceWindows()) PostMessageW(w, WM_CLOSE, 0, 0);    // polite first
    for (DWORD pid : FindTracePids()) {
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        if (!h) continue;
        if (WaitForSingleObject(h, 1500) != WAIT_OBJECT_0) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 3000);
        }
        CloseHandle(h);
    }
}

static bool DeleteTree(const std::wstring& dir) {
    bool ok = true;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(p.c_str());
                else if (!DeleteTree(p)) ok = false;
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(p.c_str())) ok = false;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryW(dir.c_str())) ok = false;
    return ok;
}

// A running EXE can't delete itself, so anything left over is removed by a
// hidden cmd.exe a couple of seconds after we exit.
static void ScheduleDelayedDelete(const std::vector<std::wstring>& dirs) {
    if (dirs.empty()) return;
    std::wstring cmd = L"cmd.exe /D /C ping -n 3 127.0.0.1 >nul";
    for (auto& d : dirs) cmd += L" & rmdir /S /Q \"" + d + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
                       NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Removes every trace of every version. Returns folders that couldn't be
// deleted yet (e.g. because this process is running from inside one).
static std::vector<std::wstring> RemoveAllVersions() {
    StopAllInstances();

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        for (auto& n : FindTraceRunValues()) RegDeleteValueW(k, n.c_str());
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_STARTAPPR, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        for (auto& n : FindTraceApprovedValues()) RegDeleteValueW(k, n.c_str());
        RegCloseKey(k);
    }
    for (auto& p : FindTraceVerbKeys())      SHDeleteKeyW(HKEY_CURRENT_USER, p.c_str());
    for (auto& p : FindTraceSoftwareKeys())  SHDeleteKeyW(HKEY_CURRENT_USER, p.c_str());
    for (auto& p : FindTraceUninstallKeys()) SHDeleteKeyW(HKEY_CURRENT_USER, p.c_str());

    std::vector<std::wstring> leftovers;
    for (auto& d : FindTraceFolders()) {
        DeleteTree(d);
        if (Exists(d)) leftovers.push_back(d);
    }
    return leftovers;
}

// If the app left the desktop in clean mode, put it back before removal.
static void RestoreDesktopIfWeChangedIt() {
    DWORD active = 0;
    if (!ReadDword(REG_SETTINGS, L"CleanActive", active) || active != 1) return;
    DWORD prev = 0;
    ReadDword(REG_SETTINGS, L"PrevAutoHide", prev);
    SetIconsHidden(false);
    SetTaskbarAutoHide(prev != 0);
}

static void RegisterUninstallEntry(const std::wstring& exe, const std::wstring& dir) {
    std::wstring k = std::wstring(REG_UNINST) + L"\\" + APP_ID;
    WriteString(k, L"DisplayName", APP_TITLE);
    WriteString(k, L"DisplayVersion", APP_VERSION);
    WriteString(k, L"Publisher", APP_TITLE);
    WriteString(k, L"InstallLocation", dir);
    WriteString(k, L"DisplayIcon", exe);
    WriteString(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    WriteString(k, L"QuietUninstallString", L"\"" + exe + L"\" --uninstall --silent");
    WriteDword(k.c_str(), L"NoModify", 1);
    WriteDword(k.c_str(), L"NoRepair", 1);
}

static int DoInstall() {
    std::wstring self = GetExePath(), dir = InstallDir(), exe = InstallExe();
    if (dir.empty()) {
        Msg(L"Could not locate your AppData folder.", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (SamePath(self, exe)) {
        Msg(L"This is already the installed copy. Run the downloaded file to reinstall.",
            MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    DWORD enabled = 1;                                  // keep the user's on/off choice
    ReadDword(REG_SETTINGS, L"Enabled", enabled);

    RemoveAllVersions();                                // clean slate: all older versions go

    CreateDirectoryW(dir.c_str(), NULL);
    if (!CopyFileW(self.c_str(), exe.c_str(), FALSE)) {
        Msg(L"Installation failed: could not copy the program into AppData.", MB_OK | MB_ICONERROR);
        return 1;
    }
    WriteDword(REG_SETTINGS, L"Enabled", enabled);
    WriteString(REG_RUN, APP_ID, L"\"" + exe + L"\"");
    RegisterUninstallEntry(exe, dir);
    UpdateContextMenu(enabled != 0, exe);

    ShellExecuteW(NULL, L"open", exe.c_str(), NULL, dir.c_str(), SW_HIDE);
    Msg(L"Desktop Toggler is installed and running.\n\n"
        L"Double-click an empty spot on the desktop to hide/show icons and the taskbar.\n"
        L"Right-click the desktop for Enable/Disable (Windows 11: \"Show more options\").\n"
        L"Uninstall: Settings > Apps, or run this file with --uninstall.",
        MB_OK | MB_ICONINFORMATION);
    return 0;
}

static int DoUninstall(bool askConfirm) {
    if (askConfirm &&
        Msg(L"Completely remove Desktop Toggler and ALL older versions of it?\n\n"
            L"Running copies will be stopped, and autostart, menu entries,\n"
            L"settings and installed files will be deleted.",
            MB_YESNO | MB_ICONQUESTION, IDYES) != IDYES)
        return 0;

    RestoreDesktopIfWeChangedIt();
    std::vector<std::wstring> leftovers = RemoveAllVersions();

    std::wstring self = GetExePath();
    for (auto& d : FindTraceFolders())
        if (std::find(leftovers.begin(), leftovers.end(), d) == leftovers.end() && Exists(d))
            leftovers.push_back(d);

    Msg(L"Desktop Toggler (all versions) has been removed.\n\n"
        L"If you still have a downloaded copy of the EXE somewhere, you can delete it.",
        MB_OK | MB_ICONINFORMATION);
    ScheduleDelayedDelete(leftovers);       // after the dialog, so our EXE is free by then
    return 0;
}

static int RunInstallerInteractive(bool forceInstall) {
    if (!forceInstall && AnyTraceExists()) {
        int r = Msg(L"Desktop Toggler (or an older version) is already installed.\n\n"
                    L"Yes  = Reinstall cleanly (all older versions removed first)\n"
                    L"No   = Uninstall everything\n"
                    L"Cancel = Do nothing",
                    MB_YESNOCANCEL | MB_ICONQUESTION, IDYES);
        if (r == IDNO) return DoUninstall(false);
        if (r != IDYES) return 0;
    }
    return DoInstall();
}

// ---------------------------------------------------------------------------
//  Entry point
// ---------------------------------------------------------------------------
static void EnableDpiAwareness() {          // manifest already asks for it; this is a safety net
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtxFn f = u ? (SetCtxFn)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (f && f((HANDLE)-4)) return;         // PER_MONITOR_AWARE_V2
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    InitializeCriticalSection(&g_logLock);
    EnableDpiAwareness();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    bool optToggle = false, optUninstall = false, optInstall = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"--toggle-enabled")) optToggle = true;
        else if (!_wcsicmp(argv[i], L"--uninstall"))  optUninstall = true;
        else if (!_wcsicmp(argv[i], L"--install"))    optInstall = true;
        else if (!_wcsicmp(argv[i], L"--silent"))     g_silent = true;
    }
    if (argv) LocalFree(argv);

    std::wstring self = GetExePath();
    if (ContainsI(PathFindFileNameW(self.c_str()), L"uninstall")) optUninstall = true;

    int rc = 0;
    if (optToggle)         { HelperToggleEnabled(); }
    else if (optUninstall) { rc = DoUninstall(!g_silent); }
    else if (optInstall)   { rc = RunInstallerInteractive(true); }
    else if (SamePath(self, InstallExe())) { rc = RunListener(); }
    else                   { rc = RunInstallerInteractive(false); }

    CoUninitialize();
    return rc;
}
