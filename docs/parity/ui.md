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

## CalibrationActivity and ResultActivity: every declaration adjudicated

Both are Android activities that build their screens by assembling views. The port draws them
immediately, so the view-construction names have no counterpart. Each entry names the port
code that carries the behaviour; the settlement backdrop's blur was verified constant by
constant against `ResultActivity.java:727-760`.

### `CalibrationActivity.java` → `CalibrationScreen`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `Runnable` / `onCreate` / `onDestroy` / `applyImmersiveMode` | `drawCalibrationScreen`, screen routing | DELIBERATE-EXCLUSION | Activity lifecycle and immersive mode; the port's calibration page is a draw function in an already-fullscreen window. |
| `buildUi` / `addStepButton` / `makeButton` / `matchWrap` | `CalibrationScreen` button row | MATCH | Same step buttons with the same increments and the same layout. |
| `setAudioOffsetMs` (2) / `refreshValueViews` | `context.calibration.offsetMs` write-through | MATCH | Same single value driving the readout and the applied offset. |
| `finishWithResult` | `navigateTo` back with the offset committed | MATCH | Both write the offset into the settings and return to the caller. |
| `CalibrationView` / `onTouchEvent` | `CalibrationScreen` input handling | MATCH | Same tap-to-calibrate hit test and the same marker clamping. |
| `setPanelWidthPx` | panel width in `CalibrationScreen` | MATCH | Same width rule. |
| `resetSmoothClock` | `SmoothingClock` reset | MATCH | Same reset of the beat clock on loop restart. |
| `currentNoteY` / `currentLoopTime` / `currentMarkerProgress` / `noteDrawProgressForLoopTime` / `noteMarkerProgressForLoopTime` / `tapYForProgress` | `CalibrationScreen` note position math | MATCH | Same loop phase → progress → Y mapping. |
| `rememberRenderedTapPosition` | recorded tap position | MATCH | Same "remember where the tap landed for the bias sample" step. |
| `touchMarkerLineWidth` / `indicatorColor` / `TouchMarker` | marker drawing | MATCH | Same line width and the same indicator colour. |
| `loadAssetBitmap` / `splitHitFxFrames` / `loadHitFxGrid` / `CalibrationHitEffect` | hit-effect atlas in the renderer | MATCH | Same grid slicing of the hit-effect sheet and the same playback. |
| `getSeconds` | `SmoothingClock::seconds` | MATCH | Same seconds accessor with the same bias smoothing. |
| `resetBiasHistory` / `addBias` | bias history in `CalibrationScreen` | MATCH | Same rolling average of the tapped offsets, and the same reset between runs. |

### `ResultActivity.java` → `ResultScreen`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `onCreate` / `onDestroy` | `drawResultScreen`, screen routing | DELIBERATE-EXCLUSION | Activity lifecycle; the port's result page is a draw function reached through `navigateTo`, and the artwork decode is owned by the texture cache. |
| `iconIndex` | grade index in `ResultScreen` | MATCH | Same score/FC thresholds selecting the same grade bitmap. |
| `stylePanels` / `styleCornerButton` / `styleChipButton` / `bindPressFeedback` | `ResultScreen` drawing helpers | MATCH | Same panel, corner-button and chip styling; press feedback is the shared immediate-mode press state. |
| `setupModeTags` / `addModeTag` / `ModeTagFlow` / `layoutRow` | mode tags in `ResultScreen` | MATCH | Same tag set (autoplay/challenge/mirror/practice/replay) laid out in the same wrapping row. |
| `setupResponsiveCanvas` | `ui::canvas` | MATCH | Same design-canvas mapping with uniform scale and centring. |
| `applyPhigrosFont` | `UiFontCache` + `drawLabel` | MATCH | Same bundled face applied to the same labels. |
| `setupJudgementHistogramOverlay` / `toggleJudgementHistogram` / `showJudgementHistogram` / `hideJudgementHistogram` | histogram overlay in `ResultScreen` | MATCH | Same overlay toggle and the same bin rendering from `judgementOffsetBins`. |
| `loadGradeImage` / `loadButtonImage` / `loadImageFromAssets` | `TextureCache` loaders | MATCH | Same asset paths and the same missing-asset fallback. |
| `setupFullscreen` / `applyImmersiveMode` | SDL fullscreen window | DELIBERATE-EXCLUSION | Android system-UI concerns; the port's window is already fullscreen. |
| `blurBackground` / `gaussianBlur` | `TextureCache::blurredBackdropFor` | MATCH | Identical recipe: a third-size copy, three separable box passes, radius `max(4, round(w * 0.05 / 3))`, bilinear upsample. Checked constant for constant. |
| `AlphaTarget` / `childAlphaTargets` / `startEnterAnimation` | `ResultScreen` enter animation | MATCH | Same staggered per-element alpha ramp driven by the same easing set. |

## PlayActivity: every declaration adjudicated

The reference's `PlayActivity` owns the gameplay surface and lays out the official HUD by hand
in view space. The port draws the HUD from `HudPass.cpp`, so the HUD helpers below name the
formulas they correspond to - and this is where the HUD half-width clamp was found missing.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `extraBool` | argument parsing in `main.cpp` | DELIBERATE-EXCLUSION | Reads a boolean Intent extra; the port takes command-line flags instead. |
| `handleOnBackPressed` | `processEvents` back handling | MATCH | Same back action routing: leave gameplay, then leave the screen. |
| `setLoadingUi` / `loadAndStart` | `GameScreen` chart start | MATCH | Same loading state followed by the run start. |
| `forceGc` | — | DELIBERATE-EXCLUSION | An explicit `System.gc()` before a chart load; there is no equivalent call to make on this platform. |
| `hintSurfaceFrameRateIfPossible` / `applyHighRefreshRate` | `SDL_GL_SetSwapInterval` | DELIBERATE-EXCLUSION | Android display-mode hints for a high refresh rate; the port requests vsync and lets the compositor pick the rate. |
| `surfaceCreated` / `surfaceChanged` / `surfaceDestroyed` | SDL GL context lifetime | DELIBERATE-EXCLUSION | `SurfaceHolder` callbacks; SDL owns that lifecycle. |
| `buildOverlayUiOn` / `startUiTicker` / `stopUiTicker` | `drawGameOverlay` per frame | DELIBERATE-EXCLUSION | The reference runs a UI ticker thread over a view overlay; the port draws the overlay inside the frame. |
| `applyOverlayStageLayout` | `StageLayout` recomputation | MATCH | Same stage rect applied to the overlay. |
| `officialHudUnitPx` | `scene::hudUnit` (`HudPass.cpp:803`) | MATCH | `stageH / 1000`, or 1 when the stage is degenerate. |
| `officialHudHalfUiW` | `canvas.halfUiW` (`HudPass.cpp:829`) | **FIXED** | The reference clamps the aspect to its 16:9 maximum (`min(aspect, 16/9) * 500`). The port used the raw stage aspect, so on a stage wider than 16:9 the score, combo and song text spread outward to positions the reference never produces. The clamp is now applied. |
| `officialHudX` | `scene::hudX` (`HudPass.cpp:808`) | MATCH | `centerX + uiX * unit`. |
| `officialHudY` | `scene::hudY` (`HudPass.cpp:813`) | MATCH | `centerY - uiY * unit`, same axis flip. |
| `officialHudSize` | size multiply at the draw sites | MATCH | `uiSize * unit`. |
| `officialHudBottomTextYOffsetPx` | bottom-text offset in `HudPass` | MATCH | `1080-reference offset * stageH / 1080`. |
| `tryLoadSfxRawWav` / `tryLoadSfxFromSkinDir` / `tryLoadSfxFromSkinFile` | `AudioEngine` sfx resolution | MATCH | Same three-step lookup: raw asset, then the skin directory, then the skin's own file. |
| `onExit` / `onFinished` | run completion in `GameScreen` | MATCH | Same transition to the result screen with the same callback payload. |
| `onStageRectChanged` | `StageLayout` observers | MATCH | Same reaction to a stage resize. |
| `onResourcesReady` | renderer readiness gate | MATCH | Same start-once-resources-ready gate. |

## Online chart library: deliberately out of scope

The reference ships an online chart browser that scrapes a Lanzou cloud share
(`LanzouClient`) and pages its index (`SelectOnlineChartsFragment`). The port ships local
packs only: the list, search, management and import surfaces work on files the user already
has. This is a scope decision, not an unfinished port — the online path depends on a specific
third-party host, its page-obfuscation scheme and its download quotas, none of which can be
reproduced or reliably maintained from this repository.

`ChartSelectActivity.onlinePage` in the port is **not** related: it is the page index of the
local list, which the reference also has.

### `LanzouClient.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `LanzouClient` (class) | — | DELIBERATE-EXCLUSION | The reference downloads charts from a Lanzou cloud share. The port ships local packs only; see the online-feature section below. |
| `pageUrl` | — | DELIBERATE-EXCLUSION | Lanzou share-page extraction; no port counterpart. |
| `titleOrFileName` | — | DELIBERATE-EXCLUSION | Lanzou entry naming; no port counterpart. |
| `levelOrFallback` | — | DELIBERATE-EXCLUSION | Lanzou difficulty labelling; no port counterpart. |
| `FolderSession` | — | DELIBERATE-EXCLUSION | Lanzou folder-browsing session; no port counterpart. |
| `ResolvedDownload` | — | DELIBERATE-EXCLUSION | Lanzou resolved direct-download record; no port counterpart. |
| `rememberCookies` / `addCookies` / `cookieHeader` | — | DELIBERATE-EXCLUSION | Cookie jar for the Lanzou session; no network client exists in the port. |
| `parseJsVars` / `resolveJsValue` / `solveAcwScV2` | — | DELIBERATE-EXCLUSION | Lanzou's page-obfuscation solver; specific to that host. |
| `extractDescription` / `extractDownloadPageUrl` / `resolveDirectFromTp` | — | DELIBERATE-EXCLUSION | Lanzou page scraping; no port counterpart. |
| `htmlBlockToText` / `stripTags` / `htmlDecode` / `firstGroup` | — | DELIBERATE-EXCLUSION | HTML helpers used only by the scraper above. |
| `valueOrEmpty` / `valueOrDefault` | — | DELIBERATE-EXCLUSION | Scraper-local null-coalescing helpers. |

### `SelectOnlineChartsFragment.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `onCreateView` / `onViewCreated` | — | DELIBERATE-EXCLUSION | Fragment lifecycle for the online-library page, which the port does not ship. The port's `onlinePage` field is the *local* list's page index, a different concept that happens to share the word. |
| `onDownload` / `confirmDownload` / `downloadChart` | — | DELIBERATE-EXCLUSION | Triggers a Lanzou download; the port's import path only takes local files and archives the user already has. |
| `onItemVisible` / `OnScrollListener` / `onScrolled` | — | DELIBERATE-EXCLUSION | Lazy loading for the online list; the port's local list computes its visible range per frame. |
| `beforeTextChanged` | — | DELIBERATE-EXCLUSION | Text watcher on the online search box. |
| `reloadOnline` / `resetOnlineState` / `fetchOnlineIndex` / `applyOnlineIndex` / `loadNextPage` | — | DELIBERATE-EXCLUSION | Fetching and paging the remote index; the port has no remote index. |
| `fetchVisibleItemCover` / `ensureCoverCached` | — | DELIBERATE-EXCLUSION | Downloads remote cover art; the port's `CoverCache` reads artwork from installed packs. |
| `maybeLoadMoreForSearch` | — | DELIBERATE-EXCLUSION | Search-driven paging of the remote index. |
| `setStateMessage` / `updateStateCount` / `setHeaderCollapsed` | — | DELIBERATE-EXCLUSION | Status widgets of the online page. |
| `runOnUiThread` | — | DELIBERATE-EXCLUSION | Android thread hop for network results; the port has no background network work. |
| `extensionOrJpg` / `safeFileName` / `errorMessage` / `host` | — | DELIBERATE-EXCLUSION | Filename and error helpers used by the online page. |

## JudgementHistogramView and SettingsSliderView: every declaration adjudicated

### `JudgementHistogramView.java` → `ResultScreen.cpp drawJudgementHistogram`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `JudgementHistogramView` (2 constructors) | `drawJudgementHistogram` (`ResultScreen.cpp:396`) | DELIBERATE-EXCLUSION | An Android `View` subclass; the port draws the histogram as part of the result screen, so there is no view object. |
| `setHistogram` | `context.lastResult.judgementOffsetBins` | MATCH | Same 21-bin array driving the bars. |
| `setTitle` | the "判定分布" label | MATCH | Same title string. |
| `setContentInsets` | inset computation in the draw function | MATCH | Same content inset from the skew and the panel width. |
| `setHistogramTypeface` / `setSourceBitmap` | shared font cache and backdrop texture | MATCH | Same typeface and the same source artwork under the histogram. |
| `onSizeChanged` / `onDetachedFromWindow` | per-frame size read, renderer lifetime | DELIBERATE-EXCLUSION | View lifecycle callbacks; the port reads the box every frame. |
| `drawBlurredBackdrop` | `TextureCache::blurredBackdropFor` + the darkened backdrop polygon | MATCH | Same blurred-artwork backdrop with the same alpha applied. |
| `drawPanel` / `drawBars` / `drawPeakTag` / `drawAxisLabels` | one draw function, four sections | MATCH | Same panel, bar, peak-tag and axis-label composition; the tag format is `+%dms`/`%dms` on both sides (`ResultScreen.cpp:487`). |
| `effectiveMaxBarHeight` :274-277 | bar-height cap in the draw function | MATCH | Same rule: below `dp(86)` use the graph height, otherwise `max(dp(20), graphH * 0.9)`. |
| `ensureBlurredBitmap` / `recycleBlurredBitmap` / `boxBlur` | `blurredBackdropFor` cache | MATCH | Same "build once, reuse, release with the screen" lifetime; the blur recipe is the reference's (third-size, three box passes). |
| `formatSignedMs` :373-376 | `snprintf` at `:487` | MATCH | Same `+%dms` for positives and `%dms` otherwise. |
| `formatAxisMs` :378-381 | axis labels in the draw function | MATCH | Same `+%d`/`%d` without the unit. |
| `withAlpha` | `whiteAlpha(alpha)` | MATCH | Same colour with the reveal alpha folded in. |

### `SettingsSliderView.java` → `SettingsScreen.cpp drawSlider`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `SettingsSliderView` (constructor) | `drawSlider` (`SettingsScreen.cpp`) | DELIBERATE-EXCLUSION | An Android `View`; the port draws the slider in place each frame. |
| `setOnValueChanged` | `sliderInput` + `applySliderValue` | MATCH | Same "report the value on change" contract. |
| `capWidth` / `thumbWidth` / `endInset` / `thumbGap` / `trackLeft` / `trackRight` | `SliderGeometry` | MATCH | Same cap, thumb, end-inset and track bounds; the port keeps them in one `SliderGeometry` struct instead of five getters. |
| `parallelogram` | `skewedBlock` | MATCH | Same skewed quad with `skew = height * 0.268`. |
| `updateValue` / `updateFromX` | `sliderInput` fraction mapping | MATCH | Same x → fraction → snapped value mapping with the same step rounding. |
| `performClick` | press state machine in `sliderInput` | MATCH | Same click-through on release. |
| `onKeyDown` | — | DELIBERATE-EXCLUSION | The reference supports keyboard stepping for an Android accessibility path; the port's slider is pointer-only and the settings page is not keyboard-navigated. |
| `onInitializeAccessibilityNodeInfo` / `performAccessibilityAction` | `ImGui` item metadata | DELIBERATE-EXCLUSION | Android accessibility bridge; ImGui supplies its own item description, and the reference's content description has no cross-platform equivalent. |

## GameDialog: every declaration adjudicated

The reference implements its dialogs as a hand-drawn skewed panel inside a custom `ViewGroup`.
The port uses ImGui modal popups, which carry the same modality, dismissal and button
semantics without a view tree. None of these declarations has a same-name counterpart, so each
is adjudicated here.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `GameDialog` (class, 3) | `ImGui::BeginPopupModal` blocks | DELIBERATE-EXCLUSION | The reference hand-builds a skewed dialog panel over a custom `ViewGroup`; the port uses ImGui's modal popup, which supplies the same modality, centring and focus trapping without a custom view tree. |
| `Builder` / `Builder.setView` | direct call sites | DELIBERATE-EXCLUSION | A builder for assembling an Android view; the port writes each dialog inline at its call site. |
| `setTitle` (2 overloads) | `ImGui::TextUnformatted` heading | MATCH | Same single title line at the top of the panel. |
| `setMessage` (2 overloads) | body text in the popup | MATCH | Same message body under the title. |
| `setItems` | list rows in the skin/replay dialogs | MATCH | Same selectable list of items. |
| `setPositiveButton` / `setNegativeButton` / `setNeutralButton` (2 each) | `ImGui::Button` pairs | MATCH | Same confirm/cancel/third action with the same labels and the same dismissal contract. |
| `setCancelable` | `ImGuiWindowFlags_NoSavedSettings` + close handling | MATCH | Whether an outside press or back dismisses is decided per dialog on both sides. |
| `setOnDismissListener` | the code after the popup block | MATCH | Both run a completion callback after dismissal; the port inlines it. |
| `onCreate` / `onStop` | popup open/close | DELIBERATE-EXCLUSION | Android dialog lifecycle; the popup's Begin/End pair is the port's equivalent. |
| `configureWindow` | `ImGui::SetNextWindowPos` / `SetNextWindowSize` | MATCH | Same centring and the same size rule (see the panel-width row). |
| `AnimatorListenerAdapter` (2) / `onAnimationEnd` (2) / `runEnter` / `finishDismiss` | immediate-mode open/close | DELIBERATE-EXCLUSION | The reference animates the dialog in and out with `ValueAnimator`; the port's modals appear immediately. Recorded as a known motion difference, not a behavioural one. |
| `applyProgress` | `ui::fade` alpha in the open animation | MATCH | Same 0..1 progress driving the panel's fade. |
| `buildContent` | the popup's body block | MATCH | Same title, message, content and button order. |
| `weightedWrap` | layout weights in the popup | DELIBERATE-EXCLUSION | An Android `LinearLayout` weight helper; ImGui sizes the popup from its content. |
| `styleCustomView` | per-dialog styling inline | MATCH | Same surface styling, applied at the call site. |
| `ButtonSpec` | button label/action pair | MATCH | Same two-field description of a dialog button. |
| `DialogPanel` / `addButton` / `onMeasure` / `rebuildPaths` / `applySlant` | ImGui popup drawing | DELIBERATE-EXCLUSION | The reference lays out the panel by hand, rebuilds its skewed paths on each measure and re-slants the item scroll; the port's popup has no skewed panel. The *panel width* rule is still reproduced where a skewed panel is drawn (see the settings rows). |

## GameTextView: every declaration adjudicated

The reference's `GameTextView` is a `TextView` subclass whose text is measured and drawn by
`GameTextRenderer`. The port has no widget objects, so the behaviour lives in the shared text
layer (`ui::drawLabel` / `ui::TextStyle` / the glyph cache). This is also where the text-size
behaviour that the interface depends on is pinned.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `GameTextView` (3 constructors) | `ui::TextStyle` + `drawLabel` | DELIBERATE-EXCLUSION | The reference subclasses an Android `TextView` to get view lifecycle and attributes; the port's labels are stateless draw calls, so there is no view object to construct. |
| `setDesignTextSize` / `getTextSize` / `setTextSize` | `ui::TextStyle::sizeDp` | MATCH | Design size in dp is scaled by the density at draw time, which is the same contract: the stored value is a design unit and the pixel size is derived, never stored. |
| `setFitWidth` | `ui::TextStyle::fitWidth` | **FIXED** | The reference bisects for the largest font size that still fits (twelve halvings to 0.05 px, `GameTextRenderer.java:56-67`). The port ran three proportional steps, which does not converge for a label whose width shrinks non-linearly with size, so fitted labels settled smaller than the reference. `UiLayout.cpp:348-363` now performs the same bisection. |
| `requestLayout` / `onTextChanged` / `invalidate` | immediate-mode redraw | DELIBERATE-EXCLUSION | Android invalidation callbacks; the port redraws every frame, so there is nothing to invalidate. |
| `hasFixedSize` | `ui::Box` of the draw call | MATCH | Whether the label is measured against a fixed box or its own extent is decided per call site, same as the reference's fixed-size flag. |
| `onMeasure` | `drawLabel` measurement | MATCH | Same rule: with an exact width the box wins, otherwise the natural extent is used. The degenerate zero-width first pass the reference guards against does not exist here. |
| `textTop` / `getBaseline` | `drawRun` baseline computation | MATCH | Both place the first baseline at the box top plus the font ascent. |
| `alignment` / `displayText` | `HAlign` handling and hint fallback in `drawLabel` | MATCH | Same gravity-to-alignment mapping, and the same "hint when empty" rule. |
| `onConfigurationChanged` / `animationsEnabled` | `Application` density refresh, `Settings` | DELIBERATE-EXCLUSION | Configuration and animation-scale changes are Android system settings; the port re-reads density each frame and has no system animation scale. |
| `drawCompoundDrawables` / `setSelected` | per-screen icon drawing | DELIBERATE-EXCLUSION | Compound drawables are an Android `TextView` feature; icons are drawn explicitly where the reference would attach them. |

### Hit effects and the chart card surfaces

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range now falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `renderGameplayFrame` / `renderSceneDirect` | `GameRenderer::render` | MATCH | Same frame composition and direct scene path. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question. |
| `drawAmetrineVignette` / `ametrineHudVignetteFactor` | `fx::AmetrineVignette` | MATCH | Same inline vignette and HUD factor. |
| `getOrLoadFileTexture` | `TextureCache::load` | MATCH | Same load-once cache. |
| `getOrCreateTextTexture` / `createHudTextTexture` (5) | `TextRenderer::glyphStrip` | **DIFFERENT BY DESIGN** | String-per-texture cache versus glyph strip per size. Same pixels, no per-value upload. |
| `measureHudTextWidth` | `TextRenderer::measure` | MATCH | Same advance-width sum. |
| `officialHudUnitPx` / `officialHudX` / `officialHudY` | `scene::hudUnit` / `hudX` / `hudY` | MATCH | Same centred mapping on the 1000-unit canvas. |
| `getJudgeOffset` | `GameplaySession::touchPosition` | MATCH | Same normalised distance pipeline; the full comparison is in the note-geometry section. |
| `spawnHoldHeadHitEffect` / `spawnHoldBodyHitEffect` | hold hit-effect spawn | MATCH | Same effect spawn for a hold head and its body, with the same alpha and part count. |
| `hasHitEffectTint` :10735-10737 | `Note::tintR/G/B` presence test | MATCH | Same one-flag presence check. |
| `hitEffectTintR` / `hitEffectTintG` / `hitEffectTintB` :10739-10749 | tint fallback in the hit-effect draw | MATCH | Same rule per channel: use the note's tint only when the flag is set and the channel is finite, otherwise the caller's fallback. |
| `getLineRotDeg` :10751-10764 | `pose.rotationDeg` (`NoteBatch.cpp:294`) | MATCH | Same `rotateDeg + (isAbove ? 0 : 180)`, with the same 0 fallback when the line or its state is missing. |
| `computeNoteHeadPositionOnLine` | `resolveNoteGeometry` head position | MATCH | Same head position from the along-line offset and the line rotation. |

## ChartSelectActivity: the tab bar and card surfaces

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `setSongName` / `setDifficulty` / `setChartOffsetMs` / `persistSelectedChartInfo` | `ChartLibrary::updateInfo` | MATCH | Same three-field edit written back to the info file and the index. |
| `getMusicVolumePct` / `setMusicVolumePct` / `getSfxVolumePct` / `setSfxVolumePct` / `clampPctToMul` | `Settings` multiplier with `x100` display and a 0..5 clamp on write | MATCH | The reference clamps 0..500 percent then divides by 100; the port stores the multiplier and clamps it to 0..5. Identical over every reachable value. |
| `loadPersistedSettings` / `editPrefs` | `Settings::load` / `Settings::save` | MATCH | Same read and write of every setting with the same defaults. |
| `clampFinite` / `clampInt` | clamp helpers in `Settings`/`SettingsScreen` | MATCH | Same finite clamp and integer clamp. |
| `restartMarquee` | marquee restart in the label draw | MATCH | Same restart of the scrolling text cycle. |
| `stableId` | stable chart id | MATCH | Same stable id derived from the pack, so a re-scan keeps the selection. |
| `shortDifficulty` | difficulty abbreviation | MATCH | Same abbreviation table (`EZ`/`HD`/`IN`/`AT` and the numeric fallbacks). |
| `ChartTabBar` (2 constructors) / `setOnTabSelected` / `drawSkewRect` / `tabProgress` / `selectedAmount` / `centerFor` | the library tab bar in `LibraryScreen` | MATCH | Same skewed tab plate with the same selection animation (`tabProgress` drives the same width interpolation) and the same centred label. |
| `ChartCardAdapter` (2 constructors) / `getItemId` / `findLocalPosition` / `bindHolder` / `applyLocalState` | chart card drawing in `drawLibraryScreen` | MATCH | Same card content and the same per-card state (selected, manage mode); the adapter's view recycling has no counterpart because the port draws only the visible range. |
| `ChartCardFrame` | chart card panel draw | MATCH | Same skewed card panel behind each row. |

## The last four declarations

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `SettingsToggleView` (constructor) | `drawSwitch` (`SettingsScreen.cpp`) | DELIBERATE-EXCLUSION | A custom toggle `View`; the port draws the switch in place each frame. |
| `setChecked` | `settings` boolean written by the switch | MATCH | Same checked state written through to the setting. |
| `drawQuad` | `fillQuad` (`SettingsScreen.cpp:140`) | MATCH | Same four-corner quad fill. |
| `isPowerOfTwo` | power-of-two test in the backdrop | MATCH | Same test used where a texture size has to be a power of two. |
| `SettingsPreviewView` (constructor) | settings preview draw | DELIBERATE-EXCLUSION | A custom preview `View`; the port draws the preview inline. |

## Final residual declarations

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `rectPanel` | rounded panel draw in the library screen | MATCH | Same panel plate with the same corner radius. |
| `sanitizeAspectRatio` :2678-2690 | `Settings` aspect clamp | MATCH | Same handling: a non-finite value becomes 0, and 0 means "follow the window" rather than a literal ratio. |
| `shouldMarquee` | marquee predicate in the label draw | MATCH | Same "the text is wider than its box" test that starts the scroll. |
| `gradeName` | grade label | MATCH | Same `PHI`/`FC`/`AP`/letter mapping from the score and combo state. |
| `alphaAt` | `PrprVideo::alphaAt` | MATCH | Same clamped event evaluation of the video alpha. |
| `sanitizeScaleType` | scale-type sanitising | MATCH | Same fallback to the default scale type for an unknown value. |
| `toPlayResult` / `fromPlayResult` | replay record conversion | MATCH | Same field-for-field conversion between the replay record and the result struct. |
| `drawHitEffects` | calibration hit-effect draw | MATCH | Same hit effect drawn at the calibration tap. |
| `drawTouchMarkers` | calibration touch markers | MATCH | Same marker set drawn for the live touches. |
| `SelectPagerAdapter` / `createFragment` | library tab pages | MATCH | Same four pages bound to the tab bar; the port switches content per tab rather than instantiating fragments. |
| `hasTrack` | level-effect track presence | MATCH | Same presence test before the effect's tracks are evaluated. |
| `bpmAtBeat` | `BpmConverter` beat lookup | MATCH | Same BPM value at a beat through the timeline. |
| `decodeAndCache` | thumbnail decode and cache | MATCH | Same decode into the cover cache keyed by chart id. |
| `appendLoadedItem` | loaded chart append | MATCH | Same append of a loaded pack into the list. |
| `updateProgress` | download progress readout | DELIBERATE-EXCLUSION | Progress of the online download, which the port does not ship. |
| `getFilter` | search filter accessor | MATCH | Same accessor over the active query. |
| `SettingsPreviewView` | settings preview surface | MATCH | Same preview rendering of the current settings. |
| `drawAccent` | skew panel accent edge | MATCH | Same accent stripe on the panel's edge. |
| `UiWarmup` | interface warm-up | MATCH | Same one-time warm-up of the interface text layer before the first frame. |
| `queryDisplayName` | document display name | DELIBERATE-EXCLUSION | Android `SAF` document-name lookup; the port works on plain filesystem paths. |
| `isDifficultyEdgeWhitespace` | difficulty trim test | MATCH | Same edge-whitespace check before the difficulty label is compared. |
| `firstString` | first non-empty string | MATCH | Same first-non-empty selection over a key list. |
| `WavDecoder` | `audio::Decoder` wav path | MATCH | Same PCM wav decode through the shared decoder. |

## Small-file residuals: overlays, thumbnails and accessors

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `dimAt` | `PrprVideo::dimAt` (`src/game/PrprEffect.cpp`) | MATCH | Same clamped event evaluation of the video's dim value at a chart time. |
| `sizeOf` | thumbnail size helper in `CoverCache` | MATCH | Same target size rule for a decoded thumbnail. |
| `CoverImageLoader` (class) | `CoverCache` | MATCH | Same thumbnail cache keyed by chart id. |
| `decodeSampled` | downsampled decode | MATCH | Same decode straight to the thumbnail size instead of full size then scale. |
| `GameTextOverlay` (class) | storyboard text overlay in `GameRenderer` | MATCH | Same attach-UI text element drawn over the play field. |
| `setColors` | overlay colour assignment | MATCH | Same per-element colour. |
| `captureExternalColor` | external colour capture | MATCH | Same sampling of the colour the overlay inherits. |
| `LocalChartAdapter` (class) | chart card drawing in `drawLibraryScreen` | MATCH | Same list adapter role; the port draws the visible range directly. |
| `isManageMode` | `context.manageMode` | MATCH | Same mode flag. |
| `selectedIds` | selection set | MATCH | Same selected-id set. |
| `getObj` | json object accessor in the info parser | MATCH | Same object lookup with a missing-value guard. |
| `parseIntOrNull` | nullable integer parse | MATCH | Same parse that yields nothing on a non-numeric value instead of zero. |
| `dotCount` / `dotAt` | particle count and accessor in `MessageParticleField` | MATCH | Same live dot count and the same indexed accessor. |

## Video settings, the result page, replays and calibration: residuals

### `SelectVideoFragment.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `refreshSkinDropdown` | skin selection row | MATCH | Same skin list with the same entries. |
| `bindHitOffsetIndicatorModeDropdown` / `buildHitOffsetModeLabels` / `getHitOffsetModeLabel` | hit-offset mode row | MATCH | Same three labels (关闭/线性/径向) bound to the same 0..2 field. |
| `syncAutoplayRevealSwitches` | autoplay and reveal switch sync | MATCH | Same two switches kept in step with the stored settings. |
| `applyPresetSelection` / `updatePresetLabelForValue` / `buildPresetLabels` / `getPresetLabel` / `findPresetIndex` | aspect-ratio preset row | MATCH | Same preset list and the same nearest-preset lookup for a stored value. |
| `getScreenAspect` :440-446 | aspect fallback in `aspectRatioText` (`SettingsScreen.cpp:606-613`) | MATCH | Same `w / h` when the window has a size and the same `16 / 9` fallback otherwise; a stored value of 0 or less shows the window aspect instead of a number. |
| `formatFloat` :462-464 | `%.5f` at `SettingsScreen.cpp:611` | MATCH | Identical format string, five decimal places. |
| `performFiltering` / `publishResults` / `NoFilterStringAdapter` | search field filtering | DELIBERATE-EXCLUSION | `Filterable` plumbing for the `AutoCompleteTextView` skin picker; the port filters the list as it draws. |

### `ResultActivity.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `illustrationScrim` | illustration scrim in `ResultScreen` | MATCH | Same darkened scrim over the artwork. |
| `ShaderFactory` | linear-gradient background shader | MATCH | Same top-to-bottom gradient behind the result panels. |
| `setupResponsiveCanvas` | `ui::canvas` | MATCH | Same uniform scale and centring on the design canvas. |
| `blurBackground` / `gaussianBlur` | `TextureCache::blurredBackdropFor` | MATCH | Identical recipe: third-size copy, three box passes, radius `max(4, round(w * 0.05 / 3))`, bilinear upsample. Verified constant for constant earlier in `ui.md`. |

### `ReplayManager.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `getReplayData` / `getReplayInfo` / `getReplayList` | `ReplayStore::read` / `list` | MATCH | Same parse of a replay file and the same sorted listing. |
| `deleteReplay` / `deleteRecursive` | `ReplayStore::remove` + `platform::removeRecursively` | MATCH | Same recursive delete of one replay directory. |

### `CalibrationActivity.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `drawTap` | tap marker draw in `CalibrationScreen` | MATCH | Same marker drawn where the tap landed. |
| `getSeconds` | `SmoothingClock::seconds` | MATCH | Same seconds accessor over the smoothed clock. |

## SkewFrameLayout, UiTypography and GameGLSurfaceView: every declaration adjudicated

These three are the Android view/platform glue for the interface. Each maps either onto a
layout helper or onto a piece of the platform layer.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `SkewFrameLayout` (3 constructors) | `skewedBlock` (`SettingsScreen.cpp:138`) | DELIBERATE-EXCLUSION | A custom `ViewGroup` that draws a skewed panel; the port draws the skew inline with `skewedBlock` and has no widget. |
| `setPanelColors` | fill and stroke colours at the draw site | MATCH | Same fill and stroke palette. |
| `setSkewDegrees` | `kSlope` (tan 15°) and `kToggleSkewFactor` | MATCH | Same 15° slant, expressed as a slope constant. |
| `setStrokeWidthDp` / `drawStroke` | stroke draw in `skewedBlock` callers | MATCH | Same dp stroke, skipped when the colour alpha or the width is zero. |
| `setClipContentToSkew` / `setAntialiasedClip` | clip and antialias handling | DELIBERATE-EXCLUSION | Android clip-path API for keeping children inside the skew; the port's skewed quads are drawn with the surrounding scissor instead. |
| `setStraightLeftEdge` / `setStraightRightEdge` / `setTopWider` / `setAccentEdge` | per-block quad shaping in `skewedBlock` callers | MATCH | Same four panel variants (straight edge, top-wider, accent edge), each produced by its own quad. |
| `dispatchDraw` | per-frame draw | DELIBERATE-EXCLUSION | `ViewGroup` draw dispatch; the port draws the block inline. |
| `dispatchTouchEvent` / `containsPoint` :245-254 | rectangle hit test in `pressReleased` | MATCH (different shape) | The reference walks the skewed edge, so it narrows the hit region toward the slanted side; the port tests the row's bounding rectangle. The difference is the triangular sliver at each end of a row, which the row-based hit target deliberately covers so a tap near the edge still registers. |

### `UiTypography.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `UiTypography` (class) | `ui::TextStyle` + `Canvas::dp` | DELIBERATE-EXCLUSION | A static helper class over Android `Context`; the port folds the same conversions into the layout helpers. |
| `px` :26-28 | `Canvas::dp` / the density product | MATCH | Same `designDp * density`, and the density itself is the reference's 1.75 scaled by the user's interface zoom. |
| `preloadAsync` / `gameTypeface` / `loadGameTypeface` | `UiFontCache::initialize` | MATCH | Same one-time load of the bundled face, decoded before the first frame so the first screen is not stalled. |
| `applyTypeface` | face selection in `UiFontCache` | MATCH | Same face applied to every label, with the same bold synthesised when asked. |
| `useIndependentRenderer` | renderer choice | DELIBERATE-EXCLUSION | Selects the canvas-based text renderer on Android; the port has one text renderer. |
| `bindSingleLine` / `bindAllSingleLine` | label draw call sites | DELIBERATE-EXCLUSION | Attaches layout/text listeners so a fit re-runs on change; the port fits every frame, so there is nothing to bind. |
| `fitSingleLine` (2 overloads) :170 | the bisection in `UiLayout.cpp:348-363` | **FIXED** | The reference bisects for the largest size that still fits. The port ran three proportional steps, which did not converge for a label whose width shrinks non-linearly, so fitted text settled smaller than the reference. The bisection is now the same. |

### `GameGLSurfaceView.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `GameGLSurfaceView` | SDL GL context | DELIBERATE-EXCLUSION | A `GLSurfaceView` subclass; SDL owns the context and surface lifecycle. |
| `enableMultisample` | swap-chain MSAA attributes | MATCH | Same multisample request, applied through the platform's GL attributes. |
| `enableUnlimitedFrameRate` | `SDL_GL_SetSwapInterval(0)` | MATCH | Same "do not block on vsync" switch, exposed as the swap-interval diagnostic. |
| `bindToRenderer` | renderer attachment | MATCH | Same renderer bound to the surface. |
| `SimpleOnGestureListener` / `onSingleTapConfirmed` | tap routing in `processEvents` | MATCH | Same single-tap confirmation used for the play-field taps. |
| `createContext` / `destroyContext` | context creation and teardown | MATCH | Same create/destroy points. |
| `Es31ConfigChooser` / `chooseConfig` / `buildConfigAttribs` | GL context attributes | MATCH | Same ES 3.1 request with the same fallbacks; on the packaged platforms the attribute block in `Application::initialize` carries them. |

### Remaining renderer draws and activity helpers

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range now falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `renderGameplayFrame` / `renderSceneDirect` | `GameRenderer::render` | MATCH | Same frame composition and direct scene path. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question. |
| `drawAmetrineVignette` / `ametrineHudVignetteFactor` | `fx::AmetrineVignette` | MATCH | Same inline vignette and HUD factor. |
| `getOrLoadFileTexture` | `TextureCache::load` | MATCH | Same load-once cache. |
| `getOrCreateTextTexture` / `createHudTextTexture` (5) | `TextRenderer::glyphStrip` | **DIFFERENT BY DESIGN** | String-per-texture cache in the reference, glyph strip per size in the port. Same pixels, no per-value upload. |
| `measureHudTextWidth` | `TextRenderer::measure` | MATCH | Same advance-width sum. |
| `officialHudUnitPx` / `officialHudX` / `officialHudY` | `scene::hudUnit` / `hudX` / `hudY` | MATCH | Same centred mapping on the 1000-unit canvas. |
| `hitOffsetArrowHeight` :9211 | `arrowHeight` (`HudPass.cpp:645-648`) | MATCH | Same aspect derivation from the glyph texture, and the same square fallback when the texture is missing. |
| `drawHitOffsetText` | hit-offset readout text | MATCH | Same signed millisecond readout drawn under the dial. |
| `hitOffsetColor` | tick colour by window | MATCH | Same perfect/good/bad colour split. |
| `hitOffsetPointX` / `hitOffsetPointY` | dial point projection | MATCH | Same polar-to-cartesian placement at the offset angle. |
| `hitOffsetLineX` | line-mode tick position | MATCH | Same linear tick position along the indicator line. |
| `drawPauseTouchRing` | pause touch feedback ring | MATCH | Same ring drawn at the pause tap. |
| `drawTextureAnchored` (2 overloads) | anchored texture draw helpers in the batch | MATCH | Same anchor handling (centre and corner anchored variants). |

## ChartSelectActivity: the remaining accessors

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `refreshLocalChartsOnResume` / `findLocalRecordById` | library re-scan and id lookup | MATCH | Same behaviour. |
| `settingsCard` / `addSwitch` / `addSliderRow` / `addEditRow` / `addActionRow` / `addDualActions` | `BlockBuilder` control set | MATCH | Same controls with the same metrics. |
| `setSongName` / `setDifficulty` / `setChartOffsetMs` / `persistSelectedChartInfo` | `ChartLibrary::updateInfo` | MATCH | Same three-field edit written back to the pack's info file and the index. |
| `setAspectRatio` / `setKeyScale` / `setScrollSpeed` / `setBackgroundDim` | `Settings` with the same clamps | MATCH | Same field, same range (`aspectRatio` 0.5..5 or 0 for window-follow, `keyScale` 0.7..1.5, `scrollSpeed` 0.1..5, `backgroundDim` 0..1). |
| `setLowResMode` / `setShowFps` / `setRealtimeAcc` / `setScoreJumpEffect` / `setDisableShader` / `setStoryMode` / `setShowDebugInfo` / `setChartReveal` / `setMultiPressHighlight` / `setApfcIndicator` | `Settings` booleans | MATCH | Same field, same write-through. |
| `getHitOffsetIndicatorMode` / `setHitOffsetIndicatorMode` | `settings.hitOffsetIndicatorMode` | MATCH | Same 0..2 field; the setter goes through the same sanitizer. |
| `getMusicVolumePct` / `setMusicVolumePct` / `getSfxVolumePct` / `setSfxVolumePct` / `clampPctToMul` :2674-2676 | `SliderBind::MusicVolume` / `SoundVolume` (`SettingsScreen.cpp:416,452`) | MATCH | The reference clamps the percentage to 0..500 then divides by 100; the port stores the multiplier 0..5, shows it as `x100` and clamps the written value to 0..5. Identical for every reachable value. |
| `getAudioOffsetMs` | `settings.audioOffsetMs` | MATCH | Same field, clamped to -500..500. |
| `loadPersistedSettings` | `Settings::load` | MATCH | Same read of every setting with the same defaults and the same sanitising pass. |
| `isApfcIndicatorEnabled` | `settings.apfcIndicator` | MATCH | Same read. |
| `editPrefs` | `Settings::save` | MATCH | Same write-back to persistent storage. |
| `isReadyToStart` | start-button enable rule | MATCH | Same "a chart is selected and its files exist" gate. |
| `autoSingleLineText` / `uiTypeface` / `assetImage` | label and asset helpers | MATCH | Same single-line fitting, same bundled face and same asset lookup. |
| `tryTakePersistableRead` | document-permission handling | DELIBERATE-EXCLUSION | Android `SAF` permission persistence; the port works on plain filesystem paths. |
| `hasLowerSuffix` :2644-2646 | case-insensitive suffix test in the import path | MATCH | Same lowercased suffix comparison. |

## Local/replay pickers, the online adapter and the level-effect loader

### `SelectLocalChartsFragment.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `onSelectionChanged` / `refreshFromActivity` | selection sync in `drawLibraryScreen` | MATCH | Same selection maintained when the list is rebuilt. |
| `openRecord` / `showRecordDetail` / `populateDetail` | detail open path | MATCH | Same open-the-detail-page behaviour with the same fields filled. |
| `handleBackPressed` | back handling in the library screen | MATCH | Same back chain: leave manage mode, then leave the list. |
| `showList` (2 overloads) | list draw | MATCH | Same list view, filtered by the same query. |
| `updateManageActions` / `updateStateText` | manage-mode action row and empty-state text | MATCH | Same action set in manage mode and the same `本地暂无谱面` empty message. |
| `exportSelected` | export-selected action | MATCH | Same export of the selected packs to the documents directory. |
| `attachTextWatchers` / `setTextIfDifferent` | search field binding | DELIBERATE-EXCLUSION | Android text-watcher binding to keep the query in sync; the port reads the field directly each frame. |

### `SelectReplayFragment.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `enterSelectionMode` (2 overloads) / `exitSelectionMode` | replay list selection mode | MATCH | Same enter/exit of multi-select. |
| `handleExportClick` / `handleDeleteClick` | export and delete actions | MATCH | Same export and the same confirmed delete of the selected replays. |
| `refreshFromActivity` / `refreshList` | replay list rebuild | MATCH | Same re-read of the replay directory. |
| `updateActionButtons` | action-button state | MATCH | Same enable rule (nothing selected disables the actions). |
| `buildReplayItem` | replay row draw | MATCH | Same row content: song, difficulty, score, timestamp. |
| `toggleReplaySelection` | per-row selection toggle | MATCH | Same toggle. |

### `OnlineChartAdapter.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `OnlineChartAdapter` (class) | — | DELIBERATE-EXCLUSION | The remote-index list adapter; the port ships local packs only. |
| `addItems` / `setQuery` / `hasQuery` / `visibleCount` / `totalCount` / `notifyItemUpdated` | — | DELIBERATE-EXCLUSION | Remote-index paging and filtering; no port counterpart. |
| `onCreateViewHolder` / `onBindViewHolder` / `getItemCount` | — | DELIBERATE-EXCLUSION | `RecyclerView` adapter callbacks for the remote list. |
| `buildMeta` | — | DELIBERATE-EXCLUSION | Remote entry metadata formatting. |

### `OfficialLevelEffects.java` -> `src/game/OfficialLevelEffects.cpp`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `loadRoot` | level-effect root load | MATCH | Same read of the chart's `effectObjects` root. |
| `settingParams` / `effectSettings` | per-effect settings parse | MATCH | Same settings block parsed per effect. |
| `dogConsts` | Glitch-Dog constants | MATCH | Same constant set for the glitch-dog effect. |
| `slashUniforms` / `slashTracks` / `addSlashVars` / `slashKeys` | dimension-slash tracks | MATCH | Same per-slash variable and uniform wiring; strengths arrive pre-scaled by `0.01` on both sides. |
| `posEvents` | position event track | MATCH | Same position track feeding the effect. |

## ChartSelectActivity: the settings accessors

Most of the activity's remaining declarations are one-line accessors over its preference fields.
The port stores the same values in the `Settings` struct and writes them through the settings
file, so each accessor maps onto one field rather than one function.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `setKeyScale` | `Settings::keyScale` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`0.7..1.5`) and the same write-through on change. |
| `setAspectRatio` | `Settings::aspectRatio` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`0.5..5 / 0 follows window`) and the same write-through on change. |
| `setScrollSpeed` | `Settings::scrollSpeed` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`0.1..5`) and the same write-through on change. |
| `setBackgroundDim` | `Settings::backgroundDim` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`0..1`) and the same write-through on change. |
| `setLowResMode` | `Settings::lowResolutionMode` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setAntialias` | `Settings::antialias` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setShowFps` | `Settings::showFps` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setRealtimeAcc` | `Settings::realtimeAccuracy` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setScoreJumpEffect` | `Settings::scoreJumpEffect` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setDisableShader` | `Settings::disableChartShader` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setStoryMode` | `Settings::storyMode` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setShowDebugInfo` | `Settings::showDebugInfo` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setChartReveal` | `Settings::chartReveal` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setMultiPressHighlight` | `Settings::multiPressHighlight` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |
| `setApfcIndicator` | `Settings::apfcIndicator` (via `applySliderValue`/`drawSwitch`) | MATCH | Same field with the same clamp (`bool`) and the same write-through on change. |

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `getKeyScale` / `getAspectRatio` / `getScrollSpeed` / `getBackgroundDim` | `Settings` field read | MATCH | Same field, same value returned. |
| `isLowResMode` / `isAntialias` / `isShowFps` / `isRealtimeAcc` / `isScoreJumpEffect` / `isDisableShader` / `isStoryMode` / `isShowDebugInfo` / `isChartReveal` / `isMultiPressHighlight` / `isApfcIndicator` | `Settings` bool read | MATCH | Same field, same value returned. |
| `getSongName` / `getDifficulty` / `getChartOffsetMs` | `ChartRecord` fields | MATCH | Same per-chart metadata read from the selected record. |
| `setSongName` / `setDifficulty` / `setChartOffsetMs` / `persistSelectedChartInfo` | `ChartLibrary::updateInfo` | MATCH | Same three-field edit written back to the pack's info file and the index. |
| `setLocalChartDetailVisible` / `updateStartButtonState` | detail panel and start-button state | MATCH | Same detail visibility and the same enable rule for the start button. |
| `clearSelectedChartIfDeleted` | selection reset after a delete | MATCH | Same clearing of the selection when the selected pack is removed. |
| `onOnlineChartImported` | — | DELIBERATE-EXCLUSION | Import callback for the online library, which the port does not ship. |

## ChartSelectActivity: list, detail, dialogs and file pickers

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `refreshLocalChartsOnResume` | `ChartLibrary::records` + `warmUpAsync` | MATCH | Both re-scan the pack directory when the list may be stale. |
| `findLocalRecordById` | `ChartLibrary::find` | MATCH | Same lookup by id. |
| `settingsCard` | `BlockBuilder::heading` + section gap | MATCH | Same heading and bottom gap. |
| `addSwitch` / `addSliderRow` / `addEditRow` / `addActionRow` / `addDualActions` | `BlockBuilder::toggle`/`slider`/`edit`/`action`/`dual` | MATCH | Same control set with the same metrics. |
| `rowBase` / `smallButton` | row and small-button helpers in `SettingsScreen` | MATCH | Same base row and the same small button style. |
| `applyDetailBackground` / `clearDetailBackground` | detail backdrop in `DetailScreen` | MATCH | Same blurred-artwork backdrop, applied and cleared on the same transitions. |
| `renderDetail` | `drawDetailScreen` | MATCH | Same detail page. |
| `updateDetailTextSafety` / `updateTextSafety` / `textInset` | detail text fitting | MATCH | Same inset and the same ellipsis when the text would overflow. |
| `DetailCanvasHost` | `DetailScreen` draw host | DELIBERATE-EXCLUSION | A custom `ViewGroup` that hosts the detail canvas; the port draws the page directly. |
| `addDetailText` | detail label draw calls | MATCH | Same labels at the same anchors. |
| `SettingsBackdrop` | settings backdrop drawing | MATCH | Same slanted band behind the settings rows. |
| `showSearchDialog` | library search field | MATCH | Same query box and the same filter application. |
| `showPageJumpDialog` | page jump control | MATCH | Same jump-to-page field with the same clamp. |
| `showDetailInfoDialog` | detail info panel | MATCH | Same metadata panel. |
| `showSkinSelectDialog` | skin selection surface | MATCH | Same selection surface with the same entries; the row metrics are in `ui.md`. |
| `deleteSelectedSkin` | skin delete path | MATCH | Same delete with the same built-in-entry refusal. |
| `cycleHitOffsetMode` | hit-offset mode cycle | MATCH | Same 0→1→2→0 cycle through the sanitizer. |
| `loadOnlineIndex` / `ensureOnlineCover` | — | DELIBERATE-EXCLUSION | Online library paging and remote covers; the port ships local packs only (see the online section in `ui.md`). |
| `filterLocal` | query filter in `drawLibraryScreen` | MATCH | Same case-insensitive substring match over song name and difficulty. |
| `filterOnline` | — | DELIBERATE-EXCLUSION | Filter over the remote index, which the port does not have. |
| `toggleLocalSelection` / `updateLocalSelectionState` / `toggleSelectAllLocal` | selection set in `drawLibraryScreen` | MATCH | Same per-row toggle, same count readout, same select-all that respects the active filter. |
| `confirmDeleteSelected` | delete-selected action | MATCH | Same confirmation then delete of every selected id. |
| `startSelectedRecord` / `startPlay` / `putPlaySettings` | `populatePlay` + `navigateTo(Game)` | MATCH | Same play-setup assembly and the same hand-off to the gameplay screen. |
| `handleIncomingIntent` | `handleIncomingPath` | MATCH | Same routing of a delivered document to the import path. |
| `describeFile` | import dialog description | MATCH | Same kind label shown for a picked file. |
| `initLaunchers` / `launchPickZip` / `launchPickMusic` / `launchPickChart` / `launchPickBg` / `launchPickSkin` / `launchPickReplayZip` | `ImportDialogs` pickers | MATCH | Same seven picker entry points; the port drives them with path fields and the platform picker. |
| `selectLocalChart` / `getSelectedChartId` | `context.activeChartId` | MATCH | Same selected-chart state. |
| `tryUpdateSongInfo` | info edit path | MATCH | Same info-file update for a selected pack. |

## Every remaining `ChartSelectActivity` declaration, adjudicated

The reference builds its screens by assembling Android views; the port draws them immediately.
Most of the activity's methods are therefore view construction, recycling callbacks or Activity
lifecycle hooks that have no line-for-line counterpart. Each is adjudicated here so the
coverage matrix can distinguish "restructured by design" from "not yet looked at".

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `onCreate` :1-120 | `Application::initialize` + `main.cpp` | DELIBERATE-EXCLUSION | The platform hands an Activity its lifecycle; this application owns its window and loop directly, so there is no framework callback to reproduce. Startup work is recorded in `platform.md`. |
| `onSaveInstanceState` / `restoreUiState` / `renderRestoredScreen` | `AppContext` fields, persisted `settings.txt` | DELIBERATE-EXCLUSION | The reference saves view state because Android may destroy and recreate it; a single SDL process keeps its state, and the settings file covers what must survive a restart. |
| `onNewIntent` | `handleIncomingPath`, `handlePickerResult` | MATCH | A second launch with a document routes to the same import entry points; the frame loop drains the drop queue. |
| `onDestroy` | `Application::shutdown` | DELIBERATE-EXCLUSION | No Activity teardown callback exists here; shutdown releases the same resources. |
| `refreshLocalChartsOnResume` | `ChartLibrary::records`, `warmUpAsync` | MATCH | Both re-scan the pack directory when the list may be stale; the port also warms the index off the launch path. |
| `setupRoot` / `baseScreen` | `navigateTo`, per-screen draw functions | DELIBERATE-EXCLUSION | The reference builds a view tree as its root; the port draws each screen immediately and routes through `navigateTo`. |
| `applyImmersiveMode` | `SDL_WINDOW_FULLSCREEN` in `Application::initialize` | DELIBERATE-EXCLUSION | Hiding system UI is an Android window concern; the port's window is already fullscreen. |
| `renderHome` / `applyHomeLayout` | `drawHomeScreen` (`CommonScreens.cpp:324`) | MATCH | Same screen switch and section layout; the port draws it instead of assembling views. |
| `renderChartList` | `drawLibraryScreen` (`LibraryScreen.cpp:1133`) | MATCH | Same entry point, tab state and clear of the detail backdrop. |
| `renderSettings` | `drawSettingsScreen` (`SettingsScreen.cpp:1867`) | MATCH | Same tab clamp and scroll bookkeeping; the port also keeps a per-tab scroll offset. |
| `switchChartTab` :895-905 | `LibraryScreen.cpp` tab settle | MATCH | Same early-out on the current tab and the same minimum interval between switches. |
| `rememberLocalScrollPosition` / `restorePendingLocalScroll` | settings and library scroll state | MATCH | Both remember the offset when leaving a list and restore it on return. |
| `preloadLocalCovers` | `CoverCache`, `coverTexture` | MATCH | Both decode thumbnails ahead of the frame that shows them; the port bounds the work per frame instead of blocking. |
| `OnChildAttachStateChangeListener` / `onChildViewAttachedToWindow` / `onChildViewDetachedFromWindow` | `updateVisibleLocalCards` loop | DELIBERATE-EXCLUSION | A RecyclerView recycling callback; the port's list has no recycled views, so visibility is computed from the scroll window each frame. |
| `OnPreDrawListener` / `onPreDraw` | `syncWindowMetrics`, frame loop | DELIBERATE-EXCLUSION | Android's pre-draw hook for deferred layout; the port lays out inside the frame. |
| `menuButton` / `menuButtonLp` | `drawHomeScreen` icon row | MATCH | Same buttons in the same order with the same actions. |
| `openGithub` | `openExternalUrl` | MATCH | Both hand the URL to the platform opener. |
| `addSettingsTabs` | `kTabLabels` and the tab plate | MATCH | Same four labels, same order, same animation timing. |
| `buildVideoSettings` / `buildAudioSettings` / `buildReplaySettings` / `buildMoreSettings` | `buildVideoBlocks`/`buildAudioBlocks`/`buildReplayBlocks`/`buildMoreBlocks` | MATCH | Same section order and the same rows; row detail is in the settings table below. |
| `settingsCard` | `BlockBuilder::heading` plus section gap | MATCH | Same heading and bottom gap. |
| `addSwitch` / `addSliderRow` / `addEditRow` / `addActionRow` | `BlockBuilder::toggle`/`slider`/`edit`/`action` | MATCH | Same control metrics; the switch's checked mark and hit area are recorded below. |
| `syncSettingsToggles` | per-frame `sliderValue`/`toggleProgress` read | MATCH | The reference re-syncs view state after a programmatic change; the port reads the settings struct every frame, so it cannot go stale. |
| `SimpleWatcher` / `afterTextChanged` | `drawEdit` commit on Enter or focus loss | DELIBERATE-EXCLUSION | A TextWatcher is an Android text-view callback; the port has one immediate-mode edit field. |
| `returnFromSettings` | `navigateTo(context, context.settingsReturn)` | MATCH | Same return-to-caller behaviour. |
| `settingsRowWidth` / `applySettingsRowWidths` / `offsetSettingsRows` | `rowWidthPx`, `contentTopPx`, slanted band | MATCH | Same slanted row geometry; metrics are recorded below. |
| `updateSettingsPreview` | live re-read of `context.settings` | MATCH | The preview is the same read, so no explicit update call is needed. |
| `addBackButton` | `drawBackButton` | MATCH | Same icon and hit target. |
| `addChartListPanel` / `addChartTabs` / `addSearchBox` / `addChartActions` | `drawLibraryScreen` sections | MATCH | Same panels in the same order. |
| `addSideIcon` / `addDetailTopIcon` | detail screen icons | MATCH | Same icons and actions. |
| `addOnlinePageControls` | — | DELIBERATE-EXCLUSION | The reference's online-library page has no port counterpart; the port ships local packs only. Recorded as a known gap. |
| `updateVisibleChartItems` / `updateVisibleLocalCards` / `updateVisibleLocalCard` / `applyVisibleLocalCard` | `drawLibraryScreen` visible-range loop | MATCH | Same on-screen computation, done per frame instead of through view binding. |
| `setManageModeInPlace` | `context.manageMode` | MATCH | Same mode flag toggling the row actions. |
| `updateSelectionStateView` | selection highlight in `drawLibraryScreen` | MATCH | Same selected-row highlight. |
| `notifyVisibleChartItems` | — | DELIBERATE-EXCLUSION | RecyclerView adapter notification; the port redraws the list every frame. |
| `findLocalRecordById` | `ChartLibrary::find` | MATCH | Same lookup by id with the same miss behaviour. |
| `chartListRightMarginDp` | `rowWidthPx` margin | MATCH | Same margin value. |

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
