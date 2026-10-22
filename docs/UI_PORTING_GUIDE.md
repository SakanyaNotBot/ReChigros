# Interface porting guide

This document is the contract for every screen port. The C++ interface is an immediate-mode
redraw of the same view tree the reference implementation builds programmatically, so the
goal is **identical geometry, typography, colour and motion**, not a reinterpretation.

Read `MIGRATION.md` for the module status and `docs/COVERAGE_AUDIT.md` for functional gaps.

## 1. Coordinate spaces (the single most important rule)

The reference layout is authored twice in the same screen:

* **Layout units** — a `731 x 415` design canvas. Every `*scale` multiplication in the
  reference (`scale = min(screenW / 731, screenH / 415)`) is in this space.
* **Density units (dp)** — Android dp. Every `dp(x)` and every text size (`GameTextView`
  design size) is a density value. At the reference device (1920x1080, 280 dpi) the density
  is `1.75` and the canvas scale is `1080 / 415 = 2.60241`, so
  `1 dp = 1.75 / 2.60241 = 0.67246 layout units`.

`ui::Canvas` (in `UiLayout.hpp`) owns both conversions:

```cpp
const ui::Canvas c = ui::canvas();   // scale, origin, viewport of the current frame
ui::Box box{137.0F, 134.0F, 429.0F, 338.0F};   // layout units
ui::drawPanelShape(drawList, c, box, ui::glassPanel(fill, stroke));
c.units(v)  // layout units -> pixels
c.dp(v)     // density units -> pixels  (use for everything the reference wrote as dp())
c.point(x, y), ui::toPixels(c, box)
```

When porting a line of reference code:

| Reference | Port |
| --- | --- |
| `dp(72)` margin inside a 731x415-scaled screen | `72.0F` layout units (because `dp()` and `scale` are both linear; 72 dp becomes 72*0.67246 layout units **only if the reference multiplied the value by scale** — check the call site) |
| `x * scale` where `x` is a layout-space number | `x` layout units |
| `text(label, 18, ...)` (GameTextView design size) | `TextStyle::sizeDp = 18.0F` |
| `setTextSize(PX, somePx)` | `sizeDp = somePx / densityPx`, then `canvas.dp(sizeDp)` inside the helper |
| `SkewFrameLayout` width/height from `dp(w), dp(h)` | `c.dp(w)` / `c.dp(h)` in pixels, or convert to layout units by `c.dp(w)/c.scale` |

Text is rasterised at its final pixel size by `ui::UiFontCache` (`UiText.hpp`), so a label
drawn through `ui::drawLabel` is always sharp. Never use `ImGui::Text`/`drawText` for
screen content; those magnify a single atlas.

### Text rules

* `TextStyle::sizeDp` is the reference's GameTextView design size (dp).
* **Calibrate against the reference capture, not against the source arithmetic.** The
  deployed reference build renders several labels smaller than the Java source numbers
  suggest (the start-panel label is 28 density units on device although the source text
  implies 38; the detail and settlement screens show the same effect). Treat the ink
  bounding box measured on `build/ref/java_*.png` as the target and choose the density
  size that reproduces it; record the factor in the screen's report.
* Default style matches `text()` in the reference: bold, colour 0xFFFFFFFF, single line,
  fit to width, vertically centred.
* `fitWidth = true` shrinks the font until the line fits its box (reference behaviour).
* `ellipsize = true` truncates with `...` instead (reference `autoSingleLineText`).
* `marquee = true` scrolls a too-long single line the way the list cards do (30 dp/s, one
  second delay, then a sawtooth over `textWidth + 40 dp`).
* Alignment: `HAlign::Left/Center/Right`, `VAlign::Top/Middle/Bottom` reproduce the
  reference gravity flags.

### Panels

`PanelStyle` is an exact port of `SkewFrameLayout`:
`skew = min(tan(15deg) * h, w * 0.42)`, fill + stroke (1 dp), `straightLeft`,
`straightRight`, `topWider`, and a bright accent strip (`accentEdge` 1=left, 2=right).
`ui::panelInteraction` reproduces the reference hit test (a tap only counts inside the
skewed shape) and fires on release.

Convenience constructors mirror the reference helpers:
`ui::glassPanel(fill, stroke)` (15 deg, 1 dp stroke) and `ui::rectPanel(fill, stroke)`
(0 deg). Panels with no visible stroke use the transparent colour `0x00FFFFFF`.

### Icons and images

`ui::drawImage` supports the `ImageView.ScaleType` values the reference uses
(`Fill`, `FitCenter`, `CenterCrop`, `Center`). `ui::drawIconPlate` reproduces
`iconButton(...)`: skewed plate plus a FIT_CENTER glyph with the reference's 16/10 dp
padding and optional translate.

### Motion

Reference animations are small time-driven state machines, not ImGui animations:
`ui::progress(startSec, durationSec, Interp, nowSec)` with the same interpolators the
platform uses (`DecelerateQuad` for the 220 ms screen transitions, `EaseOutCubic` for the
340 ms tab plate, `Linear` for the pause dialog's enter/exit). Store `startSec` in a static
or per-screen state next to the draw code and advance it when the reference starts its
animator.

## 2. Verification loop (mandatory for every screen)

```powershell
# 1. build
E:\ReChigros\build.bat desktop Release

# 2. open one screen with representative state and capture it at the reference resolution
E:\ReChigros\build\desktop-release\src\app\ReChigros.exe `
  --data-root E:\ReChigros\build\capture-data --screen <name> `
  --window 1920x1080 --frames 60 --fixed-delta `
  --capture-frame E:\ReChigros\build\shots\cpp_<name>.png

# 3. compare against the reference capture (1920x1080, same device profile)
#    reference shots: E:\ReChigros\build\ref\java_*.png
```

`--screen` names: `home`, `library`, `library-online`, `library-manage`,
`settings-video`, `settings-audio`, `settings-replay`, `settings-more`, `detail`,
`result`, `calibration`, `game`, `game-pause`, `game-practice`, `game-countdown`.
`build/capture-data` is pre-populated with the same chart library the reference device
holds (names, levels, covers), so card grids can be compared one to one. `--fixed-delta`
makes frame counts deterministic; `--capture-time` pins the GPU backdrop clock.

Compute a diff and inspect it with Python (Pillow is available through the bundled
runtime): absolute difference image, per-region means, and element bounding boxes (ink
bbox of text, panel edges along a scanline). Keep a small script per screen if useful.

## 3. Reference sources (read-only)

| Area | Files |
| --- | --- |
| Home | `ChartSelectActivity.renderHome` (469-571), `applyHomeLayout` (573-653), `menuButton` (663-676) — ported |
| Chart lists | `ChartSelectActivity` 663-1062, `ChartTabBar` 2844-2980, `ChartCardAdapter` 2982-3186, `ChartCardFrame` 3188-3204, dialogs 1753-1801 |
| Settings | `ChartSelectActivity` 1064-1483, `SettingsToggleView`, `SettingsSliderView`, `SettingsPreviewView` |
| Detail | `ChartSelectActivity` 1485-1752, 1803-1829 |
| Result | `ResultActivity` (43 KB), `res/layout/activity_result.xml`, `JudgementHistogramView` |
| Gameplay HUD / pause / practice | `PlayActivity` (buildOverlayUiOn 520-591, applyOverlayStageLayout 634+, tickers), `GameDialog`, HUD constants in `GameRenderer` 785-789, 8340-8700; `res/anim/*` |

The reference app builds each screen in `renderXxx` methods: replicate the view tree in the
same order, with the same layout parameters, colours, sizes, gravity and animation timing.
Do not redesign; if the reference does something odd (three different skew helpers, uneven
tab slices), copy it.

## 4. Code rules

* Comments: neutral technical English, never name another project or mention provenance.
* Do not edit the shared foundation (`UiLayout.*`, `UiText.*`, `UiStyle.*`,
  `ScreenCommon.*`, `UiWidgets.hpp`) — ask the Lead for an API addition instead.
* One owner per screen file (`CommonScreens.cpp`, `LibraryScreen.cpp`, `SettingsScreen.cpp`,
  `DetailScreen.cpp`, `ResultScreen.cpp`, `CalibrationScreen.cpp`, `GameScreen.cpp`).
* Build only in your own build directory (`build/agent-<name>`); never touch
  `build/desktop-release` or another agent's directory. Do not run `git commit`.
* All six ctest suites, `--self-test` and `build.bat verify-app` must keep passing.
* Every screen has to stay usable without a GPU context (the self-test draws screens
  headlessly): guard anything that needs a live texture and fall back to `ImGui` text.
