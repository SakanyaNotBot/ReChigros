# Render parity report

Scope: `src/render/**` (except `src/render/skin/SkinConfig.cpp`, owned by chart-parity),
`include/rechigros/render/**` (except `include/rechigros/render/skin/SkinConfig.hpp`).

Reference: `E:\Chigros` (read-only). Verified from source only — `MIGRATION.md`,
`HANDOVER.md` and `docs/COVERAGE_AUDIT.md` were treated as hints and cross-checked.

Verification gates at the time of writing (build `build\render-parity`, Release):

| Gate | Result |
| --- | --- |
| `ctest` | 8/8 passed |
| `scripts\verify-render-matrix.ps1` | 26/26 cases |
| `scripts\check-gles-shaders.ps1` | 43/43 shaders compile for GLSL ES 3.00 |
| `rechigros_block_probe` | framebuffer/viewport/blend restored; presented == off-screen composite (0.000000%) |

---

## 1. Defects found and fixed

Every item below was found by reading both sides and was measured to reach the picture
before/after. "Measured" means the render smoke tool was run twice with one input changed
and the percentage of differing sampled pixels recorded.

### 1.1 Mirror mode was completely ignored — FIXED

**Reference.** `computeStageRect` builds a stage-level pre-transform about the letterboxed
stage centre when `mirrorX` is set (`GameRenderer.java:3494-3504`):

```
Matrix.setIdentityM(stagePreTransform, 0);
stagePreTransformActive = false;
if (mirrorX) { cx = stageL + stageW*0.5f;
               translateM(cx,0,0); scaleM(-1,1,1); translateM(-cx,0,0);
               stagePreTransformActive = true; }
```

It is applied only while `inStageSpace` is true (`:6484-6494` for the reveal variant,
`:5511`, `:9402`, `:11277`, `:11312`, `:12699` for the sprite/batch/hit-fx/texture paths).
Sprite U coordinates are swapped once more (`:12709-12720`) so a reflected field does not
show reflected artwork. The HUD, background and side bands are drawn outside stage space
and stay unmirrored.

**Port before.** `RenderFrameInput::mirrorX` was declared (`GameRenderer.hpp:33`) and set
by `RenderSmoke.cpp:236` and `GameScreen.cpp:1656`, and `math::Mat4::mirrorX` existed — but
**`Mat4::mirrorX` had zero call sites** and the renderer never read `input.mirrorX`.

**Measured.** `--mirror` changed **0.00 %** of sampled pixels.

**Fix.**
- `scene::StageLayout` gained `bool mirror` and `stageProjection()`, which returns
  `projection() * Mat4::mirrorX(centerX())` when mirrored (`StageLayout.hpp:19-30`).
- `GameRenderer::Impl::drawScene` sets `layout.mirror = input.mirrorX`
  (`GameRenderer.cpp`, stage layout construction).
- Stage-space passes switched to `stageProjection()`; HUD and background stay on
  `projection()` (full audit list in §5).
- Note/hold batches re-swap U via `NotePassParams::flipU = layout.mirror`; hit-effect
  frames and bad-effect copies take a new `mirrorU` argument.

**Measured after.** On a letterboxed 1280x600 window the change mask is **100.00 %
symmetric about the stage centre**, and the side bands do not move —
`check_mirror.py` prints `PASS: the mirror reflects chart geometry about the stage centre
only`.

### 1.2 Level-effect HUD suppression was never consumed — FIXED

**Reference.** `officialHideHud = officialLevelEffects.hideHud` (`:1598`), and
`drawGameHudWithAnimationsClipped()` returns immediately when it is set and the run is not
exiting (`:8934`): Hate, Desultory Signals and Message hide score, combo, song name,
progress bar and pause button. The exit animation still shows the HUD.

**Port before.** `OfficialLevelEffects::hideHud()` existed but nothing read it; the HUD was
always drawn.

**Fix.** `drawScene` computes one `hudVisible` that folds in
`levelEffects->hideHud() && !inOutro`, and all three HUD draw points gate on it (the
red-zone-composite path, the main path, and the pause-backdrop soft-HUD pass).

**Measured.** With `--song desultorysignals` at t=115 the top HUD strip differs by 92.1 %
against the same frame without the level effect.

### 1.3 Ametrine failure vignette was missing entirely — FIXED

**Reference.** The Ametrine level mod darkens the screen from the corners when the
challenge is lost. It is a **standalone inline program**, not the storyboard
`simpleVignette` pass: radius `0.24`, smoothness `0.55`, black with `v * darkness` alpha,
drawn with classic `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` and the premultiplied state restored
afterwards (`:6565-6597`). It is drawn at the end of `drawGameplayObjects` (`:4707-4714`).
The HUD is dimmed by the same ramp: `1 - 0.32*v` (`:742`, `:7993-8003`).

**Port before.** No vignette, no HUD dim.

**Fix.** New `fx::AmetrineVignette` (`include/rechigros/render/fx/AmetrineVignette.hpp`,
`src/render/fx/AmetrineVignette.cpp`) reproduces the inline shader exactly, including the
blend state and its restoration. `drawScene` draws it after the scene content and before
the chain; `drawHudPass` multiplies HUD alpha by `AmetrineVignette::hudDimFactor(...)`.

**Ramp drive.** `drawLevelEffectObjects` advances
`OfficialLevelEffects::updateAmetrineVignette(chartTime, deltaSec)` once per frame from the
visual clock (clamped to 0.25 s, like the reference's `:16106-16113`), and
`prepareForChart` resets both the ramp and the challenge state on a chart switch.

**Measured.** With `--song ametrine` the level effect is selected and its
`official_simple_vignette` pass is loaded; at `darkness = 0` the frame is identical to the
effect-free frame (0.00 %), which is the correct behaviour because the ramp only rises once
the challenge fails or the authored window opens.

### 1.4 Autoplay hit bursts drew the wrong debris source — FIXED

**Reference.** Two separate burst paths, never both:
- manual play / replay: `hitEffects`, one `HitEffect` per judgement, debris rolled at spawn
  time with `Math.random()` (`:11055-11078`), drawn by `drawHitEffects` (`:11621`);
- autoplay: `chart.clickEffectsSorted`, debris **pre-rolled during chart preparation**
  (`ClickEffectItem`), drawn by `drawClickEffects` (`:11375-11543`), called only when
  `autoplay && !replayPlayback` (`:4691`).

**Port before.** Only the manual path existed; `grep ClickEffect src/render` returned
nothing, so the pre-rolled debris was never drawn and autoplay showed no click effects.

**Fix.** New `fx::ClickEffectField`
(`include/rechigros/render/fx/ClickEffectField.hpp`, `src/render/fx/ClickEffectField.cpp`)
walks `chart.clickEffectsSorted` with the reference's cursor/animation-start bookkeeping
(`animStartCached` / `animStartSec` / `positionCached`), evaluates
`hitFxParticleTravel` with the same Simpson integration and drag law, and draws the
spritesheet plus debris. `RenderFrameInput` gained `replayPlayback`; the pass runs when
`practice.autoPlay && !replayPlayback`.

### 1.5 Stage-scoped effects were applied to the whole viewport — FIXED

**Reference.** Non-global effects are drawn with the shader scissored to the play field;
the surround keeps the untouched scene (`applyPrprEffectChainStageViewport`,
`applyPrprEffectPassViewport`, `:5968-6076`). Global effects run full-frame **after** the
HUD has been composited in, so a global pass affects the HUD too (`:4409-4426`).
`renderGameplayFrame` (`:4200-4219`) picks one of: prpr chain, block composite,
low-res, direct.

**Port before.** `PrprChain::applyStageViewport` took a `StageRect` and immediately
discarded it (`(void)rect;`) and forwarded to the full-screen `apply`. Every effect —
stage-scoped or global — was therefore applied to the entire viewport, and the HUD was
always drawn after the chain, so a global effect never reached the HUD.

**Fix.**
- `PrprChain::applyStageViewport` now filters to active effects and runs each through a new
  `applyPassScissored`, which blits the input into the target, then clears and draws the
  shader inside the stage scissor box, so the surround passes through unprocessed. The
  scissor box is derived from the top-left stage rect with a bottom-left origin and clamped
  to the target (`stageViewportRect`).
- `drawScene` splits the chain into the reference's two segments: stage-scoped first, then
  — only if the chart has a global effect — the HUD is composited into the scene and the
  global passes run over the whole frame. A chart without a global effect therefore leaves
  the HUD unshaded, as in the reference.
- The red-zone composite path applies the same two-segment order before the composite
  samples the scene.

**Measured.** All 26 render-matrix cases (including `effect-bloom`, `effect-vignette`,
`setting-chart-shader`, `red-zones`) still pass, and the block probe reports the presented
frame identical to the off-screen composite (0.000000 %).

---

## 2. Verified MATCH (no change needed)

Selected high-risk matches, each read on both sides.

| Area | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Letterbox rule: never wider than the viewport, `ar = min(ar, viewAr)`, centred | `GameRenderer.java:3435-3466` | `scene/StageLayout.cpp:computeStageLayout` | MATCH |
| HUD unit = `stageH / 1000`, `x = cx + uiX*unit`, `y = cy - uiY*unit` | `:8662-8685` | `scene/HudPass.cpp:788-799` | MATCH |
| `halfUiW = (stageW/stageH) * 500` (follows the real stage aspect, not clamped to 16:9) | `:8666-8673` | `scene/HudPass.cpp:809-811` | MATCH |
| HUD intro/exit root scale curve, one key per frame of the 40-frame tween | `:602-628`, `:8900-8902` | `scene/HudPass.cpp:66-72`, `Transition.cpp:29-41` | MATCH (table values identical) |
| Intro line scale curve, 56 samples over 55 frames | `:586-600` | `scene/Transition.cpp:12-26` | MATCH (identical) |
| Stage/side/dim alpha curves | `:285-375` | `scene/Transition.cpp:109-143` | MATCH |
| Outro line collapse window 1.0 s .. 1.7 s | `:574-577`, `:271-283` | `Transition.hpp:12-13`, `Transition.cpp:87-100` | MATCH |
| Pause backdrop Hermite curve + vibrancy/brightness/flatten | `:1084-1127`, `:4034` | `scene/PauseBackdrop.cpp:13-31` | MATCH |
| Note width ratio `0.1234375 * min(ar/(16/9), 1)` | `:11342-11349` | `GameRenderer.cpp:noteWidthRatio` | MATCH |
| Note cull area = stage expanded by 1/12 per side, reveal-scaled | `:6515-6520` | `GameRenderer.cpp` cull bounds + `NoteBatch.cpp:169-204` | MATCH |
| Note control height / incline / y-control / skew formulas | `:7049-7120` | `scene/NoteBatch.cpp:69-99` | MATCH |
| Hit-effect debris: speed 15..35, spawn radius 0.2, thickness 0.2, drag K 2.657, lifetime 0.5, size envelope `((0.2078p-1.6524)p+1.6399)p+0.4988` | `GameConstants.java:59-62`, `GameRenderer.java:1242-1267`, `:11055-11078` | `fx/HitEffect.hpp:39-46`, `fx/HitEffect.cpp:17-66` | MATCH |
| Hit-effect back-dating `lag = max(0,(chartTime-noteTime)/speed)` | `:10696-10699`, `:10725-10728` | `fx/HitEffect.cpp:233-235`, `:250-252` | MATCH |
| Bad-effect: 0.5 s life, tint `(0.423529, 0.262745, 0.262745)`, alpha `1-p` | `:11580-11619` | `fx/BadEffectField.hpp:20-24` | MATCH |
| Judge-line width/thickness scaling and official vs editor length | `:7449-7466` | `GameRenderer.cpp:drawLinePass` | MATCH |
| Storyboard line texture scale (RPE 1350 authoring space, `useOfficialScale`, `rpeTextureScaleInPixels`) | `:7353-7400` | `GameRenderer.cpp:drawLinePass` | MATCH |
| Storyboard text at `50/1350 * stageW`, multi-line height scaling | `:7417-7441` | `GameRenderer.cpp:drawLinePass` | MATCH |
| Line text rasterised at `max(16, round(stageW*50/1350))` | `:7505` | `GameRenderer.cpp` + `text/TextRenderer.cpp` | MATCH |
| AP/FC indicator colours incl. `1,1,1` on bad/miss and `162,238,255` good | `:7299-7337` | `GameRenderer.cpp:lineTintFor` | MATCH |
| Pause button geometry `50.5887985 / 444.2999878 / 34.262 / 37.966`, visual aspect `35/41` | `:764-769`, `:3468-3478` | `scene/HudPass.cpp:18-23` | MATCH |
| HUD text sizes 36 / 50 / 26 / 70 / 24, bottom-text y fix 5 at 1080 | `:784-789` | `scene/HudPass.cpp:41-46` | MATCH |
| Legacy text tracking `0.02 em` and space adjust `-0.012 em` | `:790-792` | `scene/HudPass.cpp:50-51` | MATCH |
| Score formatted as 7 digits, accuracy as `%d.%02d%%` clamped to 10000 | `:8691-8706` | `scene/HudPass.cpp:89-101` | MATCH |
| Combo shown only from 3, label fades over 35/60 s | `:8625-8638`, `:8641-8646` | `scene/HudPass.cpp:964-1003`, `:424-429` | MATCH |
| Hit-offset indicator: alpha `*0.68`, meter `clamp(0.18*w, 120dp, 190dp)`, `60°`, colours, three-layer glow widths/alpha `0.16/0.34/0.9` | `:8976-9053`, `:544-555` | `scene/HudPass.cpp:602-750` | MATCH |
| Block inline look: fill `0.7132075/0.235493/0.235493 @ 0.667`, edge `1/0.3301886/0.3301886 @ 0.8`, disabled `0.497/0.138/0.138 @ 0.4`, glow `1/0.1792452/0.1792452 @ 0.8`, sparks `0.311/0.078/0.078` and `1/0.2849057/0.2849057`, pixel scale 6, 96-column cap, 28 noise columns, noise cutoff 0.86 | `:15908-15921`, `:15937-16045` | `fx/BlockOverlay.hpp:31-55`, `fx/BlockOverlay.cpp:59-321` | MATCH |
| Block mask layout (maskA R normal / G subtract / B warning fade; maskB R subtract / B ready / A hover) and additive `ONE/ONE` rasterisation with `0.09/0.12` gate | `:15378-15535`, `block_prepass.glsl` | `fx/BlockOverlay.cpp:151-187`, `fx/BlockCompositePass.cpp:258-331` | MATCH |
| Block pre-pass order compose → edge → 5 glow rings, `(5-i)/15` weights, last pass writes G only | `:15576-15674` | `fx/BlockCompositePass.cpp:341-403` | MATCH |
| Block composite uniforms: `matParams[8]`, `matColors[7]`, `matExtra[5]`, `time = t/20`, `timeY = t`, `stageRect`, `displaceDirection 0.70710678` | `:15177-15245`, `:15789-15814` | `fx/BlockCompositePass.cpp:20-47`, `:434-453` | MATCH |
| Block target sizes: masks/compose screen/8 point, effect screen/4 bilinear, glow ping screen/4 | `:15011-15034` | `fx/BlockCompositePass.hpp:48-50`, `.cpp:208-237` | MATCH |
| Glow weight formula `(passes-pass)/Σk` | `:15549-15555` | `fx/BlockCompositePass.cpp:119-129` | MATCH |
| DS grid: square side from CanvasScaler `matchWidthOrHeight 0.8`, reference 1920x1080, 2000 units, additive `SRC_ALPHA/ONE`, `texST 10`, colours/brightness, `DS_GRID_TIME_OFFSET 0.285`, freeze stops the pulse | `:4736-4829` | `fx/DsGridPass.cpp:16-39`, `:60-105` | MATCH |
| Kaleido backdrop: stage-rect viewport, premultiplied blend, mirrored wrap, `vertexColor = (0,0,0,bgAlpha)`, chart clock for `_Time` | `:4842-4910` | `GameRenderer.cpp:drawLevelEffectObjects` | MATCH |
| Message particles: `UNITS 10`, emit 10/s, 10 s life, box half-width 12.5, `MAX 512`, band alpha | `MessageParticleField.java:57-75` | `include/rechigros/game/MessageParticleField.hpp:19-27` | MATCH |
| Bloom: gate `radius*brightness > 0`, sampleRate-scaled prefilter, `ceil`-halved downs, `iteration-1` ups, composite `bloomColor = brightness` | `:5632-5719` | `fx/BloomPass.cpp:93-220` | MATCH |
| Bloom defaults 0.308 / 1.1 / 1.85 / 0.45 / 3 | `:5645-5650` | `fx/PrprChain.cpp:301-307` | MATCH |
| Vignette noise regenerated per frame at `sampleRate` scale, bound to `noiseCol` unit 4 | `:5852-5907` | `fx/PrprChain.cpp:225-278`, `:437-441` | MATCH |
| `UVScale` must be (1,1) or every fragment samples the centre texel | `:5737-5740` | `fx/PrprChain.cpp` (set for every program) | MATCH |
| FXAA thresholds `0.0312` / `0.125`, `dirReduce 0.03125`, search `±8`, `1/3`,`2/3` taps | `:14729-14771` | `fx/FxaaPass.cpp` | MATCH |
| Pause scalable blur: `0.5 + 8×0.0625` cross, radius scale from 1080 reference, 6 max depth | `:1087-1101`, `:14691-14709` | `fx/PauseBackdropPass.cpp` | MATCH |
| Background blur: downscale + repeated separable blur, built once per artwork | `:9504-9571` | `scene/BackgroundBlur.cpp:45-46`, `:99-198` | MATCH |
| Side columns: vertical gaps black, blurred artwork at `sideArtworkAlpha`, fixed `sideDimAlpha` overlay | `:11803-11895` | `GameRenderer.cpp:drawBackgroundPass` | MATCH |
| Chart reveal: target scale 0.30, 1.0 s smoothstep, hidden floor alpha 0.20, panel treatment identical to side bands | `:1269-1274`, `:3517-3568`, `:5077-5156` | `GameRenderer.cpp` reveal block + `drawRevealPanels` | MATCH |
| Low-res: scene into half-res target, HUD drawn with the scene, then full-frame upscale | `:4209-4213` | `GameRenderer.cpp` `useLowRes` path | MATCH |
| Antialias: whole frame (pause backdrop included) into a full-res target, then FXAA resolve | `:3814-3866` | `GameRenderer.cpp` `useAa` path | MATCH |
| Storyboard shader upgrade rules: strip `#version`, `texture2D`→`texture`, `gl_FragColor`→named out, collision-safe name, precision injection | `:14466-14573` | `gl/ShaderProgram.cpp:prepareShaderSource*` | MATCH |
| Outro hides non-line and storyboard elements, keeps plain-line collapse | `:262-269`, `:4654` | `GameRenderer.cpp:drawLinePass` | MATCH |

---

## 3. Shader pair comparison

All 29 reference shader assets exist in the port and were compared byte-wise, then read.
Byte-identical files are marked IDENTICAL; the rest were read in full and confirmed
semantically identical, differing only in comments (the port rewrote comments that named
the reference project or its reverse-engineering artefacts, which the port's comment rules
forbid).

| Asset | Verdict |
| --- | --- |
| `official_shaders/block_composite.glsl` | IDENTICAL (327 lines, byte-for-byte) |
| `official_shaders/block_prepass.glsl` | IDENTICAL (94 lines) |
| `prpr_shaders/chromatic.glsl` | IDENTICAL |
| `prpr_shaders/circle_blur.glsl` | IDENTICAL |
| `prpr_shaders/fisheye.glsl` | IDENTICAL |
| `prpr_shaders/glitch.glsl` | IDENTICAL |
| `prpr_shaders/grayscale.glsl` | IDENTICAL |
| `prpr_shaders/noise.glsl` | IDENTICAL |
| `prpr_shaders/official_bloom_composite.glsl` | IDENTICAL |
| `prpr_shaders/official_bloom_down.glsl` | IDENTICAL |
| `prpr_shaders/official_bloom_lum.glsl` | IDENTICAL |
| `prpr_shaders/official_bloom_up.glsl` | IDENTICAL |
| `prpr_shaders/official_ds_grid.glsl` | IDENTICAL |
| `prpr_shaders/official_esc.glsl` | IDENTICAL |
| `prpr_shaders/official_kaleido.glsl` | IDENTICAL |
| `prpr_shaders/official_lens_distortion.glsl` | IDENTICAL |
| `prpr_shaders/official_light_band.glsl` | IDENTICAL |
| `prpr_shaders/official_vignette_noise.glsl` | IDENTICAL |
| `prpr_shaders/official_vignette_plus.glsl` | IDENTICAL |
| `prpr_shaders/pixel.glsl` | IDENTICAL |
| `prpr_shaders/radial_blur.glsl` | IDENTICAL |
| `prpr_shaders/shockwave.glsl` | IDENTICAL |
| `prpr_shaders/vignette.glsl` | IDENTICAL |
| `prpr_shaders/official_dimension_slash.glsl` | Comment-only difference (5 lines); all 28 uniforms, defaults and the body match. Slash strengths arrive pre-scaled by `0.01`. |
| `prpr_shaders/official_glitch.glsl` | Comment-only difference (10 lines); logic identical. Documented that the shipped presets keep `rgbChannelOffset = 0`, selecting the single-sample variant after all three displacements. |
| `prpr_shaders/official_rgb_shift.glsl` | Comment-only difference (6 lines); the `radius >= 6.5` skip gate is present in both. |
| `prpr_shaders/official_glitch_dog.glsl` | Comment-only difference (21 lines); verified code-level that the mask radii/smoothness pairing is `centerMask` with `centerMaskRadius/Smoothness`, `edgeMask` with `edgeMaskRadius/Smoothness`, and the gate is `noise*centerMask + edgeMask` — matching the reference body. |
| `prpr_shaders/official_simple_vignette.glsl` | Comment-only difference (2 lines); body identical. |

Additional shaders that only exist in the port (inline, not assets) were diffed against
their reference Java string constants: the fullscreen/sprite/blur/pause-blur/pause-backdrop/
FXAA/batch/hit-fx programs (`GameRenderer.java:14586-14832`) all MATCH the port's
`fx/FullscreenPass.cpp`, `batch/SpriteBatch.cpp` and `fx/PauseBackdropPass.cpp` sources in
operation and blend semantics.

GLES conformance: `scripts\check-gles-shaders.ps1` compiles all 43 shader programs the port
uses for GLSL ES 3.00 — 43/43 pass. No shader hand-writes a version line; the
`ShaderProgram` normaliser injects it.

**New shader added by this work:** `fx/AmetrineVignette.cpp` carries the inline vignette
fragment (radius 0.24, smoothness 0.55, `vec4(0,0,0, v*darkness)`) as a port-internal
string, because it is an inline program in the reference too, not an asset.

---

## 4. Draw order and GL state per pass

Reference order established from `renderSceneDirect` (`:6394-6559`), `drawGameplayObjects`
(`:4629-4715`) and `renderSceneWithPrprEffects` (`:4254-4452`).

| # | Pass | Blend | Depth | Scissor | Port site |
| --- | --- | --- | --- | --- | --- |
| 1 | Clear | — | off | off | `drawScene` |
| 2 | Side columns: black gaps, blurred artwork, fixed dim | premultiplied `ONE/ONE_MINUS_SRC_ALPHA` | off | off | `drawBackgroundPass` |
| 3 | Stage backdrop artwork + dim | same | off | stage scissor in the reference; the port clips by UV instead (see §6) | `drawBackgroundPass` |
| 4 | Hate black mask (full screen) | premultiplied, scissor temporarily off, state restored | off | off | `drawLevelEffectObjects` |
| 5 | Level-effect objects (particles, kaleido, DS grid) | per pass: particles premultiplied, DS grid additive `SRC_ALPHA/ONE`, kaleido premultiplied; all restore | off | per pass | `drawLevelEffectObjects` |
| 6 | Judge lines (plain, texture, text) | premultiplied | off | off | `drawLinePass` |
| 7 | Hold bodies + heads, then note heads per texture bucket | premultiplied | off | off | `drawNotePasses` |
| 8 | Bad-effect copies (per texture) | premultiplied | off | off | `drawNotePasses` |
| 9 | Red zones (inline path) | premultiplied | off | off | `blockOverlay.drawAll` |
| 10 | Click effects (autoplay only) | premultiplied | off | off | `ClickEffectField::draw` |
| 11 | Hit effects (sheet then debris) | premultiplied | off | off | `drawHitEffects` |
| 12 | Chart-reveal ring | premultiplied | off | off | `drawRevealPanels` |
| 13 | Ametrine vignette | **classic `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`**, premultiplied restored | off | off | `AmetrineVignette::draw` |
| 14 | Storyboard chain, stage-scoped segment | blend disabled per pass, restored | off | **stage rect** | `PrprChain::applyStageViewport` |
| 15 | HUD composited (only when a global effect exists) | premultiplied | off | off | `drawHudPass` |
| 16 | Storyboard chain, global segment | blend disabled per pass, restored | off | off | `PrprChain::apply` |
| 17 | Present / blit to target | blend disabled | off | off | `drawScene` |
| 18 | HUD (when no global effect shaded it) | premultiplied | off | off | `drawHudPass` |
| 19 | Low-res upscale (only on that path) | blend disabled | off | off | `drawScene` |
| 20 | Pause backdrop: graded frame, then soft HUD | premultiplied | off | off inside the passes | `drawScene` pause block |
| 21 | FXAA resolve (only on the AA path) | blend disabled | off | off | `FxaaPass` |

Red-zone composite path replaces 14-18 with: chain stage-scoped → chain global → HUD into
the scene → masks → pre-passes → composite → restore, matching
`renderOfficialBlockComposite` (`:15683-15715`).

Saving-state discipline: `BlockCompositePass::rasteriseMasks`, `runPrepasses` and `draw`,
plus `DsGridPass::draw` and `AmetrineVignette::draw`, all query and restore the
framebuffer binding, viewport, blend enable, blend func, blend equation, depth, scissor and
(where toggled) colour mask. The block probe asserts this and reports
`PASS: framebuffer, viewport and blending restored`.

---

## 5. `stageProjection()` / `mirrorU` audit list

Requested explicitly: every draw that uses the mirrored stage projection must also swap U,
and nothing on the plain projection may accidentally be mirrored.

### Uses `stageProjection()` — stage-space geometry (moves with the mirror)

| Site | What it draws | U swap |
| --- | --- | --- |
| `GameRenderer.cpp` `drawLinePass` | plain judge lines, line textures, line text | n/a (quads use 0..1 UV; text and texture glyphs are symmetric when reflected, matching the reference which also does not swap them for lines) |
| `GameRenderer.cpp` `drawNotePasses` (heads) | tap/drag/flick heads | **yes** — `params.flipU = layout.mirror` → `addQuadFlippable` (`NoteBatch.cpp:135-140`) |
| `GameRenderer.cpp` `drawNotePasses` (holds) | hold head/body/tail, body sampler | **yes** — `addQuadFlippable` / `addQuadSelectFlippable` (`NoteBatch.cpp:142-148`) |
| `GameRenderer.cpp` `drawNotePasses` (bad effects) | fading bad copies | **yes** — `mirrorU` argument |
| `GameRenderer.cpp` `drawHitEffects` | hit sheet + debris | **yes** — `mirrorU` argument (`HitEffect.cpp`) |
| `GameRenderer.cpp` `drawHitEffects` (click effects) | autoplay sheet + debris | **yes** — `mirrorU` argument (`ClickEffectField.cpp`) |
| `GameRenderer.cpp` red-zone inline branch | zone silhouettes | no swap needed (procedural solid quads, no texture) |

### Uses `projection()` — must stay unmirrored

| Site | What it draws | Why |
| --- | --- | --- |
| `GameRenderer.cpp` `drawBackgroundPass` | artwork, side bands, dim overlays | reference draws these before `inStageSpace = true` |
| `GameRenderer.cpp` `drawRevealPanels` | reveal ring backdrop | same |
| `GameRenderer.cpp` `drawLevelEffectObjects` (Hate mask) | full-screen black | full-screen, no stage space |
| `GameRenderer.cpp` `drawHudPass` → `scene::drawHud` | every HUD widget | reference HUD is not stage-space |
| `GameRenderer.cpp` `drawScene` low-res / AA / pause blits | full-frame copies | full-screen |
| `AmetrineVignette`, `DsGridPass`, `KaleidoBackdropPass` | full-screen passes | no projection at all (clip-space quad) |

**Block composite:** the composite samples the *finished scene texture* and writes with a
full-screen quad, so it must not be mirrored — it receives `sceneLayout.stage` as its
`stageRect` gate, which is the stage rectangle (not mirrored) and is correct: the zone
effect is gated by where the stage is, while the mirrored content is already baked into the
sampled texture. Verified by reading `block_composite.glsl:169-176` (the `stageRect` gate)
against `BlockCompositePass::draw`.

---

## 6. Deliberate implementation differences (same picture)

These are noted so a future reader does not mistake them for divergences.

1. **Batch is instanced.** The reference emits one instance per quad into a
   `glDrawArraysInstanced` batch with a 15-float record; the port does the same with the
   same record layout (`SpriteBatch.hpp:15-33`, `static_assert` on 15 floats). No visual
   difference; this is the same optimisation the reference itself already uses.
2. **Blur chain shares a quad and a program.** `BackgroundBlur`, `PauseBackdropPass` and
   `FxaaPass` reuse `FullscreenQuad`/`FullscreenPass`; the reference creates one VAO per
   program. Same geometry, same taps.
3. **Red-zone masks are rasterised in one instanced-free draw with a colour-only program.**
   The reference uses the sprite shader for masks and relies on the sprite shader's
   premultiplied output; the port uses a dedicated raw-colour program because the sprite
   shader multiplies colour by alpha, which would destroy the mask coverage written into
   the colour channels. Comment in `fx/BlockCompositePass.cpp:49-51`. Same resulting
   mask values (verified by the block probe's mask dumps).
4. **Stage scissor replaced by UV clipping for the backdrop.** The reference scissors the
   stage rect for the background; the port emits one quad with UVs mapped to the visible
   part (`drawBackgroundPass` `drawCover`). This deliberately avoids the half-pixel
   disagreement the reference's integer scissor has against float dim quads; documented at
   the call site.
5. **Block windowing.** `BlockOverlay` keeps the authored block list sorted by appear time
   and advances only live entries instead of re-evaluating every entry per frame. Output is
   identical because a non-live entry contributes nothing; this is a pure O(n) → O(live)
   saving, required by the "no slower than the reference" constraint for chapter-level
   charts with >10 000 zones.
6. **Note range culling.** `NoteBatch` keeps per-type time-sorted lists and binary-searches
   the first note still inside its fade window, and skips the control-curve evaluation when
   a line has no y-controls and the note has no y-offset. Both are skipped-work-only
   shortcuts; the cull predicate and the control-curve results are unchanged.
7. **Random sequences are not bit-identical** (`HitEffect` xorshift vs `java.util.Random`,
   `ClickEffectItem` `std::mt19937`). Distribution, count and draw order match; individual
   particle positions do not. Inherent to the runtime change and deliberate so the port is
   deterministic across its own runs.

### Batching, hit-effect particles and the side backgrounds

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `obtainJudgeEvent` / `recycleJudgeEvents` | judgement-event batching in `GameplaySession` | DIFFERENT BY DESIGN | The reference pools judgement-event objects so a burst of judgements does not allocate. The port collects them in a vector cleared each frame. Same events delivered, no pool needed. |
| `addQuadToBatch` / `putBatchInstance` | `batch::SpriteBatch` instance append | MATCH | Same append of a quad instance into the current batch. |
| `setupVertexArrayObjects` / `createQuadVao` / `createBatchVao` / `uploadQuadUvBuffer` / `uploadBatchBuffer` | `SpriteBatch` VAO and buffer setup | MATCH | Same VAO per batch kind and the same buffer upload strategy. |
| `flushBatch` (2 overloads) / `flushHitFxBatch` | `SpriteBatch::flush` and the hit-effect batch flush | MATCH | Same flush of accumulated instances in one draw call, with the same per-kind state. |
| `hitFxDragIntegral` :11352-11357 | `dragIntegral` (`HitEffect.cpp:39-43`) | MATCH | Identical polynomial `((0.05u - 0.4666667)u + 1.1)u^2` with the same clamp of u into 0..1 and the same zero at u <= 0. |
| `hitFxParticleTravel` :11363-11374 | `particleTravel` (`HitEffect.cpp:48-61`) | MATCH | Same Simpson integration over `n = 24` intervals (`kTravelSteps = 24`), same endpoint/odd/even weights, same `sum * h / 3 * lifetime`, with `HIT_FX_PARTICLE_LIFETIME = 0.5` and `HIT_FX_PARTICLE_DRAG_K = 2.657` matching `kParticleLifetime = 0.5` and `kDragK = 2.657`. |
| `drawClickEffects` | click-effect field draw | MATCH | Same effect drawn from the hit-effect sheet with the same grid slicing and per-frame travel. |
| `getHistoricalLineState` | historical line state lookup | MATCH | Same lookup of a line's state at a past chart time, used by the replay click effects. |
| `shouldSkipReplayClickEffect` | replay click-effect gate | MATCH | Same skip when the replay did not record a click at that moment. |
| `removeExpiredPrefix` | expired-entry trim | MATCH | Same trim of the leading expired entries in the effect list. |
| `drawBadEffects` | bad-effect draw | MATCH | Same miss effect drawn from its sheet. |
| `drawTextureCover` | cover-fit texture draw | MATCH | Same cover-fit quad for a texture. |
| `drawOfficialBackgroundClip` | background clip draw | MATCH | Same clipped background quad. |
| `drawOfficialSideBackgrounds` / `drawOfficialSideBackgroundPanel` / `drawSideDimPanel` / `drawSideVerticalGaps` | side artwork and dim bands | MATCH | Same side art with the same dim panels and the same vertical gap fill; the alpha and dim constants are in section 2. |
| `drawDeferredSidePanelsForDsChain` | deferred side panels for the DS chain | MATCH | Same deferral so the side panels are drawn after the level-effect chain. |
| `drawOfficialSideColumnsOverlay` | side column overlay | MATCH | Same overlay columns over the side art. |
| `hasHorizontalSideBlanks` / `isStageFullscreen` | side-blank and fullscreen tests | MATCH | Same predicate deciding whether the side art is needed at all. |
| `enableStageScissor` / `disableStageScissor` | scissor around the stage | MATCH | Same scissor box restricted to the stage. |

### Effect pools and the miss effect

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
| `getJudgeOffset` | `GameplaySession::touchPosition` | MATCH | Same normalised distance pipeline; compared term by term in the note-geometry section. |
| `hasHitEffectTint` | `Note::tintR/G/B` presence flag | MATCH | Same single flag deciding whether the note's tint is used. |
| `spawnBadEffect` | bad-effect spawn on a miss | MATCH | Same miss effect spawned at the note with the same quad and UVs from the effect sheet. |
| `obtainHitEffect` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |
| `obtainBadEffect` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |
| `recycleHitEffect` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |
| `recycleBadEffect` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |
| `recycleAllHitEffects` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |
| `recycleAllBadEffects` | `std::vector` of effect records in the renderer | **DIFFERENT BY DESIGN** | The reference keeps object pools (`EFFECT_POOL_MAX`) so a burst of hits does not allocate per effect. The port stores the same effect records by value in a vector cleared each frame, so there is no pool to refill and no per-effect allocation either. The effects produced per frame are the same. |

### Note geometry and the judge-offset computation

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
| `trackActiveHold` / `useTrackedHoldOptimization` / `updateTrackedHoldNotes` | active-hold tracking in `GameplaySession` | MATCH | Same "keep only the holds that are actually down" tracking used to skip per-frame work for finished holds. |
| `getJudgeOffset` :10556-10570 | `GameplaySession::touchPosition` (`:152-171`) | MATCH | Same pipeline: fill the line state, project the touch into line-local space with the line rotation, compute the note's local x, then divide the absolute difference by the effective judge area. Both return an unbounded value (the reference `Float.MAX_VALUE`, the port `numeric_limits::max`) when the line or state is missing. |
| `computeNoteControlHeight` :10595-10609 | `resolveNoteGeometry` (`src/game/NoteGeometry.cpp`) | MATCH | Same base distance `(note.floorPosition - lineFp) * PGRH * (PGRBEAT / bpm) * stageH * speed`, then the same `rpeControlHeightFromBasePx` conversion with the note's y offset. The control-height table itself is in `core.md` section 5a. |
| `toLineLocalX` / `toLineLocalY` | line-local projection in `touchPosition` | MATCH | Same rotation by the line angle into line-local coordinates (`toLineLocalX` is the along-line axis the port computes as `alongPixels`). |
| `computeNoteLocalX` | `geometry.alongLineOffsetPx` | MATCH | Same per-note local x from the incline and x controls; a hold keeps its raw offset on both sides. |
| `spawnHitEffect` | hit-effect spawn in the renderer | MATCH | Same spawn of the hit effect at the judged note with the same tint. |

### The gameplay update and the background blur

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
| `drawGifFrame` | gif frame draw in the line pass | MATCH | Same frame selected from the pre-decoded gif and drawn on the line. |
| `renderSceneToFbo` | scene render target | MATCH | Same offscreen scene target, used by the low-res and effect paths alike. |
| `pauseScalableBlurPass` / `createBackgroundBlur` / `renderBackgroundToFbo` | `BackgroundBlur` (`src/render/scene/BackgroundBlur.cpp`) | MATCH | Same four-level scalable blur pyramid with the same radius derivation. |
| `drawTimerLine` | timer line in the HUD | MATCH | Same timer bar drawn between its two caps. |
| `updateGameplay` :9642-9665 | `gameUpdate` (`GameScreen.cpp:1574`) | **FIXED** | The reference substitutes one 60 Hz frame when the chart delta is non-positive or above 0.25 s. The port clamped the interval to `0..0.1`, so a zero interval advanced nothing (the playhead stalled for a frame) and a 150 ms hitch advanced less than the reference would. `GameScreen.cpp` now applies the reference rule, and `Application.cpp` bounds the interval at 0.25 instead of 0.1 so the gameplay update sees the real value. |
| `activateHoldAfterHead` | hold activation in the judgement engine | MATCH | Same "activate the hold once its head is judged" step; the hold tail itself was verified in `judging.md`. |

### Hit-offset indicator geometry

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
| `officialHudUnitPx` / `officialHudX` / `officialHudY` | `scene::hudUnit` / `hudX` / `hudY` | MATCH | Same `stageH / 1000` unit and the same centred mapping. |
| `drawGameHudWithAnimationsClipped` | HUD draw inside the clip rect | MATCH | Same animated HUD drawn within the frame clip. |
| `drawHitOffsetLineIndicator` :9055-9100 | the line-mode branch in `HudPass.cpp:643-760` | MATCH | Same half-width, same per-window tick positions along the line and the same indicator alpha. |
| `hitOffsetLineBottomSafety` :9103-9105 | bottom-safety term in the dial placement | MATCH | Same `arrowHeight + dp(5)`. |
| `hitOffsetBottomSafety` :9107-9118 | dial bottom-safety term | MATCH | Same `dp(3)` base, then the arrow footprint added when the glyph is available, so the dial never runs off the bottom edge. |
| `drawHitOffsetArc` / `drawHitOffsetRadialLine` | arc and radial ticks in the dial | MATCH | Same arc from the perfect/good/bad window angles and the same radial tick placement. |
| `drawHitOffsetLine` | line-mode dashes | MATCH | Same line mode drawing. |
| `drawHitOffsetArrowLinear` / `drawHitOffsetArrow` | pointer glyph draw | MATCH | Same glyph drawn at the computed offset, with the same linear fallback when the texture is missing. |
| `hitOffsetArrowWidth` :9207-9209 | `arrowWidth` (`HudPass.cpp:642`) | MATCH | Identical: `clamp(meterW * 0.095, dp(12), dp(17))`. |

### HUD text formatting and the level transition scale

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
| `clampScoreInt` :8539-8543 | the clamp inside `formatScore` (`HudPass.cpp:91-95`) | MATCH | Same 0..1,000,000 clamp. |
| `formatScore7` :8691-8700 | `formatScore` `%07d` (`HudPass.cpp:93`) | MATCH | Same seven zero-padded digits for every value that survives `clampScoreInt`. The reference's digit-by-digit loop would drop the leading digit of a value above 9999999, which the clamp already excludes. |
| `formatPercentHundredths` :8702-8705 | `formatAccuracyHundredths` (`HudPass.cpp:97-101`) | MATCH | Same 0..10000 clamp and the same `d.dd%` split. |
| `getAttachUiState` | attach-UI element lookup | MATCH | Same id-to-line index lookup with the same bounds guard. |
| `applyUiTransformAndDraw` / `applyUiTransformAndDrawUv` | attach-UI draw | MATCH | Same transform applied before drawing the attached element, with and without explicit UVs. |
| `drawGameHudWithAnimations` | the HUD draw with the enter/outro animation | MATCH | Same animated HUD composition. |
| `drawHudTexts` | HUD text block in `HudPass` | MATCH | Same readouts drawn in the same order at the same anchors. |
| `officialHudUnitPx` | `scene::hudUnit` | MATCH | `stageH / 1000`, or 1 when degenerate. |
| `officialHudX` / `officialHudY` | `scene::hudX` / `scene::hudY` | MATCH | Same `centerX + uiX * unit` and `centerY - uiY * unit`. |
| `officialComboLabelOutroAlpha` | combo-label outro fade | MATCH | Same fade-out curve for the combo label. |
| `officialLevelStartEase` / `officialLevelOverRootScaleY` | level start/over scaling | MATCH | Same ease curve and the same root scale applied at the level transition. |

### HUD readouts, the Ametrine vignette and the broken-combo overlay

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range now falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `renderGameplayFrame` | `GameRenderer::render` | MATCH | Same frame composition. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question. |
| `resetSmoothBiasHistory` / `primeSmoothClock` / `cacheSmoothedPlayhead` / `addSmoothBias` / `getSmoothedPlayheadSeconds` | `SmoothingClock` | MATCH | Same 60-entry bias window, same re-anchor and same once-per-frame cache. |
| `renderSceneDirect` | the scene body of `GameRenderer::render` | MATCH | Same direct scene path. |
| `drawAmetrineVignette` / `ametrineHudVignetteFactor` | `fx::AmetrineVignette` | MATCH | Same inline vignette (radius 0.24, smoothness 0.55) and the same HUD factor driving its strength. |
| `getOrLoadFileTexture` | `TextureCache::load` | MATCH | Same load-once-per-path cache. |
| `getOrCreateTextTexture` / `createHudTextTexture` (5 overloads) | `TextRenderer::glyphStrip` plus per-frame draw | **DIFFERENT BY DESIGN** | The reference rasterises each string into its own GL texture cached by text and size; the port rasterises a glyph strip once per pixel size and draws strings from it. Same pixels, no per-value allocation or upload. |
| `measureHudTextWidth` | `TextRenderer::measure` | MATCH | Same advance-width sum for a string at a size. |
| `drawHudText` | text draw in `HudPass` | MATCH | Same draw with the same alignment and colour. |
| `updateHudDynamicText` / `drawHudDynamicText` / `updateHudTextTexturesIfNeeded` | per-frame HUD readout draw | MATCH | Same "refresh the readout then draw it" step; the port has no texture to refresh (see the strip note above), so the update only recomputes the value. |
| `updateHudDisplayValues` :8505-8517 | `updateDisplayValues` (`HudPass.cpp:468`) | MATCH | Same per-frame advance with the elapsed time clamped to 100 ms and the same 50 ms catch-up constant (`HUD_SCORE_DISPLAY_CATCH_UP_MS = 50.0` vs the `deltaMs / 50.0` factor). |
| `approachHudDisplayValue` | `approachDisplayValue` (`HudPass.cpp:454-466`) | MATCH | Same linear approach to the target, same saturation at one full step so a long frame cannot overshoot, same snap when the remainder is under 0.0001. Also returns the target unchanged for a non-finite current value. |
| `buildBlockedComboPattern` / `drawBlockedComboPattern` / `blockedComboNoiseSample` / `blockedComboNoise` | broken-combo noise overlay | MATCH | Same deterministic noise pattern over the HUD when the combo is broken, with the same sampling. |

### Texture and text caches

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `getOrLoadFileTexture` | `TextureCache::load` | MATCH | Same load-once-per-path cache. |
| `getOrCreateTextTexture` / `createHudTextTexture` (5 overloads) | `TextRenderer::glyphStrip` + per-frame text draw | **DIFFERENT BY DESIGN** | The reference rasterises a whole string into its own GL texture and caches it by text and size. The port rasterises a glyph strip once per pixel size and draws each string from it, so a changing readout (score, combo, timer) costs no per-value texture. Both produce the same pixels; the port avoids an allocation and an upload per changed value. |
| `getTextPaint` | `TextRenderer` style state | MATCH | Same paint configuration (size, colour, alignment) applied per draw. |
| `deleteGlTexture` | `TextureCache` release | MATCH | Same GL texture delete. |
| `invalidateAllCachedTextures` / `invalidateGl` | cache invalidation on context loss | MATCH | Same full invalidation when the GL context is recreated. |
| `invalidateHudDynamicText` / `clearHudDynamicText` / `clearHudTextTextures` | the strip cache's invalidation | MATCH | Same "drop the cached text so the next frame re-renders it" behaviour; in the port the readouts are redrawn from the strip each frame, so only the strip cache can go stale and it is invalidated the same way. |
| `ensureTextureValid` | `TextureCache` validity check | MATCH | Same "is this handle still alive after a context reset" check. |
| `ellipsizeToWidth` (2 overloads) | `truncateWithEllipsis` (`UiLayout.cpp:151`) | MATCH | Same binary-search-free width walk that appends an ellipsis when the text would not fit. |
| `isAmetrineComboBlocked` | combo-blocked state for the Ametrine vignette | MATCH | Same predicate gating the vignette when the combo is broken. |
| `getRenderLineInfo` | per-line render info in `LinePass` | MATCH | Same cached per-line state (rotation, screen position, state holder) built once per frame. |

### Line drawing, debug overlay and the native path selection

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range now falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset of the transient note and line fields. |
| `renderGameplayFrame` | `GameRenderer::render` | MATCH | Same frame composition. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question answered through the sorted window index. |
| `resetSmoothBiasHistory` | `SmoothingClock::reset` | MATCH | Same ring-buffer, count and sum reset. |
| `primeSmoothClock` | `SmoothingClock::reset(reportedSec, nowSec)` | MATCH | Same re-anchor on a seek. |
| `cacheSmoothedPlayhead` / `getSmoothedPlayheadSeconds` | `SmoothingClock::advance` | MATCH | Same once-per-frame cached value. |
| `addSmoothBias` | ring-buffer block in `SmoothingClock::advance` | MATCH | Same 60-entry window and the same fill-then-overwrite order. |
| `renderSceneDirect` | the scene body of `GameRenderer::render` | MATCH | Same direct scene path; pass order in section 4. |
| `drawAmetrineVignette` | `fx::AmetrineVignette` | MATCH | Same inline vignette (radius 0.24, smoothness 0.55); the port carries it as an inline program like the reference does. |
| `canUseNativeRenderCommands` / `canUseNativeTimeline` / `prepareNativeTimelineLineStates` / `applyNativeTimelineLineStates` / `prepareNativeLineState` / `drawNativeHoldNotes` / `drawNativeNoteHeads` | — | DELIBERATE-EXCLUSION | These select and drive the JNI render path. The port renders in-process, so the native path does not exist; the same lines and notes are drawn by `LinePass` and `NoteBatch`. See the `NativeRenderCommands` section. |
| `resolveLineText` | text-event resolution in `LinePass` | MATCH | Same text value resolved from the line's text events. |
| `currentChartGlobalBeat` | global beat in the renderer | MATCH | Same global-beat derivation from the playhead and the BPM timeline. |
| `normalizeAlpha01` | alpha clamp at the draw sites | MATCH | Same clamp of the line alpha into 0..1. |
| `getOrCreateDebugText` | debug overlay text cache | MATCH | Same cached debug label, rebuilt only when the text changes. |
| `ensureLineDrawOrder` / `getLineZOrder` | `buildLineDrawOrder` (`NoteBatch.cpp`) | MATCH | Same stable draw ordering by z-order; the ordering contract is recorded in `game_renderer_residual.md`. |
| `drawFakeJudgeLine` | fake line draw | MATCH | Same single decorative line at the stage centre, drawn at full width during the intro collapse. |
| `isStoryboardJudgeLine` | storyboard-line test in the line pass | MATCH | Same "this line belongs to the storyboard" test that lets the outro hide storyboard elements only. |
| `isLineAttachedToPrprVideo` | prpr-video attachment test | MATCH | Same attach-line lookup; the record is parsed even though video playback is out of scope. |
| `drawJudgeLineVisualWithOutroLineCollapse` / `drawJudgeLineVisual` | line draw with the outro collapse scale | MATCH | Same per-line visual with the outro collapse applied to the plain and textured cases. |

### Smoothing clock, effect programs and storyboard variables

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `renderGameplayFrame` | `GameRenderer::render` | MATCH | Same frame composition. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question via the sorted window index. |
| `setUniform2f` / `setUniform3f` / `setUniform1i` | uniform setters in the pass wrapper | MATCH | Same vec2/vec3/int uniform upload. |
| `evalPrprVarScalar` / `evaluatePrprVarValue` / `evalEventList` / `clone` | storyboard variable evaluation | MATCH | Same constant/keyframe/reference resolution for a storyboard variable, and the same per-frame list evaluation. |
| `ensureOfficialBloomPrograms` / `ensureVignetteNoiseProgram` / `buildPrprProgram` | program construction in the effect passes | MATCH | Same lazy program build per effect kind. |
| `obtainBloomFbo` / `releaseBloomFbos` | bloom render targets | MATCH | Same acquire-on-first-use and release-with-the-pass lifetime. |
| `bindEffectSampler` / `bindVignetteNoiseIfNeeded` | sampler and noise binding | MATCH | Same sampler slots and the same vignette-noise binding when the effect asks for it. |
| `prprNoiseClockSec` | noise clock in the effect pass | MATCH | Same derived clock feeding the noise input. |
| `applyPrprEffectChainFull` / `applyPrprEffectChainStageViewport` | global and stage-scoped chain segments (`GameRenderer.cpp:1644-1665`) | MATCH | Same two-segment split and order; the stage segment restores the caller's viewport afterwards. |
| `applyPrprEffectPassFull` / `applyPrprEffectPassViewport` / `applyPrprEffectPassCommon` | per-pass application in the chain | MATCH | The reference factors the pass into a full-frame and a viewport variant over a shared body; the port has the same shared body with the viewport restored by the caller. |
| `resetSmoothBiasHistory` | `SmoothingClock::reset` (`SmoothingClock.cpp:7`) | MATCH | Same clearing of the ring buffer, its count and its running sum. |
| `invalidateSmoothClock` | `SmoothingClock` validity flag | MATCH | Same invalidate-so-the-next-sample-re-primes behaviour. |
| `primeSmoothClock` | `SmoothingClock::reset(reportedSec, nowSec)` | MATCH | Same re-anchor of the prediction on a seek. |
| `cacheSmoothedPlayhead` / `getSmoothedPlayheadSeconds` | `SmoothingClock::advance` | MATCH | Same once-per-frame cached value, returned unchanged for a repeated call in the same frame. |
| `addSmoothBias` :6243-6258 | the ring-buffer block in `SmoothingClock::advance` | MATCH | Same 60-entry window (`SMOOTH_BIAS_HISTORY_SIZE = 60` vs `kHistorySize = 60`), same fill-then-overwrite order, same running sum. |
| `ensureRenderLineInfoCapacity` | per-line info vector growth | MATCH | Same grow-to-fit before filling the per-line scratch data. |

### Storyboard effects and video playback

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same pause-mode predicate. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same fallback chain. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range falls back to disabled; the port had clamped. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `renderGameplayFrame` | `GameRenderer::render` | MATCH | Same frame composition; pass order in section 4. |
| `blockCompositeHasWork` | `Chart::hasLiveBlock` | MATCH | Same live-block question, answered through a sorted window index. |
| `renderSceneWithPrprEffects` | stage + global effect chain in `GameRenderer.cpp:1644-1665` | MATCH | Same two-segment order: stage-scoped first, then global with the HUD composited in, so a chart with no global effect leaves the HUD unshaded. |
| `applyPrprEffectChain` | `applyStageScopedChain` / the global segment | MATCH | Same chain application, same split between stage and full-frame passes. |
| `applyPrprEffectPass` | per-effect pass in the chain | MATCH | Same single-pass application with the effect's uniforms. |
| `applyOfficialBloomPass` / `drawBloomStage` | storyboard bloom pass | MATCH | Same bloom pass over the stage; constants recorded in section 2. |
| `setUniform1f` | uniform setters in the pass wrapper | MATCH | Same scalar uniform upload. |
| `drawGameplayObjects` | `drawHudPass` + line/note batches | MATCH | Same draw order for lines, notes and HUD. |
| `drawRevealOutsidePanel` | reveal outside-panel draw | MATCH | Same dimmed region outside the reveal panel. |
| `initPrprVideoRuntimes` / `preparePrprVideoPlayer` / `releasePrprVideoRuntimes` / `pausePrprVideoRuntimes` / `resetPrprVideoRuntimes` | — | DELIBERATE-EXCLUSION | The reference plays storyboard videos through ExoPlayer on Android. The port parses and keeps the `prprVideo` records (so charts using them still load) but does not decode video: that needs a platform media stack which is outside this port's dependency set. Recorded as a known gap. |
| `onVideoSizeChanged` / `onPlaybackStateChanged` / `onPlayerError` | — | DELIBERATE-EXCLUSION | ExoPlayer callbacks for the above. |
| `setPrprVideoPlaybackSpeed` / `setPrprVideoPlaybackSpeedAll` | — | DELIBERATE-EXCLUSION | Playback-rate control for the same player. |
| `drawPrprVideos` / `canPrprVideosPlay` / `syncPrprVideo` / `seekPrprVideo` / `updatePrprVideoFrame` | — | DELIBERATE-EXCLUSION | Video texture upload and presentation; same gap as above. |
| `drawExternalVideoCentered` / `drawExternalTextureCentered` | — | DELIBERATE-EXCLUSION | Helpers for presenting the decoded video frame. |

### Scene composition, level effects and countdown drawing

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same predicate over the pause mode. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters read from the result struct. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same four-step fallback chain with the same guards. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range now falls back to disabled on both sides; the port had clamped, turning a corrupt 7 into the line indicator. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset. |
| `finishResumeCountdown` / `getResumeCountdownElapsedSec` | countdown transition in `GameScreen` | MATCH | Same countdown completion and the same elapsed clock. |
| `drawResumeCountdown` / `drawOfficialCountdownDigit` | countdown digits in `HudPass` | MATCH | Same digit rendering at the reference size. |
| `officialCountdownAlpha` | countdown alpha ramp | MATCH | Same fade curve. |
| `computeOfficialContinueBarX` | continue-bar position | MATCH | Same x from the HUD canvas geometry. |
| `easeOutBack` | `Easing` ease-out-back | MATCH | Same curve; the numeric table was proved identical in `core.md` section 2. |
| `ensureCountdownTextures` | countdown texture cache | MATCH | Same lazy creation of the digit texture. |
| `computeResumeFadeAlpha` | resume fade alpha | MATCH | Same fade curve back into gameplay. |
| `renderGameplayFrame` | `GameRenderer::render` | MATCH | Same frame composition; the pass order is recorded in section 4. |
| `drawMessageEdgeMask` | message edge mask pass | MATCH | Same edge-mask overlay for the Message level effect. |
| `blockCompositeHasWork` :4245-4251 | `Chart::hasLiveBlock` + `anyZoneVisible` (`GameRenderer.cpp:1429`) | MATCH | Same question: is any authored block live right now. The port answers it with a sorted window index instead of a linear scan. |
| `renderSceneWithPrprEffects` | storyboard pass in `GameRenderer::render` | MATCH | Same storyboard compositing before the scene is presented. |
| `drawHateBlackMaskOverlay` | Hate vignette pass | MATCH | Same black mask overlay; constants recorded in `core.md` section 8. |
| `drawBackgroundAndBarsOnly` / `drawStageBackgroundOnly` | stage backdrop passes | MATCH | Same reduced scene for the intro and the reveal transitions. |
| `officialRawBackgroundHidden` | raw-background hide predicate | MATCH | Same hide conditions for the raw background when a level effect owns the frame. |
| `ensureKaleidoProgram` / `ensureDsGridProgram` | `OfficialLevelEffects` shader setup | MATCH | Same lazy program creation for the kaleidoscope and DS grid. |
| `isDesultorySignalsLevelEffects` / `dsGridOverlayVisible` / `drawDsGridOverlay` / `dsCanvasScaleFactor` | DS grid level-effect pass | MATCH | Same level identification, visibility gate, overlay draw and canvas scale. |
| `drawOfficialKaleidoBackdrop` | Message kaleidoscope pass | MATCH | Same backdrop for the Message level effect. |
| `updateMessageStageMode` | message stage mode | MATCH | Same stage-mode switch for the Message effect. |
| `advanceMessageParticles` / `drawMessageParticles` / `createParticleGlowTexture` | `MessageParticleField` | MATCH | Same particle advance and glow-texture draw; the random-sequence caveat is recorded in `core.md` Remaining risk. |
| `drawRevealScaledMasks` | reveal-scaled mask draw | MATCH | Same masks scaled by the reveal camera. |

### Chart reveal, pause backdrop and antialias passes

The reveal camera, the pause blur and the FXAA resolve each have a named helper in the
reference. Each maps onto a pass the port implements; the constants they use are already
recorded in section 2, so the basis column names the pass rather than repeating the numbers.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters read from the result struct. |
| `sanitizeOfficialScreenDpi` | `Application::initialize` DPI block | MATCH | Same four-step fallback chain, non-finite and non-positive rejected at each step. |
| `sanitizeHitOffsetIndicatorMode` | `Settings::load` | **FIXED** | Out-of-range falls back to disabled on both sides now; the port had clamped, turning a corrupt 7 into the line indicator. See the `GameRenderer` helper section below. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset of transient fields. |
| `onSurfaceCreated` / `onSurfaceChanged` | SDL GL context lifetime | DELIBERATE-EXCLUSION | `SurfaceHolder` callbacks; SDL owns that lifecycle. |
| `chartRevealCenterX` / `chartRevealCenterY` | reveal camera origin in `GameRenderer.cpp` | MATCH | Same stage-centre origin. |
| `updateFrameChartRevealState` | per-frame reveal state | MATCH | Same per-frame progression of the reveal transition. |
| `shouldRevealHiddenChartObjects` | reveal predicate | MATCH | Same toggle gate for drawing hidden objects. |
| `chartRevealHiddenFloorAlpha` | hidden alpha in the reveal fade | MATCH | Same 0.20 hidden alpha recorded in `render.md` section 2. |
| `currentChartRevealScale` | reveal scale curve | MATCH | Same two-segment curve with the 0.30 target scale and 1.0 s duration. |
| `chartRevealScreenX` / `chartRevealScreenY` | reveal projection | MATCH | Same stage-to-screen mapping for the reveal camera. |
| `computeRevealWorldBounds` | reveal world bounds | MATCH | Same bounds computation from the stage rect and the reveal scale. |
| `computeRevealTransformedRect` | reveal rect transform | MATCH | Same transformed rect used for the object culling test. |
| `refreshPauseSceneBlur` | `PauseHudBlur` / `PauseBackdropPass` | MATCH | Same four-level scalable blur pyramid; the granularity figures are recorded in the handover. |
| `computeOfficialPauseBlurRadiusPx` | blur radius in `PauseBackdropPass` | MATCH | Same radius formula from the stage height. |
| `drawPauseBackdrop` / `drawPauseBackdropTexture` | pause backdrop passes | MATCH | Same darkened, blurred backdrop and the same texture draw. |
| `shouldPostProcessAntialias` | antialias predicate | MATCH | Same predicate; the port additionally excludes the resume fade, which the reference expresses through its pause mode (documented in `render.md`). |
| `beginAntialiasFrame` / `bindAntialiasTarget` / `endAntialiasFrame` / `drawFxaaToDefaultFramebuffer` | `FxaaPass` | MATCH | Same low-res render into an offscreen target, then an FXAA resolve to the default framebuffer. |
| `maybeAutoResumeAfterIntro` / `beginIntroAudioStartWatchdog` / `clearIntroAudioStartWatchdog` / `maybeRetryIntroAudioStart` | intro audio watchdog in `GameScreen` | MATCH | Same auto-resume after the intro and the same retry on a failed audio start. |
| `releaseIntroAudioAtGameplayStart` / `releaseAudioForResumeFade` | audio release points | MATCH | Same release at gameplay start and at the resume fade. |
| `getResumeCountdownElapsedSec` / `getPauseMenuElapsedSec` | countdown and menu timers | MATCH | Same elapsed clocks driving the countdown and the menu entrance. |

## GameRenderer: pause, judging and HUD control flow, adjudicated

`GameRenderer.java` is one 16k-line class; the port splits it across `GameScreen`, `HudPass`,
`JudgementEngine` and the gameplay session. This section covers the pause state machine, the
input and judging entry points and the HUD geometry. Rows whose behaviour is evidenced in
another report say so rather than repeating it.

### Pause state machine and tap routing

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `isMenuVisible` | `GameRuntime::Pause::Menu` | MATCH | Same predicate over the pause mode. |
| `onDoubleTap` / `onSingleTap` / `handleUiTap` / `isInPauseHitArea` | `GameScreen` tap routing | MATCH | Same double-tap-to-pause, single-tap-to-resume and same hit-area test. |
| `triggerPauseTouchFeedback` | pause feedback in `GameScreen` | MATCH | Same visual feedback on a pause tap. |
| `isBeforeGameplayStartOrIntroTransition` | intro gate in `GameScreen` | MATCH | Same still-in-the-intro condition that suppresses judging. |
| `freezePlayheadForPause` / `clearPausePlayheadFreeze` | `game.pausePlayheadSec` + `pausePlayheadValid` | MATCH | Same freeze-and-restore of the playhead. |
| `shouldUsePausedPlayhead` :1931-1937 | the same condition in `GameScreen` | MATCH | Same three modes: menu, countdown, and fadeout only while the resume fade has not unpaused. |
| `shouldPauseGameplayJudgement` :1939-1944 | judgement gate in `GameScreen` | MATCH | Same three-mode condition, identical to the playhead predicate. |
| `getPausedPlayheadSeconds` :1946-1950 | `game.pausePlayheadSec` read | MATCH | Same non-finite and negative guard returning 0. |
| `resetIntroAudioToPausedStart` | intro audio restart on resume | MATCH | Same cancel-then-restart of the intro audio against the paused start. |
| `enterPauseMenu` / `beginResumeCountdown` / `requestPauseMenu` | the pause state machine | MATCH | Same enter-menu, countdown and request transitions. |
| `restartInternal` | `restartRun` in `GameScreen` | MATCH | Same restart that resets the session and the clock. |

### Judging entry points, windows and practice

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `tryJudgeInputDown` / `tryJudgeInputMove` / `tryJudgeAt` | `judgeNewTouches` and friends | MATCH | Same down/move entry points and the same single scan body; the scan was verified line by line in `judging.md`. |
| `officialFingerWorldScalePx` :2529-2535 | `judgePositionScalePx` (`JudgementEngine.cpp:339`) | MATCH | Same `viewH * 0.1` with the same fallbacks. |
| `officialFingerWorldX` / `officialFingerWorldY` | touch coordinate conversion | MATCH | Same divide by the world scale, with y flipped against the view height. |
| `computeNoteHeadPosition` | `resolveNoteGeometry` (`NoteGeometry.cpp`) | MATCH | Same base distance, y offset, control height, incline and along-line offset; compared term by term in `core.md` section 5a. |
| `getGoodWindowSec` / `getBadWindowSec` / `getEarlyWindowSec` / `getOfficialFlickJudgeSpeed` | window accessors in `JudgementEngine` | MATCH | Same window values and the same flick speed scaling. |
| `setPracticePerfectWindowMs` / `setPracticeGoodWindowMs` / `tapGoodWindowSec` | `PracticeJudgeWindows` | MATCH | Same practice overrides feeding the same accessors. |
| `firstNoteIndexAtOrAfter` / `firstEffectIndexAtOrAfter` | cursor advance in the session | MATCH | Same first-entry-at-or-after search used to skip already-judged notes. |
| `requestPracticeSeek` / `updatePracticeRepeat` / `applyPracticeSeek` | practice seek in `GameScreen` | MATCH | Same seek request, repeat window and applied seek. |
| `resetNativeRenderBucketRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset; the native bucket itself has no port counterpart (see the `NativeRenderCommands` section below). |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result()` | MATCH | Same four counters read from the result struct. |
| `setJudgeLineWidth` | `settings.judgeLineWidth` | MATCH | Same live setting applied to the line width. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach point. |
| `hasIntroAudioReleaseElapsed` / `introDimMix` | intro timeline in `GameScreen` | MATCH | Same release timing and the same dim ramp. |

---

## NativeRenderCommands: every declaration adjudicated

The reference crosses a JNI boundary: `NativeRenderCommands` packs the chart into primitive
arrays and pushes per-frame state into a native renderer. The port links its renderer in and
passes C++ structures directly, so this whole layer has no counterpart — but the *data* it
carries does, and that is what the verdicts below point at.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `NativeRenderCommands` (class) | — | DELIBERATE-EXCLUSION | The reference marshals chart state across the JNI boundary into a native renderer. The port renders in-process, so there is no boundary to marshal across and no command buffer to fill. |
| `isAvailable` | always true | MATCH | The reference probes whether its native library loaded; the port has no such probe because the renderer is linked in. |
| `LineState` | `game::JudgeLine::State` | MATCH | Same per-line state record. |
| `LineTimeline` | `Chart` | MATCH | Same per-chart timeline. |
| `NoteBucket` | `JudgeLine::notesByType` | MATCH | Same per-type note buckets. |
| `hasWhiteDefault` | `SkinConfig` default colour handling | MATCH | Same "no explicit colour means white" rule, applied at load instead of at upload. |
| `countNonNullLayers` | `JudgeLine::eventLayers` size | MATCH | Same layer presence test. |
| `countSpeedLayers` | `JudgeLine::speedEventLayers` size | MATCH | Same. |
| `hasSpeedLayers` | `speedEventLayers.empty()` | MATCH | Same. |
| `countNoteControls` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countLineEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countMoveEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countSpeedEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countSpeedLayerEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countColorEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countGifEvents` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countSpeedBezierData` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countSpeedLayerBezierData` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countColorBezierData` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countLineBezierData` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `countMoveBezierData` | — | DELIBERATE-EXCLUSION | These size or fill the JNI buffers that carry chart data to the native renderer; the port renders in-process, so there is no buffer to size. The data itself is unchanged, and its field-for-field equivalence is in `core.md` section 14. |
| `bezierDataCount` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `LineEventWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `MoveEventWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `SpeedEventWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `ColorEventWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `GifEventWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `NoteControlWriter` | — | DELIBERATE-EXCLUSION | Sizes or fills a JNI buffer that carries chart data to the native renderer; the port renders in-process, so there is no buffer. The data is unchanged and its field-for-field equivalence is in `core.md` section 14. |
| `syncDynamicStates` | per-frame `fillState` read | MATCH | Pushes changing per-line values into native memory each frame; the port reads the same values while drawing, so there is no sync step. |
| `syncDynamicState` | per-frame `fillState` read | MATCH | Same, for a single line. |
| `resetRuntimeState` | `Chart::resetRuntime` | MATCH | Same per-run reset of the transient note and line fields. |
| `updateLoopStarts` | `SmoothingClock` loop phase | MATCH | Same restart bookkeeping when the beat clock wraps. |

## 6a. `GameRenderer` helper declarations with no same-name port symbol

`GameRenderer.java` is the largest single source of unadjudicated declarations (518), because
it is one 16k-line class whose helpers the port distributed across the renderer, the gameplay
session and the screen layer. The entries below were checked directly; each names the port
code that carries the behaviour.

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `sanitizeHitOffsetIndicatorMode` :1738-1743 | `Settings::load` (`Settings.cpp:125-128`) | **FIXED** | The reference maps any out-of-range mode to `HIT_OFFSET_INDICATOR_DISABLED` (0); the port clamped instead, so a corrupt value of 7 became the line indicator (2) rather than turning the feature off. The port now applies the same out-of-range-to-disabled rule. |
| `sanitizeOfficialScreenDpi` :1728-1736 | `Application::initialize` display-DPI block | MATCH | Same four-step fallback chain: `densityDpi`, then `density * 160`, then `xdpi`, then `ydpi`, then the reference DPI constant; every step also rejects non-finite and non-positive values. The port reads SDL's display scale first because that is this platform's equivalent of `densityDpi`. |
| `isMenuVisible` | `GameRuntime::Pause::Menu` check | MATCH | Same predicate over the pause state. |
| `shouldResumeAudioOnActivityResume` | `AudioEngine` resume path | DELIBERATE-EXCLUSION | An Android lifecycle hook; the port resumes the stream from the pause state machine instead. |
| `setReplayRecorder` | `GameplaySession::setReplayRecording` | MATCH | Same attach-point for the recorder. |
| `beginIntro` / `beginIntroWithAudioResume` / `scheduleIntroAutoResume` / `cancelIntroAutoResume` / `isIntroGenerationCurrent` | `GameScreen` intro state | MATCH | Same intro timeline and the same guard that a superseded intro generation cannot resume the audio. |
| `elapsedIntroSeconds` / `outroUiProgress` / `elapsedOutroSeconds` / `isInOutroLineCollapse` / `shouldHideNonLineStageElementsForOutro` / `shouldHideStoryboardElementsForOutro` | `GameScreen` outro state | MATCH | Same outro timeline and the same element-hiding predicates; the reveal and collapse curves are recorded further down. |
| `computeOutroPlainLineScaleX` | `computeOutroLineCollapseScale` companion | MATCH | Same per-line scale split for the plain and textured cases. |
| `getPerfectCount` / `getGoodCount` / `getBadCount` / `getMissCount` | `JudgementEngine::result().perfect` and friends | MATCH | The reference exposes four getters over `judgeCounts`; the port reads the same four fields from the result struct. |
| `PreloadedGif` / `preloadGifTextures` / `decodeGifFrames` / `uploadPreloadedGifTextures` | `GifTexture` cache in the renderer | MATCH | Same "decode every frame of a `.gif` line texture once, then upload once" behaviour; the port keeps the frames in a renderer-side cache keyed by path. |
| `applyBlockTextureWrap` / `applyTextureBilinearFilter` / `applyTexturePointFilter` / `maxFragTextureUnits` | `TextureCache`, `BlockCompositePass` sampling setup | MATCH | Same wrap and filter policy per texture class; the composite's point-versus-bilinear choice is recorded in `game_renderer_residual.md`. |

## 7. Deliberate exclusions

1. **`prprVideo` playback.** The data model is complete (`Chart.hpp:24`,
   `ChartLoader.cpp` parses `path`/`startTime`/`scaleType`/`alpha`/`dim`/`attachLineIndex`),
   but nothing renders it. The reference decodes video through the mobile platform's media
   pipeline and an external OES texture (`GameRenderer.java:5158-5540`), which has no
   desktop equivalent without bundling a decoder. **No shipped chart uses a video.** The
   parsing side is complete, so a future playback path has correct inputs. Recorded as
   remaining risk rather than a gap.
2. **`NativeRenderCommands.java` is not ported as a JNI layer.** It is a set of `native`
   declarations (`NativeRenderCommands.java:72-168`) whose bodies live in the reference's
   C++ side. The port implements the same batch-building kernels directly in
   `scene/NoteBatch.cpp` (`buildHeadBatch`, `buildHoldBatch`, `buildLineDrawOrder`,
   `noteHeadPose`) and `shared` line evaluation, with the same inputs, the same arithmetic
   and the same output record. The kernels are exercised by every note-related render-matrix
   case and by `tests/`. Porting the JNI shim itself would add an indirection with no
   behavioural difference.
3. **Debug overlay** (`drawDebugOverlay`, `:6908-7035`) is not rendered. It is developer
   tooling behind `showDebugInfo`, not part of the shipped picture; the reviewer-facing
   frame must not contain it.
4. **`GameGLSurfaceView.java`** is EGL/context setup and frame pacing, which the port
   handles through SDL in `src/app` (outside this task's scope). No render-side content.

---

## 8. Reference files not read in full (explicit)

Stated plainly rather than implied as verified:

| File | Status |
| --- | --- |
| `ui/GameTextRenderer.java`, `ui/GameTextView.java`, `ui/GameTextOverlay.java` | **Not read.** The port rasterises text with stb_truetype (`text/TextRenderer.cpp`) and reproduces the *observable* results the renderer depends on (ink box, ascent/descent, legacy advance, tracking `0.02 em`, space adjust `-0.012 em`, ellipsis) — all verified against the constants and the `createHudTextTexture` layout path (`:7837-7985`) and `measureHudTextWidth` (`:8183-8205`) that the renderer calls. The sub-pixel layout internals of those three UI classes were not read line-by-line. |
| `ui/AssetBitmaps.java`, `ui/CoverImageLoader.java` | **Not read.** Texture loading, filtering and V-flip behaviour were verified from the renderer's own call sites (`applyTexturePointFilter`, `applyBlockTextureWrap`, `loadOfficialShaderTexture`, `loadStackedAtlasSlicesVFlip`) and by the render matrix's skin/GIF/hold cases. The two UI loaders' own downsampling heuristics were not compared. |
| `docs/CHAPTER9_PHASE2_ANOMALIES.md` (130 105 bytes) | **Partially read** — the sections on block-area phase semantics, the `0.09/0.12` subtract gate and the mask layout. The remaining sections were not read; they mostly describe the reverse-engineering process rather than shipped behaviour. |
| `docs/OFFICIAL_BLOCK_SHADERS.md` | Read; used to confirm the uniform array layout and pass states. |
| `docs/analyses/*.md` | Used selectively for constants (bloom, vignette-noise, message stage). Not read end-to-end. |
| `game/OfficialLevelEffects.java` (733 lines) | Read. Owning port (`src/core/OfficialLevelEffects.cpp`) was rebuilt by core-parity; this report only covers the render-side consumption. |
| `game/PrprEffect.java`, `game/PrprVideo.java` | Read. |

---

## 9. Cross-area requests

Changes needed outside my write scope. Each is a real divergence.

1. **`src/app/**` or `src/core/**`: `OfficialLevelEffects::onBadOrMiss` is never called.**
   `grep onBadOrMiss src/` returns only its own definition
   (`src/core/OfficialLevelEffects.cpp:369`). The reference calls it once per frame with the
   bad/miss delta (`GameRenderer.java:16100-16113`), which is what makes the Ametrine and
   Petrichor challenges able to fail. Until it is called, `isChallengeFailed()` stays false,
   so `updateAmetrineVignette` never ramps up and the Ametrine vignette I wired in never
   appears. Required change: in the gameplay update, after judgement, call
   `effect->onBadOrMiss(chartTimeSec, missCountIncreased)` for the loaded chart's
   `levelEffects`.
   *Render side is complete*: `drawScene` draws the vignette from `ametrineVignette()` and
   dims the HUD by `hudDimFactor`, both verified to be live.
2. **`src/app/GameScreen.cpp`: `onBlockStateChanged` is never called.** Same situation for
   Petrichor: the reference tracks whether a finger holds a red zone
   (`processBlockedTouchesOfficial`, `:15853-15872`) and feeds it to the level mod.
   Required change: when the set of blocked touches becomes empty/non-empty, call
   `levelEffects->onBlockStateChanged(touching)`.
3. **`src/app/GameScreen.cpp`: `lowPassOnBlock()` is not consumed.** Reference ducks the
   audio while a red zone is held (`updateLowPassFilterStateOfficial`, `:15875-15885`).
   `OfficialLevelEffects::lowPassOnBlock()` now returns the right value but no audio code
   reads it. Owner: gameplay/audio.
4. **`src/app/GameScreen.cpp`: blocked-touch pulse inside a zone is not drawn.** The
   reference draws a red pulse disc and ring at each blocked finger inside an active zone
   (`:16070-16086`). The port's inline zone path has no equivalent, and the input layer does
   not expose the blocked-touch positions to the renderer. Needed: pass the blocked touch
   positions (and the pulse timestamp) through `RenderFrameInput`, then draw them in
   `drawLevelEffectObjects`. I did not add an unused field speculatively.
5. **`src/render/skin/SkinConfig.cpp` (chart-parity's file): no action requested.** Verified
   the render side consumes every material field it publishes (`holdAtlas`, `holdAtlasMH`,
   `holdRepeat`, `holdKeepHead`, `holdCompact`, `hitFx[]`, `hitFxScale/Duration/Rotate/
   Tinted`, `hideParticles`, `perfect*`, `good*`, `*FromSkin`) — `GameRenderer.cpp:
   applyResPackSettings`. Consistent.
6. **`include/rechigros/render/scene/LinePass.hpp:17,20` owns `LINE_WIDTH_OFFICIAL` /
   `LINE_WIDTH_EDITOR`.** core-parity correctly declined to duplicate them into core. No
   change requested; noting for the master report that the render side is the single home.

---

## 10. Remaining risk

1. **Mirror is verified geometrically, not against a reference capture.** `build/ref/` has
   no mirrored screenshot. The check is that the change mask is a 100.00 % reflection about
   the stage centre and the side bands do not move — which matches the reference's transform
   by construction, but is not a pixel diff against the reference app.
2. **The Ametrine vignette has never been seen on screen.** Its ramp needs a failed
   challenge, which needs cross-area request #1. The pass itself is compared line-by-line
   against `:6565-6597` and the constants are identical, and its GL state discipline is
   asserted by the probe's state check, but the end-to-end picture is unverified.
3. **Autoplay click effects are wired but not diffed.** The port draws the pre-rolled
   debris; the reference's `java.util.Random` sequence cannot be reproduced, so a pixel
   comparison of individual particles is impossible. What is verified: the pass runs only
   for autoplay-not-replay, the cursor/anim-start bookkeeping matches the reference, and the
   travel/size laws are numerically identical.
4. **Two-segment chain split changes when the HUD is shaded.** Charts whose effects are all
   `global: true` (both effect fixtures) shade the HUD; charts with only stage-scoped effects
   no longer do. This matches the reference's structure, but every fixture in
   `tests/fixtures` uses `"global": true`, so the stage-scoped branch (`applyPassScissored`)
   is exercised only by the DS-bloom/dimension-slash level presets, which have no fixture and
   no reference capture. **This is the least-verified change in this batch.**
5. **`prprVideo` exclusion** is justified by "no shipped chart uses it", which I verified
   only by inspecting the shipped chart assets in `assets/` (no `prprVideos` entries) — not
   by exhaustively scanning all installed charts on a device.
6. **Chapter-9 level-effect rendering is verified structurally, not visually.** The pass
   order, uniforms and constants match, and the DS grid + Message kaleido are covered by
   render-matrix cases, but `build/ref/` has no Chapter 9 level-mod capture, so the Hate
   vignette, DS dimension-slash and Message light-band pictures are uncompared.
7. **Text glyph shapes differ by font rasteriser.** The port uses stb_truetype, the reference
   the platform rasteriser. Metrics and layout are matched; individual glyph outlines are
   not pixel-identical. This is inherent and not fixable without shipping the same
   rasteriser.
8. **`GameTextRenderer`/`AssetBitmaps`/`CoverImageLoader` were not read** (§8). Any
   divergence confined to those files' internals is outside what I verified.
