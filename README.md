# Dynamic Island for Windows — modified build

A modified build of the **[Dynamic Island for Windows](https://github.com/devcode90/Dynamic-Island-for-Windows)**
Windhawk mod by **Himanshu ([@devcode90](https://github.com/devcode90))**.

This repository is **not** an original project. The mod — its architecture,
Direct2D renderer, spring animations, media / weather / clipboard / battery
integration, roughly 6200 lines — is Himanshu's work, used under the MIT
license. What is kept here is a personal build with the changes below:
855 lines added, 204 changed, across 88 places.

If you just want the mod, install the original from Windhawk. This repository
exists to keep these particular modifications versioned, and to write down why
each one was needed — several of them turned out to have non-obvious causes.

---

## 1. The visualizer was a ticker, not a visualizer

**What was there.** A 48-slot ring of amplitude samples. Each bar showed one
past moment, so the pattern could only crawl sideways. That is an
oscilloscope-style history, not the equalizer the design suggests.

**What it does now.** A real spectrum analyzer: an iterative radix-2
Cooley–Tukey FFT over 1024 points with a Hann window, about 47 analyses a
second, mapped into 16 log-spaced bands from 45 Hz to 14 kHz. Bar `i` always
shows band `i`, so bars stand still and bounce in place.

Magnitudes go through a 60 dB window rather than a linear scale, so the display
reads the same at any listening volume:

```cpp
const float db = 20.0f * std::log10(mag + 1e-9f);
raw[b] = Clamp((db + 85.0f) / 60.0f, 0.0f, 1.0f);
```

**Three bugs were found on the way there**, each measured rather than guessed:

- **Everything sat on the floor.** Sampling the endpoint peak meter during
  playback gave 0.05 — about 5 % of full scale. The old gain was `rms * 4.0`,
  which turns a typical 0.015 RMS into 0.06, and `std::max(3.0f, amp * maxH)`
  then clamped every bar to its 3 px floor. A row of dots, not a visualizer.
- **Then it vibrated.** The envelope coefficients (`0.7` attack, `0.15`
  release) were per-sample, and `PushAudioChunks` fires ~750 times a second at
  64-frame chunks: a 1.3 ms attack and a 9 ms release. They are time constants
  now (70 ms / 300 ms), so the feel no longer depends on the packet rate.
- **The ring held 64 ms.** Writing every packet into 48 slots meant the whole
  display scrolled 750 times a second. Writes are on a fixed cadence now.

## 2. Light theme

**The hard part was not the palette.** 33 places drew surfaces as translucent
*white* over a dark pill — chips, tracks, badges, dividers, pagination dots,
the button discs. On a light pill every one of them disappears. They all go
through one helper, so the alpha carries over and only the ground flips:

```cpp
D2D1_COLOR_F Overlay(float alpha) const {
    return lightTheme_ ? D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha)
                       : D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha);
}
```

The same flip works for foreground glyphs (battery fill, active dots), since
those must darken on a light ground too.

The Windows app theme is read from
`HKCU\...\Themes\Personalize\AppsUseLightTheme`, polled once a second, so
switching the system theme switches the island live. Shadow opacity drops from
0.70 to 0.22 in light mode: a shadow tuned for black is a smear on white.

## 3. Contrast of controls

With `Accent color mode = Auto` the accent is sampled from album art. A dark
cover produced a near-black glyph on a near-black pill, and the media buttons
were effectively invisible — as was the visualizer, which uses the same brush.
The button disc did not help either: white at 4 % alpha.

`Contrast boost` lifts the glyph colour to a minimum relative luminance by
mixing toward white, rather than scaling channels — scaling leaves a black
accent black, and mixing also preserves the hue as far as it can:

```cpp
const float t = (minLuma - luma) / std::max(0.0001f, 1.0f - luma);
c.r += (1.0f - c.r) * t;   // and g, b
```

In light mode the mirror image (`DropLuminance`) darkens instead.

## 4. Localization

Month and weekday names already came from `GetDateFormatEx`, but the week grid
was hard-coded: `{L"S", L"M", L"T", ...}`, Sunday-first, with weekends pinned to
columns 0 and 6. Windows knows all three, so it is asked:
`LOCALE_SSHORTESTDAYNAME1..7` for the letters, `LOCALE_IFIRSTDAYOFWEEK` for the
start of the week, and the day-of-week index is shifted into that order.

Following the user locale alone was not enough — on an `en-US` machine the
calendar stays English no matter what the rest of the UI is set to. A
`Language` setting (auto / English / Russian) now drives both the strings and
the locale handed to the date APIs.

Weather is requested from wttr.in with `lang=ru` and reads the `lang_ru` field
with a fallback to `weatherDesc`. Compass directions are translated per letter
(`N→С`, `S→Ю`, `E→В`, `W→З`), which covers all 16 points without a table.

## 5. Sizing

`sizeScale` was read in 23 places. All of them now go through one accessor, so
the pill, its contents, the hit-testing and the render transform can never
disagree within a frame:

```cpp
inline float EffectiveSizeScale(const Settings& settings) {
    return settings.sizeScale * g_stateScale.load(std::memory_order_relaxed);
}
```

`g_stateScale` is the collapsed / expanded multiplier, carried by its own
spring so the switch does not pop. It is a multiplier rather than a
replacement, so it composes with the user's base scale.

"Expanded" means the big card is actually on screen — hover, pinned, or a track
that just changed art. The first attempt used `kind != Idle`, which counted the
compact media and volume pills as expanded and scaled the small pill by the
wrong factor.

Compact cards (volume, notification, caps lock, device, battery, clipboard)
had text rows in fixed pixel bands — a 16 px row for an 11 px font. At a text
scale of 1.4 the glyphs overflowed into the row below. Those bands are derived
from the text scale now, and the cards grow to match.

## 6. Shift to peek

Holding `Shift` with the pointer over the island fades it out and lets the
click through. One multiply on the layered-window blend fades every layer at
once, which no per-element opacity could do as cleanly:

```cpp
blend.SourceConstantAlpha = static_cast<BYTE>(
    Clamp(settings.pillOpacity, 0.35f, 1.0f) *
    Clamp(g_peekAlpha.load(std::memory_order_relaxed), 0.0f, 1.0f) * 255.0f);
```

Two details make it feel right. `hover` is computed from the window rect rather
than from mouse messages, so it keeps working after the window goes
click-through — otherwise the island would flicker back the moment it became
transparent. And the expansion state is frozen for the duration of the peek:
letting hover through would open the island and only then fade it, forcing it
closed would collapse it mid-fade.

## 7. Seeking, and why the bar used to snap back

The progress bar is draggable. Coordinates come from the drawing code rather
than from measurement, the pointer is captured so the drag survives leaving the
bar, and while dragging the bar renders the dragged position instead of the
player's.

The interesting part is what it exposed. **Yandex Music publishes a playback
position once per track and never refreshes it** — sampling
`GetTimelineProperties()` for ten seconds during playback returns the same
`Position` and the same `LastUpdatedTime` every time. The mod extrapolates from
an anchor, which is fine, except the anchor was re-synced like this:

```cpp
if (np != g_state.media.positionTicks || ...)   // np is frozen
```

That compares the player's reading against *our own extrapolated state*. They
drift apart by design, so every poll looked like fresh news and reset the
position to the stale value — about once a second, and immediately after any
seek. The comparison is now against the previous raw reading, plus the track
identity, so a new track still re-anchors:

```cpp
const std::wstring rawTrack = next.title + L"\x01" + next.artist;
if (trackChanged || np != sLastRawPosition || ne != sLastRawEnd || ...)
```

Our own seek writes the anchor itself, since the player will not report it.

Seeking from inside the player app remains invisible: same track, same
duration, nothing published. No mod can see it through the system controls.

## 8. Smaller things

- Pressing next / previous **on the island** no longer makes it pop back open
  a moment later. The auto-open on track change could not tell an external
  change from one the user had just made, so it reopened behind the cursor.
- The clipboard card's title used to overlap the copied text at larger text
  scales, and its icon badge is square now instead of stretching with the card.
- The volume card showed the raw endpoint name ("System audio") in English,
  with the value floating in the middle of its own column. Caption and value
  are measured and set as one centred group.

## Verification

Every change was compiled with Windhawk's own toolchain before being handed
over — `clang++ -fsyntax-only` with the mod's flags and `windhawk_api.h`
force-included, which is a full parse and semantic analysis of the translation
unit. Behaviour was measured rather than assumed: endpoint peak levels through
`IAudioMeterInformation`, session capabilities and timeline through
`GlobalSystemMediaTransportControlsSession`, GPU engine counters during 4K60
playback, and screen captures of the island itself to look at the result.

## Install

1. Install [Windhawk](https://windhawk.net/).
2. Create a new mod, replace its contents with
   [`dynamic-island-for-windows.wh.cpp`](dynamic-island-for-windows.wh.cpp),
   compile with `Ctrl+B`.

Settings added by this build live in `Appearance` (text scale, collapsed and
expanded scale, clipboard scale, media pill width, language) and in
`Colors & Theming` (theme, contrast boost).

## License

MIT, see [LICENSE](LICENSE). Original copyright Himanshu; modifications
copyright mishmax.
