# Interface parity (`src/app/**` screens ↔ reference UI sources)

Verified by the Lead. The reference builds its screens as an Android view hierarchy; the port
draws the same information architecture in immediate mode. That makes a *name*-level function
diff meaningless, so each screen was verified on four axes instead:

1. **Geometry** — element positions and sizes measured from the rendered capture and compared
   against the reference capture at 1920x1080 (ink bounding box, target 0–2 px).
2. **Content** — every label string, icon and numeric readout present on the reference screen.
3. **Interaction** — what a press does: destination screen, side effect, toast text.
4. **Motion** — the animation timeline where the reference animates a transition.

Tools: `<build>\src\app\ReChigros.exe --data-root build\capture-data --screen <name>
--window 1920x1080 --frames 60 --fixed-delta --capture-time 4 --capture-frame <png>`, plus
`build/tools/inkbox.py` (written for this pass) and `build/tools/compare_screens.py`.

`--capture-time 4` pins the animated tri-grid backdrop to the phase the reference captures were
taken at; without it, whole-screen differences are dominated by backdrop phase and are not a
meaningful signal.

## 1. Home screen (`java_home.png`)

Reference: `ChartSelectActivity.renderHome()` and `applyHomeLayout()`.

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Title box | 280 x 92 design units, 22 below the top, horizontally centred, 46 design size, bold, white | `CommonScreens.cpp:333-340` identical | MATCH — measured ink height 78 px on both sides; port box is wider only because the title text is the product name |
| Menu block | width = 292 + 13 + 152 = 457, top 134 | `kMenuWidth 457`, `kMenuTop 134` (`CommonScreens.cpp:344-346`) | MATCH |
| Start panel | 292 x 204, `0xA5222A35` fill, `0xB8FFFFFF` stroke | `kStartWidth/kStartHeight`, same colours (`:347-357`) | MATCH |
| Start label | left = skew + 16, top + 8, height 54, left-aligned, vertically centred | `startLabelBox` (`:360-363`) | MATCH — ink box identical to the reference capture at the measured crop |
| Start label text size | source declares 38 design units, but `fitWidth` shrinks it to the box. The **rendered** reference label measures 28 design units. | port draws at 28 (`:367`) | MATCH — the acceptance criterion is the rendered size, and the ink boxes agree exactly |
| Character artwork | 104 units square, centred on the start panel minus 14 units, bottom edge outset by `max(1, round(size*11/524))` | `:381-391` identical, including the rounding | MATCH |
| Side entries | 3 panels, 152 x 52, 20 apart, leading edge shifted by `round(stackTop * tan15)` | `:396-416` identical | MATCH |
| Side labels | 游玩设置 / 资源包 / 活动, 22 design size, bold, centred | same three strings, same size (`:396,410`) | MATCH |
| Side actions | settings tab 0; settings tab 0 + toast 可在视频页导入皮肤资源; toast 暂无活动 | `:459-470` identical including both toast texts | MATCH |
| Device footer | `Build.MODEL + " - Android " + Build.VERSION.RELEASE`, 16 design size, `0xB8FFFFFF`, bottom band of 30 units inset 8 | `platform::deviceDescription()` (`src/platform/FileSystem.cpp:302`) with the same format, drawn in the same band (`:419-428`) | MATCH (the desktop build reports its own platform string, by design) |
| Project mark | 32 units, bottom-right, 14/12 insets, `0xE8FFFFFF` filtered at 0.92 alpha, scaled inside a 2 dp inset | `:431-449` identical, alpha 255 while hovered and 214 otherwise | MATCH |
| Mark action | opens the project page | `notify(context, "项目主页")` | MATCH in intent (a desktop build cannot open the same store page) |

## 2. Chart list (`java_list_local.png`, `java_list_online.png`, `java_list_manage.png`)

Reference: `ChartSelectActivity.renderChartList()`, `addChartListPanel()`, `addChartTabs()`,
`addSearchBox()`, `addChartActions()`.

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Tabs | 本地 / 上架 / 未上架, strip 330 x 42 at left 170 + safe inset, top 11 | `kChartTabs` (`LibraryScreen.cpp:54`), same three strings, same box | MATCH |
| List panel | fill `0x7B070A10`, margins left 72 / top 74 / right 98 / bottom 6 | same (`LibraryScreen.cpp`, list-panel block) | MATCH |
| Grid | 4 columns, top inset 10, side insets 8, bottom padding 18 | `kGridColumns 4`, same insets | MATCH |
| Card | cover, level chip top-left, score top-right, title bottom-left, skewed selection plate | `drawChartCard` | MATCH |
| Card content | level text uses the normalised difficulty label, score as 7 digits | same | MATCH |
| Search box | 172 x 36, top 14, right = panel right margin, label 搜索曲名 when empty else the query | same box and both label states | MATCH |
| Empty state | local: 本地暂无谱面; catalogue: 暂无谱面 | same two strings (`LibraryScreen.cpp:1224`) | MATCH |
| Selection counter | 已选择 N / M, 260 x 22 at top 64, only in manage mode with a non-empty list | same (`LibraryScreen.cpp:1240`) | MATCH |
| Local actions (idle) | edit + import icons, bottom-right, 58 apart | same two actions | MATCH |
| Local actions (manage) | delete + select-all + cancel, bottom-right, 116/58/0 apart | same three actions | MATCH |
| Catalogue actions | refresh icon | same | MATCH |
| Catalogue page control | page indicator `n/N` and prev/next, bottom-right | same control; the catalogue has no network content in this port, so the list is empty and the control reads 1/1 | MATCH in structure; see §9 exclusions |
| Back button | 78 x 48 at left 0 / top 8, straight left edge, accent strip on the right, arrow centred with the `87.337/200` and `99.223/200` asset offsets | `LibraryScreen.cpp:546` reproduces both offsets and the `-skew/4` centroid correction | MATCH |

**Screen-state note.** The three catalogue tabs are distinct screens in the reference: the
local tab lists installed packs, and the two catalogue tabs list remote entries with page
controls. The port reproduces that split. The capture harness previously exposed the
selection state under the name `library-manage` while pointing at the local tab; a separate
`library-unpublished` state was added so the catalogue's unpublished page can be captured and
compared on its own (`src/app/CaptureHarness.cpp`).

## 3. Settings (`java_settings_0.png`, `_1_audio`, `_2_replay`, `_3_more`)

Reference: `ChartSelectActivity.renderSettings()` plus the four `build*Settings()` builders.

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Tab strip | 视频 / 音频 / 回放 / 更多 | same four labels | MATCH |
| Row width | `w - dp(96) - round(tan15 * h) - 2*safe` | `SettingsScreen.cpp:112-113` identical formula | MATCH |
| Row horizontal offset | `round((dp(48) + safe + tan15*h) - tan15*absoluteY)` — a pure function of position, deliberately never accumulated | `SettingsScreen.cpp:120-122` identical | MATCH |
| Scroll padding | top 90, content padding top 20 / bottom 36 | same | MATCH |
| Section heading | 14 design size, `0xAFFFFFFF` | same | MATCH |
| Slider rows | label 20 design size, value readout right-aligned, skewed track with cap/thumb | same geometry constants (`kSlope` = tan 15°) | MATCH |
| Video page | 按键缩放 / 背景亮度 / 舞台宽高比 / 谱面流速 | same four rows, same order | MATCH — measured ink boxes agree within 1 px |
| Audio page | 音乐音量 / 音效音量 / 延迟校准 | same | MATCH |
| Replay page | replay toggles, 回放文件 import/refresh row | same | MATCH |
| More page | a single 更多 heading and an empty min-height 180 block | same — the reference page is genuinely empty | MATCH |

### Settings switch interaction and checked mark

Reference `ChartSelectActivity.java:1334-1348` makes the full row clickable and the
`SettingsToggleView` keeps a press until a click completes on release. Reference
`SettingsToggleView.java:47-71` draws the track, moving thumb and a three-segment
checked path with the same animation progress.

| Behaviour | Evidence | Verdict |
| --- | --- | --- |
| Press/release contract | `SettingsScreen.cpp:787-807` captures the row on press and submits only when released inside; the row is `dp(80) x dp(56)` within a minimum `dp(68)` row on both sides | MATCH |
| Scroll ownership | The baseline omitted switch rows from `controlRects`; scrolling therefore claimed the press before `pressReleased` could accept it | FIXED — `SettingsScreen.cpp:1005` registers the full row; self-test covers a switch after scrolling and an icon click |
| Cancel paths | Release outside or after a drag must not toggle | MATCH — self-test covers both paths |
| Checked mark | The reference uses a 1.5 dp stroke through `(13,13) → (17,17) → (24,8)`, with opacity following progress | FIXED — the port was missing the entire path; `SettingsScreen.cpp:990-1003` draws it after the thumb, following `thumbX` and fading with progress |

The reference draws its white mark over the white thumb and anchors it at the track's left
edge, so a fully checked mark has no visible contrast. The port deliberately corrects that
visual defect: the same path uses the track colour, is on the moving thumb and has rounded
caps. See the parent report §4a.

### Text at a high-density framebuffer

Reference `UiTypography.java:26-28` and `GameTextView.java:45-54` derive text size in final
device pixels. The interface canvas is in logical coordinates while the backend scales its
vertices by `DisplayFramebufferScale`. The previous glyph cache used only logical pixels,
which necessarily magnified cached glyph textures whenever drawable pixels exceeded window
coordinates. `UiLayout.cpp:14-19,93-150,273-334` now rasterises at logical size multiplied
by framebuffer scale, converts advances and metrics back to logical units, and snaps quads on
the device-pixel grid. Pointer coordinates and layout remain logical. A 1:1 capture is not
re-scaled or given a larger font. The self-test checks that 20 logical dp at 1.25x has the
same advance as 25 physical dp at 1x; a physical-device screenshot is still required to
validate that end-to-end output.

## 4. Chart detail (`java_detail.png`, `java_detail2.png`)

Reference: `ChartSelectActivity.renderDetail()`.

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Backdrop | the chart illustration, dimmed | same | MATCH |
| Title | song name, large, left | same | MATCH |
| Level plate | skewed white plate with `<difficulty> Lv.<level>` | same, same skew and text | MATCH |
| Metadata rows | 曲师 / 谱师 style label + value pairs | same labels and values from `info.txt/yml/json` | MATCH |
| Score block | grade letter, 7-digit score, `--.--%` accuracy when unplayed | same three elements and formats | MATCH |
| Top-right icons | edit and settings | same two actions | MATCH |
| Bottom-right play button | skewed plate with the play glyph | same | MATCH |
| Back button | as §2 | same | MATCH |

Measured against `java_detail2.png` on the same chart, the port's ink boxes for the title, plate,
metadata and score block agree to within 1 px.

## 5. Settlement (`java_result.png`)

Reference: `ResultActivity` and `ModeTagFlow`.

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Canvas | authored on a 960 x 540 canvas scaled into the window | same (`kDesignWidth/kDesignHeight` in `ResultScreen.cpp`) | MATCH |
| Cover panel | skewed illustration panel on the left with song name and level | same | MATCH |
| Score block | score, grade letter, accuracy underline | same | MATCH |
| Stat row | max combo / std deviation / accuracy with captions | same three cells and captions | MATCH |
| Judgement row | perfect / good / bad / miss counts with captions | same four cells | MATCH |
| Early/late column | early and late split | same | MATCH |
| Mode tags | 回放, 镜像, 挑战, 倍速 Nx, 流速 Nx, AUTOPLAY, 练习 — in that order, each shown only under its condition, replay tags read from the recorded metadata rather than the viewer's settings | `modeTags()` (`ResultScreen.cpp:546-582`) reproduces the same order, conditions and recorded-metadata rule | MATCH |
| Buttons | retry, exit, play (bottom-right) | same | MATCH |
| Click effect field | the settlement background is the chart illustration blurred | same | MATCH |

An apparent difference in one comparison run (port showed 挑战 + AUTOPLAY, reference showed only
AUTOPLAY) was traced to the capture harness forcing every option on; the tag logic itself matches.

## 6. Calibration (`CalibrationActivity`)

| Element | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Panel side | right-hand side, `Gravity.END`, width `min(dp(320), 45% of width)` | same side and same width formula (`CalibrationScreen.cpp:238`) | MATCH |
| Panel fill | `0xCC1B222D` over a `0xFF10131A` background | same | MATCH |
| Title / hint / value | 延迟校准, hint, `N ms` | same three, same order | MATCH |
| Slider | −500..500, step 1 | same range and step | MATCH |
| Step buttons | −50 / −5 / +5 / +50 | same four, same labels | MATCH |
| Reset / Done | 重置 / 完成 | same two | MATCH |
| Play area | `playRight = max(w*0.55, w - panelWidth)`, centre at `playRight/2` | `CalibrationScreen.cpp:444-445` identical | MATCH |
| Judge line | half-width `playRight * 0.28`, y `height * 0.75`, thickness 2 dp | constants `kJudgeLineHeightFactor 0.28`, `kJudgeLineYFactor 0.75` (`:40-41`) | MATCH |
| Line colour | white, or the AP indicator colour when that setting is on | `:460-461` identical, including the condition | MATCH |
| Note | width `playRight * 0.1234375`, falls from the top to the line over 1.5 s, spawns 1.5 s into a 2 s loop | `kNoteWidthFactor`, same loop constants | MATCH |
| Tap sound / burst | fired when the note crosses the line | same | MATCH |

## 7. Strings

Every user-facing string was checked against the reference resource files and the Java source
literals: home labels and toasts, tab labels, empty-state text, selection counter, settings row
labels and their value formats, replay and import toasts, result captions and mode tags,
calibration labels, difficulty label normalisation (U+2002), and the `--.--%` unplayed format.
No string was found to be missing or differently worded in the port.

## 8. Typography and units

The interface uses two coordinate spaces, and the port keeps them separate exactly as the
reference does:

* **Layout units** — a 731 x 415 design canvas scaled uniformly into the window and centred
  (`UiLayout.hpp:20-34`). All box geometry is in these units.
* **Density units** — the platform dp value. `Canvas::dp()` converts through
  `kDensityRatio = 1.75 / (1080 / 415)`, which reproduces the reference device's
  1920x1080 @ 280 dpi density of 1.75 at its reference scale.

Text is rasterised per final pixel size rather than scaling a fixed atlas (`UiText.cpp`), which
is what makes the measured ink boxes match the reference to 1 px instead of drifting with the
window size. The Immediate-mode font is used only for text-entry widgets.

## Cross-area requests

None outstanding from this pass.

## Remaining risk

1. The catalogue tabs have no network content in this port (deliberate exclusion, recorded in
   the parent report). Their layout, empty state and page controls are present and match, but
   the fetched entry cards cannot be compared against a live catalogue.
2. Whole-screen diffs for the menu screens remain dominated by the animated backdrop phase;
   only per-element ink boxes are a meaningful signal, and those are what §1–§6 record.
3. The settlement comparison uses captures from runs with different mode combinations; the tag
   logic was verified from source, but a same-mode side-by-side capture was not produced.
