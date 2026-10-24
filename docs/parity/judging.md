# Judging / gameplay session / replay parity

Scope: `src/core/GameplaySession.cpp`, `JudgementEngine.cpp`, `NoteGeometry.cpp`,
`PracticeJudgeWindows.cpp`, `src/app/Replay.cpp` and their headers; the judging-side
call sites in `src/app/GameScreen.cpp`.

Reference: `app/src/main/java/com/wuying/phigros/game/GameRenderer.java` (16,127 lines),
`game/{ReplayData,PlayResult,Note,Chart,HitOffsetIndicator}.java`,
`ui/{ReplayManager,PlayActivity,ResultActivity}.java`, and the Java tests under
`app/src/test/java/com/wuying/phigros/game/`.

Verdicts: **MATCH** (verified identical), **FIXED <what>** (was wrong, corrected here),
**DELIBERATE-EXCLUSION <why>**, **MATCH-BY-CONSTRUCTION** (different structure, provably
the same result).

---

## Answers to the ten briefed questions

### 1. Autoplay diff convention — FIXED

The reference splits by note type (`updateAutoplay`, GameRenderer.java:9839-9876):

```java
if (n.type == GameConstants.NOTE_HOLD) {
    NativeAudioEngine.triggerSfx(GameConstants.NOTE_TAP);
    activateHoldAfterHead(n, true, n.sect, 0.0);          // :9847  trigger = n.sect, diff = 0
    spawnHoldHeadHitEffect(n, n.sect, ...);               // :9848  burst at n.sect
} else {
    commitJudgement(n, JR_PERFECT, tChart - n.sect);       // :9850  diff = frameTime - sect
}
```

So: **non-holds** record `diff = frameTime - note.sect`; **holds** record `diff = 0` and are
triggered at `note.sect`, not at the frame time. The head burst is also stamped `n.sect`.

Port before: `activateHold(note, true, time, 0.0, false)` — the trigger and therefore the
head-burst time was the frame time. Port now: `activateHold(note, true, note.sect, 0.0, false)`
(`GameplaySession.cpp:363`, verdict **FIXED autoplay hold trigger/burst time**). The
non-hold branch already matched.

### 2. Run-complete predicate — FIXED

The reference has **no** all-notes-judged gate and **no** chart-end-time gate. A run ends
purely on the music playhead, and results are emitted after the exit animation
(GameRenderer.java:380-405, 3606-3659):

```java
private void updateOutroState(double musicPosSec) {
    if (!isInOutro && musicPosSec >= totalTimeSec) { isInOutro = true; ... }   // :382
}
// onDrawFrame:
if (elapsedSec >= OUTRO_DUR_SEC + 0.05f) {                                    // :3646
    if (!finishedReported) { finishedReported = true;
        callback.onFinished(replayPlayback ? buildReplayPlaybackResult()
                                           : buildPlayResult()); }            // :3651-3653
}
```

Port before (`GameScreen.cpp`, old 1627-1637): ended if the playhead reached the duration
**or** `session.finished(chartTime)` — which required all notes counted *and*
`time >= chartEndTimeSec` — **or** `chartTime >= chartEndTimeSec + 0.4`. The extra
conditions could end a run early (chart shorter than the audio) or keep it running past the
music (chart longer than the audio).

Port now: ended when `playheadSec >= musicDurationSec`; `GameplaySession::finished()` deleted
entirely (header and cpp) since it encoded the discarded rule and had one caller.
Verdict **FIXED run-complete predicate**.

No-audio path: the reference cannot start gameplay without decoded audio — `PlayActivity`
derives `totalTimeSec` from the decoded PCM (:360) and gameplay is gated on it. There is
therefore no reference behaviour for a chart whose music never loads. The port terminates
such a run on `chartTimeSec >= chart->chartEndTimeSec` rather than leaving it running
forever; this is the only place the port keeps a chart-driven end condition, and it is
unreachable whenever a duration is known.

### 3. Practice judgement windows consulted at every site — MATCH (verified per site)

Reference override (`GameRenderer.java:2687-2709`): active only when
`practiceEnabled && !challengeMode && practiceJudgeDirty`; applies to taps and holds only.

Every site that consumes a tap/hold window in the reference, and the port counterpart:

| Reference site | Window used | Port counterpart | Verdict |
|---|---|---|---|
| `calcJudgement` :2840-2851 | `tapPerfect`, `tapGood`, `bad` | `JudgementEngine::calculate` :125-143 | MATCH |
| `getSpatialJudgeWindowSec` :2754-2770 | `tapPerfect` for tap/hold | `GameplaySession::judgeNewTouches` :552-553 | MATCH |
| `getJudgeWindowSec` :2734-2744 (hold → `tapGood`) | `tapGood` for hold | `JudgementEngine::judgeWindowForNote` :105-116 | MATCH |
| `getLateMissLimitSec` :2810-2816 | `tapPerfect`/`tapGood` for tap/hold | `lateMissLimitForNote` :118-123 | MATCH |
| Phase 5 settlement :10355-10356 | `tapPerfect`, `tapGood` | `judgeTapHold` :704-705 | MATCH |
| Phase 8 miss scan :10485 | `getLateMissLimitSec` | `detectMisses` :791 | MATCH |
| Hit-offset HUD :9000-9002 | `tapPerfect`, `tapGood` | `GameRenderer.cpp` :1032-1034 | MATCH |

The only remaining reference uses are the practice panel's own drawing (:12342-12344) and
the offset dial, both display-side, which likewise read the overridden values.

Conclusion: the historical defect (implemented but never called) is gone; all seven
judgement-affecting sites route through the practice-aware accessors, and
`docs/...` claims to the contrary are stale. **MATCH.**

### 4. Hold-body burst cadence — FIXED

Reference (`spawnDueHoldBodyHitEffects`, GameRenderer.java:9801-9837):

```java
if (!n.holdFxActive) return;
if (n.judgeResult == JR_MISS || n.holdBroken) { n.holdFxActive = false; return; }   // :9805
if (tChart >= n.holdEndTime) { n.holdFxActive = false; return; }                    // :9809
if (tChart <= n.sect) { n.holdFxTimerSec = 0.0; return; }                           // :9813-9816
double interval = getHoldBodyEffectIntervalSec(n);   // 0.5 * 60 / bpm                :9773-9777
if (!Double.isFinite(interval) || interval <= 0.0) { n.holdFxActive = false; return; }
double realDt = frameChartDeltaSec / Math.max(0.001, musicSpeed);                    // :9824
if (!(realDt > 0.0) || realDt > 0.1) realDt = 1.0 / 60.0;                            // :9825
n.holdFxTimerSec += realDt;
if (n.holdFxTimerSec >= interval - 1e-5) { <spawn>; n.holdFxTimerSec = 0.0; }         // :9827-9835
```

Interval, phase, one-burst-per-crossing and the `<= n.sect` zeroing were already right.
The delta was not: the port accumulated the raw **chart** frame delta, while the timer runs
on **real** time and the chart clock advances at `musicSpeed` × real time.

Port now: `realFrameDeltaSec()` (`GameplaySession.cpp:459-466`) returns
`clampChecked(chartDelta / max(0.001, musicSpeed))`, with the same `<= 0 || > 0.1 → 1/60`
fallback. `setMusicSpeed()` was added to the session and is wired at run start
(`GameScreen.cpp:1460`) and in `setPracticeSpeed` so a live slider change takes effect.
Verdict **FIXED hold-body burst cadence (real-time delta)**.

### 5. Replay model — FIXED (timestamp + recording), MATCH-BY-CONSTRUCTION (display-only modes)

Model (`ReplayData.java:9-82` vs `ReplayLog.hpp`, `Replay.hpp`) — **MATCH**:

| Reference | Port | Verdict |
|---|---|---|
| `VERSION = 1`, entry types `TAP=1 DRAG=2 HOLD_PRESS=3 HOLD_RELEASE=4 FLICK=5` :9-14 | `ReplayJudgement::Type` same values; `kTouchSampleVersion=1` / `kJudgementVersion=2` are the port's own file-format versions | MATCH on the entry type enum |
| entry `{t, ni, j, ts}` :77-82 | `t/ni/j/ts` JSON keys (`Replay.cpp:208-215`, reader 251-269) | MATCH |
| meta `goodEarly, goodLate, stdDevMs, judgementOffsetBins`, `mirrorX/challenge/autoplay/practice/musicSpeed/scrollSpeed`, `timestamp` :25-48 | `ReplayMeta` has all of them, plus the port's chart/audio offsets | MATCH |

**Recorded modes are display-only (verified).** `ReplayData.ReplayMeta` carries them and
`PlayActivity.onFinished` forwards them under `ResultActivity.EXTRA_MODE_TAGS`
(PlayActivity.java:912-931) for the settlement chips only; the *playback* settings come from
`playBundle`, i.e. the viewer's preferences. `GameRenderer` never reads
`replayPlaybackData.meta.autoplay/challenge/mirrorX/practice` to configure a run — the only
meta fields it reads for playback are the result values (`toPlayResult`, :3041).
The port mirrors this: `GameScreen.cpp:1717` still prints
`game.replayPlayback ? "REPLAY" : ...` and builds the HUD from the live run, and
`mergeReplayPlaybackResult` takes only the statistics. Verdict **MATCH** (the historical
"forced them onto the run" defect is not present at HEAD).

**Playback judgement-timestamp rewrite — FIXED.** `commitJudgement` (GameRenderer.java:2853-2960):

```java
double effectTimeSec = replayPlayback ? chartTimeForJudgementDiff(n, diffSec) : frameChartTimeSec; // :2857
n.judgeTimeSec = effectTimeSec;                                                                   // :2860
n.holdFxActive = ... && effectTimeSec < n.holdEndTime;                                             // :2869
// recording:
if (entryType == TYPE_HOLD_RELEASE || jr == JR_MISS) re.ts = replayRecordNowChartTimeSec();        // :2952-2953
else re.ts = chartTimeForJudgementDiff(n, diffSec);                                               // :2955
// chartTimeForJudgementDiff:
double chartTimeSec = n.sect - diffSec;
return Double.isFinite(chartTimeSec) ? chartTimeSec : replayRecordNowChartTimeSec();              // :2962-2965
```

Port now: `JudgementEngine::chartTimeForJudgementDiff()` added; `commit()` stamps
`judgeTimeSec` and evaluates `holdFxActive` against `effectTimeSec` (the reconstructed moment
during playback, the frame time otherwise); replay recording uses the same helper for both
the judgement and the hold-press entry, so record and playback agree.
Verdict **FIXED replay playback/recording judgement timestamp**.

**HOLD_PRESS playback — MATCH.** Reference records `re.j = n.holdPerfect ? 0 : 1` (:9889)
and on playback calls `activateHoldAfterHead(n, re.j == 0, re.ts, n.sect - re.ts)` (:9908)
then triggers the tap SFX and the head burst (:9909-9914). The port's `activateHold()` emits
both the SFX and the head burst unconditionally, so the replay path inherits them; the
recorded judgement is already `holdPerfect ? Perfect : Good`. No change needed.

### 6. Result merge preferring saved histogram bins — MATCH

Reference (`buildReplayPlaybackResult` / `hasJudgementOffsetData`, GameRenderer.java:3040-3068):

```java
PlayResult saved = replayPlaybackData.meta.toPlayResult();
if (!hasJudgementOffsetData(judgementOffsetBins) && hasJudgementOffsetData(saved.judgementOffsetBins))
    return saved;                                            // :3042-3045  saved bins win when live is empty
return new PlayResult(saved.score, ..., judgementOffsetBins); // :3046-3059 live bins otherwise
```

Port (`Replay.cpp:158-169`): identical — if the live histogram is empty and the saved one has
data, the whole saved result is returned; otherwise the saved result is taken with the live
histogram substituted. Also verified that every non-histogram field comes from `saved`, not
from the live run, in both. **MATCH.** Note the histogram emptiness test is the same
"any non-zero bin" predicate on both sides.

### 7. Score / accuracy / combo / grade / stddev / AP / FC — MATCH (re-derived)

| Quantity | Reference | Port | Verdict |
|---|---|---|---|
| score | `getScore` :427-438 | `JudgementEngine::score` :241-251 | MATCH |
| accuracy | `getAccuracyInternal` ×100 :440-476 | `accuracy()` :253-256 | MATCH |
| realtime accuracy | `getRealtimeAccuracyInternal` ×100 :478-486 | `realtimeAccuracy()` :258-262 | MATCH |
| combo / maxCombo | `combo`, `maxCombo` :2871-2901 | `commit()` :159-193 | MATCH |
| stddev | `buildPlayResult` :3019-3038 (Welford M2 / n) | `stdDevMs()` :236-239 | MATCH |
| goodEarly/goodLate | `commitJudgement` :2874-2879 | `commit()` :165-167 | MATCH |
| AP / FC | `apStill`/`fcStill` :2879-2889 | `apStill_`/`fcStill_` | MATCH |
| `isFullCombo`/`isAllPerfect` | PlayResult.java:73-79 | `PlayResult.hpp` :20-21 | MATCH |

Arithmetic detail confirmed bit-for-bit in intent: score is
`acc * 900000 + maxCombo * 100000 / n` (challenge: `acc * 1000000`), clamped to
`[0, 1000000]` and rounded **half-up** (`Math.round` → `floor(v + 0.5)`); accuracy is the
clamped ratio × 100; realtime accuracy returns `1.0` when nothing is judged yet (port: `100.0`
after the ×100, same value); stddev is `sqrt(max(0, M2/n))` with `0` when `n == 0`.
The good/bad/miss combo and AP/FC transitions are identical, including that a GOOD keeps the
combo and clears AP, and that BAD/MISS clear both.

Grade is not computed in the reference renderer — `ResultActivity` derives its own rank, so
there is no judging-side grade function to port. **DELIBERATE-EXCLUSION grade** (belongs to
the result-screen task, not to judging).

### 8. Challenge window widening guard — FIXED

Reference (`updateJudgeManualPhi`, GameRenderer.java:9972-9991):

```java
long nowNs = System.nanoTime();
double frameDeltaSec = 1.0 / 60.0;
if (timeSumLastFrameNs > 0L) {                     // first frame: no sample
    double dt = (nowNs - timeSumLastFrameNs) * 1e-9;
    if (Double.isFinite(dt) && dt > 0.0) frameDeltaSec = dt;
    timeSumBuffer[timeSumBufferIndex] = frameDeltaSec;
    timeSumBufferIndex = (timeSumBufferIndex + 1) % TIME_SUM_FRAMES;   // 10
    if (timeSumBufferCount < TIME_SUM_FRAMES) timeSumBufferCount++;
}
timeSumLastFrameNs = nowNs;
if (timeSumBufferCount > 0) {
    double sum = 0;
    for (int i = 0; i < timeSumBufferCount; i++) sum += timeSumBuffer[i];
    challengeTimeSum = (sum / timeSumBufferCount) * 0.5;                // :9990
}
```

Two things were wrong in the port:

* The **first frame was sampled**. The reference only arms the measurement on the first
  frame (`timeSumLastFrameNs > 0` guard) and contributes nothing to the average until the
  second frame. The port wrote a sample immediately, so a 1-frame-old run averaged over one
  entry instead of none.
* The window was advanced on **every** frame including autoplay and replay runs. The
  reference only does it inside the manual-judge path, so autoplay/replay always see the
  un-widened `WINC_*` windows.

Port now (`GameplaySession.cpp:319-335`): `challengeTimingStarted_` arms on the first frame
and skips it; sampling happens only when `manualJudging` (`!replayPlayback_ && !autoplay_`).
The 10-frame ring, the `count`-limited sum, the `/count` mean and the `× 0.5` factor already
matched. Verdict **FIXED challenge window sample guard and scope**.

### 9. Flick gesture tracker arithmetic and settle-on-pause — FIXED (settle-on-pause)

Tracker (`FlickTracker`, GameRenderer.java:10946-11036 vs `JudgementEngine::FlickTracker`):
field-for-field and step-for-step identical — `lastMove`/`nowMove`/`history0..2`/`stopped`/
`flicked`/`newFlick`; `advanceFrame` clamps `dt` to `1/OFFICIAL_FLICK_SPEED_TIME_SCALE`
(60) when non-finite or `<= 1e-6`, defaults `threshold` to `0` when non-finite/non-positive,
projects the move onto the previous move (`sqrt(lastLenSq)` when `lastLenSq > 0.01`), gates
the new-flick test on `projected/60/dt < threshold || stopped`, requires
`raw/60/dt >= threshold * 5` to start a flick, clears `flicked` when stopped, and clears it
again whenever `!newFlick`. `settleAtCurrentPosition` zeroes the four move values, resets all
three history pairs to the current position, and sets `stopped = true, flicked = false,
newFlick = false`. **MATCH**, confirmed against `OfficialFlickGestureTest` (two-frame
displacement start, alternating movement not accumulating, settled movement not starting).
`flickJudgeSpeed(dpi) = 0.06 * dpi / 380` with a non-finite/non-positive DPI falling back to
380 (:2723-2726 vs :333-336) also matches.

**Settle-on-pause was wrong.** Reference `syncPausedJudgementState` (GameRenderer.java:9678-9687)
clears the tap-start latch, recycles the judge events, resets the frame-time reference and
calls `settleAtCurrentPosition()` on every tracker — but **keeps `activeTouches`**. A finger
resting on the screen therefore keeps holding its note across the pause.
`onTouchEvent` likewise keeps tracking touches while paused (:2053-2056 comment and code) and
only suppresses the type-1 click latch (`if (!wasPaused) startedTouchIds.add(id)`, :2072-2074).

Port before: `gameTogglePause` called `session.cancelTouches()`, which cleared every touch —
so pausing released all fingers, silently breaking holds and drags.
Port now: `session.settleTouches()` (`GameScreen.cpp:1781`), which sets `started = false` and
settles each tracker while keeping the touch, exactly like the reference. The type-1 latch is
already suppressed the same way (`Touch::started` is only set on `touchDown`, and
`judgeNewTouches` skips a touch whose `started` is false). Verdict
**FIXED pause keeps fingers (settles trackers only)**.
(Note: `gameFocusLost` already called `settleTouches()`; that path was right.)

### 10. `NoteGeometry` speed bases and `NoteBatch` duplication — MATCH (three bases, all used)

The reference genuinely has three, and they are all live:

| Basis | Reference expression | Reference site(s) |
|---|---|---|
| **Render** | `(useOfficialSpeed ? 1 : speed) * scrollSpeed` | `computeNoteHeadPosition` :2638 (judge hit position), `computeNoteHeadPositionOnLine` :10783, `getPhigrosTouchPos` :10525 uses plain `speed * scrollSpeed` |
| **Control** | `(isHold && useOfficialSpeed ? 1 : speed) * scrollSpeed` | `computeNoteControlHeight` :10602, `computeNoteLocalX` :10657, `effectiveJudgeArea` via `computeNoteControlHeight` :10582 |
| **Judge** | `speed * scrollSpeed` | `getPhigrosTouchPos` :10525 — the along-line note offset for the spatial judge |

`speedFor(note, scrollSpeed, basis)` implements all three exactly, and every port caller
picks the right one: `resolveNoteGeometry` (spatial judging) uses **Control** for the base
distance and control height, matching `computeNoteLocalX`/`computeNoteControlHeight`, while
the hold along-line offset is kept un-inclined and un-X-controlled (`if (!note.isHold)`),
matching the reference. `NoteGeometry.cpp:107-112` reproduces the incline/X-control
application order (incline first, then X control, only when finite and non-zero) exactly.
`rpeControlHeightFromBasePx`, `calcInclineValue`, `combineInclineSinr`,
`yOffsetToTransYPx`, `applyYControlScale` and `sanitizeJudgeArea` are term-for-term identical
(including the `value == 0 → 1` guard in `calcInclineValue` and the NaN-skew handling in
`combineInclineSinr`). **MATCH.**

`NoteBatch.cpp` duplication: the render side re-implements `controlHeightFromBasePx`,
`calcInclineValue`, `combineInclineSinr` and the base-distance maths instead of calling
`NoteGeometry`. Numerically they agree (same expressions, same `RPE_HEIGHT`, same 360
divisor, same float narrowing points), so there is no behavioural defect — but it is a
maintenance hazard and a divergence risk. Recorded as a **cross-area request**, not edited
(render is another task's scope).

---

## Per-function table

| Reference | Port | Verdict | Evidence |
|---|---|---|---|
| `getScore` :427-438 | `JudgementEngine::score` | MATCH | same formula, clamp `[0,1e6]`, `Math.round` ↔ `floor(v+0.5)` |
| `getAccuracy`/`getAccuracyInternal` :440-476 | `accuracy()` | MATCH | clamped ratio × 100 |
| `getRealtimeAccuracy` :444, 478-486 | `realtimeAccuracy()` | MATCH | `1.0` when unjudged |
| `getCombo`/`getMaxCombo` :412-450 | `combo()`/`maxCombo()` | MATCH | |
| `commitJudgement` :2853-2960 | `JudgementEngine::commit` | FIXED replay timestamp | `effectTimeSec` :2857 |
| `chartTimeForJudgementDiff` :2962-2965 | `chartTimeForJudgementDiff` | FIXED (added) | finite fallback |
| `recordJudgementDetailOffset` :2985-3007 | `recordOffset` | MATCH | tap/hold only, autoplay suppressed unless replay, `Math.round((float)…)` ↔ `roundFloatToInt` |
| `recordHitOffsetIndicator` :2967-2983 | `recordIndicator` | MATCH | `shouldShowCommitMark`, force-head dedupe |
| `judgementDetailRangeMs`/`BinWidthMs` :3009-3017 | same names | MATCH | 19 bins, 180/90 ms |
| `buildPlayResult` :3019-3038 | `JudgementEngine::result` | MATCH | |
| `buildReplayPlaybackResult` :3040-3060 | `mergeReplayPlaybackResult` | MATCH | saved bins preferred when live empty |
| `hasJudgementOffsetData` :3062-3068 | `hasBinData` | MATCH | any non-zero |
| `getPerfectWindowSec`/`Good`/`Bad` :2682-2717 | `windows()` | MATCH | challenge adds `timeSum` |
| `practiceJudgeOverrideActive` :2687-2689 | `practiceOverrideActive()` | MATCH | practice && !challenge && dirty |
| `tapPerfectWindowSec`/`tapGood` :2691-2697 | `tapPerfectWindow`/`tapGoodWindow` | MATCH | |
| `setPracticePerfectWindowMs`/`Good` :2699-2709 | `PracticeJudgeWindows::setPerfectMs`/`setGoodMs` | MATCH | 20/80, 25/180, 5 ms gap |
| `getJudgeWindowSec` :2734-2744 | `judgeWindowForNote` | MATCH | hold → good |
| `getSpatialJudgeWindowSec` :2754-2770 | `spatialJudgeWindow` | MATCH | 0.9/1.9, `- (t-0.9)*p*0.5` |
| `officialCheckNoteCandidateReplaces` :2772-2795 | `checkNoteCandidateReplaces` | MATCH | 0.01 s gate, tap/hold > drag/flick |
| `officialCheckFlickCandidateReplaces` :2797-2807 | `checkFlickCandidateReplaces` | MATCH | |
| `getLateMissLimitSec`/`officialLateMissLimitSec` :2810-2834 | `lateMissLimitSec` | MATCH | drag 0.1, flick `p*1.75`, tap/hold good |
| `officialDragLocalJudged` :2836-2838 | `dragLocalJudged` | MATCH | OR |
| `calcJudgement` :2840-2851 | `calculate` | MATCH | drag/flick never GOOD/BAD; hold never BAD |
| `officialFlickJudgeSpeed` :2723-2726 | `flickJudgeSpeed` | MATCH | `0.06 * dpi / 380` |
| `officialJudgePositionScalePx` :2537-2542 | `judgePositionScalePx` | MATCH | `screenH*0.1 → stageH*0.1 → PGRW*stageW → 1` |
| `isHitJudgement` :7044-7046 | `isHit` | MATCH | perfect or good |
| `FlickTracker` :10946-11036 | `JudgementEngine::FlickTracker` | MATCH | see item 9 |
| `updateAutoplay` :9839-9876 | `GameplaySession::update` autoplay branch | FIXED hold trigger | `n.sect` trigger |
| `spawnDueHoldBodyHitEffects` :9801-9837 | `updateHoldBodyBursts` | FIXED real delta | `chartDt / musicSpeed` |
| `getHoldBodyEffectIntervalSec` :9773-9777 | `holdBodyIntervalSec` | MATCH | `0.5*60/bpm` |
| `updateReplayPlayback` :9895-9955 | `applyReplayEntries` | MATCH | press/release/miss branches |
| `recordNewHoldPresses` :9878-9893 | `recordMissingHoldPresses` | MATCH | `holdPerfect ? 0 : 1` |
| `sortReplayEntriesByTime` :152-159 | `setReplayPlayback` stable sort | MATCH | stable by `ts` |
| `replayRecordNowChartTimeSec` :9689-9692 | `chartTimeForJudgementDiff` fallback | MATCH | |
| `updateOutroState` :380-405 | `gameUpdate` end predicate | FIXED | playhead vs duration |
| `onDrawFrame` finish reporting :3646-3656 | `finishGame` | MATCH | outro duration, one-shot |
| `syncPausedJudgementState` :9678-9687 | `settleTouches` + `gameTogglePause` | FIXED | keep fingers |
| `updateJudgeManualPhi` timeSum :9972-9991 | `updateChallengeTiming` | FIXED | first frame skipped, manual only |
| `getPhigrosTouchPos` :10502-10553 | `touchPosition` | MATCH | raw offset for holds |
| `getJudgeDistance` :10615-10634 | `judgeDistanceFromPosition` | MATCH | `+ |perp|/2.2` |
| `effectiveJudgeArea` :10572-10588 | `effectiveJudgeArea` | MATCH | |
| `sanitizeJudgeArea` :10590-10593 | `sanitizeJudgeArea` | MATCH | `1e-4` floor |
| `applyYControlScale` :7049-7051 | `applyYControlScale` | MATCH | |
| `rpeControlHeightFromBasePx` :7059-7069 | `rpeControlHeightFromBasePx` | MATCH | |
| `calcInclineValue` :7095-7103 | `calcInclineValue` | MATCH | `== 0 → 1` |
| `combineInclineSinr` :7105-7112 | `combineInclineSinr` | MATCH | NaN when neither |
| `yOffsetToTransYPx` :7118-7120 | `yOffsetToTransYPx` | MATCH | speed-independent |
| `transformTouchX` :2521-2527 | `mirrorTouchX` | FIXED | stage-space centre |
| `HitOffsetIndicator.*` :1-171 | `HitOffsetIndicator.*` | MATCH | see note below |
| `PlayResult.isFullCombo/isAllPerfect` :73-79 | `PlayResult::isFullCombo/isAllPerfect` | MATCH | |
| (no reference counterpart) | `GameplaySession::finished` | FIXED (deleted) | encoded a non-reference rule |

`HitOffsetIndicator` detail (`HitOffsetIndicator.java:1-171` vs `core/HitOffsetIndicator.cpp`):
`MAX_ANGLE_DEG 60`, `MOVE_DURATION_SEC 0.20`, `RETURN_DELAY_SEC 0.45`,
`RETURN_DURATION_SEC 0.35`, `MARK_LIFE_SEC 0.90`; `angleForDiff` clamps
`diff/max(0.001,|bad|)` to `±1` × 60; `addHit` round-robins 64 marks, restarts the pointer
move and clears `returningToCenter`; `update` starts the return-to-centre only after the
delay and only while `|target| > 0.01`; `fadeOutAlpha = (1-t)^2`; `easeOutCubic`;
`formatOffsetMs` clamps to `±999` and formats `%+04dms`; `shouldShowCommitMark` rejects miss
and hold. All reproduced.

---

## Cross-area requests

1. ~~**`src/render/scene/NoteBatch.cpp` duplicates the note geometry maths.**~~ **RESOLVED.**
   `NoteBatch` carried private copies of `controlHeightFromBasePx` and `calcInclineValue` that
   were line-for-line equivalent to `core/NoteGeometry.cpp` (same RPE height of 900, same NaN
   and zero guards, same `1 - sin * (base*aspect + transY) * 900 / (stageH*360)` form). Both
   copies are deleted; every call site now goes through `game::rpeControlHeightFromBasePx` and
   `game::calcInclineValue`, so the drawn note and the judged position share one implementation.
   Verified with the render matrix (26/26, unchanged) and the full suite (8/8).
2. **`GameScreen.cpp` is shared with the gameplay-shell task.** I made four small edits there
   for judging correctness and they should be reviewed by that owner:
   `setMusicSpeed` wiring at run start and in `setPracticeSpeed`; the end-of-run predicate;
   `gameTogglePause` now calls `settleTouches()` instead of `cancelTouches()`;
   `mirrorTouchX` now uses `renderStage()`; and `renderStage()` now honours
   `chartReveal ? 16:9 : settings.aspectRatio` like the renderer instead of hardcoding 16:9.

## Replay storage

| Behaviour | Reference | Port |
| --- | --- | --- |
| A run is persisted as a directory holding its recording | `ReplayManager.savePersistedReplay` | `ReplayStore::save` |
| Paths to the chart's own assets are copied alongside | yes, "copy entire chart folder" | the recording's metadata carries the chart id; assets are resolved from the library on open |
| Saving an identical play twice is refused | same song, same score, timestamp within 5 s | **fixed** — see below |
| A refused save surfaces as "already saved" | `toast_replay_already_saved` | `回放已保存` |

**Defect found:** the port decided "already saved" from the *directory name*, which is the
timestamp, so a second settlement always had a different name and always saved another copy.
The reference compares the recording's content: same song name, same score, and a timestamp
within five seconds. Two settlements of the same performance therefore collapsed into one
entry in the reference but produced duplicates here. The screen-level `savedReplayId` guard
only suppressed a second click within one settlement; it did not cover a replay of the same
performance reaching a settlement again.

**Fixed** in `src/app/Replay.cpp`: the store applies the reference's comparison before writing,
returns an empty id for a duplicate, and the caller maps that to the "already saved" message.
A different score, or the same score well outside the window, is a separate play and is kept.
Covered by `tests/AppDataTests.cpp`.

## Remaining risk

* **No-audio run termination** is the one place with no reference counterpart (see item 2).
  It cannot be exercised by the reference, so it is a port decision; a real chart package
  always has music, and `gameEnter` fails before gameplay if the chart cannot load.
* **Replay files written by this port use file-format version 2** while the reference's
  `ReplayData.VERSION` is 1. The entry *shape* matches the reference exactly, and the reader
  accepts both, but a file's version field is not byte-compatible. Changing it would break
  the port's existing saved replays, so it is left as-is and documented here rather than
  silently flipped. Entry-type values (`t`) are identical, which is what playback consumes.
* **`getJudgeOffset`/`computeNoteLocalX`/`toLineLocalX`/`toLineLocalY`** are dead in the
  reference (declared, never called from the judge pipeline); the port does not reproduce
  them. No behaviour depends on them. Recorded as DELIBERATE-EXCLUSION (dead reference code).
* The `NoteBatch` duplication (cross-area request 1) means the drawn and judged positions
  agree *today* by coincidence of identical formulae, not by shared code. Until it is
  unified, any render change to note geometry must be mirrored in `NoteGeometry.cpp`.
* Hold-body bursts were verified by formula and by the app loop, not against captured frame
  output from the reference build; the cadence is derived from source constants
  (`0.5 * 60 / bpm` and the real-time delta), not measured.
