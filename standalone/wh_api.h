// Stand-in for windhawk_api.h.
//
// Windhawk force-includes its own header into every mod and supplies these
// functions from the engine. A standalone build has no engine, so the same
// surface is implemented here on top of two ini files. Keeping the signatures
// identical is what lets island.cpp stay a byte-for-byte copy of the mod body.
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Diagnostics. Windhawk routes these to its log viewer; here they go to the
// debugger output and, when a console is attached, to stderr.
void Wh_Log(PCWSTR format, ...);

// User settings, read from config.ini. The name may itself be a format string:
// Windhawk allows Wh_GetIntSetting(L"item[%d].value", i), and a few mods rely
// on it, so the shim formats the name the same way.
int Wh_GetIntSetting(PCWSTR valueName, ...);
PCWSTR Wh_GetStringSetting(PCWSTR valueName, ...);
void Wh_FreeStringSetting(PCWSTR string);

// Values the mod persists itself (pinned state, current tab, and so on),
// stored separately from user settings in state.ini.
int Wh_GetIntValue(PCWSTR valueName, int defaultValue);
BOOL Wh_SetIntValue(PCWSTR valueName, int value);

#ifdef __cplusplus
}
#endif

// Entry points the mod defines; main.cpp drives them in place of the engine.
BOOL WhTool_ModInit();
void WhTool_ModSettingsChanged();
void WhTool_ModUninit();

// Host side, implemented in wh_api.cpp.
namespace whshim {

// Creates the config directory, writes config.ini with the generated defaults
// if it is missing, and loads both files. Returns the config directory.
const wchar_t* Initialize();

// Re-reads config.ini. Returns false if nothing changed on disk since the last
// load, so the caller can skip a pointless settings reload.
bool ReloadIfChanged();

const wchar_t* ConfigPath();

}  // namespace whshim
