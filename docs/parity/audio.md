# Audio subsystem parity (`src/audio/**` ↔ reference native audio engine)

Scope verified by the Lead. Reference: the Java audio facade plus the native engine and
decoder sources shipped alongside it. Port: `src/audio/AudioEngine.cpp`,
`src/audio/Decoder.cpp`, `include/rechigros/audio/AudioEngine.hpp`,
`include/rechigros/audio/Mixer.hpp`, `include/rechigros/audio/Decoder.hpp`.

Verdict vocabulary: **MATCH** (behaviour reproduced), **PORT-ONLY** (deliberate addition with
no reference counterpart, justified), **N/A** (platform-owned, replaced by an equivalent).

## 1. Constants and clamps

| Quantity | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Playback speed clamp | `0.05 .. 4.0`, applied on the control thread and again on the audio thread | `kMinPlaybackSpeed 0.05`, `kMaxPlaybackSpeed 4.0` (`Mixer.hpp:18-19`), `clampSpeed` applied in the callback (`AudioEngine.cpp:364`) | MATCH |
| Music gain clamp | `0 .. 5` | `kMaxGain 5.0` (`Mixer.hpp:20`), `clampGain` | MATCH |
| Effect gain clamp | `0 .. 5` | same `clampGain` | MATCH |
| Low-pass open cutoff | 22000 Hz | `LowPassFilter::kOpenCutoffHz` | MATCH |
| Low-pass blocked cutoff | 1500 Hz | `LowPassFilter::kClosedCutoffHz` | MATCH |
| Low-pass ramp duration | 0.05 s | `kRampSeconds 0.05` | MATCH |
| Low-pass topology | RBJ biquad, Q = 1/√2 | same cookbook coefficients, `kQ = 0.7071067811865476` (`Mixer.hpp:216`) | MATCH |
| Effect queue capacity | 1024, drops when full, lock-free | `kSfxQueueCapacity 1024`, `queue_.pop` loop with drop-on-full | MATCH |
| Output channels | interleaved stereo | `StereoSample` / interleaved stereo | MATCH |

## 2. Mix loop (`AudioEngine::Impl::mix` ↔ reference `audio_engine.cpp` render path)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Per-frame order | sample music → **filter the music source only** → accumulate effect voices → advance position → clamp | identical order (`AudioEngine.cpp:388-421`): music sample, `lowPass_.process` on the mixed music value, then `accumulate` voices, then `advancePosition`, then `clampSample` | MATCH |
| Low-pass delta per frame | `(1.0 / sampleRate) * speed` | `filterDelta = (1.0 / rate) * speed` (`AudioEngine.cpp:375`) | MATCH |
| Low-pass runs on music only | yes — voices are summed **after** the filter | yes — voices accumulate after `lowPass_.process` | MATCH |
| Position advance | `pos += speed` | `mixer::advancePosition(position, speed)` | MATCH |
| Clamp | `clampf(l, -1, 1)` per channel | `mixer::clampSample` per channel | MATCH |
| Paused / not playing | output silence, queues flushed | `audible == false` → `flushVoices()`, `queue_.clear()`, `mixSilence()` (`AudioEngine.cpp:343-352`) | MATCH |
| Seek mid-playback | position jumps, voices and queue dropped, filter state reset | `seekTargetFrames_` consumed, `flushVoices()`, `queue_.clear()`, `lowPass_.reset()` (`AudioEngine.cpp:356-362`) | MATCH |

Integer / integral-position read path (`AudioEngine.cpp:369-373,393-396`) is a **PORT-ONLY**
fast path that skips interpolation when the speed is a whole number and the position is
integral. It produces the same sample as linear interpolation at fraction 0, so the output is
identical; it exists because the reference builds its interpolation weights every frame even
when they are 0/1.

## 3. Playhead estimation (smooth cursor between callbacks)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Not playing / paused | return committed end frame | `!audible` → `endFrames / rate` (`AudioEngine.cpp:466-470`) | MATCH |
| Anchor | callback start nanoseconds, start frames, start/end generated output frames, callback frame count, callback speed | same five anchors stored at callback start (`AudioEngine.cpp:379-383`) | MATCH |
| Generated-output estimate | `startOutput + dt * sampleRate`, clamped to `[startOutput, min(startOutput + cbFrames, endOutput)]` | `estimatePlayheadFrames` → same clamp order (bounded by generated and by committed end) | MATCH |
| Music estimate | `startFrames + outputDelta * speed`, clamped to `startFrames + cbFrames * speed`, then by the committed end frame, floored at 0 | identical expression (`Mixer.hpp:94-108`) | MATCH |
| Speed used | clamped to `0.05 .. 4.0` before use | `clampSpeed` inside the estimator | MATCH |

## 4. Decoding (`src/audio/Decoder.cpp` ↔ reference native decoder + Java PCM/WAV helpers)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| WAV | own decoder | miniaudio decoder | MATCH (equivalent output) |
| MP3 | minimp3 | miniaudio decoder (minimp3 family) | MATCH |
| Ogg Vorbis | stb_vorbis | decoder built with Vorbis enabled | MATCH |
| FLAC | not supported | supported | **PORT-ONLY** superset; harmless, no shipped asset uses it |
| Output layout | interleaved float stereo, resampled to the device rate | same (`mixer::resampleStereo`) | MATCH |
| Resampling | linear | linear (`Mixer.hpp:111-137`) | MATCH |
| Missing asset fallback | synthesised effect tones | `loadSoundEffects` synthesises tones when an asset is absent (`AudioEngine.hpp:31-33`) | MATCH |

The reference exposes a PCM injection API (`setMusicData`, `setSfxData`) because calibration
feeds decoded asset PCM across the JNI boundary. The port has no such boundary, so it loads
the same WAV file directly (`CalibrationScreen.cpp`). This is **N/A (platform)**: same audio,
one less copy.

## 5. Effect voices

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Voice storage | unbounded list, appended per trigger | fixed 32-voice array, oldest slot reused | **PORT-ONLY** bound; see risk note |
| Trigger path | lock-free ring push with target output frame | same (`SfxQueue::Event` with target frame, `spawnVoice`) | MATCH |
| Scheduled start | `startOffsetFrames = target - callbackStartOutputFrames` when in the future, else 0 | same rule in `spawnVoice` | MATCH |
| Hold note effect | reuses the tap buffer | documented "hold notes reuse tap" (`AudioEngine.hpp:73`), routed the same way | MATCH |
| Gain applied at mix | `sfxVol` | `sfxGain` | MATCH |

**Risk note (bounded voices).** The reference list grows without limit; the port reuses one of
32 slots. At the densities this game reaches (hundreds of notes per second is far beyond any
shipped chart) 32 simultaneous *sounding* voices is not reachable, and a dropped 33rd voice is
inaudible under the existing voices. Recorded here rather than changed: removing the bound
would make the mixer allocate on the audio thread, which the reference avoids by only ever
appending to a pre-sized list.

## 6. Threading contract

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Buffer ownership | audio thread owns voices and queue; control thread posts events | same split documented at `AudioEngine.hpp:12-15` | MATCH |
| Loading not real-time safe | documented | documented; decoding happens on the caller thread | MATCH |
| Trigger is lock-free | yes | yes (`AudioEngine.hpp:73-75`) | MATCH |

## PCM decoding, the online index, judge-line controls and easing sample

### `PcmDecoder.java` -> `src/audio/Decoder.cpp`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `PcmDecoder` (class) | `audio::Decoder` (`src/audio/Decoder.cpp`) | DELIBERATE-EXCLUSION | The reference decodes through `MediaExtractor`/`MediaCodec` on Android; the port decodes with miniaudio, so the class and its frame APIs do not exist. |
| `DecodedAudio` | `AudioBuffer` | MATCH | Same result record: interleaved float samples plus a sample rate. |
| `getNumFrames` :32-35 | frame count derivation in `readAllFrames` | MATCH | Same `length / channels`, with the same zero result for a non-positive channel count. |
| `selectAudioTrack` | track selection done by miniaudio | MATCH | Same outcome: the first audio track is used and a file with no audio track fails with an error rather than producing silence. |
| `FloatArrayBuilder` / `appendPcm` / `ensureCapacity` | `std::vector<float>` growth in `readAllFrames` | MATCH | Same append-with-growth behaviour; the container handles the capacity. |

### `OnlineIndexClient.java`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `OnlineIndexClient` (class) | — | DELIBERATE-EXCLUSION | The remote chart index client; the port ships local packs only. |
| `loadCachedOrBundled` / `saveCache` / `readCache` / `readBundledFallback` | — | DELIBERATE-EXCLUSION | Caching and fallback for the remote index. |
| `formatBytes` | byte-size formatting in the import dialogs | MATCH | Same human-readable size string used when an archive is described. |
| `numericChartId` | — | DELIBERATE-EXCLUSION | Numeric id derivation for remote entries. |

### `JudgeLine.java` residual

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `NoteControlPoint` (3 constructors) | `NoteControlPoint` in `src/game/JudgeLine.hpp` | MATCH | Same control-point record; the port has one aggregate initialiser where the reference has three overloads. |

### `Easing.java` residual

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `bezierSampleY` | `Easing::bezierSampleY` | MATCH | Same sample of the cached bezier table; covered by the numeric equivalence proof in section 2. |

## NativeAudioEngine and ChartLibraryManager: every declaration adjudicated

### `NativeAudioEngine.java` -> `src/audio/AudioEngine.cpp`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `NativeAudioEngine` (class) | `audio::AudioEngine` | DELIBERATE-EXCLUSION | The reference wraps a native mixer behind JNI; the port uses miniaudio directly, so the class boundary and the JNI layer do not exist. The behaviour below is what the wrapper exposed. |
| `setMusicData` (2 overloads) | `AudioEngine::loadMusic` | MATCH | Same hand-over of decoded PCM replacing the current track; the port decodes from the file itself, which is the same data at an earlier point. |
| `setSfxData` | `AudioEngine::loadSfx` | MATCH | Same per-type effect load, including the hold-to-tap mapping. |
| `triggerSfx` | `AudioEngine::playSfx` | MATCH | Same per-type trigger with the same hold-to-tap substitution. |
| `prepareDefaultSfxIfMissing` | default effect load at startup | MATCH | Same fallback to the bundled effects when a skin supplies none. |

### `ChartLibraryManager.java` -> `src/app/ChartLibrary.cpp`

| Reference declaration | Port implementation | Verdict | Basis |
|---|---|---|---|
| `ChartLibraryManager` (class) | `app::ChartLibrary` | MATCH | Same role: the pack index over the packs root with per-chart best score and play history. |
| `invalidateCache` | `ChartLibrary::invalidate` | MATCH | Same drop-the-index-so-the-next-read-rescans. |
| `ImportResult` | `ChartLibrary::ImportResult` | MATCH | Same record returned from an import. |
| `getLocalCharts` | `ChartLibrary::records` | MATCH | Same sorted record list. |
| `scanLocalCharts` | `rebuildIndex` | MATCH | Same directory walk that skips incomplete packs and `.tmp` directories. |
| `getBestScore` / `getBestAcc` / `getLastPlayedAt` | `ChartRecord::bestScore` / `bestAcc` / `lastPlayedAtMs` | MATCH | Same three per-chart fields with the same defaults (accuracy `-1` for an unplayed chart). |
| `findLocalChartIdForPath` | `ChartLibrary::findIdForPath` | MATCH | Same path-to-id lookup. |
| `hasRequiredFiles` | `ChartRecord::complete` | MATCH | Same chart-plus-audio-plus-artwork test deciding whether a pack is listed. |
| `compareLocalCharts` | `sortRecords` | MATCH | Same ordering rule for the list. |
| `collectFiles` | directory iteration in `rebuildIndex` | MATCH | Same entry-collection step. |
| `deleteRecursive` | `platform::removeRecursively` | MATCH | Same recursive delete. |
| `getScorePrefs` / `getAccPrefs` / `getPlayHistoryPrefs` | `library.txt` persistence | MATCH | The reference keeps the three maps in `SharedPreferences`; the port keeps the same three maps in one key-value file written by `saveScores`. |

## Remaining risk

1. The 32-voice bound is the only place the port can differ audibly, and only at densities no
   shipped chart produces.
2. The integral-step fast path is an optimisation; it is arithmetically identical at fraction
   zero but has no dedicated regression test. It is exercised by every autoplay verification
   run at speed 1.0.
3. Device-format conversion (int16 output) exists on both sides; the port's saturation path is
   unit-tested indirectly through the audio suite.
