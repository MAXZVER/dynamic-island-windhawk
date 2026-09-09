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

## Memory, and which number to believe

The standalone build was expected to be lighter. Pick the wrong counter and it
looks twice as heavy; the counters disagree by a factor of four:

| Counter | This build |
|---|---|
| Working set (includes shared DLLs) | ~72 MB |
| Private bytes / commit charge | ~70 MB |
| Committed private, walked with `VirtualQueryEx` | 33.9 MB |
| **Working set – private (actual physical memory)** | **~17.5 MB** |

The app owns about 17 MB of physical memory. The ~70 MB figure is commit
charge, which counts committed-but-not-resident regions — the largest single
one being 13.3 MB of `PAGE_WRITECOMBINE`, a Direct2D staging buffer. None of it
is application code: it is Direct2D, WinRT, WIC and UI Automation.

A like-for-like comparison against the Windhawk build was not made: the only
figure taken there (33.5 MB) is commit charge, and by that same counter this
build reads ~70 MB — but the Windhawk build was no longer running when the
physical footprint was measured, so the two cannot be placed side by side.

Two hypotheses for the difference were tested and both failed: a dynamically
linked build measured no lighter than the static one (68.4 vs 67.8 MB commit),
and the PE stack reserve/commit is identical to `windhawk.exe`'s.

The reason to build standalone is independence — no Windhawk dependency, no
catalog to pass through — not footprint.

## Idle work

Three things the island did while idle were removed: the WASAPI loopback stream
is opened only while a session is playing (its FFT output is drawn only then),
the render loop waits 48 ms instead of 16 ms when nothing animates, and the PDH
GPU counter is queried only when the game overlay that reads it is on.

Whether this is worth anything could not be shown. Repeated 25-second CPU
samples of a single build spread across 4.4–5.3% of one core, wider than any
difference between builds. The whole app costs about 0.17% of a 28-core
machine, so there was little to win. A cursor-proximity check for the render
interval was tried and reverted: it measured worse, because the island sits at
the top centre where the pointer passes often.

## Settings window

The tray icon opens a window built from the same YAML the mod declares its
settings in: `gen_island.py` turns it into a table of captions, descriptions
and choices, so adding a setting to the mod and regenerating is enough for it
to appear here as a dropdown, checkbox or field. Descriptions are tooltips.
Saving writes `config.ini` and applies immediately.

Launching the executable again opens that window on the copy already running,
rather than doing nothing.

## Autostart

Tray menu, "Start with Windows": a per-user `Run` entry, so no elevation and it
follows the account rather than the machine. The path is quoted, since a space
in it would otherwise split the command.

## Not done yet

- The executable is unsigned, so SmartScreen will warn on a fresh machine.
- The settings window has no scrolling: it sizes itself to the largest group,
  which is fine for 18 rows and would need revisiting well before 40.
