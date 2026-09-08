# Dynamic Island for Windows — modified build

A modified build of the **[Dynamic Island for Windows](https://github.com/devcode90/Dynamic-Island-for-Windows)**
Windhawk mod by **Himanshu ([@devcode90](https://github.com/devcode90))**.

This repository is **not** an original project. The mod — its architecture,
Direct2D renderer, spring animations, media / weather / clipboard / battery
integration, roughly 6200 lines — is Himanshu's work, used under the MIT
license. What is kept here is a personal build with the changes listed below.

If you just want the mod, install the original from Windhawk. This repository
exists to keep these particular modifications versioned.

## What is changed

**Audio visualizer**
- Replaced the scrolling amplitude history with a real spectrum analyzer:
  1024-point radix-2 FFT, Hann window, 16 log-spaced bands from 45 Hz to 14 kHz.
  Bars now sit still and bounce in place instead of scrolling sideways.
- Levels are mapped through a 60 dB window with slow auto-gain, so the display
  behaves the same at any listening volume. The previous fixed `rms * 4.0` gain
  left every bar on its 3 px floor at normal levels.
- Attack and release are time constants (70 ms / 300 ms) rather than
  per-sample factors, which used to work out to a 9 ms release at the packet
  rate and read as a flicker.

**Light theme**
- New `Theme` setting: follow Windows, always dark, always light. The system
  choice is polled once a second and switches live.
- Every translucent-white overlay (chips, tracks, badges, dividers, button
  discs — 33 places) goes through one helper that flips to black on a light
  pill. Tint and shadow have light-mode values of their own.

**Localization**
- New `Language` setting: auto, English, Russian. It drives both the UI strings
  and the locale used for month and weekday names, so the calendar starts on
  the locale's first day of week with its own short day names.
- Weather is requested from wttr.in with `lang=ru` and reads the localized
  description; compass directions are translated.

**Sizing and layout**
- Separate scale settings: text, collapsed pill, expanded card, clipboard card,
  media pill width.
- Compact cards (volume, notification, caps lock, device, battery, clipboard)
  derive their rows from the text scale instead of fixed pixel bands, which
  used to make larger type collide with the row below.

**Interaction**
- Hold `Shift` while pointing at the island to fade it out and click through to
  what is underneath.
- The progress bar is draggable: click or drag to seek.
- Contrast boost for control glyphs and muted text, so the media buttons stay
  visible when the accent is sampled from dark album art.

**Fixes**
- Position anchor compared the player's reading against our own adjusted state,
  which re-anchored to a stale value and undid every seek about a second later.
  Yandex Music publishes a position once per track and never refreshes it.
- Pressing next/previous on the island no longer makes it pop back open.

## Install

1. Install [Windhawk](https://windhawk.net/).
2. Create a new mod, replace its contents with
   [`dynamic-island-for-windows.wh.cpp`](dynamic-island-for-windows.wh.cpp),
   compile with `Ctrl+B`.

## License

MIT, see [LICENSE](LICENSE). Original copyright Himanshu; modifications
copyright mishmax.
