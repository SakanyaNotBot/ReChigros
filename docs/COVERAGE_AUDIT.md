# Functional coverage audit — Chigros (Java/Android) vs ReChigros (C++20/SDL3)

Read-only source comparison. Reference = `E:\Chigros`, port = `E:\ReChigros`.
Nothing was built or executed; every claim below is a source-level observation with both
sides cited. Items that could not be settled from source alone are marked **UNVERIFIED**
with the check that would settle them.

**Snapshot caveat.** The port tree was being edited by another process while this audit ran:
`src/app/CaptureHarness.cpp` and `src/app/UiText.cpp` appeared mid-audit,
`src/render/GameRenderer.cpp` received a write inside the audit window, and `git status` in
`E:\ReChigros` reports ~18 modified files plus 2 untracked files besides this report. Every
headline finding (§1 #1, #2, #4, #12) was re-verified against the tree at the end of the audit
and still holds, but **line numbers are as-of this snapshot** and will drift as the port
continues.

## 0. Baseline measured in this checkout

| Item | Java | C++ |
| --- | --- | --- |
| Main sources | 75 files, 40,024 lines | 74 sources, 20,604 lines |
| Headers | — | 76 files, 4,201 lines |
| Test sources | 14 files, 837 lines | 6 files, 1,972 lines |
| Total first-party | 40,861 | 24,805 (+1,972 tests) |

Both existing port notes are unreliable and are contradicted by this audit:

* `MIGRATION.md:8` states "**18,800 lines** of first-party C++ … about 47% of the reference",
  while the tree measures 24,805 lines — the note is stale by roughly 6,000 lines.
* `MIGRATION.md:45` claims practice judgement windows are "done … applied to taps and holds
  only". They are **not applied anywhere** in the judging path (§4.3).
* `MIGRATION.md:58` calls the official parser "in progress"; it is in fact complete for the
  documented schema (§2.3) — the note understates on this axis while overstating elsewhere.
* `docs/PORTING_INVENTORY.md` is dead: it references 11 C++ files that do not exist
  (`src/core/Chart.cpp:23`, `include/rechigros/game/PrprVideo.hpp:28`,
  `ReplayData.hpp:33`, `SkinConfig.hpp:34`, `ResPackInfo.hpp:35`,
  `src/platform/ZipArchive.cpp:96`, `src/platform/net/LanzouClient.cpp:98`,
  `src/platform/net/OnlineIndexClient.cpp:99`, `src/app/screens/*.cpp:68-87`,
  `src/app/ChartMetadata.cpp:95`, `src/app/ui/*.cpp:72-80`) and marks 30+ units "todo" that
  are implemented. It should be deleted or regenerated before anyone relies on it again.

Severity vocabulary used below: **S1** player-visible correctness, **S2** missing feature,
**S3** fidelity/robustness, **S4** hygiene.

---

## 1. Executive summary — top 15 actionable gaps by player-visible impact

| # | Sev | Gap | Evidence (Java → C++) |
| --- | --- | --- | --- |
| 1 | S1 | **Practice judgement-window sliders have no effect.** The port implements the override but nothing consults it: `JudgementEngine::calculate()` — the only reader of `practiceWindows_` — has no call site. All judging uses raw chart windows. | `GameRenderer.java:10355-10356`, `:2695-2697` (settlement uses `tapPerfectWindowSec/tapGoodWindowSec`) → `JudgementEngine.cpp:114-132` (dead) + `GameplaySession.cpp:489,496,513` (`judgement_.windows()`) |
| 2 | S1 | **Replay model is incompatible, and playback re-imposes recorded modes.** Java records *judgement entries* keyed by note index (`t/ni/j/ts`, types TAP/DRAG/HOLD_PRESS/HOLD_RELEASE/FLICK) and states that recorded modes are display-only; the port records *touch samples* (`Down/Move/Up` + normalised x/y), re-synthesises input on playback, and forces the recorded mirror/challenge/speeds/offsets onto the run. Consequences: no note-by-note verification, no hold press/release record, no Java replay can be played back, and a replay recorded in challenge mode is judged with challenge windows. | `ReplayData.java:9-14,41-42,77-82`, `ReplayManager.java` → `Replay.hpp` (`ReplayTouchEvent`), `GameScreen.cpp:139-167`, `CommonScreens.cpp:127-132,283-290` |
| 3 | S1 | **Header-less JSON charts are forced to official format 3**, which flips the hold-geometry branch (`official` vs integrated floor position) for every such pack. Java binds them as a plain `Chart` leaving `formatVersion` at its filed value (0). | `ChartLoader.java:881-887` (`parseChartJson`), `Chart.java:237,264-281` → `ChartLoader.cpp:537-544`, `OfficialChartParser.cpp:131`, `JudgeLine.cpp:122-125` |
| 4 | S1 | **`info.yml`'s `useAttachUiFix` is parsed but never applied**, so ATTACH-UI elements (pause / combo / score / bar / name / level) keep the default `true` even when a pack disables the fix. | `ChartLoader.java:910-922` (`tryApplyAttachUiFixFlag`) → `InfoFile.cpp:246-258` writes `Info::useAttachUiFix`, `hasUseAttachUiFix`; no consumer exists in `src/` or `include/` |
| 5 | S1 | **RPE colours written as objects (`{r,g,b}`) are dropped**, and mixed-magnitude colours are rescaled per channel instead of all-or-nothing. Affects note tints, hit-effect tints and `colorEvents`. | `RpeChartParser.java:2445-2480` (array *and* object, one shared `/255` decision) → `RpeChartParser.cpp:674-686` (arrays only, per-channel `/255`) |
| 6 | S1 | **Combined line-scale events (`scaleEvents`) are never parsed**, and the `xEvents`/`moveX`/`moveY`/`yEvents` aliases are unsupported, so authored line scaling and some move tracks are lost. | `RpeChartParser.java:864-866` (move aliases), `:1205` (`scaleEvents`) → `RpeChartParser.cpp:817-824` (only `moveXEvents`/`judgeLineMoveXEvents`), no `scaleEvents` key anywhere |
| 7 | S1 | **Lines that only carry text *events* are tinted P-CHIGROS colour instead of white.** Java's white-tint condition includes a non-empty `judgeLineTextEvents`; the port omits that term. | `JudgeLine.java:415-418` → `JudgeLine.cpp:96` |
| 8 | S2 | **Mixed move layers interpolate differently.** When a layer has moveX/moveY lists, Java uses them and *ignores* `MoveEvents`; the port falls back per-axis to `MoveEvents` whenever the X list is empty. | `JudgeLine.java:352-358` → `JudgeLine.cpp:86` |
| 9 | S2 | **Hold body feedback is missing.** Java emits a burst every `holdFxTimerSec` while a hold is active (`spawnDueHoldBodyHitEffects`, driven by `note.holdFxActive`); the port has no equivalent and even replaced the accumulator with a timestamp (`holdFxAtSec`), so the head burst is the only hold feedback. | `Note.java:70-73` (`holdFxTimerSec` = seconds since last burst), `GameRenderer.java:10445-10447` → `Note.hpp:40` (`holdFxAtSec`), no `spawnDueHoldBodyHitEffects` equivalent |
| 10 | S2 | **Difficulty/level label text is normalised differently.** Java collapses space runs and converts them to U+2002 EN SPACE; the port maps every Unicode space to ASCII 0x20 and does not collapse. The Java unit test's expectation is therefore not covered. | `InfoFileUtils.java` `normalizeDifficultyText`/`collapseNormalSpaceRuns` + `InfoFileUtilsTest` → `InfoFile.cpp` `normalizeDifficultyText` → `normaliseUnicodeSpaces` |
| 11 | S2 | **`judgeWindowSec(NOTE_HOLD)` ignores the practice override.** Java returns `tapGoodWindowSec()` (practice-aware); the port returns the chart good window. Consequence of #1 that persists even after #1 is fixed at the settlement site. | `GameRenderer.java:2739-2740` → `JudgementEngine.cpp:107-108` |
| 12 | S2 | **Replay metadata lost**: `goodEarly`, `goodLate`, `stdDevMs`, `judgementOffsetBins` are absent from the port's replay meta, so a replayed run cannot reproduce the reference's result screen (histogram, std-dev, early/late split), and Java's fallback that prefers saved bins when the live histogram is empty has no counterpart. | `ReplayData.java:36-39`, `GameRenderer.java:3040-3060` → `Replay.hpp` `ReplayMeta` |
| 13 | S3 | **`useOfficialSpeed` interacts differently between render and judge paths in the port's geometry helpers**: `NoteGeometry` documents three speed bases but only one is ever used, `renderSpeed`/`judgeSpeed` are dead, and note geometry is duplicated in `NoteBatch.cpp`. Today's numerics agree for the paths that matter, but there is no single source of truth. | `GameRenderer.java:2638` (render basis), `:10525` (judge basis), `:10602` (control basis) → `NoteGeometry.hpp:13-24`, `NoteGeometry.cpp:15-37` (unused), `NoteBatch.cpp:294,449` (second copy) |
| 14 | S3 | **`Chart::prepareRuntime` is single-threaded**; Java parallelises per-line runtime init when `cores≥2 && lines≥2 && notes≥800`, and Java additionally uses an incremental `floorPositionMonotonic` cursor for note floor positions whereas the port binary-searches every note. Startup time on large charts will be measurably worse. | `Chart.java:208-228,301-408,115-169` → `JudgeLine.cpp:110-135` |
| 15 | S2 | **Default play-field aspect differs**: Java defaults to 16:9 (clamped 0.5–5.0, fallback 16:9 on corrupt values); the port defaults to `0.0` meaning "fill the window" and applies no clamp on load, so a fresh install on a 21:9 display letterboxes differently from the reference. | `ChartSelectActivity.java:211,2678-2681` → `Settings.hpp:15`, `Settings.cpp:77,106-114` |

Secondary but concrete: judge-line alpha `-1`/`-2` handling matches; note-control easing clamp matches;
score/accuracy/grade formulas match exactly (see §4.4). Those are *not* problems and are recorded
so the Lead does not re-litigate them. Repository hygiene: a zero-line file literally named
`$null` sits at the repository root (`E:\ReChigros\$null`) — evidence of a botched shell redirect
in a build/verification script; harmless but should be deleted and its script fixed.

---

Status update: items #1, #8, #11 are FIXED by the interface/audit pass; items #2, #3, #4, #9,
#10, #12 and #15 are FIXED; see the "Fixed" tables below. Items #5, #6 and #7 were fixed
earlier in commit 9b0bc7d. Remaining open items are the refactors #13 and #14 (S3).

### Fixed by the interface/audit pass

| # | Sev | Gap | Fix and evidence |
| --- | --- | --- | --- |
| 1 | S1 | Practice judgement-window sliders have no effect | `JudgementEngine::calculate` reads `practiceOverrideActive()`/`practiceWindows_` for tap and hold perfect/good, `judgeWindowForNote`/`lateMissLimitForNote` route through `tapPerfectWindow`/`tapGoodWindow`, and `GameplaySession` uses them for classification, spatial falloff, detection and late-miss. `CoreTests` practice-window case, app loop `practice-window` check |
| 8 | S2 | Mixed move layers interpolate differently | `JudgeLine::fillState` resolves a layer's move from the split X/Y tracks whenever either is present and falls back to the packed move events only for layers without them, matching the reference branch. `ChartParserTests`, render matrix |
| 11 | S2 | `judgeWindowSec(NOTE_HOLD)` ignored the practice override | `JudgementEngine::judgeWindowForNote(NOTE_HOLD)` returns `tapGoodWindow()`, which applies the practice override; used by the judging and settlement paths |

### Fixed by task-11

| # | Sev | Gap | Fix and evidence |
| --- | --- | --- | --- |
| 2 | S1 | Replay model incompatible | Judgement-entry model ported: `game::ReplayJudgement` + recording in `GameplaySession::commitNote`/`recordHoldPress`, playback in `applyReplayEntries`; v2 file format `{t,ni,j,ts}` with reference field names, legacy touch files still read; recorded modes are display-only on playback. `CoreTests`, `AppDataTests`, app round trip 3/3 exact match |
| 3 | S1 | Header-less JSON forced to official 3 | New `PlainChartParser` (ChartLoader routes `JsonObject` to it): `formatVersion` stays 0, no bpm clamp, `isCover`/`invertRotation` keep plain defaults, coordinates not re-packed, no block areas. `ChartParserTests` |
| 4 | S1 | `useAttachUiFix` never applied | `ChartLoader::applyAttachUiFixFlag` searches six parent levels for info.yml/yaml/json and applies the flag; nearest file wins. `ChartParserTests` |
| 9 | S2 | Hold body feedback missing | `Note::holdFxTimerSec` timer in `GameplaySession::updateHoldBodyBursts` (half-beat interval, one burst per crossing, timer reset while the note has not started); bursts flow through `RenderFrameInput` to the hit-effect pass. `CoreTests` + capture |
| 10 | S2 | Difficulty label normalisation | `InfoFile::normalizeDifficultyText` collapses ASCII space runs then emits U+2002; default label uses an en space. `AppDataTests` |
| 12 | S2 | Replay metadata lost | `ReplayMeta` carries `goodEarly/goodLate/stdDevMs/judgementOffsetBins`; saved on record and used by `mergeReplayPlaybackResult` (recorded stats win, live histogram replaces saved when non-empty). `AppDataTests` |
| 15 | S2 | Default play-field aspect differs | `Settings::aspectRatio` defaults to 16:9 and is sanitised on load (0 = follow window, 0.5..5.0 clamp, corrupt → 16:9). `AppDataTests` |

## 2. Chart parsing

### 2.1 Format detection and loader wiring

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Detection source | first 64 KiB, BOM+whitespace skipped, must start with `{` | same (`kDetectionPrefixBytes = 64*1024`, `firstSignificantByte`, `looksLikeJsonObject`) | MATCHES |
| Official marker | `"formatVersion"` substring | same | MATCHES |
| RPE markers | `"RPEVersion"`, `"BPMList"`, `"META"`, `"JudgeLineList"` | same four | MATCHES |
| Fallback | `PHIEDIT` → if body is a JSON object: Jackson-bind into `Chart`; else PEC text | `PhiEdit`/`JsonObject` → **always `OfficialChartParser`** for JSON objects | **DIFFERS** (row 3 of §1) |
| Offset units | RPE `/1000`; Official raw; PEC `(ms-150)/1000` rounded to 4 dp | identical three rules | MATCHES |
| Normalised digest | MD5 over bytes minus BOM/` \t\r\n\f\v` | identical algorithm (`Md5` over the same filter) | MATCHES |
| `line.csv` application | Official + PHIEDIT only, case-insensitive chart-name match, single-chart fallback | Official + JsonObject + PhiEdit, lowercase match, same single-chart fallback, plus `useOfficialScale=true` when an image is named | MATCHES (equivalent; C++ additionally sets `useOfficialScale`) |
| `extra.json` search | up to 6 parent levels, `extra.json`/`Extra.json` | same (`kExtraSearchDepth=6`) | MATCHES |
| Level-effect table | selected elsewhere in Java | `ChartLoader.cpp:559-566` appends `effect->buildPrprEffects()` to `chart.prprEffects` | **DIFFERS (structure)** — port-only wiring; see §3.6 |
| `chart.useAttachUiFix` | from `info.yml` | never set | **MISSING** (§1 #4) |

Java's `parseChartJson` (`ChartLoader.java:881-887`) is a plain field-binding. Two consequences the
port does not reproduce: (a) `formatVersion` stays 0 when absent (see §1 #3); (b) Jackson's
field-visibility binding ignores none of the port's coercions (e.g. `isCover=true`,
`invertRotation=false`, coverage events) because those live in the official parser, not in the
binder. Practically, a header-less pack authored by this project will load in both, but with
different hold geometry and different judge-line alpha/rotation defaults.

### 2.2 RPE parser (`RpeChartParser.java` 2,308 lines vs `RpeChartParser.cpp` 957)

Key-level diff of every quoted JSON key: Java reads 188 distinct keys, the port 174; the port
adds none. The 14 Java-only keys are exactly the alias/object-form gaps below.

| Field group | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Note type mapping | RPE 1→TAP, 2→HOLD, 3→FLICK, 4→DRAG; invalid type ⇒ note dropped | identical mapping and order (PEC numbering, not RPE numbering — intentional fidelity) but invalid type ⇒ **TAP** | **PARTIAL** (`RpeChartParser.java:1520-1524` vs `:690-700`) |
| Note time | `bpm.normBeat(beat)` → normalised ticks | same | MATCHES |
| Hold duration | `holdTime` beat → end beat, converted through the BPM timeline to seconds, then to line ticks | same three sources in the same order, same `*32` for `hold`/`duration` | MATCHES |
| `speed`, `positionX` (`/75.375`), `isFake`, `size`/`width`/`xScale`, `alpha` byte-scaling `(>1.5 or <-2.5 ⇒ /255)`, `visibleTime` (`≥999999 ⇒ -1`), `yOffset` (`/900`), `judgeArea` (8 aliases), `tint`, `hitEffectTint`, `tintHitEffects` fallback | `:1562-1630` | `:711-744` | MATCHES |
| Note controls (alpha/size/pos/y/skew/judgeArea) | RPE interval easing belongs to the *following* point | same shift (`raw[i+1].easing`), `easing-1` clamped 0..28 | MATCHES |
| Speed events | `v / 4.5`, end defaults to start, gap-filled | same `/4.5`, same default, `ensureSpeedCoverage` gap fill | MATCHES |
| Speed **layers** | per-layer lists, each integrated separately and **summed**; a merged list is *not* kept | per-layer lists kept **and** a merged `speedEvents` curve is synthesised (`mergeSpeedLayers`) *and* a legacy re-sampling mode is applied (`applyLegacySpeedHeightMode`) | **DIFFERS (port-only transform)** — see risk note below |
| Line fields | `bpmfactor`, `zOrder`, `isCover`, `father`, `rotateWithFather`, `attachUI` (string or int), texture (drops `line.png`), `invertRotation=false`, `rpeTextureScaleInPixels=false` | identical, including dropping `line.png` and `invertRotation=false` | MATCHES |
| Anchor | `{x,y}` **or** `{anchorX,AnchorX}`/`{anchorY,AnchorY}` inside the anchor node | `vec2({anchor,Anchor})`; object-form key set is `x/X`,`y/Y` only | **PARTIAL** (`:2425-2443` vs `:797`) — `AnchorX/AnchorY` spellings ignored |
| Event-layer move X/Y | `moveXEvents`/`judgeLineMoveXEvents`/**`xEvents`**/**`moveX`** (and Y equivalents); if *either* X or Y list exists, `MoveEvents` is not used | only `moveXEvents`/`judgeLineMoveXEvents`; per-axis fallback into `MoveEvents` | **PARTIAL + DIFFERS** (`:864-866,352-358` vs `:817-824,86`) |
| Alpha events | key matched by substring: contains `alpha` \| `disappear` \| `opacity` | explicit list `alphaEvents`,`opacityEvents` plus `judgeLineDisappearEvents`,`disappearEvents` | **PARTIAL** (`:743` vs `:228-232`) — e.g. `AlphaEvents`, `disappear` alone are missed |
| `extended.scaleXEvents/scaleYEvents` | parsed, `syEvents`/`sxEvents` aliases | same aliases | MATCHES |
| `extended.scaleEvents` (combined MoveEvent scale) | parsed into `judgeLineScaleEvents` | **absent** | **MISSING** (`:1205` vs no key) |
| `extended.inclineEvents` / `skew` | degrees → radians, coverage prepend, `skew` folded in | identical, same `π/180`, same folding rule | MATCHES |
| `extended.colorEvents` | `start`/`end` as arrays **or objects** | arrays only | **MISSING** (§1 #5) |
| `extended.textEvents`, `gifEvents` | parsed with easing | identical | MATCHES |
| Coverage synthesis | `ensureCoverageRotate` (0.0, extrapolate), `ensureCoverageAlpha` (0.0, no extrapolate), `ensureCoveragePos` (0.0, extrapolate), `ensureCoverageMove`, `ensureSpeedCoverage` | one generic `ensureCoverage(events, fallback, extrapolate)` called with the same triples | MATCHES — **note**: both sides therefore make a line with no alpha events fully transparent (`alpha=0`); this is reference behaviour, faithfully copied. |

Two port-only behaviours in the speed path deserve an explicit decision because they change
`EventUtils::getFloorPosition` inputs rather than merely reshape data:

* `applyLegacySpeedHeightMode` (`RpeChartParser.cpp:616-621,841`) rewrites every speed layer by
  re-sampling it through `buildLegacySpeedHeightLayer`. Java has no such pass; its layers go
  straight from parse to integration (`Normalize` → `initSpeedEvents`). If the re-sampling is not
  an exact identity, note-to-line distances differ from the reference on every chart with layers.
* `mergeSpeedLayers` (`:846`) produces a combined `speedEvents` list that Java never builds for
  layer charts. Because `Chart.prepareRuntime` prefers the layer sum when layers exist
  (`EventUtils.cpp:905-918` / `Chart.java:181-202`), the merged list is only observable through
  the judge line's own `speedEvents` (speed-at-time readouts, some HUD paths). Harmless only if
  no consumer reads `speedEvents` on a layered chart — **UNVERIFIED**; settle by listing every
  read of `JudgeLine::speedEvents` outside `prepareRuntime`/`normalize`.

### 2.3 Official parser (`OfficialChartParser.java` 543/628 lines vs `.cpp` 137, dense style)

| Field group | Java | C++ | Verdict |
| --- | --- | --- | --- |
| `formatVersion` | default/coerced to 3 (accepts 1 and 3) | identical | MATCHES |
| `offset` | raw, no unit conversion | raw | MATCHES |
| `isCover=true`, `invertRotation=false`, `bpm<=0 ⇒ 120` | `:250-254` | `:46` | MATCHES |
| Notes | `type` mapped 1..4 (identity onto GameConstants), `time`, `holdTime`, `positionX`, `speed`, `isFake`, `alpha`, `size`, `xScale`, `yOffset`, `visibleTime`, `judgeArea` | same 12 fields, same key aliases | MATCHES |
| Line events | `judgeLineDisappearEvents`/`disappearEvents`, `judgeLineRotateEvents`/`rotateEvents` (negated), sorted, coverage prepended | same, same negation, same coverage | MATCHES |
| Move packing | V1: unpack `packed = x*1000 + y` by integer div/mod, then `/= 880` (X) or `/= 520` (Y), then `-0.5`; V3: `start`/`start2`, `end`/`end2`, then `-0.5` | identical, including the 880/520 divisors and the `-0.5` applied after division | MATCHES |
| Speed events | value/endValue = `value`, `endTime` defaults to `start+1e9`, sorted, `initSpeedEvents` | same, init deferred to `normalize()` | MATCHES |
| Block areas | `topRightPercentage`/`bottomLeftPercentage` with per-component defaults 1/0; `appearTime` 0; `enableTime` 0; `disableTime`/`disappearTime` `Float.MAX_VALUE`; `isSubtract`; rotate (`anchor` 0.5, `time`, `easeType`, `rotation`); move (`endPosition` 0.5, `easeTypeX/Y`); scale (`anchor`, `easeTypeX/Y`, `scale` 1.0) | identical field set, defaults and sort order (`3.402823466e+38` == `Float.MAX_VALUE`) | MATCHES |
| Attach-UI index | read but never assigned by this parser (dead on both sides) | same (dead) | MATCHES (both dead) |
| Parallel line parse | yes | no | **PARTIAL** (perf only) |

The audit finds **no field gap** in the official parser despite `MIGRATION.md:58`'s "in progress".
The one structural difference is the loader's format coercion (§1 #3), not the parser.

### 2.4 PEC parser (`PecChartParser.java` 721 lines vs `.cpp` 339)

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Offset | `round4((ms - 150)/1000)` | `round((ms-150)/1000*1e4)/1e4` | MATCHES |
| Commands | `bp`,`n1..n4`,`cv`,`cm`,`cp`,`cr`,`cd`,`cf`,`ca`; `#` sets previous note speed; `&` sets previous note xScale | identical set and ordering, identical `#`/`&` handling | MATCHES |
| Note beam/lane ids | `(int)parseDouble(tok[1])` map key | same | MATCHES |
| Hold duration | `max(0, endBeat - timeBeat)` in ticks | same | MATCHES |
| `positionX` | `raw * 9/1024` | same | MATCHES |
| `cv` speed | `v/7` | same | MATCHES |
| `cm`/`cp` | x `/2048`, y `/1400`, easing clamped | same | MATCHES |
| `cr`/`cd`/`cf`/`ca` | same defaults (0°, 255) and `easingType` | same | MATCHES |
| Alpha specials | `-1→-255`, `-2→-510`; `< -100` encodes visibleTime `(-100 - v)/10` beats; samples on a 4-tick grid; value `/255` at the end | identical, including the 4-tick grid and per-note BPM for the seconds conversion | MATCHES |
| Note normalisation | sorts, clamps `xScale`/`size`/`speed` | same | MATCHES |

PEC is the most complete area of the port.

### 2.5 Library / import / archive / info

| Unit | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Zip extraction | hand-rolled reader incl. Zip64, `DetectedPack` = {chart, music, bg, info} resolved through `info.yml` | `Archive.cpp:215-258` extraction only, **but** `ChartLibrary.cpp:62-216` supplies `findInfoFile`, `findChartFile`, `findByInfoName`, `DetectedPack`, `detectPack` | MATCHES (relocated) |
| `ZipPackUtilsTest` semantics (info.yml names chart/music/illustration inside a subdirectory) | required by the test | satisfied by `findByInfoName` + `detectPack` fallbacks (`ChartLibrary.cpp:181-216`) | **PARTIAL** — the port's candidate rules are name-heuristic (`music`/`song` substring, audio extension) plus info-name lookup; the Java test's exact triangle (info.yml → chart.json/song.ogg/bg.png) is not directly asserted by any C++ test — **UNVERIFIED** without running both on that fixture |
| Info file readability | 4 formats (`info.yml`, `info.yaml`, `info.json`, `info.txt`?) via `readInfoFile` | `InfoFile.cpp` parses yml/json/csv-ish key/value | MATCHES at feature level |
| `InfoFileUtilsTest` (space normalisation) | U+2002, run-collapsing | ASCII space, no collapsing | **DIFFERS** (§1 #10) |
| Online catalogue (`LanzouClient`, `OnlineIndexClient`) | 767 lines of client code | absent | **MISSING (deliberate)** — `MIGRATION.md:161` |
| `FileUtils` | copy/move/delete helpers | `FileSystem.cpp` | MATCHES |

---

## 3. Runtime model

### 3.1 `Chart` / runtime preparation

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Fields | `offset`, `formatVersion`, `rpeVersion=160`, `bpmTimeline`, `packDir`, `normalizedMd5`, `prprEffects/Videos`, `attachUiLineIndex[8]={-1…-8}`, `useAttachUiFix=true`, `uiTransforms`, `prprShaderSources`, `blockAreaList`, `allNotesSorted`, `clickEffectsSorted`, `chartEndTimeSec` | all present; `normalizedMd5→normalizedDigest`, plus port-only `songName`, `levelEffects` | MATCHES (+2 port-only fields) |
| Per-note runtime | `isHold`, `clicked=false`, `floorPosition`, `sect`, `secht`, `holdEndTime`, `useOfficialSpeed=official`, hold tail geometry (official: `holdTime*speed*PGRH`; other: integrated `floorPosition`), `morebets` from a 1e-6-quantised `sect` histogram | identical formulas and identical quantisation (`llround(sect*1e6)`) | MATCHES |
| `morebets` | computed over **all** notes (fakes included) | same (`allNotes` includes fakes) | MATCHES |
| `allNotesSorted` | fakes excluded, sorted by `sect`, `sortedIndex` assigned | same | MATCHES |
| `clickEffectsSorted` | one `ClickEffectItem` per non-fake note, RNG-seeded, sorted by `timeSec` | same, `std::mt19937(random_device)` | MATCHES (different RNG stream, same use) |
| `nearNotes` | same-type (flick vs tap/hold) notes within 0.01 s, fakes excluded | same, same `<=0.01` boundary | MATCHES |
| `chartEndTimeSec` | max over note end (`holdEndTime` for holds) | same | MATCHES |
| Parallel init | `cores>=2 && lines>=2 && notes>=800` → thread pool; falls back to sequential on task failure | none | **MISSING (perf)** |
| `resetRuntime` | Java has no equivalent (renderer resets fields ad hoc); port clears 24 note fields + cursors | — | port-only, benign |

### 3.2 `JudgeLine` evaluation

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| `sec2beat`/`beat2sec` | `sec / (PGRBEAT/bpm)` | same, but **guards `bpm` with `max(1e-9,bpm)`** while Java lets `bpm=0` produce ±Inf | **DIFFERS (edge)** — a chart with `bpm<=0` yields `Inf`/`0` in Java and huge finite values here |
| RPE layers | rotation/alpha summed; move uses X/Y lists if either exists, else `MoveEvents` | summed; per-axis fallback into `MoveEvents` | **DIFFERS** (§1 #8) |
| Non-layer defaults | x,y non-finite ⇒ 0.5; `invertRotation` applied after sampling | same fallbacks, inversion applied to the sum | MATCHES |
| Scale | `scaleXEvents` if either X or Y present, else `judgeLineScaleEvents` (MoveEvent), fallback 1 | same, but `judgeLineScaleEvents` is never populated by any parser | **PARTIAL** (§2.2 `scaleEvents`) |
| Incline | `sin(angle)` only when the sampled angle is finite | `sin` unconditionally (no finite check) | **DIFFERS (tiny)** — a non-finite incline event yields NaN in Java (`inclineSinr=NaN`) and NaN in C++ as well; equal in practice, but the guard is gone |
| Colour default | `PCOLOR` unless texture **or text** **or text events** **or attachUI** ⇒ white | same minus **text events** | **DIFFERS** (§1 #7) |
| `hasColorEvent` | set only when the event list is non-empty | same | MATCHES |
| Text/GIF tracks | fallback to static `text`, NaN gif position | same | MATCHES |
| Father transform | `nx = cx*cos + (cy*sin)/aspect + fx`, `ny = cy*cos - (cx*sin)*aspect + fy`, aspect fallback 16/9, `rotateWithFather` adds | identical | MATCHES |
| Hide flags | `alpha<0 && floor(-alpha)==1` ⇒ hide line+notes | identical (`RpeLineScaleSemanticsTest`/`JudgeLineAlphaExtensionTest` semantics preserved) | MATCHES |
| State cache | per-line memo keyed on `lastStateTimeSec` | same | MATCHES |
| `calcNoteControl` | binary search, `dy<=1e-6 ⇒ b.value`, easing clamped 0..28, non-finite eased ⇒ `p` | `lower_bound`, same clamp and epsilon, **no non-finite fallback** | **DIFFERS (tiny)** |
| `initSpeedEventsPhi` | `bpm<=0 ⇒ bpm=120` (mutates the line) | `max(1e-9,bpm)` | **DIFFERS (edge)** |

### 3.3 `EventUtils` / event structs

`EventUtils.java` (1,071 lines) and `EventUtils.cpp` (961) are structurally the same file: same
`containsEventTime` rule (last event inclusive, others exclusive), same "later event already
started ⇒ hold previous end" walk, same NaN-vs-0 defaults, same integral speed model.

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| `getEventVal` (no events) | 0.0 | `lineValue(...,0)` | MATCHES |
| `getMoveEventVal1/2` (no events) | `NaN` | fallback supplied by caller | MATCHES |
| Cursor variants | monotonic clamp with restart when rewound | same (`activeEvent`) | MATCHES |
| Colour value | start/end per channel, eased progress, returns event presence | same (`colorValue`) | MATCHES |
| Text value | holds `start` until `endTime`, then `endText` if non-empty | same | MATCHES |
| GIF value | eases between start/end, NaN before start | same | MATCHES |
| `speedAt` | `easingType==0 ⇒ v0`, `<=1 ⇒ lerp`, else raw-RPE easing | identical three branches | MATCHES |
| `initSpeedEvents` | 5 branches (dt≤1e-9; type 0 or equal values; type≤1 with tween 6/7 re-anchoring; else integral); `nextStart` = next **non-null** event start (else own end); `fp` chained through `computeFpForCumulative` | identical branches, identical tween 6/7 rewriting; `nextStart` = `events[i+1].startTime` when finite (container has no nulls) | MATCHES (null-skip nuance unreachable) |
| `getFloorPosition` | binary search + three extrapolation regimes (`t<start`, `t<=end`, `t>end`) | identical, including the `totalHeight*(ed-st)` end extrapolation | MATCHES |
| Layer sum | `getFloorPosition(t, line)` sums non-empty layers, else the single list | identical | MATCHES |
| `floorPositionMonotonic` | present, used by `Chart` for notes | present (`EventUtils.cpp:924+`) but **not used** by `prepareRuntime` | **PARTIAL (perf)** |

Event structs (`Event.hpp`) carry `LineEvent`/`MoveEvent`/`SpeedEvent`/`ColorEvent`/`TextEvent`/`GifEvent`
with the same field sets as the six Java classes, plus a shared `EasingSpec` (`type`, `left`, `right`,
`bezier`, `bezierPoints`, `bezierLut`). `EventCursor` is a 1-field index on both sides. No gap.

### 3.4 `Easing` / `OfficialEase`

* `Easing.applyTween` / `applyTweenInt`: all 33 ids present on both sides with identical formulas
  and identical constants (`C1=1.70158`, `N1=7.5625`, `D1=2.75`, elastic `C4=2π/3`, expo `ln2`
  normalisation). Java's `bezierSampleY` is textual but numerically identical to `bezierSampleX`;
  the port reuses the X form — same polynomial, no numeric difference.
* `RPE_TWEEN_MAP` is byte-identical (30 entries: `2,2,4,3,7,6,5,8,10,9,13,12,11,14,16,15,19,18,22,21,25,24,23,26,28,27,31,30,32,29`).
* `clampedTween` / `intClampedTween`: same epsilon set (`1e-5`, `1e-8`, `1e-9`, `1e-12`), same
  degenerate returns (`p` vs `p²/2`).
* Cubic bezier: same 21-sample table, same Newton (4 iters, SLOPE_EPS 1e-7, NEWTON_MIN_STEP 1e-3),
  same 10-iteration bisection fallback, same 128-sample LUT. MATCHES.
* `OfficialEase`: `TWEEN_BY_OFFICIAL[16]` identical; `toRpeEasing` identical 12-case table with
  `default: 0`; `evaluate` handles Zero/One steps and AnimationCurve linear fallback on both sides.

No numeric easing divergence was found. This is the port's strongest area.

### 3.5 BPM timeline

Both keep `(beat, bpm, startSeconds)` segment lists, convert with `PGRBEAT/bpm` per segment, and
extrapolate the first segment backwards to beat 0. `BpmTimeline.java:86` vs `BpmTimeline.cpp:32`
plus the private converters in each parser (`RpeChartParser.cpp:55-85`, `PecChartParser.cpp:55-85`,
`ChartLoader.cpp:106-167`). MATCHES.

### 3.6 `OfficialLevelEffects` / `OfficialBlock` / `MessageParticleField`

* `MessageParticleField`: every constant matches exactly — `MAX_DOTS=512`, `UNITS=10`,
  `EMIT_RATE=10`, `LIFE=10`, `FULL_SIZE=0.5/UNITS`, `HIDE_SMOOTH=0.1`, box `12.5 × 4.5`, near
  edge `3.75`, speed `0.05..0.10`, `FORCE_ACCEL=0.05`, `FIELD_ACCEL=0.5`, `FIELD_DRAG=0.8`,
  field `7.5 × 1.5/band`, size slopes `3.6587481` / `0.23306048`
  (`MessageParticleField.java:66-112` ↔ `.hpp:19-27`, `.cpp:14-30`). MATCHES.
* `OfficialBlock`: the port keeps the parsed geometry + per-frame `update` + `tryGetBlockingBlock`
  latch semantics that Java has (`OfficialBlock.java:433` ↔ `.cpp:277`), and `GameplaySession`
  mirrors Java's "blocked finger produces no judge events" rule
  (`GameRenderer.java:10029-10034` ↔ `GameplaySession.cpp:205-231`). The **screen-space
  composite** is not ported — documented at `MIGRATION.md:78-81,164`. **MISSING (declared)**.
  Constant check: `MIGRATION.md:144` cites the `0.09`/`0.12` overlap gates; the composite pass
  exists (`src/render/fx/BlockCompositePass.cpp`) but is not driven from `drawScene`, so the
  "torn" look inside a red zone is absent. **MISSING**.
* `OfficialLevelEffects`: the port has the track evaluator, the per-song pass lists and
  `buildPrprEffects` (`OfficialLevelEffects.cpp:326`), and `LevelEffectTests.cpp` asserts shapes;
  Java is 733 lines including every effect id and its parameter table. A field-by-field diff of
  the effect table was **not** completed in this pass — **UNVERIFIED**. Settle by diffing the
  effect-id → pass-list table in `OfficialLevelEffects.java` against the shipped
  `assets/official_effects/*.json` the port loads: if the port derives passes from the JSON and
  Java hard-codes them, any key present in Java but absent in the JSON is a silent gap.

### 3.7 `PrprEffect` / `PrprVideo` / shader chain

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Effect record | `startSec`, `endSec`, `global`, `shader`, `vars` (ConstFloat / ConstVec / FloatEvents) | `startTime`, `endTime`, `global`, `shader`, `variables{values, events}` | MATCHES |
| Variable parsing | number, number array, or `{values, events}` with keyframe pairing | identical three forms, same pairing rule (next key closes the span) | MATCHES |
| Video record | `path`, start/end, **`scale` (SCALE_CROP_CENTER/INSIDE/FIT)**, **`dim` animation**, **`attach` line**, **`alpha` animation** | `path`, `startTime`, `endTime`, `alpha` (constant), `loop` | **MISSING** — `PrprVideo.java:1-82` vs `PrprEffect.hpp:31-38` |
| Video playback | platform video pipeline (external texture), seek throttle 350 ms / drift 260 ms | none | **MISSING (declared)** `MIGRATION.md:160` |
| Legacy GLSL upgrade | `upgradeLegacyFragmentShader` (#version 310 es, `texture2D`→`texture`, `gl_FragColor`→named out, collision-safe out name) + `PrprShaderCompatibilityTest` | **UNVERIFIED** — `PrprChain.cpp:443` is the shader chain; no test in `tests/` is named for the upgrade. Check whether `PrprChain` performs the same three rewrites and the alternate-out collision rule. |

---

## 4. Judgement & scoring

### 4.1 Windows and classification — all numeric constants identical

| Constant | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Perfect / Good / Bad | `0.080 / 0.180 / 0.220` (`GameRenderer.java:1211-1213`) | same (`JudgementEngine.cpp:11-13`) | MATCHES |
| Challenge P/G/B | `0.040 / 0.090 / 0.140` (`:1214-1216`) | same (`:14-16`) | MATCHES |
| Challenge widening | `WINC_* + challengeTimeSum`, `challengeTimeSum = avg(10 frame deltas) * 0.5` (`:9976-9990`, `TIME_SUM_FRAMES=10`) | identical formula, `std::array<double,10>` (`GameplaySession.cpp:233-242`) | MATCHES (guard nuance: Java skips a sample when the wall-clock delta is non-positive/absurd; the port clamps negatives to 0 and always pushes — **UNVERIFIED** impact) |
| Drag late-miss | `0.100` (`:1223`) | `0.100` (`JudgementEngine.cpp:17`) | MATCHES |
| Flick late-miss | `perfect * 1.75` (`:2818-2826`) | same (`:304`) | MATCHES |
| Hold/Tap late-miss | `goodWindow` (`:2827-2832`) | same (`:305-306`) | MATCHES |
| Classification order | perfect → (drag/flick⇒Miss) → good → (hold⇒Miss) → bad ⇒ else Miss (`:2840-2851`) | identical (`JudgementEngine.cpp:126-131`) | MATCHES |
| Spatial window | `bad - (touchPos-0.9)*perfect*0.5`, 0 beyond 1.9 (`:2754-2770`) | identical (`JudgementEngine.cpp:270-275`) | MATCHES |
| Candidate replacement (tap/hold tie 0.01 s, drag/flick always replace) | `:2772-2795` | identical (`:277-289`) | MATCHES |
| Flick candidate replacement | `:2797-2807` | identical (`:291-297`) | MATCHES |
| Judge-position scale | `screenH*0.1`, fallbacks `stageH*0.1`, `PGRW*stageW`, then 1 (`:2537-2541`) | identical (`JudgementEngine.cpp:314-320`) | MATCHES |
| Flick gesture speed | `0.06 * dpi/380` (`:2723-2726`) | same (`:18-19,322-325`) | MATCHES |
| Flick tracker | 2-frame displacement, `0.01` squared-length gate, `5×` new-flick multiplier, `60` time scale, settle-on-pause | identical (`JudgementEngine.cpp:30-78`), and `OfficialFlickGestureTest`'s three cases hold in the port's arithmetic (hand-checked: 0.16 then 0.32 at 1/60 s ⇒ `newFlick` on the second frame; alternating 0.16/0 never accumulates; settling clears) | MATCHES |
| Judgement-detail bins | 19 bins, range 180/90 ms, width `max(1, 2*range/(19-1))` = 20/10 ms, index `round((displayMs+range)/width)`, clamp, `displayMs = -diffSec*1000` (`:2985-3017`) | identical (`JudgementEngine.cpp:185-207,327-334`) | MATCHES |

### 4.2 Judging pipeline (session) — faithful where it exists, incomplete at the edges

The port's `GameplaySession` is a 1:1 transcription of Java's phases 1–8 for the common cases.
Confirmed matches: scan window `[-good, +bad]`, spatial limit 1.9, drag prime radius 2.1 and
`|Δ|≤0.1` gate, flick prime radius 2.1, hold body radius 1.9 with `safeFrame` starting at 2 and
tripping at `<0`, hold-tail settle at `holdEndTime - 0.220`, miss detection skipping fakes and
breaking on an active hold, and `0.25` as the diff recorded on misses.

Divergences found:

1. **Autoplay diff convention** — the port commits autoplay non-hold notes with
   `diff = time - note.sect` (`GameplaySession.cpp:266`) while holds get `diff = 0`
   (`:264`). Java's autoplay path was not read in this pass, so whether it uses 0 is
   **UNVERIFIED**. It matters because `judgeDiffSec` feeds replay recording and any offset
   statistics; settle by reading Java's autoplay branch and comparing `commitJudgement(n, JR_PERFECT, x)`.
2. **No hold-body burst scheduling** (`spawnDueHoldBodyHitEffects` absent) — §1 #9.
3. **Replay `HOLD_PRESS` recording is absent** — the port records touch samples instead, so the
   Java invariant "record HOLD_PRESS immediately when the head activates so short holds are not
   missed" (`GameRenderer.java:10368-10380`) has no analogue.
4. **Replay-playback effect time** — Java rewrites the judgement timestamp as
   `n.sect - diffSec` during playback (`:2962-2965`, used at `:2857`); the port passes the frame
   time straight into `commit` (`JudgementEngine.cpp:139`). Visible only in replay playback
   visuals (hit-effect timing) and in `judgeTimeSec`.
5. **`finished()`** — port: all notes judged **and** `time >= chartEndTimeSec`
   (`GameplaySession.cpp:555-560`). Java's end-of-run condition lives in the activity/renderer;
   the port's extra `time >= chartEndTimeSec` clause may delay or (if a note ends after
   `chartEndTimeSec` due to fakes) alter completion. **UNVERIFIED** — needs a side-by-side of
   Java's "run complete" predicate.

### 4.3 Practice window overrides — implemented, never consulted

This is the single most important behavioural defect. Full evidence chain:

* Override object exists and is a faithful port of Java's clamp rules:
  `PracticeJudgeWindows.hpp:17-22` (20/80 ms perfect, 25/180 ms good, 5 ms gap),
  `.cpp:14-32` (same clamp order, same "dirty" flag, `reset()` → 80/180/`dirty=false`),
  matching `GameRenderer.java:998-1008,2699-2709`.
* The UI mutates it: `GameScreen.cpp:317-336` (two sliders + reset), and the session is given the
  pointer and the practice flag at `:404-406`.
* The **only** reader is `JudgementEngine::calculate()` (`JudgementEngine.cpp:120-125`) via
  `practiceOverrideActive()`. Across `src/`, `include/` and `tests/` the symbol appears exactly
  four times: the declaration (`JudgementEngine.hpp:59`), an inline 2-argument overload that
  delegates to it (`JudgementEngine.hpp:60-62`), and the definition (`JudgementEngine.cpp:114`).
  **No translation unit invokes either overload**, so the override is unreachable.
* Every judging site uses `judgement_.windows()` (chart windows) instead:
  `GameplaySession.cpp:285,332,368,439,540`, and the settlement bands
  `:489,496,503,513` — compare `GameRenderer.java:10355-10356`, which uses
  `tapPerfectWindowSec()`/`tapGoodWindowSec()` exactly there.
* The spatial reduction also uses the chart perfect window (`GameplaySession.cpp:313`) where Java
  passes `tapPerfectWindowSec()` for taps/holds (`GameRenderer.java:2756`).
* `judgeWindowSec(HOLD)` (`JudgementEngine.cpp:107-108`) returns the chart good window where Java
  returns `tapGoodWindowSec()` (`GameRenderer.java:2739-2740`).

Player impact: in practice mode, neither slider changes any judgement, and `MIGRATION.md:45` is
factually wrong. Fix = call `JudgementEngine::calculate()` (or the two window accessors) at the
five settlement/spatial sites, and make `judgeWindowSec` take the note so it can pick the
practice good window for holds.

### 4.4 Combo / score / accuracy / grade — formulas identical

| Item | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Score | `challenge ? acc*1e6 : acc*9e5 + maxCombo*1e5/n`, clamped `[0,1e6]`, `Math.round` | identical, `floor(v+0.5)` | MATCHES (`GameRenderer.java:427-438` ↔ `JudgementEngine.cpp:230-240`) |
| Accuracy | `(perfect + 0.65*good)/n * 100`, clamped | identical | MATCHES (`:468-476` ↔ `:242-245`) |
| Realtime accuracy | judged denominator, `1.0` when none | identical | MATCHES (`:478-486` ↔ `:247-251`) |
| Combo / AP·FC flags | perfect/good keep combo; bad/miss reset it; AP broken by good; FC broken by bad/miss | identical | MATCHES (`:2871-2895` ↔ `:148-167`) |
| goodEarly/goodLate | `diffSec > 0` ⇒ early | identical sign convention | MATCHES |
| Std-dev | Welford on hit diffs (`timingM2/timingCount`) | same recurrence | MATCHES (`:3020-3023` ↔ `:170-176,225-228`) |
| Grade index | `1e6→7`, `fullCombo→6`, `<700k→0`, `<820k→1`, `<880k→2`, `<920k→3`, `<960k→4`, else `5` | identical thresholds | MATCHES (`ResultActivity.java:83-92` ↔ `ChartLibrary.cpp gradeIndexForScore`) |
| Library grade name | `>=1e6 AP`, `>=960k V`, `>=920k S`, `>=880k A`, `>=820k B`, `>=700k C`, else `F` (no FC tier) | same via `gradeIndexForScore(score,false)` and `{"F","C","B","A","S","V","FC","AP"}` | MATCHES (nuance: the result screen uses `score == 1e6` in Java vs `>=1e6` in the port; unreachable difference because the score is clamped) |
| `PlayResult.isFullCombo/isAllPerfect` | `bad==0&&miss==0&&n>0` / `good==0&&…` (`PlayResult.java:73-79`) | identical predicates (`PlayResult.hpp:20-21`) | MATCHES |

### 4.5 Java unit tests vs port coverage

| Java test | Semantics asserted | Port coverage |
| --- | --- | --- |
| `OfficialCheckNoteSelectionTest` | 12 cases: candidate-replacement tie rules, per-type late-miss windows, `dragLocalJudged`, judge-position scale incl. letterbox case | `JudgementEngine.cpp:277-320` reproduces all formulas. `CoreTests.cpp` asserts some windows; **no port test reproduces the 12-case matrix** (especially `officialJudgePositionScalePx(900,675,1200)==90`, letterbox-independent). **PARTIAL** |
| `HitOffsetIndicatorTest` | angle mapping, judgement-by-offset, challenge angle scaling, `shouldShowCommitMark` (hold/head/miss) | `HitOffsetIndicator.cpp:33-38` identical; `CoreTests.cpp` covers angle/format. **MATCHES** |
| `OfficialFlickGestureTest` | 3 tracker cases | arithmetic identical; **no dedicated port test** — **PARTIAL** (covered indirectly by app-loop autoplay/replay checks) |
| `JudgeLineAlphaExtensionTest` | `alpha=-1` hides both, `-2` does not, `-200` keeps integer part | `JudgeLine.cpp:100` identical; `CoreTests`/`TransitionTests` reference alpha. **MATCHES** |
| `HoldNegativeSpeedGeometryTest` | `applyYControlScale(holdLength<0)` must stay signed (no squaring) | `NoteGeometry.cpp:39-41` and `NoteBatch.cpp` multiply linearly. **MATCHES** |
| `RpeParentTransformTest` | father rotation uses stage aspect (`dy/dx == tan(rot)`) | `JudgeLine.cpp:99` identical formula. **MATCHES** |
| `RpeJudgeAreaTest` | `judge_area` alias, `judgeAreaControl`, `judge_area_control`, interpolation 1→2→3 / 1→2.5→4 | keys and `calcNoteControl` identical (`RpeChartParser.cpp:806-808`, `JudgeLine.cpp:103-107`); `RpeLineScaleSemanticsTest`-style assertions exist in `ChartParserTests.cpp`. **MATCHES** |
| `RpeLineScaleSemanticsTest` | `scaleXEvents` values survive parse verbatim (0.04/0.045) | `RpeChartParser.cpp:855` uses the same `parseLineEvents` without rescaling. **MATCHES** |
| `RpeAttachUiDebugTest` | `attachUI=7` line parsed; `moveY==0` at beat 86×32 | `attachUiId` maps `"level"→7` (`RpeChartParser.cpp:778-785`) and the attach index is filled (`:933-937`). **MATCHES** at parse level; the *consumer* (`useAttachUiFix`, uiTransforms) is incomplete (§1 #4) |
| `NativeRenderBucketRuntimeResetTest` | bucket reset clears `lineLoopStart`, `judgeResult=-1`, `clicked=false` | no `NoteBucket` type in the port; `NativeRenderCommands.java` (1,245 lines) is replaced by `NoteBatch`/`SpriteBatch` with no equivalent "reset runtime state" contract. **MISSING** (structural) — see §8 |
| `PrprShaderCompatibilityTest` | 2 GLSL-upgrade cases | **UNVERIFIED** (see §3.7) |
| `InfoFileUtilsTest` | en-space normalisation | **DIFFERS** (§1 #10) |
| `ZipPackUtilsTest` | zip extract + info-driven detection | detection relocated to `ChartLibrary.cpp`; exact-case parity **UNVERIFIED** |

---

## 5. Audio

Port uses miniaudio (device + `ma_decoder`) with a hand-written mixer; Java uses Oboe plus its own
decoder (`audio_decoder.cpp`, minimp3 + stb_vorbis) and its own mixer (`audio_engine.cpp`).

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Speed clamp | `[0.05, 4.0]` (`audio_engine.cpp:345-349`) | `kMinPlaybackSpeed 0.05`, `kMaxPlaybackSpeed 4.0` (`Mixer.hpp:18-19`), clamped per block (`AudioEngine.cpp:364`) | MATCHES |
| Gain clamp | `[0, 5]` for music and SFX (`:352-356,368-372`) | `kMaxGain 5.0` (`Mixer.hpp:20`), `clampGain` | MATCHES |
| Low-pass | RBJ biquad, Q=1/√2, open 22000 Hz, blocked 1500 Hz, ~0.05 s ramp (`:570-626,928-949`) | `kOpenCutoffHz 22000`, `kClosedCutoffHz 1500`, `kQ 0.7071…`, `kRampSeconds 0.05` (`Mixer.hpp:145-147,216-217`), same ramp + restart-on-target-change (`AudioEngine.cpp:375-406`) | MATCHES |
| Playhead smoothing | interpolates from the last callback anchor with the callback speed, clamps to one block (`:502-556`) | `mixer::estimatePlayheadFrames` with callback anchor + elapsed (`AudioEngine.cpp:461-477`) | MATCHES in design; formula parity **UNVERIFIED** line-by-line |
| Seam behaviour | — | port adds integer-step fast path when `speed` is integral and position integral (`AudioEngine.cpp:369-373`) | **port-only optimisation**; must be bit-exact or resampling differs at 1×/2× — **UNVERIFIED** |
| SFX voices | `mVoices` list, per-trigger voice, hold reuses tap (`NativeAudioEngine.java:41-51`) | fixed-capacity pool, `voiceCapacity()` (`AudioEngine.hpp:82`), hold→tap documented | MATCHES at semantics level; capacity value **UNVERIFIED** vs Java's unbounded list |
| SFX default assets | `prepareDefaultSfxIfMissing()` (native synth fallback) | `loadSoundEffects` synthesises tones when assets are missing (`AudioEngine.hpp:34-36`) | MATCHES |
| PCM injection API | `setMusicData(float[], len, sr, ch)`, `setSfxData(...)` — used by calibration to feed decoded asset PCM | **no equivalent**; the port loads files by path | **DIFFERS (API shape)** — calibration therefore re-loads `audio/calibration.wav` through the file decoder (`CalibrationScreen.cpp:70-78`) instead of feeding PCM. Functionally equivalent for shipped WAVs. |
| Formats | WAV, MP3, OGG | miniaudio: WAV, MP3, FLAC, OGG only when built with vorbis (`Decoder.cpp:112-117`) | **port superset** (FLAC) — over-implementation |
| `MusicLooping`, `restartPaused`, `seek` while paused | present | present (`AudioEngine.hpp:38-52`) | MATCHES |
| `musicSpeed` interaction with SFX pitch | Java: SFX voices are independent of music speed | port: `speed` scales music stepping; SFX mixing must not inherit it — verify `AudioEngine.cpp:398-418` uses `speed` only for music | **UNVERIFIED** (non-blocking) |

---

## 6. Persistence, settings, replay storage

### 6.1 Settings keys, defaults, clamps

| Setting | Java default / clamp | C++ default / clamp | Verdict |
| --- | --- | --- | --- |
| Aspect ratio | `16f/9f`, clamp `0.5..5.0`, fallback 16/9 (`ChartSelectActivity.java:211,2678-2681`) | `0.0F` (= follow screen), **no clamp on load** (`Settings.hpp:15`, `Settings.cpp:77`) | **DIFFERS** — different default play-field shape on non-16:9 windows, and a corrupt value is not repaired |
| `keyScale` | 1.0, `0.7..1.5` | same default, same clamp (`Settings.cpp:106`) | MATCHES |
| `scrollSpeed` | 1.0, `0.1..5.0` | same | MATCHES |
| `backgroundDim` | 0.25, `0..1` | same | MATCHES |
| `judgeLineWidth` | 1.0, `0.5..2.0` | same | MATCHES |
| `musicSpeed` | 1.0, `0.5..2.0` | same | MATCHES |
| Volumes | percent ints `0..500` | floats `0..5`, written with 6 significant digits | MATCHES numerically |
| `audioOffsetMs` | int, `-500..500` | same | MATCHES |
| `hitOffsetIndicatorMode` | `sanitizeHitOffsetIndicatorMode(...)` | `clamp 0..2` | **UNVERIFIED** (Java's sanitiser range not read) |
| Keys / file format | `SharedPreferences` XML, keys `key_scale`, `scroll_speed`, … | INI-ish `key=value`, keys `keyScale`, `scrollSpeed`, … | **DIFFERS (by design)** — settings are not portable between the two binaries; note in migration docs |
| Missing keys | preserve defaults | preserve defaults (`number(key, field)`) | MATCHES |

### 6.2 Chart library storage

`ChartLibraryManager.java` (704) ↔ `ChartLibrary.cpp` (610). Port covers: record discovery, info
read, best-score store, `recordBestResult`, `recordPlayTime`, sort, import (zip/directory),
`requireComplete` gate, ids. Not diffed line-by-line in this pass; the observable contract in the
headers (`ChartLibrary.hpp:16-70`) matches Java's manager responsibilities. **PARTIAL (audited at
API level only)** — a field-level diff of the score-file schema is still owed.

### 6.3 Replay storage

Covered in §1 #2/#12. Additional concrete differences:

| Aspect | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Version field | `ReplayData.VERSION = 1`, entry type constants 1..5 | none (`Replay.hpp`) | **MISSING** |
| Storage layout | per-replay directory + meta (`ReplayManager.java`) | `ReplayStore` root + `save/list/find/remove` writing JSON per replay (`Replay.hpp:60-90`) | **DIFFERS (compatible in spirit)** |
| Meta on save | `fromPlayResult` copies score/acc/combo/counts/early/late/stddev/bins/timestamp | `applyResultToReplayMeta` copies score/acc/combo/counts only | **MISSING** (§1 #12) |
| Modes recorded | mirrorX, challenge, autoplay, practice, musicSpeed, scrollSpeed | same six plus `chartId`/offsets | MATCHES |
| Modes on playback | recorded modes are display-only; playback uses the viewer's own settings (`ReplayData.java:41-42`) | recorded mirror/challenge/speeds/offsets are forced onto playback (`CommonScreens.cpp:127-132,283-290`) | **DIFFERS** (§7) |
| Playback | replays judgement entries | replays touch samples, feeding the same session | **DIFFERS** (§1 #2) |

---

## 7. Interaction semantics

| Behaviour | Java | C++ | Verdict |
| --- | --- | --- | --- |
| Pause trigger | HUD pause button; hit rect built from `OFFICIAL_HUD_PAUSE_X_OFFSET 50.5887985`, `Y 444.2999878`, `W 34.262`, `H 37.966`, aspect `35/41` (`GameRenderer.java:764-769`, `1030-1038`) | `isPauseButtonHit` uses the **same** 50.5887985/444.2999878 constants scaled by `stage.height/1000` (`GameScreen.cpp:62-69`) | MATCHES (constants identical; the W/H terms and the ring fade constants were not compared — **UNVERIFIED** for the exact rect) |
| Back key | `OnBackPressedCallback`: if the pause menu is not visible, request it; otherwise `finish()` (`PlayActivity.java:258-269`) | `SDLK_ESCAPE` opens the pause menu; pressing again resumes (`Application.cpp:494-513`), key handled at `:851` | MATCHES in intent; Android hardware back mapping **UNVERIFIED** on device |
| Resume countdown | pause → countdown → play, `PAUSE_TOUCH_FADE_IN_SEC 0.25`, `VISIBLE 1.0` (`GameRenderer.java:1030-1031`) | `GameRuntime::Pause::Countdown` with `pauseElapsedSec` (`GameScreen.cpp:486-488`) | **PARTIAL** — countdown duration constant not compared (**UNVERIFIED**) |
| Intro | `INTRO_HOLD_SEC = 10/60`, `INTRO_BUFFER_MS = INTRO_AUDIO_DELAY_MS`, audio start epsilon 0.001 (`GameRenderer.java:564-568,628`) | `kIntroAudioStartSec 0.35`, `kIntroEndSec 0.45` (`GameScreen.cpp:25-26`) | **UNVERIFIED** — `INTRO_AUDIO_DELAY_MS` was not read; if it is not 350 ms the audio start differs |
| Outro | `OUTRO_DUR_SEC = 1.0 + 0.7 = 1.7`, `OUTRO_WAIT_SEC = 0`, line collapse at 1.0 s over 0.7 s, bg transmission 0.6, combo fade 35/60 (`GameRenderer.java:568-578`) | `kOutroDurationSec 1.7` with a single linear fade (`GameScreen.cpp:27,542,620`) | **PARTIAL** — total duration matches; the staged collapse/fade curves are collapsed into one fade |
| Mirror X | renderer mirrors stage-space drawing and flips UVs (`GameRenderer.java:2522,3497,5523,6485,12711`); `mirrorX` is also recorded in replay meta | picture mirrored via `render::Mat4::mirrorX` (`Mat4.hpp:61`, `GameRenderer.hpp:30-31`); touch X mirrored by `mirrorTouchX` **before** the sample is recorded so a replay reproduces the same play (`GameScreen.cpp:695-728`) | MATCHES — note the port docs mirror input rather than the picture (`render/GameRenderer.cpp:664`), which is equivalent only if the two transforms use the same centre; `mirrorTouchX` divides by the window width (`:698-703`) while the renderer mirrors inside the letterboxed stage (`Mat4::mirrorX(centerX)`), so a letterboxed window is the case to check — **UNVERIFIED (fidelity)** |
| Replay playback modes | explicitly documented: the recorded modes are **display-only**; "replay playback itself keeps using the viewer's own settings" (`ReplayData.java:41-42`) | playback **forces the recorded** `mirrorX`, `challenge`, `musicSpeed`, `scrollSpeed`, `chartOffsetMs`, `audioOffsetMs` onto the run (`CommonScreens.cpp:127-132` and `:283-290`), and clears autoplay/practice (`:125-126`) | **DIFFERS** — a replay recorded in challenge mode is judged with challenge windows on playback in the port, and with the viewer's windows in Java. For the port's touch-synthesis model this is arguably required for reproduction, but it is a stated behavioural divergence and should be documented as such |
| Challenge mode | shrinks windows and drops the combo term (§4.1) | same | MATCHES |
| Autoplay | perfect hits, no offset statistics (`autoplay && !replayPlayback` guard, `:2969,2987`) | `setAutoplay` + `recordOffset` guard identically (`JudgementEngine.cpp:188,212`) | MATCHES (diff value caveat §4.2) |
| Practice mode | seek bar, per-window overrides, speed slider | seek (`GameScreen.cpp:102-119`, rebuilds judging state and audio seek), speed slider, window sliders (dead — §4.3) | **PARTIAL** |
| Calibration | loop 2.0 s, note hit at 1.0 s, spawn 1.5 s, fall 1.5 s, offset clamp ±500, reset to 0, ±1 ms buttons, slider (`CalibrationActivity.java:338-344,79,229-245`) | identical constants and clamp (`CalibrationScreen.cpp:17-20,50`) | MATCHES |
| Result → replay saving | `buildPlayResult`, `buildReplayPlaybackResult` prefer saved bins when live bins are empty (`GameRenderer.java:3019-3060`) | no equivalent merge; result from live session only | **MISSING** (§1 #12) |

---

## 8. Java-only and C++-only inventory

**No C++ counterpart (feature absent):**

| Java | Lines | Status |
| --- | --- | --- |
| `util/LanzouClient.java` | 552 | deliberate (`MIGRATION.md:161`) |
| `util/OnlineIndexClient.java` | 215 | deliberate |
| `game/PrprVideo.java` | 82 | struct-only shell (`PrprEffect.hpp:31-38`) — playback and 4 of 7 fields absent |
| `game/ReplayData.java` | 74 | replaced by a different model (§1 #2) |
| `game/SkinConfig.java`, `game/ResPackInfo.java` | 25 + 11 | **no equivalent type** — skin/respack data is read ad hoc (`TextureCache.cpp`) |
| `game/NativeRenderCommands.java` | 1,245 | replaced by `render/batch/*` + `render/scene/NoteBatch.cpp`; the `NoteBucket` reset contract has no equivalent |
| `ui/*` widget classes (`SkewFrameLayout`, `SettingsSliderView`, `SettingsToggleView`, `SettingsPreviewView`, `UiTypography`, `TriGridBackgroundView`, `JudgementHistogramView`, `GameTextView`, `GameTextRenderer`, `GameTextOverlay`, `AssetBitmaps`, `CoverImageLoader`) | 2,132 | deliberate (immediate-mode UI); behaviours re-implemented in `UiStyle`, `UiWidgets`, `SettingsScreen`, `ResultScreen`, `TriGridBackdrop`, `TextRenderer`, `CoverCache` |
| `ui/Select*Fragment`, `LocalChartAdapter`, `OnlineChartAdapter`, `SelectPagerAdapter` | 1,942 | `LibraryScreen` covers the local paths; the online paths are absent by design |
| `ui/GameDialog.java` | 688 | dialogs re-implemented in `ScreenCommon`/`SettingsScreen` (not diffed) |
| `util/ZipPackUtils.java` | 856 | relocation into `Archive.cpp` + `ChartLibrary.cpp` (no `DetectedPack` in `Archive`) |

**C++-only (no Java source counterpart) — over-implementation to review:**

| C++ | Why it matters |
| --- | --- |
| `chart/Md5.cpp` | Java uses `java.security.MessageDigest`; fine, but confirms the digest must match byte-for-byte to select level effects |
| `core/NoteGeometry.cpp` (+ `SpeedBasis`) | a *third* geometry implementation; Java has one renderer path and two helpers. Only `Control` basis is ever used; `Render`/`Judge` are dead (`NoteGeometry.cpp:15-37`) while `NoteBatch.cpp:294,449` duplicates the maths independently |
| `core/PracticeJudgeWindows.cpp` | correctly ported but unwired (§4.3) |
| `app/SelfTest.cpp` (539), `render/RenderSmoke.cpp` (243), `tests/*` | port-only verification scaffolds |
| `platform/FileDialog.cpp`, `app/CoverCache.cpp`, `render/fx/*` passes | platform/render equivalents of Android classes |
| `Chart.songName`/`normalizedDigest`/`levelEffects` | level-effect matching moved from Java's renderer into the loader |

---

## 9. Numeric constant comparison (highest-value table)

All values below were read from source on both sides. "≡" means identical.

| Quantity | Java value | C++ value | Status |
| --- | --- | --- | --- |
| `PGRW`, `PGRH`, `PGRBEAT` | 0.05625, 0.6, 60/32 = 1.875 | 0.05625, 0.6, 1.875 | ≡ |
| P/G colours | `1, 0xec/255, 0xa0/255` / `0xb4/255, 0xe1/255, 1` | same rationals | ≡ |
| P/G alpha | `0xe1/255`, `0xeb/255` | **not defined in `GameConstants.hpp`** | **MISSING** — where the port gets them is **UNVERIFIED** (`HitEffect.cpp`/skin colours) |
| `RPE_WIDTH/HEIGHT`, `HEIGHT_RATIO`, `SPEED_RATIO` | 1350, 900, 0.83175, 10/45/0.83175 | same | ≡ |
| `EPS` | `1e-5` | not defined | **MISSING** constant (inline literals instead) |
| Hit-effect burst | speed 15..35, radius 0.2, thickness 0.2 | **not in `GameConstants.hpp`** | **UNVERIFIED** — locate in `HitEffect.cpp` |
| Judgement windows | 0.080/0.180/0.220; 0.040/0.090/0.140 | same | ≡ |
| Drag miss | 0.100 | 0.100 | ≡ |
| Flick miss factor | 1.75 | 1.75 | ≡ |
| Flick gesture | 0.06 base, 380 dpi ref, 60 time scale, 5× multiplier | same | ≡ |
| Hold tail settle | 0.220 | 0.220 | ≡ |
| Hold destroy delay | 0.250 (used as miss diff) | 0.25 literal | ≡ |
| Judge Y scale | 2.2 | 2.2 | ≡ |
| Practice windows | 20/80, 25/180, gap 5 | same | ≡ |
| Score weights | 900000 / 100000 / 1000000, good weight 0.65 | same | ≡ |
| Grade thresholds | 700k/820k/880k/920k/960k/1e6 | same | ≡ |
| Detail bins | 19, 180 ms, 90 ms | same | ≡ |
| Challenge avg frames | 10, ×0.5 | 10, ×0.5 | ≡ |
| Hit-offset arc | 60°, move 0.20 s, return delay 0.45 s, return 0.35 s, mark life 0.90 s | same | ≡ |
| Calibration | loop 2.0, hit 1.0, spawn 1.5, fall 1.5, ±500 ms | same | ≡ |
| Pause rect | 50.5887985, 444.2999878, 34.262, 37.966, 35/41 | 50.5887985, 444.2999878 (W/H not compared) | **PARTIAL** |
| Intro/outro | intro hold 10/60 s, intro buffer `INTRO_AUDIO_DELAY_MS`, outro 1.7 s, collapse 1.0+0.7 | 0.35, 0.45, 1.7 | **intro UNVERIFIED** |
| Low-pass | 22000 / 1500 Hz, Q 1/√2, ~0.05 s | same | ≡ |
| Audio clamps | speed 0.05..4, gain 0..5 | same | ≡ |
| Particle field | 512, 10 units, emit 10/s, life 10 s, 12.5/4.5/3.75 box, 0.05/0.10 speed, 0.05 force, 0.5 field accel, 0.8 drag, 7.5/1.5 field, 3.6587481/0.23306048 slopes | same | ≡ |
| RPE speed divisor | 4.5 | 4.5 | ≡ |
| RPE note X divisor | `670 * 9/80 = 75.375` | same | ≡ |
| RPE yOffset divisor | 900 | `RPE_HEIGHT` = 900 | ≡ |
| Official V1 move divisors | 880 (X), 520 (Y), then −0.5 | same | ≡ |
| PEC offset | `(ms−150)/1000` rounded 4 dp | same | ≡ |
| PEC X divisor | `*9/1024` | same | ≡ |
| PEC move/rotate divisors | `/2048`, `/1400`, speed `/7` | same | ≡ |
| Skin/HUD text | en space U+2002 | ASCII 0x20, no run-collapse | **DIFFERS** |
| Settings aspect default | 16/9, clamp 0.5..5 | 0.0 = follow screen, no clamp | **DIFFERS** |
| `bpm<=0` handling | `Inf` propagation (`sec2beat`), `bpm=120` mutation (`initSpeedEventsPhi`) | `1e-9` fallback | **DIFFERS (edge)** |

---

## Appendix A — Java file → C++ counterpart map

`game/`

| Java | C++ |
| --- | --- |
| `Chart.java` | `include/rechigros/game/Chart.hpp` + `src/core/JudgeLine.cpp:109-136` (header-only class; runtime init lives in `JudgeLine.cpp`) |
| `ChartLoader.java` | `src/chart/ChartLoader.cpp` |
| `PecChartParser.java` | `src/chart/PecChartParser.cpp` |
| `RpeChartParser.java` | `src/chart/RpeChartParser.cpp` |
| `OfficialChartParser.java` | `src/chart/OfficialChartParser.cpp` |
| `JudgeLine.java` | `include/rechigros/game/JudgeLine.hpp` + `src/core/JudgeLine.cpp` |
| `Note.java` | `include/rechigros/game/Note.hpp` |
| `EventUtils.java` | `src/core/EventUtils.cpp` |
| `Easing.java` | `src/core/Easing.cpp` |
| `OfficialEase.java` | `src/core/OfficialEase.cpp` |
| `BpmTimeline.java` | `src/core/BpmTimeline.cpp` (+ per-parser converters) |
| `EventCursor.java`, `LineEvent/MoveEvent/SpeedEvent/ColorEvent/TextEvent/GifEvent.java` | `EventCursor.hpp`, `Event.hpp` |
| `GameConstants.java` | `include/rechigros/game/GameConstants.hpp` (subset — see §9) |
| `MathUtils.java` | inline `std::clamp`/`std::max` |
| `GameRenderer.java` (judging, ~3,000 lines) | `src/core/GameplaySession.cpp`, `src/core/JudgementEngine.cpp`, `src/core/NoteGeometry.cpp` |
| `GameRenderer.java` (rendering, ~11,000 lines) | `src/render/GameRenderer.cpp`, `render/scene/*`, `render/fx/*`, `render/text/*` |
| `NativeRenderCommands.java` | `render/batch/SpriteBatch.cpp`, `render/scene/NoteBatch.cpp` (no `NoteBucket` reset contract) |
| `GameGLSurfaceView.java` | `src/app/Application.cpp` (SDL3 window/GL context) |
| `HitOffsetIndicator.java` | `src/core/HitOffsetIndicator.cpp` |
| `ClickEffectItem.java` | `src/core/ClickEffectItem.cpp` |
| `PlayResult.java` | `include/rechigros/game/PlayResult.hpp` |
| `ReplayData.java` | `include/rechigros/app/Replay.hpp` (**different model**) |
| `PrprEffect.java` | `include/rechigros/game/PrprEffect.hpp` + `src/core/PrprEffect.cpp` |
| `PrprVideo.java` | `PrprEffect.hpp:31-38` (**fields missing**) |
| `OfficialBlock.java` | `src/core/OfficialBlock.cpp` |
| `OfficialLevelEffects.java` | `src/core/OfficialLevelEffects.cpp` (parity **UNVERIFIED**) |
| `MessageParticleField.java` | `src/core/MessageParticleField.cpp` |
| `SkinConfig.java` | none |
| `ResPackInfo.java` | none |
| `audio/NativeAudioEngine.java` | `include/rechigros/audio/AudioEngine.hpp` + `src/audio/AudioEngine.cpp` |
| `app/src/main/cpp/audio_engine.cpp` | `src/audio/AudioEngine.cpp` + `include/rechigros/audio/Mixer.hpp` |
| `app/src/main/cpp/audio_decoder.cpp` | `src/audio/Decoder.cpp` (+ miniaudio, stb_vorbis) |
| `util/PcmDecoder.java`, `WavDecoder.java`, `NativeAudioDecoder.java` | `src/audio/Decoder.cpp` (miniaudio handles WAV/MP3; stb_vorbis optional OGG) |

`ui/` → app screens

| Java | C++ |
| --- | --- |
| `ChartSelectActivity.java` | `src/app/LibraryScreen.cpp` + `src/app/DetailScreen.cpp` + `src/app/CommonScreens.cpp` |
| `PlayActivity.java` | `src/app/GameScreen.cpp` + `src/app/Application.cpp` |
| `ResultActivity.java` | `src/app/ResultScreen.cpp` |
| `CalibrationActivity.java` | `src/app/CalibrationScreen.cpp` |
| `GameDialog.java` | `src/app/ScreenCommon.cpp` (+ `SettingsScreen.cpp`) |
| `ReplayManager.java` | `src/app/Replay.cpp` |
| `SkinManager.java` | none (skin layering in `render/asset/TextureCache.cpp`) |
| `JudgementHistogramView.java` | `src/app/ResultScreen.cpp` |
| `TriGridBackgroundView.java` | `src/app/TriGridBackdrop.cpp` |
| `SkewFrameLayout.java`, `SettingsSliderView.java`, `SettingsToggleView.java`, `SettingsPreviewView.java`, `UiTypography.java`, `UiWarmup.java` | `src/app/UiStyle.cpp`, `include/rechigros/app/UiWidgets.hpp`, `src/app/SettingsScreen.cpp` |
| `GameTextView/GameTextRenderer/GameTextOverlay.java` | `src/render/text/TextRenderer.cpp`, `render/scene/HudPass.cpp` |
| `AssetBitmaps.java`, `CoverImageLoader.java` | `render/asset/TextureCache.cpp`, `src/app/CoverCache.cpp` |
| `LocalChartAdapter/OnlineChartAdapter/SelectPagerAdapter`, `Select*Fragment.java` | `src/app/LibraryScreen.cpp` (local paths only) |

`util/`

| Java | C++ |
| --- | --- |
| `ChartLibraryManager.java` | `src/app/ChartLibrary.cpp` |
| `InfoFileUtils.java`, `InfoJsonUtils.java` | `src/app/InfoFile.cpp` |
| `ZipPackUtils.java` | `src/platform/Archive.cpp` (extract) + `src/app/ChartLibrary.cpp` (detect) |
| `FileUtils.java` | `src/platform/FileSystem.cpp` |
| `LanzouClient.java`, `OnlineIndexClient.java` | none (deliberate) |

---

## Appendix B — Verification backlog (what is still unproven, and how to settle it)

1. **Autoplay diff convention** — read Java's autoplay branch; compare `commitJudgement(n, JR_PERFECT, x)` with `GameplaySession.cpp:264-266`. Settles §4.2(1).
2. **Mirror geometry under letterboxing** — `mirrorTouchX` normalises by the window width (`GameScreen.cpp:698-703`) while the renderer mirrors inside the letterboxed stage (`Mat4::mirrorX`). On a window whose aspect is not the target aspect these two centres differ. Settle by mirroring a known note position at 21:9 and comparing the drawn X with the hit-tested X.
3. **`applyLegacySpeedHeightMode` / `mergeSpeedLayers` numeric identity** — dump a layered RPE chart's `speedEventLayers` and `speedEvents` before/after those passes and compare floor positions with Java's integration.
4. **`OfficialLevelEffects` table parity** — diff every effect id and parameter table between `OfficialLevelEffects.java` and the shipped `assets/official_effects/*.json` the port loads.
5. **`PrprChain` GLSL upgrade** — confirm the three rewrites and the alternate-output collision rule from `PrprShaderCompatibilityTest` exist in `src/render/fx/PrprChain.cpp`.
6. **Replay playback result merge** — confirm the port cannot restore a saved histogram/stddev (expected, given §1 #12) and decide whether to extend `ReplayMeta`.
7. **Reset contract for the note batch** — `NativeRenderBucketRuntimeResetTest` has no counterpart; decide whether `NoteBatch` needs an explicit `resetRuntimeState` and assert it.
8. **Intro audio delay** — read `INTRO_AUDIO_DELAY_MS` and compare with `kIntroAudioStartSec = 0.35`.
9. **SFX voice capacity and pitch coupling** — compare `voiceCapacity()` with Java's unbounded voice list, and confirm SFX does not inherit `playbackSpeed`.
10. **`PlayResult` FC/AP helpers** — verify predicates match `PlayResult.java:73-79`.
11. **Zip import parity** — run the `ZipPackUtilsTest` fixture (info.yml + chart.json + song.ogg + bg.png in a `pack/` folder) through `ChartLibrary::importZip` and compare the resolved record.
12. **`mora`/`holdFxTimerSec` cadence** — recover the reference burst interval from `HoldControl.timeOfJudge` usage and implement it, or record it as a declared exclusion.
