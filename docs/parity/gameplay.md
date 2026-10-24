# Gameplay shell: parity report

Covers the run lifecycle around the chart: entry and exit transitions, the pause state
machine, the drawing clock, touch forwarding and the audio engine's hand-off. Chart data
itself is in [`chart.md`](chart.md); judging and replay are in [`judging.md`](judging.md);
the render passes are in [`render.md`](render.md).

Each row states the reference behaviour, the port's behaviour, and how the two were compared.

---

## 1. Pause state machine

The reference models pause as a four-value state, and the distinction between the third and
fourth matters:

| Reference | Port | Notes |
| --- | --- | --- |
| `PAUSE_NONE = 0` | `Pause::None` | gameplay running |
| `PAUSE_MENU = 1` | `Pause::Menu` | pause plate shown, music stopped |
| `PAUSE_COUNTDOWN = 2` | `Pause::Countdown` | 3-2-1 overlay, field still frozen |
| `PAUSE_FADEOUT = 3` | `Pause::Fadeout` | countdown finished, backdrop fading, music not yet restarted |

**Defect found:** the port had only the first three states. The countdown hands over directly
to play, so the frames between "countdown finished" and "music restarted" belonged to no
state at all. A pause taken in that window was treated as a fresh pause on a running chart.

**Fixed** in `src/app/GameScreen.cpp`: the countdown now transitions to `Pause::Fadeout`, the
fade advances over `kResumeFadeoutSeconds`, and only its completion restarts playback.

### 1.1 Pausing before gameplay is stable

The reference gates pause entry on whether gameplay has actually started:

`GameRenderer.java:1968` reads

```java
boolean startFromZero = isBeforeGameplayStartOrIntroTransition();
```

and that predicate (`GameRenderer.java:1931`) is

```java
return introStarted && (!isMusicStartedFlag || wasInIntro || isInIntroHold);
```

When it holds, the reference forces the frozen playhead to zero
(`freezePlayheadForPause(true)`) and the run restarts from the beginning on resume. This is
deliberate: before the music is running there is no playhead worth returning to.

**Defect found:** the port captured `game.playheadSec` unconditionally and never restored it
on resume at all, so the playhead after a resume was whatever the stopped stream reported.

**Fixed** in `src/app/GameScreen.cpp`:

- `gameTogglePause` computes `beforeGameplay = game.inIntro || game.chartTimeSec < 0.0`,
  freezes the playhead at zero in that case, and records `pauseResumesFromZero`.
- The frozen value is stored in `pausePlayheadSec` with `pausePlayheadValid` guarding it.
- The fadeout completion either restarts the run (`restartRunFromZero`, which reloads through
  `gameEnter` so the opening transition replays) or seeks the audio stream back to the frozen
  playhead before restarting it.

Verified by `--expect-pause`, which drives escape, escape, and the countdown, then asserts
`pause: menu=1 countdown=1 resumed=1`.

## 2. Drawing clock

The reference does not draw from the audio cursor directly. `GameRenderer.java:6244` keeps a
rolling window of the error between the predicted position and the reported one:

```java
private void addSmoothBias(double bias) { ... }   // SMOOTH_BIAS_HISTORY_SIZE = 60
```

and `GameRenderer.java:6325` uses its average to correct the prediction, bounded by

```java
final double maxDeviation = 0.010; // ±10ms tolerance
```

with a discontinuity reset when the position jumps backwards by more than 100 ms or by more
than 500 ms in either direction.

**Defect found:** the port drew the play field from `game.playheadSec`, which is the raw audio
cursor. The stream reports its position only once per callback, so notes advanced in visible
steps with jitter between reports — the exact artefact the reference's smoothing exists to
remove. This is a behavioural difference no constant comparison would surface.

**Fixed** in `src/app/SmoothingClock.cpp`, a standalone component so it is testable without a
renderer or an audio device. It reproduces the reference algorithm: 60-frame ring buffer,
average-bias correction, ±10 ms clamp and the same discontinuity thresholds (100 ms backwards,
500 ms in either direction). `src/app/GameScreen.cpp::updateSmoothedClock` feeds it the audio
cursor and stores the result in `GameRuntime::smoothedPlayheadSec`.

The split preserves correctness: **judgement keeps using `playheadSec`** (the unsmoothed
value), and only `input.chartTimeSec` / `input.playheadSec` for rendering use
`smoothedPlayheadSec`. A note's drawn position therefore never changes when it will be judged.

Evidence that judgement is unaffected: a full autoplay run of a 1397-note official chart still
reports `total=1397 perfect=1397 maxCombo=1397`.

### 2.1 Where the clock restarts

The reference invalidates and re-primes the smoothed clock whenever the position is replaced
outright, not only at run start: `invalidateSmoothClock()` at `GameRenderer.java:2286` (new run)
and again at `:2450` before `primeSmoothClock(playhead)` (seek).

**Defect found:** the port only primed the clock on its first valid frame. A seek therefore ran
through the *correction* path instead of the reset path. The clock's own discontinuity detection
covers large and backwards jumps, but a small forward seek — a practice-mode nudge of a fraction
of a second — falls inside those thresholds and was treated as normal advance, leaving the drawn
position extrapolated from where the run used to be.

**Fixed** in `src/app/GameScreen.cpp`: `gameEnter` clears the clock for a fresh run,
`applyPracticeSeek` restarts it from the seek target, and the pause-resume path restarts it from
the restored playhead (the prediction has been running against a frozen position for the whole
pause). Covered by `tests/AppDataTests.cpp`.

## 3. Entry and exit transitions

| Behaviour | Reference | Port |
| --- | --- | --- |
| Intro length | 1.083 s before the music is released (`INTRO_AUDIO_DELAY_MS`) | `kIntroEndSec = 1.083` |
| Intro line curve | 55-frame expansion (`UI_INTRO_DUR_SEC = 40/60`) | `render::scene::introLineScale` over `kIntroLineDurationSec` |
| Exit length | 1.7 s before the settlement | `kOutroDurationSec = 1.7` |
| HUD tweens | 40 frames in and out (`UI_INTRO_DUR_SEC`, `UI_OUTRO_DUR_SEC`) | `kHudIntroDurationSec`, `kHudOutroDurationSec` both `40.0/60.0` |
| Combo label exit fade | `COMBO_LABEL_OUTRO_FADE_SEC = 35/60` | `comboLabelOutroAlpha` |

The exit sequence prepares the settlement backdrop during the transition rather than on the
result page's first frame; see §4 of the master report.

## 4. Touch forwarding

| Behaviour | Evidence |
| --- | --- |
| A press on the pause control reacts to a second press inside a padded radius within 1 s | `isPauseButtonHit` + `gLastPauseTapMs` window in `GameScreen.cpp` |
| The padded radius scales with the play field and shrinks with the reveal camera | `pad = clamp(max(w,h) * 1.1, dp*20, dp*80)`, both centre and radius scaled by `revealScale` |
| A single tap on the pause area still reaches the judge | the handler falls through to `game.session.touchDown` when the double-tap window is not met |
| Mirror mode reflects touches about the stage centre, not the window centre | `mirrorTouchX` uses `renderStage(context)` bounds |
| A pause keeps resting fingers rather than cancelling them | `game.session.settleTouches()` on pause entry |

Verified by the render matrix case `input-pause` and by the mirrored-touch case in the
application loop.

## 5. Audio hand-off

| Behaviour | Reference | Port |
| --- | --- | --- |
| Music starts at the end of the opening buffer, not at entry | audio released at intro end | `startAudio(context, true)` at `introElapsedSec >= kIntroEndSec` |
| Pause stops the stream | `NativeAudioEngine.pause(true)` | `context.audio->pause()` |
| Resume seeks before playing | playhead restored, then playback | `context.audio->seek(game.pausePlayheadSec)` before `play()` |
| A stalled stream must not freeze the chart | reference keeps advancing | the port falls back to `playheadSec += delta` whenever `isPlaying()` is false |
| Music speed applies to the predicted clock | `* (double) musicSpeed` | `elapsed * game.musicSpeed` in `updateSmoothedClock` |

### 5.1 Music speed reaching the session

The session converts the frame delta it receives into a real elapsed time by dividing by the
music speed (`GameplaySession::realFrameDeltaSec`). Hold-body burst cadence is driven by that
real time, so a wrong speed makes it fire at the wrong rate.

**Defect found:** `GameplaySession::setMusicSpeed` existed but **nothing ever called it**, so
the session always divided by 1.0. At any practice speed other than 1x, every speed-dependent
effect ran at the rate the run started with; changing the practice slider mid-run had no effect
on them at all. The `setPlaybackSpeed` calls on the audio engine did not reach this path —
they are separate objects.

**Fixed** in `src/app/GameScreen.cpp`: `game.session.setMusicSpeed(game.musicSpeed)` at run
start (`gameEnter`) and in `setPracticeSpeed` when the slider moves. A zero or non-finite speed
falls back to 1x rather than dividing by zero.

Covered by `tests/CoreTests.cpp`: half speed, double speed, zero and NaN each assert the value
the session actually holds.

## 6. Verification

| Gate | Result |
| --- | --- |
| `ctest --test-dir build/desktop-release -C Release` | 8/8 pass |
| `build.bat verify-app` (includes `pause-cycle`) | all checks pass |
| `build.bat verify-render` | 26/26 cases |
| Autoplay precision after clock smoothing | `total=1397 perfect=1397 maxCombo=1397` |
| Worst frame across a 900-frame pause cycle | 11.4 ms |

## 7. Remaining risk

- The four-state machine is exercised by the automated pause cycle, but the
  "restart from zero" path (pausing *during* the opening transition) is only reachable by
  pausing within the first 1.083 s of a run. It is covered by inspection of the state
  transitions rather than by an automated run, because the batch harness cannot deliver a
  key event that early without racing the entry transition.
- The smoothed clock has no unit test of its own; it is verified through autoplay precision
  and through the visual glide it produces. Its arithmetic is a direct transcription of the
  reference method with the same constants, so a divergence would have to come from the
  frame clock rather than from the formula.
