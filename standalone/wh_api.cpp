#include "wh_api.h"

#include <shlobj.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct SettingDefault {
    const wchar_t* name;
    const wchar_t* value;
};

#include "settings_defaults.inc"

// One lock for both maps. Every getter copies what it needs out before
// returning, because the mod reads settings from its render, audio, weather
// and media threads while the tray thread can be reloading them.
std::mutex g_mutex;
std::map<std::wstring, std::wstring> g_settings;
std::map<std::wstring, int> g_values;

std::wstring g_dir;
std::wstring g_configPath;
std::wstring g_statePath;
FILETIME g_configWriteTime{};

std::wstring FormatName(PCWSTR format, va_list args) {
    // The name is usually a plain key, but Windhawk permits a format string.
    if (!wcschr(format, L'%')) {
        return format;
    }

    va_list copy;
    va_copy(copy, args);
    const int needed = _vscwprintf(format, copy);
    va_end(copy);
    if (needed < 0) {
        return format;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
    vswprintf_s(buffer.data(), buffer.size(), format, args);
    return buffer.data();
}

std::wstring Trim(const std::wstring& s) {
    const size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    return s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}

// Minimal "key=value" reader. Lines starting with # or ; are comments; the
// value keeps any inner spaces, since city names and hex colours use them.
void ReadIni(const std::wstring& path,
             void (*sink)(const std::wstring&, const std::wstring&)) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rt, ccs=UTF-8") != 0 || !f) {
        return;
    }

    wchar_t line[1024];
    while (fgetws(line, ARRAYSIZE(line), f)) {
        std::wstring text = Trim(line);
        if (text.empty() || text[0] == L'#' || text[0] == L';') {
            continue;
        }
        const size_t eq = text.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        sink(Trim(text.substr(0, eq)), Trim(text.substr(eq + 1)));
    }

    fclose(f);
}

void SettingSink(const std::wstring& key, const std::wstring& value) {
    g_settings[key] = value;
}

void ValueSink(const std::wstring& key, const std::wstring& value) {
    g_values[key] = _wtoi(value.c_str());
}

bool GetWriteTime(const std::wstring& path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    *out = data.ftLastWriteTime;
    return true;
}

// First run only. If this machine has the Windhawk build of the mod, carry its
// settings over: without this, switching to the standalone app silently resets
// every scale, colour and language the user had chosen, which looks exactly
// like the app losing its settings.
void SeedFromWindhawk() {
    static const wchar_t* kCandidates[] = {
        L"SOFTWARE\\Windhawk\\Engine\\Mods\\local@dynamic-island-for-windows\\Settings",
        L"SOFTWARE\\Windhawk\\Engine\\Mods\\local@dynamic-island-enhanced\\Settings",
        L"SOFTWARE\\Windhawk\\Engine\\Mods\\dynamic-island-enhanced\\Settings",
        L"SOFTWARE\\Windhawk\\Engine\\Mods\\dynamic-island-for-windows\\Settings",
    };

    for (const wchar_t* sub : kCandidates) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &key) !=
            ERROR_SUCCESS) {
            continue;
        }

        int carried = 0;
        for (DWORD index = 0;; ++index) {
            wchar_t name[256];
            DWORD nameLen = ARRAYSIZE(name);
            DWORD type = 0;
            BYTE data[2048];
            DWORD dataLen = sizeof(data) - sizeof(wchar_t);
            if (RegEnumValueW(key, index, name, &nameLen, nullptr, &type, data,
                              &dataLen) != ERROR_SUCCESS) {
                break;
            }

            // Only keys this build actually has; anything else is stale.
            if (g_settings.find(name) == g_settings.end()) {
                continue;
            }

            if (type == REG_SZ) {
                // The registry does not promise a terminator.
                data[dataLen] = 0;
                data[dataLen + 1] = 0;
                g_settings[name] = reinterpret_cast<wchar_t*>(data);
                ++carried;
            } else if (type == REG_DWORD && dataLen == sizeof(DWORD)) {
                wchar_t buffer[16];
                _itow_s(*reinterpret_cast<DWORD*>(data), buffer,
                        ARRAYSIZE(buffer), 10);
                g_settings[name] = buffer;
                ++carried;
            }
        }

        RegCloseKey(key);
        if (carried > 0) {
            Wh_Log(L"Carried %d settings over from the Windhawk build", carried);
            return;
        }
    }
}

void WriteDefaultConfig() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_configPath.c_str(), L"wt, ccs=UTF-8") != 0 || !f) {
        return;
    }

    fwprintf(f, L"# Dynamic Island settings.\n");
    fwprintf(f, L"# Edit and save; the app reloads this file within a second.\n\n");
    // Values come from the map, not from kSettingDefaults: a migration from a
    // Windhawk install has already written into it, and taking the compiled-in
    // defaults here would throw that away the moment the file is read back.
    // The array is still what sets the order, so the file stays grouped the way
    // the mod's own settings are.
    for (const auto& def : kSettingDefaults) {
        const auto it = g_settings.find(def.name);
        fwprintf(f, L"%ls=%ls\n", def.name,
                 it == g_settings.end() ? def.value : it->second.c_str());
    }
    fclose(f);
}

void WriteState() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_statePath.c_str(), L"wt, ccs=UTF-8") != 0 || !f) {
        return;
    }
    for (const auto& entry : g_values) {
        fwprintf(f, L"%ls=%d\n", entry.first.c_str(), entry.second);
    }
    fclose(f);
}

}  // namespace

void Wh_Log(PCWSTR format, ...) {
    va_list args;
    va_start(args, format);
    wchar_t buffer[1024];
    _vsnwprintf_s(buffer, ARRAYSIZE(buffer), _TRUNCATE, format, args);
    va_end(args);

    OutputDebugStringW(L"[DynamicIsland] ");
    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\n");
}

int Wh_GetIntSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    const std::wstring name = FormatName(valueName, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_settings.find(name);
    if (it == g_settings.end()) {
        return 0;
    }

    // Booleans are spelled true/false in the mod's own defaults.
    if (_wcsicmp(it->second.c_str(), L"true") == 0) {
        return 1;
    }
    if (_wcsicmp(it->second.c_str(), L"false") == 0) {
        return 0;
    }
    return _wtoi(it->second.c_str());
}

PCWSTR Wh_GetStringSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    const std::wstring name = FormatName(valueName, args);
    va_end(args);

    std::wstring value;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_settings.find(name);
        if (it != g_settings.end()) {
            value = it->second;
        }
    }

    // Windhawk returns an owned string and never null; the mod relies on both.
    wchar_t* copy = new wchar_t[value.size() + 1];
    wcscpy_s(copy, value.size() + 1, value.c_str());
    return copy;
}

void Wh_FreeStringSetting(PCWSTR string) {
    delete[] const_cast<wchar_t*>(string);
}

int Wh_GetIntValue(PCWSTR valueName, int defaultValue) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_values.find(valueName);
    return it == g_values.end() ? defaultValue : it->second;
}

BOOL Wh_SetIntValue(PCWSTR valueName, int value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_values.find(valueName);
    if (it != g_values.end() && it->second == value) {
        return TRUE;  // nothing changed, do not rewrite the file
    }
    g_values[valueName] = value;
    WriteState();
    return TRUE;
}

namespace whshim {

const wchar_t* Initialize() {
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                       &appData)) &&
        appData) {
        g_dir = appData;
        CoTaskMemFree(appData);
    } else {
        // SHGetKnownFolderPath wants COM on this thread; fall back to the
        // environment rather than refusing to start over a settings path.
        const wchar_t* env = _wgetenv(L"APPDATA");
        if (env && *env) {
            g_dir = env;
        } else {
            Wh_Log(L"No APPDATA path available");
            return nullptr;
        }
    }
    g_dir += L"\\DynamicIsland";
    CreateDirectoryW(g_dir.c_str(), nullptr);

    g_configPath = g_dir + L"\\config.ini";
    g_statePath = g_dir + L"\\state.ini";

    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& def : kSettingDefaults) {
        g_settings[def.name] = def.value;
    }

    if (GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SeedFromWindhawk();
        WriteDefaultConfig();
    }

    ReadIni(g_configPath, SettingSink);
    ReadIni(g_statePath, ValueSink);
    GetWriteTime(g_configPath, &g_configWriteTime);
    return g_dir.c_str();
}

bool ReloadIfChanged() {
    FILETIME now{};
    if (!GetWriteTime(g_configPath, &now)) {
        return false;
    }
    if (CompareFileTime(&now, &g_configWriteTime) == 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Start from the defaults so a key deleted from the file goes back to
        // its default instead of keeping the last value it happened to have.
        g_settings.clear();
        for (const auto& def : kSettingDefaults) {
            g_settings[def.name] = def.value;
        }
        ReadIni(g_configPath, SettingSink);
    }

    g_configWriteTime = now;
    return true;
}

std::wstring GetSetting(const wchar_t* key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_settings.find(key);
    return it == g_settings.end() ? std::wstring() : it->second;
}

void SetSetting(const wchar_t* key, const wchar_t* value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings[key] = value;
}

bool SaveConfig() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        WriteDefaultConfig();  // writes the map, ordered by the defaults table
    }
    // Adopt our own write so the watcher does not reload it right back.
    GetWriteTime(g_configPath, &g_configWriteTime);
    return true;
}

const wchar_t* ConfigPath() {
    return g_configPath.c_str();
}

}  // namespace whshim
