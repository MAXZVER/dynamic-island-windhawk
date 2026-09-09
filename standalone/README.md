# Standalone build

The island as a plain `.exe`, with no Windhawk installed.

This works because the mod never injected into anything: its only hook targeted
the entry point of `windhawk.exe` itself, purely to turn that process into a
host. A standalone executable already is that process, so the hook and the
whole bootstrap around it are dropped.

## Layout

| File | Role |
|---|---|
| `gen_island.py` | Generates `island.cpp` and `settings_defaults.inc` from `../dynamic-island-for-windows.wh.cpp` |
| `wh_api.h` / `wh_api.cpp` | Stands in for `windhawk_api.h`: logging, settings, persisted values |
| `main.cpp` | `wWinMain`, single instance, tray icon, settings-file watch |
| `build.bat` | Builds with the compiler Windhawk ships |

The mod file stays the single source of truth. `island.cpp` is generated from
it — never edit it by hand; change the mod and run the generator again. The
generator refuses to run if anything above the bootstrap has come to depend on
it, so the two cannot drift apart silently.

## Settings

Windhawk rendered a settings UI from the mod's YAML block. Without it, the same
35 settings live in `%APPDATA%\DynamicIsland\config.ini`, generated with their
defaults on first run. The app re-reads the file about a second after it is
saved, so editing it is the settings UI. Values the mod persists itself (pinned
state, current tab) go to `state.ini` beside it.

## Build

```
build.bat
```

Requires Windhawk installed for its bundled clang, or any mingw-w64 clang++ —
point `CLANG` at it. The link is static, so the result is a single self-
contained executable with no runtime DLLs to ship.

## Memory: measured, not assumed

The standalone build was expected to be lighter. It is not:

| | Working set | Private | Threads |
|---|---|---|---|
| Windhawk host running the mod | 40.2 MB | 33.5 MB | 25 |
| Bare Windhawk host, no mod | ~15 MB | ~3 MB | 3–4 |
| **This standalone build** | **73.7 MB** | **70.1 MB** | 40 |

Windhawk's own overhead is about 3 MB of private memory — everything else was
always the island itself: Direct2D surfaces, album art, WinRT, the FFT buffers.
Linking libc++ statically then costs more than the host ever did. Dropping
`-static` and shipping the runtime DLLs alongside would win some of it back.

The reason to build standalone is independence — no Windhawk dependency, no
catalog to pass through — not footprint.

## Not done yet

- No autostart entry; add a shortcut to `shell:startup` by hand for now.
- The tray menu only opens the settings file, reloads it, and exits. The
  island's own right-click menu still works as before.
- The executable is unsigned, so SmartScreen will warn on a fresh machine.
