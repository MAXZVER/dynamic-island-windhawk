// Standalone host for the Dynamic Island overlay.
//
// Windhawk used to provide all of this: a process to live in, a settings UI, and
// a lifecycle. It did it by hooking the entry point of windhawk.exe and
// re-launching it as a host — machinery that exists only to get a process. A
// plain .exe already is that process, so all that is left is to start the mod's
// threads, keep a tray icon around, and shut down cleanly.
#include "wh_api.h"

#include <shellapi.h>
// Known folders, IShellLink and SHCreateDirectoryExW, for the self-install.
#include <shlobj.h>

#include <cmath>
#include <string>

// Implemented in settings_ui.cpp.
void ShowSettingsWindow(HINSTANCE instance);
HWND SettingsWindowHandle();

namespace {

constexpr wchar_t kWindowClass[] = L"DynamicIslandStandaloneHost";
constexpr wchar_t kInstanceMutex[] = L"DynamicIslandStandalone.SingleInstance";
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT kTimerReloadCheck = 1;

constexpr UINT kMenuSettings = 100;
constexpr UINT kMenuOpenFile = 101;
constexpr UINT kMenuReload = 102;
constexpr UINT kMenuAutostart = 103;
constexpr UINT kMenuExit = 104;

constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"DynamicIsland";

HINSTANCE g_instance = nullptr;
// Lets a second launch of the exe open the settings of the running copy
// instead of silently doing nothing.
UINT g_showSettingsMsg = 0;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;

// The tray needs an icon of our own; LoadIcon(IDI_APPLICATION) gives the
// generic window placeholder, which says nothing about what is running.
// Drawing the island's own shape avoids carrying an .ico and a resource
// compiler around for 32x32 pixels.
HICON CreateIslandIcon() {
    constexpr int kSize = 32;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = kSize;
    header.bV5Height = -kSize;  // top-down
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP colour = CreateDIBSection(screen, (BITMAPINFO*)&header,
                                      DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!colour || !bits) {
        return nullptr;
    }

    // A pill: 24x11 centred, fully rounded ends. Filled with the default
    // accent so it stays legible on both a light and a dark taskbar.
    const float halfW = 12.0f;
    const float halfH = 5.5f;
    const float radius = halfH;
    auto* pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = std::abs(x + 0.5f - kSize / 2.0f) - (halfW - radius);
            const float dy = std::abs(y + 0.5f - kSize / 2.0f) - (halfH - radius);
            const float ax = dx > 0.0f ? dx : 0.0f;
            const float ay = dy > 0.0f ? dy : 0.0f;
            const float dist = std::sqrt(ax * ax + ay * ay) - radius;

            // One pixel of falloff so the edge is not stair-stepped.
            float coverage = 0.5f - dist;
            coverage = coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);

            const BYTE a = static_cast<BYTE>(coverage * 255.0f + 0.5f);
            // Premultiplied, which is what the shell expects for 32-bit icons.
            const BYTE r = static_cast<BYTE>(0x4c * a / 255);
            const BYTE g = static_cast<BYTE>(0xc9 * a / 255);
            const BYTE b = static_cast<BYTE>(0xf0 * a / 255);
            pixels[y * kSize + x] =
                (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | b;
        }
    }

    HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colour;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(colour);
    DeleteObject(mask);
    return icon;
}

void AddTrayIcon(HINSTANCE instance) {
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = CreateIslandIcon();
    if (!g_tray.hIcon) {
        g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(g_tray.szTip, L"Dynamic Island");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    UNREFERENCED_PARAMETER(instance);
}

bool AutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const bool present =
        RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) ==
        ERROR_SUCCESS;
    RegCloseKey(key);
    return present;
}

// Per-user Run key: no elevation, and it follows the account rather than the
// machine. The path is quoted so a space in it cannot split the command.
void SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    if (enable) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) {
            const std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
            RegSetValueExW(
                key, kRunValue, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(quoted.c_str()),
                static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, kRunValue);
    }

    RegCloseKey(key);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0),
                kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuOpenFile, L"Open settings file");
    AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
    switch (message) {
        case WM_APP_TRAY:
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (LOWORD(lParam) == WM_LBUTTONUP) {
                ShowSettingsWindow(g_instance);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuSettings:
                    ShowSettingsWindow(g_instance);
                    return 0;
                case kMenuAutostart:
                    SetAutostart(!AutostartEnabled());
                    return 0;
                case kMenuOpenFile:
                    ShellExecuteW(nullptr, L"open", whshim::ConfigPath(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case kMenuReload:
                    WhTool_ModSettingsChanged();
                    return 0;
                case kMenuExit:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;

        case WM_TIMER:
            // Editing the settings file is the settings UI here, so watch it.
            if (wParam == kTimerReloadCheck && whshim::ReloadIfChanged()) {
                WhTool_ModSettingsChanged();
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            if (message == g_showSettingsMsg && g_showSettingsMsg) {
                ShowSettingsWindow(g_instance);
                return 0;
            }
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ── Self-install ─────────────────────────────────────────────────────────────
// One file is the whole product. Run it from a Downloads folder and it offers
// to put itself where it belongs and start with Windows; -Install does that
// without asking, -Uninstall takes it back out.
//
// It writes a Startup shortcut rather than the Run key that "Start with
// Windows" in the tray menu uses: a shortcut is visible in Explorer and can be
// removed without a registry editor. The two are independent - switching both
// on would launch two islands.

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        result = raw;
    }
    if (raw) CoTaskMemFree(raw);
    return result;
}

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return buf;
}

std::wstring InstallDir()   { return KnownFolder(FOLDERID_LocalAppData) + L"\\DynamicIsland"; }
std::wstring InstalledExe() { return InstallDir() + L"\\DynamicIsland.exe"; }
std::wstring StartupLink()  { return KnownFolder(FOLDERID_Startup) + L"\\Dynamic Island.lnk"; }

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool RunningFromInstallDir() { return SamePath(SelfPath(), InstalledExe()); }

bool WriteShortcut(const std::wstring& link, const std::wstring& target,
                   const std::wstring& workDir) {
    IShellLinkW* shortcut = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&shortcut)))) {
        return false;
    }
    shortcut->SetPath(target.c_str());
    shortcut->SetWorkingDirectory(workDir.c_str());
    shortcut->SetDescription(L"Dynamic Island for Windows");

    IPersistFile* file = nullptr;
    bool saved = false;
    if (SUCCEEDED(shortcut->QueryInterface(IID_IPersistFile,
                                           reinterpret_cast<void**>(&file)))) {
        saved = SUCCEEDED(file->Save(link.c_str(), TRUE));
        file->Release();
    }
    shortcut->Release();
    return saved;
}

// A copy already running out of the install folder holds its own .exe open, so
// ask it to quit before overwriting the file.
void StopInstalledCopy() {
    HWND running = FindWindowW(kWindowClass, nullptr);
    if (!running) return;
    PostMessageW(running, WM_CLOSE, 0, 0);
    for (int i = 0; i < 50 && IsWindow(running); ++i) {
        Sleep(100);
    }
}

bool InstallSelf() {
    const std::wstring dir = InstallDir();
    const std::wstring dst = InstalledExe();

    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);

    if (!RunningFromInstallDir()) {
        StopInstalledCopy();
        if (!CopyFileW(SelfPath().c_str(), dst.c_str(), FALSE)) {
            MessageBoxW(nullptr, L"Could not copy the executable into your profile.",
                        L"Dynamic Island", MB_ICONERROR);
            return false;
        }
    }

    WriteShortcut(StartupLink(), dst, dir);
    ShellExecuteW(nullptr, L"open", dst.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    return true;
}

void UninstallSelf() {
    StopInstalledCopy();
    DeleteFileW(StartupLink().c_str());

    // and the Run entry, in case "Start with Windows" was ever ticked
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
    }

    // The folder cannot remove itself while this exe runs out of it.
    const std::wstring command =
        L"/c timeout /t 2 /nobreak >nul & rmdir /s /q \"" + InstallDir() + L"\"";
    ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_HIDE);
}

// Returns true when the command line was the whole job and the process should
// stop rather than go on to show an island.
bool HandleSetupCommandLine() {
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) return false;

    bool install = false, uninstall = false, noPrompt = false;
    for (int i = 1; i < count; ++i) {
        std::wstring arg = argv[i];
        while (!arg.empty() && (arg.front() == L'-' || arg.front() == L'/')) arg.erase(0, 1);
        if (_wcsicmp(arg.c_str(), L"install") == 0)   install = true;
        if (_wcsicmp(arg.c_str(), L"uninstall") == 0) uninstall = true;
        if (_wcsicmp(arg.c_str(), L"noprompt") == 0)  noPrompt = true;
    }
    LocalFree(argv);

    if (uninstall) {
        UninstallSelf();
        MessageBoxW(nullptr, L"Dynamic Island has been removed.", L"Dynamic Island",
                    MB_ICONINFORMATION);
        return true;
    }
    if (install) {
        InstallSelf();
        return true;
    }

    if (!noPrompt && !RunningFromInstallDir() &&
        GetFileAttributesW(InstalledExe().c_str()) == INVALID_FILE_ATTRIBUTES) {
        const int answer = MessageBoxW(
            nullptr,
            L"Install Dynamic Island for your user account and start it with Windows?\r\n\r\n"
            L"It will be copied to your local app data folder. No administrator rights are "
            L"needed, and running this file with -Uninstall removes it again.",
            L"Dynamic Island", MB_ICONQUESTION | MB_YESNOCANCEL);
        if (answer == IDCANCEL) return true;
        if (answer == IDYES) {
            InstallSelf();
            return true;
        }
        // IDNO: carry on running from wherever this copy sits
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;

    // Setup runs before the single-instance mutex: installing means replacing
    // the copy that already holds it.
    const HRESULT hrSetup = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool handled = HandleSetupCommandLine();
    if (SUCCEEDED(hrSetup)) CoUninitialize();
    if (handled) return 0;

    // The mod already guards against two islands fighting over the overlay;
    // keep that guarantee now that there is no engine to enforce it.
    g_showSettingsMsg = RegisterWindowMessageW(L"DynamicIsland.ShowSettings");

    HANDLE single = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        // Already running: ask that copy to show its settings, then step aside.
        if (g_showSettingsMsg) {
            PostMessageW(HWND_BROADCAST, g_showSettingsMsg, 0, 0);
        }
        return 0;
    }

    // The tray, the shell menu and SHGetKnownFolderPath all expect COM on this
    // thread. The mod's own threads initialize their own apartments.
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!whshim::Initialize()) {
        MessageBoxW(nullptr, L"Could not create the settings folder.",
                    L"Dynamic Island", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // Hidden, but a real top-level window rather than message-only: those
    // receive no broadcasts, and the second-instance handoff below needs one.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Dynamic Island",
                             WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                             nullptr);
    if (!g_hwnd) {
        return 1;
    }

    if (!WhTool_ModInit()) {
        MessageBoxW(nullptr, L"The island failed to start.", L"Dynamic Island",
                    MB_ICONERROR);
        DestroyWindow(g_hwnd);
        return 1;
    }

    AddTrayIcon(instance);
    SetTimer(g_hwnd, kTimerReloadCheck, 1000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Without this the settings window gets no Tab, Enter or Esc.
        HWND settings = SettingsWindowHandle();
        if (settings && IsDialogMessageW(settings, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(g_hwnd, kTimerReloadCheck);
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
    }

    // Stops the mod's threads and destroys the overlay window. Unlike the
    // Windhawk build this returns instead of calling ExitProcess, so the
    // shutdown path actually runs.
    WhTool_ModUninit();

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    ReleaseMutex(single);
    CloseHandle(single);
    return 0;
}
