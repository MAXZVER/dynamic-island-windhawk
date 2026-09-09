// Standalone host for the Dynamic Island overlay.
//
// Windhawk used to provide all of this: a process to live in, a settings UI, and
// a lifecycle. It did it by hooking the entry point of windhawk.exe and
// re-launching it as a host — machinery that exists only to get a process. A
// plain .exe already is that process, so all that is left is to start the mod's
// threads, keep a tray icon around, and shut down cleanly.
#include "wh_api.h"

#include <shellapi.h>

namespace {

constexpr wchar_t kWindowClass[] = L"DynamicIslandStandaloneHost";
constexpr wchar_t kInstanceMutex[] = L"DynamicIslandStandalone.SingleInstance";
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT kTimerReloadCheck = 1;

constexpr UINT kMenuOpenSettings = 100;
constexpr UINT kMenuReload = 101;
constexpr UINT kMenuExit = 102;

HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_tray{};
bool g_trayAdded = false;

void AddTrayIcon(HINSTANCE instance) {
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"Dynamic Island");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
    UNREFERENCED_PARAMETER(instance);
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuOpenSettings, L"Open settings file");
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
            if (LOWORD(lParam) == WM_RBUTTONUP ||
                LOWORD(lParam) == WM_LBUTTONUP) {
                ShowTrayMenu();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuOpenSettings:
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
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // The mod already guards against two islands fighting over the overlay;
    // keep that guarantee now that there is no engine to enforce it.
    HANDLE single = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 1;
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

    // Message-only window: it exists for the tray icon and the timer.
    g_hwnd = CreateWindowExW(0, kWindowClass, L"Dynamic Island", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, instance, nullptr);
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
