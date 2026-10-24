# Per-function parity report

This is the top-level report for the port-verification pass. It states what was verified, how,
what was found wrong, and what remains. The per-area evidence lives in the sibling reports:

| Report | Covers |
| --- | --- |
| [`chart.md`](chart.md) | chart parsers, metadata, library, archive, skin config |
| [`core.md`](core.md) | chart/judge-line/event/easing/effects data model |
| [`judging.md`](judging.md) | judging, gameplay session, replay model |
| [`render.md`](render.md) | render pipeline, effects, HUD, text |
| [`ui.md`](ui.md) | every interface screen, layout, typography, interaction |
| [`gameplay.md`](gameplay.md) | gameplay shell, transitions, touch, audio engine |
| [`platform.md`](platform.md) | orientation, window/surface lifecycle, assets, archive, pickers |
| [`audio.md`](audio.md) | mixer, low-pass, playhead, decoding, effect voices |
| [`coverage-matrix.md`](coverage-matrix.md) | generated index: one row per reference declaration |

Every claim below is backed by a file:line pair on both sides in the corresponding report, and
by one of the verification runs in §4.

---

## 1. Method

The reference is an Android application; the port is a cross-platform C++20 program. A
name-to-name function diff is therefore not a valid parity test — the port legitimately
restructures classes, and the reference delegates a great deal to the Android framework.

So parity was established per *unit of behaviour*:

1. **Enumerate.** `tools/parity/inventory.py` lexically extracts every declared member on both
   sides (2,312 reference declarations across 75 files; 1,825 port declarations across 173
   files). `tools/parity/coverage_matrix.py` turns that into a one-row-per-reference-declaration
   index with the port symbol that corresponds to it.
2. **Read both sides.** For each reference unit the Java source was read in full, then the port
   implementation was read in full, and the two were compared on: numeric constants and their
   order, clamps, defaults and fallbacks, non-finite handling, integer-vs-float rounding,
   evaluation order and short-circuits, and data-shape edge cases.
3. **Decide.** Each unit gets one verdict: **MATCH**, **FIXED** (was wrong, corrected),
   **DIFFERS** (a real remaining difference), or **DELIBERATE-EXCLUSION** (with a reason).
4. **Prove.** Behavioural fixes are backed by a numeric check where one is possible (see the
   `yNorm` invariant below), otherwise by a rendered capture compared against the reference
   capture, or by a test in `tests/`.

### What the coverage matrix does and does not say

`coverage-matrix.md` lists all 2,312 reference declarations. Of those, **466 carry an explicit
verdict from a report** (298 MATCH, 165 FIXED, 3 deliberate exclusion, 0 remaining difference);
188 more have a port symbol whose normalised name is identical, and 1,470 have no same-name
counterpart.

That last number is **not** a defect count. The port legitimately restructures: the reference
builds a view hierarchy where the port draws immediate-mode screens, and Android framework
callbacks (`onCreate`, `onResume`, `onWindowFocusChanged`) have no per-function analogue. The
honest reading is that 466 declarations were individually adjudicated and evidenced, and the
remaining 1,658 were covered by *behavioural* verification (screen captures, render matrix,
app-loop, audio tests) rather than by a per-declaration verdict. Anyone extending this work
should treat the matrix as a worklist of what has not yet been individually adjudicated, not as
a claim that those units are equivalent.

A spot check of the largest group (`GameRenderer.java`, 518 declarations without a report
verdict) shows the gap is mostly naming rather than coverage: those entries include the
reference's private timing helpers, which the port implements under their own names in
`render/scene/Transition.{hpp,cpp}` (`computeOutroPlainLineScaleX` → `outroLineCollapseScale`,
`computeStageTextureAlpha` → `stageTextureAlpha`, `computeSideArtworkAlpha` →
`sideArtworkAlpha`, `computeBackgroundDimAlpha` → `backgroundDimAlpha`, `computeSideDimAlpha` →
`sideDimAlpha`, `outroDimMix`, `introLineScale`, `smoothStep01`, and the six transition
constants, all present with identical values). That is one sampled file, not a systematic
result — the remaining files in the worklist have not been sampled this way.

## 2. What was found

The headline finding is that the handover documents were **substantially wrong**. They reported
the port as essentially complete with "S1/S2 items cleared". The verification found real,
player-visible defects in every area examined. The most serious:

| Area | Defect | Effect |
| --- | --- | --- |
| Core | `JudgeLine` computed `yNorm = y` where the reference computes `1 - y` on the non-layer path | every `.pec` and plain-JSON chart had its judge lines and notes **vertically mirrored** |
| Core | `OfficialLevelEffects` mapped chart digests to *song ids* then looked them up in a table keyed by *effect class names* | **no level effect could ever be selected** |
| Core | level-effect `settings` were read at the wrong JSON depth | every numeric shader setting silently used its fallback (e.g. the Hate vignette rendered `0` instead of `1.0`) |
| Core | `buildPrprEffects` omitted the lead span, the easing mapping, the ease-15 sampling and the tail span | storyboard tracks animated wrongly or not at all |
| Judging | autoplay holds were triggered at the frame time instead of `note.sect` | hold head effects and SFX misplaced under autoplay |
| Judging | the hold-body burst timer used the raw frame delta instead of the chart delta divided by music speed | burst cadence wrong whenever music speed ≠ 1 |
| Judging | replay playback stamped `judgeTimeSec` with the frame time instead of the reconstructed event time | replay hit effects timed wrongly |
| Judging | the run-complete predicate had an extra all-notes-judged and chart-end gate | results could be delayed or suppressed |
| Chart | `PrprVideo` was a 5-field stub against a 8-field record; the extra.json video parser read invented keys (`end`, `loop`) | storyboard video backgrounds could not be represented |
| Chart | `prprShaderSources` was never populated, `packDir` was overwritten after being computed | file-based shaders never loaded; resource lookup used the wrong root |
| Render | mirror mode reflected the *touch input* about the window centre instead of reflecting the *play field* about the stage centre | mirror mode drew an unmirrored field with mirrored input — a tap did not land on the note under it |
| Platform | the Android activity declared `sensorLandscape` and the toolkit re-requested a system-following orientation at surface creation | the app could come up in **portrait** on a portrait device |
| Platform | the `file://` VIEW intent filters for `.cgrp`/`.zip`/`.pez` were missing | a package opened from a file manager never reached the importer |
| App | the library pre-installed a bundled example chart that the reference does not have | a fresh profile showed a chart the reference never shows |
| Gameplay | the pause state machine had three states; the reference has four (`PAUSE_FADEOUT` between countdown and play) | a pause could resume a run whose playhead had never become stable |
| Gameplay | pause entry did not distinguish "before gameplay" from "mid-run", and the captured playhead was never restored on resume | pausing during the opening transition left the run inconsistent |
| Gameplay | the drawing clock used the raw audio cursor; the reference smooths it with a 60-frame rolling bias average bounded to ±10 ms | notes stepped once per audio callback instead of gliding |
| App | the home-to-library path and `ensureLibrary` forced a full library rescan on every entry | entering the local chart list stalled ~2.9 s on real packs; first-frame draw fell from 2869 ms to 5.0 ms once removed |
| App | `readInfoFromChartFile` read and parsed the **whole** chart document to reach its `META` block | every library scan paid for tens of megabytes of JSON parsing |
| Render | judge lines and notes were not confined to the play field | the reference scissors the stage; the port drew past its edge |
| App | chart artwork was decoded synchronously on the render thread, for both list thumbnails and the detail cover | the first frame showing a grid of covers blocked on as many decodes |
| App | the settlement backdrop (decode plus three box passes) was built on the result page's first frame | a multi-second stall when a run finished |
| Gameplay | `GameplaySession::setMusicSpeed` was never called, so the session always converted frame time at 1x | at any practice speed other than 1x, hold-burst cadence and every other speed-dependent effect ran at the wrong rate; moving the slider mid-run changed nothing |
| Gameplay | the smoothed drawing clock was primed only on its first valid frame, never on a seek | a small forward seek (a practice nudge) fell inside the clock's discontinuity thresholds and was treated as normal advance, so the drawn position stayed extrapolated from the pre-seek location |
| Judging | replay storage detected duplicates from the timestamp-derived directory name instead of comparing the recording | saving the same performance from two settlements produced two entries where the reference keeps one |
| Render | the gameplay screen's debug switch drew a text panel of internal counters | the reference's debug switch draws the chart's geometry — a marker at every judge line and note — which is what makes an authored chart's placement checkable. The text panel was replaced by that overlay |

The full lists are in the per-area reports. Counts of confirmed defects by area are in §3.

## 3. Status by area

Counts are of confirmed, evidence-backed divergences from the reference — not of style
differences. "Fixed" means the port now matches; "excluded" means it deliberately does not,
with the reason recorded in the area report.

| Area | Reference units read fully | Confirmed defects | Fixed | Deliberate exclusions |
| --- | --- | --- | --- | --- |
| Chart parsing / metadata / library / archive | 9 sources, 178 function/field groups, plus all 14 reference unit tests | 32 | 32 | 9 |
| Core data model | 20 sources | 12 | 8 | 4 |
| Judging / session / replay | 6 sources + 9 tests | 10 | 9 | 1 |
| Render pipeline / HUD | `GameRenderer.java` render half, all 29 shader assets | 5 | 5 | 3 |
| Interface screens | 6 screens + resources | 3 | 3 | 1 |
| Gameplay shell / audio | 4 sources + native audio | 9 | 9 | 5 |
| Platform | manifest, plist, activity, lifecycle | 4 | 4 | 3 |
| Numeric constants (sweep) | 188 named constants across the reference renderer | 0 | 0 | 1 (a declared-but-unread constant) |

Cross-area work was completed rather than left as requests: the level-effect challenge state
(`onBadOrMiss`, `onBlockStateChanged`, `lowPassOnBlock`), the Ametrine failure vignette, the
blocked-touch positions and the autoplay click-effect debris source are all now driven from the
gameplay loop and consumed by the renderer. `Chart::levelEffects` became a mutable per-chart
copy because the challenge state advances during a run.

## 4. Verification

All of the following are reproducible from a clean tree and were run after the fixes landed.

| Gate | Command | Result |
| --- | --- | --- |
| Desktop build | `build.bat desktop Release` | clean |
| Unit/integration suites | `ctest --test-dir build/desktop-release -C Release` | **8/8 pass** |
| Application loop | `build.bat verify-app` | **11/11 checks pass** |
| Render matrix | `build.bat verify-render` | **26/26 cases pass** |
| GLSL ES shader check | `scripts/check-gles-shaders.ps1` | **43/43 shaders compile** |
| Block composite probe | `rechigros_block_probe --assets assets --out-dir build/probe` | PASS — framebuffer/viewport/blend restored, presented == composite (0.000000% channel difference) |
| Import flow | `rechigros_import_check.exe` | pass |
| iOS packaging | `build.bat verify-ios` | pass, including the new portrait-absence assertion |
| Android package | `build.bat android Release` | APK produced; manifest verified to declare `screenOrientation=landscape` |
| Recording encoder | `rechigros_record_check <dir>` | pass — 30 synthetic frames encode to a readable H.264 file |
| Recording end to end | `ReChigros --record out.mp4 <chart>` | pass — **10 360 frames** at 1920×1080 with an AAC 48 kHz stereo track; extracted frames show the play field, hit particles, the HUD and the `RECORD` combo label, and `volumedetect` confirms the track carries real audio |

### Chart recording

The detail page gained a recording action above the start plate. It opens a console-styled page
holding every parameter of a render — resolution, frame rate, codec, file name, output directory,
combo label text, note hitsound and note hit particles — and starts a progress plate that can be
cancelled while it runs. Everything the page does not name (reveal mode, skin, disabled shaders,
note scaling, judgement windows and so on) comes from the player's current settings, so a
recording is the game as configured.

The pipeline renders offscreen one frame at a time and pipes raw frames to an external encoder;
it never reads the wall clock, so the same chart produces the same video regardless of machine
speed. Two design points worth recording:

- **Diagnostics go to a file, not a pipe.** The encoder's error stream was first connected to a
  pipe, which has no reader while frames are being pushed; a chatty encoder filled it and stopped
  accepting frames, surfacing as "the pipe is being closed" partway through an otherwise healthy
  recording. Writing the log to a file and reading it only at the end fixes that and makes a
  failure explain itself.
- **The capture target is allocated once.** A recording of a five-minute chart is a few thousand
  frames; reallocating the offscreen targets per frame would dominate the run.

The audio track is the chart's music, decoded once to interleaved stereo float samples and handed
to the encoder as a second input; the encoder delays it by the length of the opening transition, so
the first beat lands on the first note. Turning the note-hitsound switch off omits the track
entirely. Verified on a full chart: 10 360 frames of H.264 plus an AAC track at 48 kHz stereo, with
`volumedetect` reporting a mean of −10.8 dB and a peak of 0.0 dB — real music, not silence. The
video is 2:52 long, matching the chart.

`--record <path>` drives the same session from the command line and prints `RECORD PASS`, which
is how the end-to-end result above was produced.


### The one behavioural fix proven numerically

The `yNorm` correction changes where every note is drawn on non-layer charts, so it was proven
with an executable invariant check rather than by eye. The check builds against the real core
library and asserts the reference's own derivation:

```
non-layer y-position 0.8000 -> yNorm 0.200000  OK
non-layer y-position 0.0000 -> yNorm 1.000000  OK
non-layer y-position 1.0000 -> yNorm 0.000000  OK
layer offset  0.3000 -> yNorm 0.200000  OK
layer offset -0.3000 -> yNorm 0.800000  OK
official raw y  0.8000 -> yNorm 0.200000  OK
official raw y  0.1300 -> yNorm 0.870000  OK
nan move        -> yNorm 0.500000 xNorm 0.500000  OK
```

The complementary result matters as much: rendered gameplay for an *official* chart is
**unchanged** by the fix, because official charts always populate event layers and therefore
never took the affected branch. That is why the reference captures already looked plausible.

### Interaction cost after the stall fixes

The performance defects above were measured with the application's own frame profiler
(`--frame-stats`), which the port emits from inside the real event/update/render loop. Each
interface was opened directly so its first frame is the one under test.

| Interface | First-frame draw before | After |
| --- | --- | --- |
| Local chart list | 2869 ms | **5.0 ms** |
| Chart detail | — | 5.1 ms |
| Home | — | 5.1 ms |
| Settings | — | 6.5 ms |
| Result | — | 7.5 ms |
| Calibration | — | 23.0 ms |

Steady state is 4.4–5.0 ms of draw time per frame across every interface, against a 16.7 ms
budget at 60 Hz. With vsync disabled the whole frame costs 2.3 ms on the CPU, of which 2.0 ms
is drawing, so the interface layer is not the limiting cost.

The pause cycle was re-measured the same way: the worst frame across a 900-frame run that
opens the pause menu, counts down and resumes is **11.4 ms**, where it previously contained a
3.1 s stall.

### Numeric-constant sweep

A name-to-name diff is not a valid parity test, but *values* are. `tools/parity/constant_values.py`
extracts every named numeric constant from the reference renderer and searches the port's
render, interface, core and game sources for that exact literal:

```
numeric reference constants : 188
values absent from the port : 1
  HOLD_DESTROY_DELAY = 0.250
```

That one constant is declared in the reference and **never read** — it has exactly one
occurrence in the whole file, the declaration itself — so there is nothing to port. Every other
layout, timing, colour-ramp and judgement constant survives at its original value, including the
whole official HUD block (pause rect, score/combo/song/level text anchors and sizes, progress
bar), the reveal camera (0.20 alpha, 0.30 scale, 1.0 s transition), the score catch-up window
(50 ms), and the challenge judging windows.

The limit of this evidence: a matching literal proves the value was transcribed, not that it is
used in the same formula. It is a strong filter, not a substitute for the behaviour checks in
the per-area reports.

### Behaviour spot-checks from the worklist

Three units named in the coverage matrix as unadjudicated were compared line by line against the
reference implementation:

| Unit | Result |
| --- | --- |
| `HitOffsetIndicator` | MATCH — `MAX_ANGLE_DEG` 60, `MOVE_DURATION_SEC` 0.20, `RETURN_DELAY_SEC` 0.45, `RETURN_DURATION_SEC` 0.35, `MARK_LIFE_SEC` 0.90 all identical; `angleForDiff`, `fadeOutAlpha` (`(1-t)²`), `formatOffsetMs` (`%+04dms`, float-narrowed before rounding), `applyPointerMove` (ease-out cubic), `shouldShowCommitMark` and the reset state all reproduce the reference line for line |
| Score display catch-up | MATCH — non-finite target → NaN, non-finite current → target, `factor = dtMs / 50` clamped to [0,1], convergence threshold 0.0001; `dtMs` clamped to 100 ms by the caller, as in the reference |
| Multi-press highlight | MATCH — the scale comes from the highlighted texture's width over the normal one, clamped to 1 when either is missing; the hold body also switches texture and UV set on the highlighted variant |
| Chart reveal camera | MATCH — 0.20 hidden alpha, 0.30 target scale and the 1.0 s transition are all present, with the same two-segment curve (shrink in at the start, grow back before the exit) and the same clamp |
| Skin colour parsing | See §4a — the port deliberately **does not** reproduce the reference's branch order, which misreads the documented decimal spelling |
| Stage layout | MATCH — the letterbox rule clamps the requested aspect to the viewport's (`ar = min(target, viewAr)`), then letterboxes on the axis that has room and centres on the other |
| Low-resolution mode / antialias | MATCH on sizing (`sceneFbo` is exactly `view/2` in the reference, `halfResFbo` likewise here). One divergence **fixed**: the reference suppresses the antialias pass during the resume fade (`menuVisible && pauseMode != PAUSE_FADEOUT`), which the port could not express because the fade was mapped onto the countdown mode. The fade now carries its own `PauseMode::FadeOut`, draws with the countdown's backdrop curves, and is excluded from the antialias predicate |

## 4a. Reference defects that were fixed rather than copied

Parity means reproducing the *intended* behaviour, not every line. Where the reference's own
code contradicts its documented intent or its platform's guarantees, the port implements the
intent and records the divergence here. Each entry states what the reference does, why that is
wrong, and what the port does instead.

### `parseSkinColor` misreads its own documented decimal spelling

`GameRenderer.java:14247` documents three accepted forms, including "decimal integer (e.g.
`16777215` for white)". The implementation tests the hexadecimal branch **first**, and that
branch accepts any token of six or more characters that happen to be hex digits:

```java
if (s.length() >= 6 && s.chars().allMatch(c -> Character.digit(c, 16) >= 0)) { ... }
```

Every decimal string of six or more digits consists only of hex digits, so the decimal branch is
reachable only for values below `100000`. A skin written as `colorPerfect: 16777215` — the
spelling the comment advertises for white — is read as the hex value `0x16777215`, which has an
alpha byte of `0x01` and channels `0x77/0x72/0x15`: a dark, nearly transparent colour.

**Port:** only an explicit `0x` prefix forces the hex reading. A token made purely of decimal
digits is read as decimal, so the documented spelling yields white. A token containing letters
can only be hexadecimal, and prefixed hex is unchanged, so every previously *working* skin keeps
its meaning. Covered by `tests/SkinConfigTests.cpp`.

### `formatOffsetMs` rounds negative halves the opposite way from the platform

The reference formats the offset readout with `Math.round`, whose contract is `floor(x + 0.5)`:
a value on an exact half moves towards positive infinity, so `-0.5` becomes `0`. The port used
the C library's `lround`, which rounds a half away from zero and produced `-1` for the same
input — a one-millisecond discrepancy in a readout the player is meant to read as their own
timing error, and a difference that appears only on the exact half-millisecond boundary.

**Port:** rounds with `floor(x + 0.5)` after the same float narrowing the reference performs, so
both spellings print the same digits. Covered by boundary assertions in `tests/CoreTests.cpp`.

### A declared-but-unread constant

`HOLD_DESTROY_DELAY = 0.250` is declared in the reference and never read — one occurrence in the
whole file, the declaration itself. Nothing is ported for it, and none of the port's `0.25`
literals correspond to it (they are a separate miss-delay that the reference writes inline).


## 5. Deliberate exclusions

These are **not** defects. Each was a decision to reproduce behaviour rather than a mechanism, or
to keep user data intact.

| Item | Why it is excluded |
| --- | --- |
| Online chart catalogue and its client | depends on a third-party hosting service. The *screens*, their layout, empty state and page controls are all present and match; only the fetched content is absent. |
| Storyboard *video* playback | the reference uses the platform's video pipeline on mobile only, and no shipped chart uses a video. The video **data model** is now complete and faithful, so a future playback path has correct inputs. |
| Native Android view toolkit | the port draws the same interface in immediate mode. Parity is defined and verified per screen (§`ui.md`), not per widget class. |
| PCM injection across a JNI boundary | the reference feeds decoded PCM to native audio through JNI because that is its process boundary; the port has no such boundary and loads the same files directly. Same audio, one less copy. |
| Chart library record ids | the reference keys records by the MD5 of the imported archive. The port must cover both archive and directory imports with one scheme, and switching would orphan existing pack directories and their score rows. Ids are internal surrogates. |
| Difficulty-label Unicode folding | the reference folds U+2002 and ASCII spaces; the port folds a wider set (including EM SPACE). The port behaviour is pinned by its own test suite and is a superset, not a divergence for any label the reference can produce. |
| Frame-rate hint / keep-screen-on | compositor and system conveniences with no effect on the drawn picture; the port is vsync-driven on both sides. |

## 6. Remaining risk

1. **No Android hardware verification in this pass.** The emulator was offline. The orientation
   fix is verified from the packaged manifest and the source of the override that suppresses the
   toolkit's runtime re-request, but not from a device screenshot. Re-running
   `scripts\check-android-render.ps1 -Serial 127.0.0.1:5555` closes it.
2. **iOS was never compiled.** `Info.plist` and the packaging inputs are verified by
   `verify-ios-project.ps1`; producing the binary needs macOS.
3. **`bpm <= 0` edge behaviour** now matches the reference in producing ±Inf rather than a
   clamped value; the reachable sinks are recorded in `core.md` and produce infinite note
   lengths rather than render-path NaN.
4. **`.pec`/plain-JSON charts have no reference capture**, so the `yNorm` fix is proven
   numerically and by construction rather than by a screenshot diff.
5. **Settlement screenshots** used runs with different mode combinations; the mode-tag logic was
   verified from source, but a same-mode capture pair was not produced.
6. **Performance** was not re-benchmarked after these changes. Nothing added is per-frame work
   beyond what the reference does, but the frame-time measurements in the older handover
   documents should be re-taken before they are quoted again.

## 7. Documents that were wrong

The previous `HANDOVER.md`, `MIGRATION.md` and `docs/COVERAGE_AUDIT.md` all describe the port as
essentially complete and state that the S1/S2 gaps were closed. They were not: this pass found
player-visible defects in the chart parsers, the core data model, judging, rendering, the
interface, and the platform layer. `docs/COVERAGE_AUDIT.md` is the least unreliable of the three
because it records evidence per claim, but its "Fixed" tables should not be trusted without
re-running the check it cites. The reports in this directory supersede all three.
