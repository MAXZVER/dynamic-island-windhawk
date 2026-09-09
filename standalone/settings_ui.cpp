// The settings window.
//
// Windhawk built one of these from the mod's YAML; without it the settings
// were a text file. The same YAML is parsed by gen_island.py into the table
// included below, so the window is still generated from the mod rather than
// maintained by hand: add a setting to the mod, regenerate, and it appears
// here with its caption, description and choices.
#include "wh_api.h"

#include <commctrl.h>

#include <string>
#include <vector>

namespace {

struct SettingOption {
    const wchar_t* value;
    const wchar_t* label;
};

struct SettingMeta {
    const wchar_t* key;
    const wchar_t* group;
    const wchar_t* name;
    const wchar_t* desc;
    const wchar_t* def;
    int type;  // 0 text, 1 bool, 2 int, 3 enum
    const SettingOption* options;
    int optionCount;
};

#include "settings_meta.inc"

constexpr int kTypeText = 0;
constexpr int kTypeBool = 1;
constexpr int kTypeInt = 2;
constexpr int kTypeEnum = 3;

constexpr wchar_t kClass[] = L"DynamicIslandSettings";
constexpr int kIdFirstControl = 1000;
constexpr int kIdSave = 1;
constexpr int kIdCancel = 2;
constexpr int kIdTabs = 3;

constexpr int kWidth = 720;
constexpr int kTopPad = 60;      // below the tab header
constexpr int kBottomPad = 60;   // the button strip
constexpr int kRowHeight = 34;
constexpr int kLabelWidth = 250;
constexpr int kControlWidth = 380;

int g_height = 620;  // computed from the largest group
HWND g_settingsWnd = nullptr;
HWND g_tabs = nullptr;
HWND g_tooltip = nullptr;
HFONT g_font = nullptr;
std::vector<HWND> g_controls;   // one per setting, parallel to kSettings
std::vector<HWND> g_labels;
std::vector<std::wstring> g_groups;

int GroupIndex(const wchar_t* group) {
    for (size_t i = 0; i < g_groups.size(); ++i) {
        if (g_groups[i] == group) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AddTooltip(HWND control, const wchar_t* text) {
    if (!g_tooltip || !text || !*text) {
        return;
    }
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = g_settingsWnd;
    info.uId = reinterpret_cast<UINT_PTR>(control);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

void ShowGroup(int index) {
    for (size_t i = 0; i < g_controls.size(); ++i) {
        const bool visible = GroupIndex(kSettings[i].group) == index;
        ShowWindow(g_controls[i], visible ? SW_SHOW : SW_HIDE);
        ShowWindow(g_labels[i], visible ? SW_SHOW : SW_HIDE);
    }
}

void LoadValues() {
    for (size_t i = 0; i < g_controls.size(); ++i) {
        const SettingMeta& meta = kSettings[i];
        const std::wstring value = whshim::GetSetting(meta.key);

        switch (meta.type) {
            case kTypeBool:
                SendMessageW(g_controls[i], BM_SETCHECK,
                             _wcsicmp(value.c_str(), L"true") == 0 ||
                                     value == L"1"
                                 ? BST_CHECKED
                                 : BST_UNCHECKED,
                             0);
                break;

            case kTypeEnum: {
                int selected = 0;
                for (int o = 0; o < meta.optionCount; ++o) {
                    if (value == meta.options[o].value) {
                        selected = o;
                        break;
                    }
                }
                SendMessageW(g_controls[i], CB_SETCURSEL, selected, 0);
                break;
            }

            default:
                SetWindowTextW(g_controls[i], value.c_str());
                break;
        }
    }
}

void SaveValues() {
    for (size_t i = 0; i < g_controls.size(); ++i) {
        const SettingMeta& meta = kSettings[i];

        switch (meta.type) {
            case kTypeBool:
                whshim::SetSetting(meta.key,
                                   SendMessageW(g_controls[i], BM_GETCHECK, 0,
                                                0) == BST_CHECKED
                                       ? L"true"
                                       : L"false");
                break;

            case kTypeEnum: {
                const LRESULT sel =
                    SendMessageW(g_controls[i], CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < meta.optionCount) {
                    whshim::SetSetting(meta.key, meta.options[sel].value);
                }
                break;
            }

            default: {
                wchar_t buffer[512];
                GetWindowTextW(g_controls[i], buffer, ARRAYSIZE(buffer));
                whshim::SetSetting(meta.key, buffer);
                break;
            }
        }
    }

    whshim::SaveConfig();
    // Apply immediately: the island reads settings on the next frame.
    WhTool_ModSettingsChanged();
}

void CreateControls(HWND parent, HINSTANCE instance) {
    const int count = ARRAYSIZE(kSettings);
    g_controls.assign(count, nullptr);
    g_labels.assign(count, nullptr);

    std::vector<int> rowInGroup(g_groups.size(), 0);

    for (int i = 0; i < count; ++i) {
        const SettingMeta& meta = kSettings[i];
        const int group = GroupIndex(meta.group);
        const int row = rowInGroup[group]++;
        const int y = 60 + row * kRowHeight;

        g_labels[i] = CreateWindowExW(
            0, L"STATIC", meta.name, WS_CHILD | SS_LEFT, 28, y + 4,
            kLabelWidth, 20, parent, nullptr, instance, nullptr);

        DWORD style = WS_CHILD | WS_TABSTOP;
        const wchar_t* cls = L"EDIT";
        DWORD exStyle = 0;
        int height = 24;

        if (meta.type == kTypeBool) {
            cls = L"BUTTON";
            style |= BS_AUTOCHECKBOX;
        } else if (meta.type == kTypeEnum) {
            cls = L"COMBOBOX";
            style |= CBS_DROPDOWNLIST | WS_VSCROLL;
            height = 220;  // dropdown height, not the closed control
        } else {
            style |= ES_AUTOHSCROLL;
            exStyle = WS_EX_CLIENTEDGE;
        }

        g_controls[i] = CreateWindowExW(
            exStyle, cls, L"", style, 28 + kLabelWidth, y, kControlWidth,
            height, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFirstControl + i)),
            instance, nullptr);

        if (meta.type == kTypeEnum) {
            for (int o = 0; o < meta.optionCount; ++o) {
                SendMessageW(g_controls[i], CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(meta.options[o].label));
            }
        }

        SendMessageW(g_labels[i], WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(g_controls[i], WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_font), TRUE);

        AddTooltip(g_controls[i], meta.desc);
        AddTooltip(g_labels[i], meta.desc);
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
    switch (message) {
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom == kIdTabs && header->code == TCN_SELCHANGE) {
                ShowGroup(static_cast<int>(
                    SendMessageW(g_tabs, TCM_GETCURSEL, 0, 0)));
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kIdSave:
                    SaveValues();
                    DestroyWindow(hwnd);
                    return 0;
                case kIdCancel:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_settingsWnd = nullptr;
            g_controls.clear();
            g_labels.clear();
            return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

HWND SettingsWindowHandle() {
    return g_settingsWnd;
}

void ShowSettingsWindow(HINSTANCE instance) {
    if (g_settingsWnd) {
        SetForegroundWindow(g_settingsWnd);
        return;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    // Match the shell's font instead of the 1990s system default.
    if (!g_font) {
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                  &metrics, 0)) {
            g_font = CreateFontIndirectW(&metrics.lfMessageFont);
        }
    }

    g_groups.clear();
    for (const auto& meta : kSettings) {
        if (GroupIndex(meta.group) < 0) {
            g_groups.push_back(meta.group);
        }
    }

    // Size the window to the group with the most settings, rather than to a
    // guess: the first attempt cut the last rows off and ran them into the
    // buttons.
    std::vector<int> perGroup(g_groups.size(), 0);
    for (const auto& meta : kSettings) {
        ++perGroup[GroupIndex(meta.group)];
    }
    int maxRows = 0;
    for (int n : perGroup) {
        maxRows = n > maxRows ? n : maxRows;
    }
    g_height = kTopPad + maxRows * kRowHeight + kBottomPad;

    RECT rect{0, 0, kWidth, g_height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
    g_settingsWnd = CreateWindowExW(
        0, kClass, L"Dynamic Island — settings",
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX), CW_USEDEFAULT,
        CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
        nullptr, instance, nullptr);
    if (!g_settingsWnd) {
        return;
    }

    g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0,
                                0, 0, g_settingsWnd, nullptr, instance,
                                nullptr);
    SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 420);

    g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, 10,
                             kWidth - 20, g_height - 70, g_settingsWnd,
                             reinterpret_cast<HMENU>(kIdTabs), instance,
                             nullptr);
    SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    for (size_t i = 0; i < g_groups.size(); ++i) {
        std::wstring label;
        for (wchar_t c : g_groups[i]) {
            label += c;
            if (c == L'&') {
                label += c;  // an ampersand has to be doubled to show up
            }
        }

        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(label.c_str());
        SendMessageW(g_tabs, TCM_INSERTITEMW, i,
                     reinterpret_cast<LPARAM>(&item));
    }

    CreateControls(g_settingsWnd, instance);
    LoadValues();
    ShowGroup(0);

    HWND save = CreateWindowExW(
        0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        kWidth - 210, g_height - 46, 95, 28, g_settingsWnd,
        reinterpret_cast<HMENU>(kIdSave), instance, nullptr);
    HWND cancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        kWidth - 105, g_height - 46, 95, 28, g_settingsWnd,
        reinterpret_cast<HMENU>(kIdCancel), instance, nullptr);
    SendMessageW(save, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

    ShowWindow(g_settingsWnd, SW_SHOW);
    SetForegroundWindow(g_settingsWnd);
}
