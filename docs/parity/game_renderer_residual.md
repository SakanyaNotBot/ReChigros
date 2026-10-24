# GameRenderer residual declarations — parity evidence

Scope: the 26 declarations from
`E:\Chigros\app\src\main\java\com\wuying\phigros\game\GameRenderer.java` that the automated
inventory (`docs/parity/coverage-matrix.md`) left as `unmatched`. For each one this report
answers a single question: does `E:\ReChigros` reproduce the *behaviour*, and if so, where?

Method: every Java body was read in full together with its call sites; the port was searched
for the body's distinctive constants (e.g. `1000`, `0.095`, `0.02`, `-0.012`, `1.5`, `60`,
`374761393`, `1442695041`), for the strings it produces, and for semantically equivalent names.
Constants, clamps, rounding and evaluation order were then compared numerically. A name
difference was never treated as a finding; a formula difference always was.

Reference line numbers are `GameRenderer.java` unless stated otherwise. Port paths are relative
to `E:\ReChigros`.

> Note on tree state: the port tree was being edited by other agents while this audit ran
> (`git status` shows a dirty worktree, and `src/render/scene/LinePass.cpp` shrank from 131 to
> 123 lines between two reads). Every port citation below was re-verified against the tree as
> it stood at the end of the audit.

## Verdicts

| Reference declaration | Reference line | Verdict | Port location (or "none") | Evidence (one line, with numbers where they matter) |
| --- | --- | --- | --- | --- |
| `officialHudUnitPx` | 8662 | MATCH | `src/render/scene/HudPass.cpp:796` (`hudUnit`), `src/render/scene/StageLayout.cpp:48` (`hudUnitScale`) | Both are `stageH > 1e-6 ? stageH/1000 : 1`; the port calls it `hudUnit(layout)` and the canvas caches it as `canvas.unit` (`HudPass.cpp:816`). |
| `officialHudHalfUiW` | 8666 | DIFFERS | `src/render/scene/HudPass.cpp:817-819` | Port has the *clamped* form: `stageH > 1e-6 ? stageW/stageH*500 : 500`. Java returns `aspect * 500` with `aspect = stageH > 1e-6 ? stageW/stageH : OFFICIAL_HUD_MAX_ASPECT` (16/9 = 1.7778). The two only diverge in the degenerate `stageH <= 1e-6` case, where Java yields `888.889` and the port yields `500`. |
| `officialHudBottomTextYOffsetPx` | 8687 | MATCH | `src/render/scene/HudPass.cpp:889` | `kBottomTextYFixAt1080 * stageH / 1080` with `kBottomTextYFixAt1080 = 5.0F` (`HudPass.cpp:41`) == Java `OFFICIAL_HUD_BOTTOM_TEXT_Y_FIX_AT_1080PX = 5f` (`:784`). |
| `hitOffsetLineBottomSafety` | 9103 | DIFFERS | `src/render/scene/HudPass.cpp:655` | Java: `hitOffsetArrowHeight(meterW) + dp(5)`, and `hitOffsetArrowHeight` returns `arrowWidth` (not 0) when the arrow texture is missing. Port: `(hasArrow ? arrowHeight : 0.0F) + dp*5`. With no arrow asset the line sits `arrowWidth` px (12–17 dp) **lower** in the port than in the reference. Same divergence in the dial path (`HudPass.cpp:713-715`). |
| `hitOffsetBottomSafety` | 9107 | DIFFERS | `src/render/scene/HudPass.cpp:710-715` | Same root cause: Java's `hitOffsetArrowHeight` degrades to `hitOffsetArrowWidth(meterW)` when `texHitOffsetArrow` is null/zero-sized, so `Math.max(safety, arrowBelowArc + arrowHalfBounds + dp(2))` still adds a real icon footprint; the port short-circuits to `dp*3` alone (`hasArrow ? max(dp*3, arrowBelowArc + arrowHalfBounds + dp*2) : dp*3`). The `dp(3)`, `dp(2)` and `hypot`→`sqrt(w²+h²)` parts otherwise match. |
| `hitOffsetArrowWidth` | 9207 | MATCH | `src/render/scene/HudPass.cpp:636` | `clamp(meterWidth * 0.095, dp*12, dp*17)` — identical factor and clamps. |
| `hitOffsetArrowHeight` | 9211 | MATCH | `src/render/scene/HudPass.cpp:637-643` | `arrowHeight = arrowWidth` by default, then `arrowWidth * texH / max(1, texW)` when the texture is valid — identical aspect-preserving form and identical `max(1, w)` guard. (Only its *callers* differ; see the two safety rows.) |
| `hitOffsetColor` | 9247 | MATCH | `src/render/scene/HudPass.cpp:569-583` (`offsetColour`) | Same switch on `judgement`: good→yellow, bad→red, default→green, with the same RGB triples `(1.00,0.86,0.28)`, `(1.00,0.36,0.31)`, `(0.42,1.00,0.36)` (`HudPass.cpp:502-509` == Java `:544-555`). |
| `hitOffsetPointX` | 9258 | MATCH | `src/render/scene/HudPass.cpp:533-536` | `cx + sin(rad) * radius`. |
| `hitOffsetPointY` | 9263 | MATCH | `src/render/scene/HudPass.cpp:538-541` | `baseY - cos(rad) * radius`. |
| `blockedComboNoiseSample` | 8154 | ABSENT | none | Port has no counterpart at all: no `blockedCombo*`, no `BlockNoise1` CPU sampler (the texture is only bound as a shader uniform, `src/render/GameRenderer.cpp:327-332`), and no caller of a CPU noise sampler. |
| `blockedComboNoise` | 8176 | ABSENT | none | Grep for the exact hash multipliers `374761393`, `668265263`, `1442695041`, `1274126177` over all of `src/`, `include/`, `tools/` returns zero hits. |
| `measureHudTextWidth` | 8183 | MATCH | `src/render/scene/HudPass.cpp:139-146` (`measureLegacy`) | Same "legacy advance" model: base font advance + `0.02 em` tracking between codepoints + `-0.012 em` on every space (`HudPass.cpp:50-51` == Java `:791-792`). The HUD font (`assets/res/font.ttf`) has a space advance of `235/1000 em`, matching the reference's `res/font.ttf` (`HUD_FONT_ASSET`, Java `:753`). |
| `ellipsizeToWidth` (4-arg + 5-arg) | 7837, 7841 | MATCH | `src/render/scene/HudPass.cpp:149-182` (`ellipsizeLegacy`) | Same algorithm and the same three early-outs: `maxWidth <= 1 → text`, full text fits → text, `"..."` wider than the box → return `"..."`; then a codepoint-boundary prefix search on `prefix + "..." <= maxWidth`. |
| `normalizeAlpha01` | 7036 | MATCH | `src/render/scene/NoteBatch.cpp:34-38` (`normalizeNoteAlpha01`), `src/render/GameRenderer.cpp:1044-1045` (attachUI) | Same three rules in the same order: non-finite → `1`, `> 1.5 → clamp(a/255)`, else `clamp01`. Port also applies `a < -2.5 → /255` in `NoteBatch.cpp`, which is the same rule the RPE parser applies (`src/chart/RpeChartParser.cpp:248-256`). |
| `ensureLineDrawOrder` | 7122 | MATCH | `src/render/scene/NoteBatch.cpp:250-272` (`buildLineDrawOrder`) | Identical stable insertion sort by **ascending** `zOrder` (`otherZ > keyZ` shifts right), same `lineDrawOrderCount == n` early-out (`GameRenderer.cpp:1416-1418` re-checks size, `:518` builds once per chart). |
| `resolveLineText` | 6730 | MATCH | `src/core/JudgeLine.cpp:139-142` (`fillState`), exposed as `JudgeLine::State::text` | Same rule: with text events, `EventUtils.getTextVal(sec2beat(t), events, curText)`; without, the line's static `text`. The port resolves it inside `fillState` (so `LineFrame.text` is already resolved, `LinePass.cpp:78`) rather than in a separate renderer helper — same behaviour, different layering. |
| `currentChartGlobalBeat` | 6748 | MATCH | `src/render/GameRenderer.cpp:799-801` | Same: `chart.bpmTimeline != null ? chart.bpmTimeline.secToBeat(t) : NaN`; port stores it as `NotePassParams::chartGlobalBeat`, consumed by `isBeforeAppearTime` in `NoteBatch.cpp:365` and `:548`. `BpmTimeline::secToBeat` (`src/core/BpmTimeline.cpp:54-62`) matches the Java timeline conversion term-for-term. |
| `officialLevelStartEase` | 8918 | MATCH | `src/render/scene/Transition.cpp:70-75` (`introUiEase`) | Identical: `sampleCurve(LEVEL_START_ROOT_SCALE_Y, t)`, `denom = 1.1875 - 1`, run-forwards ease `clamp((1.1875 - rootScaleY)/denom, 0, 1)`, same `abs(denom) < 1e-6` fallback. **But the port never calls it on the HUD path** — see the note below the table. |
| `officialLevelOverRootScaleY` | 8925 | MATCH | `src/render/scene/HudPass.cpp:419-422` (`computeRootScale`, outro branch) | Same `1 + (1.1875 - 1) * smoothStep(p)`, `smoothStep = p²(3-2p)`. |
| `sampleCurve` | 8908 | MATCH | `src/render/scene/HudPass.cpp:79-87` (also `src/render/scene/Transition.cpp:50-62`) | Same piecewise-linear sampling with `clamp01(t) * (n-1)`, `floor` for the index, guards for `n == 0` (`clamp(t)`) and `n == 1`. |
| `prepareNativeTimelineLineStates` | 6619 | PORT-ONLY-N/A | none (no analogue needed) | Mechanism: a JNI bridge. `NativeRenderCommands.evaluateLineTimeline(...)` pushes a `LineState` packed array (`LINE_FLAG_STRIDE=7`, `LINE_FLOAT_STRIDE=20`, `LINE_DOUBLE_STRIDE=2`) across JNI; the port has no JNI render bridge at all (its only `jni.h` users are `src/platform/FileDialog.cpp` and `src/platform/FileSystem.cpp`). The port evaluates line state in-process in `LinePass.cpp::evaluateLines` → `JudgeLine::fillState`. |
| `applyNativeTimelineLineStates` | 6671 | PORT-ONLY-N/A | none (no analogue needed) | Same mechanism: unpacks `packedFlags/packedFloats/packedDoubles` into `RenderLineInfo`/`JudgeLine.StateHolder`. The port writes `LineFrame` structs directly from `fillState` (`LinePass.cpp:40-79`), so there is nothing to unpack. |
| `buildNativeBucket` | 12966 | PORT-ONLY-N/A | `src/render/scene/NoteBatch.cpp:319-495` / `:498+` are the *behavioural* equivalent | Builds a C-side note bucket mirror (per-line `lineStart`/`lineCountByLine`, `ordered.sort(NOTE_HOLD_END_ORDER)`, `getNoteWidthScale`) for the native batch builder. The port has no native batch builder and needs no mirror: the note passes iterate `line.notesByType` (`src/core/JudgeLine.cpp:213-217`) in time order and cull with `firstNoteAtOrAfter` / `line.maxHoldDurationSec` (`NoteBatch.cpp:349-350`, `:531-534`). |
| `canUseNativeRenderCommands` | 6599 | PORT-ONLY-N/A | none (no analogue needed) | Pure feature-gate over the native path: `NativeRenderCommands.isAvailable() && nativeRenderBucketsValid && nativeRenderSupported && nativeLineState != null && batchBuffer != null && lineDrawOrder != null && lines != null`. No native command buffer exists in the port; `SpriteBatch` draws with `glDrawArraysInstanced` (`src/render/batch/SpriteBatch.cpp:288`) from CPU-side quads. |
| `canUseNativeTimeline` | 6609 | PORT-ONLY-N/A | none (no analogue needed) | Same gate plus `nativeLineTimeline.nativeHandle != 0` and `nativeLineState.lineCount >= lines.size()`. Same mechanism, no counterpart possible. |

Resolved: **26 / 26** (20 `MATCH`, 3 `DIFFERS`, 2 `ABSENT`, 5 `PORT-ONLY-N/A`; the two
`ellipsizeToWidth` overloads and the five native rows account for the count difference).

### One cross-cutting note that is not a row

`officialLevelStartEase` itself matches, but the port's HUD consumes a **linear** intro
progress: `src/app/GameScreen.cpp:1771-1773` sets `introProgress = clamp(introElapsedSec /
(40/60), 0, 1)` and `src/render/scene/HudPass.cpp:424` feeds that straight into
`sampleCurve(kLevelStartRootScaleY, …)`. The reference first converts elapsed seconds through
`officialLevelStartEase` (`GameRenderer.java:8882`, then `:8900`). `introUiEase`
(`Transition.cpp:70`) exists and is unit-tested (`tests/TransitionTests.cpp:56-57`) but has
**zero call sites in `src/`**. Both sides end at the same root scale `1.1875 → 1`, but the
*shape* differs: the reference eases through the inverse of its own sampled curve, the port
ramps linearly. Visible consequence: the HUD start squash eases in slightly differently over
the first 40 frames. This is a property of the call site, not of the declaration, so it is
filed here rather than as a `DIFFERS` row on `officialLevelStartEase`.

## Findings that need action

### D1 — `officialHudHalfUiW`: degenerate-height fallback is `500` instead of `888.889`

* Reference (`GameRenderer.java:8666-8673`):
  ```java
  float aspect = (stageH > 1e-6f) ? (stageW / stageH) : OFFICIAL_HUD_MAX_ASPECT; // 16f/9f
  return aspect * OFFICIAL_HUD_HALF_HEIGHT;                                      // * 500
  ```
* Port (`src/render/scene/HudPass.cpp:817-819`):
  ```cpp
  canvas.halfUiW = layout.stage.height > 1.0e-6F
      ? layout.stage.width / layout.stage.height * 500.0F
      : 500.0F;
  ```
* Difference: on the fallback branch Java gives `16/9 * 500 = 888.889`, the port gives `500`
  — a 388.889-unit (≈ 39 % of the canvas half-width) error.
* Fix: `: 16.0F / 9.0F * 500.0F`.
* Reachability: only when the stage height collapses to ≤ 1e-6 px. `computeStageLayout`
  (`StageLayout.cpp:9-10`) clamps `viewHeight` to ≥ 1.0 and always derives a positive stage
  height, so this is currently unreachable in practice. Fix it for exactness, but do not
  prioritise it over D2/D3.

### D2 — `hitOffsetBottomSafety`: missing icon footprint when the arrow texture is absent

* Reference (`GameRenderer.java:9107-9117`) starts from `safety = dp(3)` and returns early
  **only** if the texture is null/zero-sized — but `hitOffsetArrowWidth`/`Height` are still
  defined in that state, and `hitOffsetArrowHeight` falls back to `hitOffsetArrowWidth(meterW)`
  (`:9211-9218`). The final value is `max(dp(3), endpointCos*max(0, radius-arrowRadius) +
  0.5*hypot(iconW, iconH) + dp(2))`.
* Port (`src/render/scene/HudPass.cpp:710-715`) skips the whole term when `!hasArrow`:
  `bottomSafety = hasArrow ? max(dp*3, arrowBelowArc + arrowHalfBounds + dp*2) : dp*3`.
* Numbers: with `dp = 1.75 * viewH/1080` (port's `referenceDp`, `HudPass.cpp:518-520`) and a
  1080-tall viewport (`dp = 1.75`), a typical `meterW = 190*1.75 = 332.5` gives
  `arrowWidth = clamp(332.5*0.095, 21, 29.75) = 29.75`. `endpointCos = cos(60°) = 0.5`,
  `radius = 166.25`, `arrowRadius = radius - 9*dp = 150.5`, so `arrowBelowArc = 7.875` and
  `arrowHalfBounds = 0.5*sqrt(29.75² + 29.75²) = 21.03`. Reference safety = `max(5.25, 7.875 +
  21.03 + 3.5) = 32.41`; port = `5.25`. The dial is placed `27.2` px lower in the reference
  than in the port when `res/arrow.png` fails to load.
* `hitOffsetLineBottomSafety` has the same shape (`HudPass.cpp:655` vs `:9103-9105`):
  reference `arrowHeight + dp(5) = 29.75 + 8.75 = 38.5`; port `0 + 8.75 = 8.75`, i.e. the
  horizontal line variant sits `29.75` px lower in the port.
* Fix: drop the `hasArrow ? … : …` special case and always use `arrowWidth` (already computed
  unconditionally at `HudPass.cpp:636`) as the height fallback, exactly as
  `hitOffsetArrowHeight` does in both codebases:
  ```cpp
  // line mode
  const float bottomSafety = arrowHeight + dp * 5.0F;   // arrowHeight already falls back to arrowWidth
  // dial mode
  const float bottomSafety = std::max(dp * 3.0F, arrowBelowArc + arrowHalfBounds + dp * 2.0F);
  ```
  Note `arrowHeight` is already initialised to `arrowWidth` at `HudPass.cpp:637`, so the fix
  is to stop re-testing `hasArrow` at the two safety sites.
* Player-visible consequence: an asset-load failure (or a skin without `arrow.png`) moves the
  judgement-offset dial/line down against the bottom edge, where it can clip. With the shipped
  asset present both sides agree; this is a robustness divergence, not a regression on the
  nominal path.

### A1 — `blockedComboNoiseSample` is entirely missing (Ametrine blocked combo)

* Reference behaviour (`GameRenderer.java:8154-8173`): samples `official_textures/BlockNoise1.png`
  on the CPU as a displacement field for the "blocked combo" pattern. Sampling is
  `px = (int)(abs(u) * w) % w`, `py = (int)(abs(v) * h) % h` (Point + Mirror, matching the
  shipped import), returning the **red** channel as `((c >>> 16) & 0xFF)/255`; when the asset
  is missing it falls back to a hash
  `(((int(u*64)*374761393 + int(v*64)*668265263) >>> 8) & 0xFFFF)/65535`.
* Reference call site (`:8086-8116`): two samples per mask cell,
  `n1 = sample(st + dir*scroll)`, `n2 = sample(st + perp*scroll)`, combined as
  `off = (dir*(n1-0.5) + perp*(n2-0.5)) * 0.1` with `dir = (0.70710678, 0.70710678)`,
  `st = (u*2.13, v*1.02)`, `scroll = (t/20)*2.59`. The mask cell is a screen/8 texel
  (8 px per texel, `padCells = 3`).
* Port: **nothing**. Searched `src/`, `include/`, `assets/`, `tools/`, `tests/` for
  `blockedCombo`, `BlockNoise`, `blockNoise`, `garbled`, `blocked_ui`, and for the hash
  multipliers `374761393`/`668265263`/`1442695041`/`1274126177` — zero hits outside of the
  unrelated GPU path. `assets/official_fonts/blocked_ui.ttf` ships in the port but has **no
  reference in any source file**, which confirms the feature was never ported, not merely
  renamed. The GPU block shader (`assets/official_shaders/block_prepass.glsl`,
  `src/render/fx/BlockCompositePass.cpp:41-42`) carries the same `(2.13, 1.02, 2.59, 0.1)`
  uniforms, but that is the **red-zone** block composite, a different feature.
* Player-visible consequence: on Ametrine (and only there, when the challenge is failed) the
  reference replaces the combo number and label with a garbled, per-frame displaced block-text
  pattern; the port keeps drawing the ordinary combo number and label. The reference also
  gates this on `isAmetrineComboBlocked() || debugBlockedComboDemo > 0`
  (`GameRenderer.java:8612`), and the port's `OfficialLevelEffects` does track
  `challengeFailed_` (`include/rechigros/game/OfficialLevelEffects.hpp:87`) — but
  `isChallengeFailed()` has **no caller outside the class** in the port
  (`src/core/OfficialLevelEffects.cpp:393` only), and `HudValues::showCombo`
  (`include/rechigros/render/scene/HudPass.hpp:72`) is never written by
  `GameRenderer.cpp` (default `true`, no assignment), so there is currently no path that can
  even suppress the plain combo widgets.
* To fix: (1) apply an `isChallengeFailed`/debug gate to `HudValues::showCombo` in
  `GameRenderer::render`; (2) add a `buildBlockedComboPattern` equivalent (rasterise the combo
  string into a boolean mask at 8 px/cell with 3 cells of padding, using
  `assets/official_fonts/blocked_ui.ttf`); (3) add the CPU `BlockNoise1` sampler above
  (note the port already loads that texture for the GPU path with the correct Point + Mirror
  state at `src/render/GameRenderer.cpp:327-332`, so the asset and sampling mode are known
  good); (4) port `drawBlockedComboPattern` (`:8086-8148`) including the per-cell
  `breathe`/`live` hash.

### A2 — `blockedComboNoise` is entirely missing

* Reference (`GameRenderer.java:8176-8181`): a deterministic per-`(cellX, cellY, frame)` value
  used to vary each block's size, fill alpha and edge alpha per frame:
  ```java
  int h = x*374761393 + y*668265263 + frame*1442695041;
  h = (h ^ (h >>> 13)) * 1274126177;
  h = h ^ (h >>> 16);
  return (h & 0xFFFFFF) / (float) 0x1000000;
  ```
  `frame = floor(t*30)` (`:8096`); `breathe` uses `frame = 0` (static) and `live` the animated
  one (`:8119-8120`), feeding `outer = cell*(0.78 - (breathe-0.5)*0.12)`, `fillA = a*(0.80 +
  0.15*breathe)`, `edgeA = a*(0.72 + 0.20*breathe)` and the three glow layers' `+0.05/0.08/0.10
  * live`.
* Port: no counterpart (same searches as A1).
* Player-visible consequence: same as A1 — the entire blocked-combo pattern, including its
  per-frame "breathing", is absent.
* To fix: port the hash verbatim. It is self-contained (no assets) and both `int` overflow and
  `>>>` must be reproduced with `std::uint32_t` and a logical shift. `frame` must come from
  `floor(chartTimeSec * 30.0)`.

## Items I could not resolve

None. All 26 declarations were resolved to a verdict with a cited port location or a
documented absence.

Two verdicts rest on a judgement call that a reviewer may want to re-confirm, both stated
explicitly above:

1. `hitOffsetArrowHeight` is marked `MATCH` because its own body is identical; the divergence
   that matters is in its two **callers**, which are the rows `hitOffsetBottomSafety` and
   `hitOffsetLineBottomSafety` (`DIFFERS`). Marking the callee `DIFFERS` too would double-count
   the same defect.
2. The five `native*`/`canUseNative*` declarations are marked `PORT-ONLY-N/A`, not `ABSENT`,
   because the port does render lines and notes — it simply does so in-process
   (`LinePass.cpp::evaluateLines`, `NoteBatch.cpp::buildNoteHead/ buildHoldBatch`) rather than
   by marshalling through a JNI command buffer. Their *behavioural* subject matter (line state
   evaluation, note bucketing, draw-order stability) is present and was checked separately:
   `buildLineDrawOrder` (`NoteBatch.cpp:250`) is the `ensureLineDrawOrder` counterpart and
   matches; `buildNativeBucket`'s ordering contract (`ordered.sort(NOTE_HOLD_END_ORDER)` by
   `holdEndTime`) is satisfied in the port by `line.notesByType` being time-sorted at
   preparation (`src/core/JudgeLine.cpp:213-217`).
