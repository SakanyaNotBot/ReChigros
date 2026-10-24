# Platform layer parity (`platform/**`, `src/platform/**`, window/event lifecycle)

Verified by the Lead. The reference targets Android only; the port targets Windows, Android and
iOS. Where the reference delegates to the Android framework, the port provides an equivalent
through the window/audio toolkit, so the acceptance criterion is *observable behaviour*, not
API shape.

Verdicts: **MATCH** (equivalent behaviour), **PORT-ONLY** (platform addition with no reference
counterpart), **DELIBERATE-EXCLUSION** (reason recorded).

## 1. Screen orientation

The interface is authored on a wide canvas, so both sides must refuse portrait.

| Aspect | Reference | Port before | Port now | Verdict |
| --- | --- | --- | --- | --- |
| Orientation declaration | every activity declares `android:screenOrientation="landscape"` (`AndroidManifest.xml`) | `sensorLandscape` on `MainActivity` | `landscape` | **FIXED** |
| Runtime re-request | reference never calls `setRequestedOrientation` (grep: 0 hits), so the manifest value always wins | the toolkit derives a requested orientation from the window size and the orientation hint while creating its surface (`setOrientationBis`), which resolves to `FULL_USER` when the window is resizable and no hint is registered; the activity then followed the system orientation and could open in portrait | `MainActivity.setOrientationBis` overrides the hook and always requests `SCREEN_ORIENTATION_LANDSCAPE` | **FIXED** |
| iOS | n/a | `Info.plist` already declared `UIRequiresFullScreen` and landscape-only `UISupportedInterfaceOrientations` | unchanged (already correct) | MATCH |
| iOS regression guard | n/a | — | `scripts/verify-ios-project.ps1` now also asserts that portrait is **not** offered | PORT-ONLY (guard) |

Verification: `aapt2 dump xmltree ... app-release.apk --file AndroidManifest.xml` reports
`screenOrientation(0x0101001e)=0` (`SCREEN_ORIENTATION_LANDSCAPE`), and the override is present
at `platform/android/app/src/main/java/com/wuying/chigros/MainActivity.java:46-49`.
No device was attached during this pass, so the on-device confirmation is outstanding; the
manifest value plus the suppressed runtime override are the two halves that decide it, and both
are now landscape-only. The reference behaves identically, so an emulator whose system is itself
in landscape is expected to look the same on both applications.

## 2. Window and surface lifecycle (`src/app/Application.cpp` ↔ activity + GL surface view)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Surface creation | `GLSurfaceView` with an ES 3.1 EGL context, RGB8888 + depth 16, optional 8/4/2× multisampling | single OpenGL 3.3 core context (desktop) / ES 3.0 (Android), created through the window toolkit | MATCH in effect; the port supersedes the reference's EGL config chooser because the toolkit owns context creation |
| Swap interval | deliberately left at the default; the reference's "unlimited frame rate" helper is an explicit no-op with a comment that unsynchronised frames are dropped by the compositor | default vsync requested, actual value reported (`present: swap interval requested=1 applied=1`) | MATCH |
| High refresh rate | `Surface.setFrameRate(rate, FRAME_RATE_COMPATIBILITY_DEFAULT, CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS)` from the surface callback | not exposed by the toolkit on Android; the port renders on the vsync signal, which tracks the display rate | DELIBERATE-EXCLUSION — the frame pacing is vsync-driven on both sides; the reference's call only asks the compositor to prefer a mode |
| Immersive mode | fullscreen flag + `SYSTEM_UI_FLAG_IMMERSIVE_STICKY` + `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES` | `SDL_WINDOW_FULLSCREEN` + the theme's `windowFullscreen`, re-applied on focus gain | MATCH in effect |
| Keep screen on | `FLAG_KEEP_SCREEN_ON` | not requested | DELIBERATE-EXCLUSION — a desktop session has no screen timeout; on Android the toolkit's window keeps the display awake while focused |
| Focus loss | `onWindowFocusChanged(false)` → request the pause menu, pause audio | `SDL_EVENT_WINDOW_FOCUS_LOST` → `gameFocusLost()` (opens the pause menu) and settle touches | MATCH |
| Background | `onPause` → request the pause menu, pause the GL view, pause audio | `SDL_EVENT_WILL_ENTER_BACKGROUND` → `gameFocusLost()`, suspend audio | MATCH |
| Foreground | `onResume` → restart the view, resume audio | `SDL_EVENT_DID_ENTER_FOREGROUND` → resume audio, reset the frame clock | MATCH |
| Back button | `OnBackPressedCallback`: pause menu closed → request it; open → finish | `SDLK_ESCAPE` opens the pause menu, pressing again resumes | MATCH in intent; the Android hardware back key maps to the same handler through the toolkit |
| Window resize | n/a (fixed display) | resize/pixel-size events update window and drawable metrics and resize the renderer | PORT-ONLY (desktop) |
| Frame capture | n/a | `--capture-frame` reads the back buffer and writes a PNG | PORT-ONLY (verification scaffold) |

## 3. Asset access (`src/platform/AssetManager.cpp`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Read from the package | `AssetManager.open()` | `AssetManager::readBinary` over the toolkit's asset layer | MATCH |
| Read as a file path | not needed (the framework opens assets directly) | `AssetManager::materialize` copies a packaged asset to a readable path when a dependency needs a real file (font loading, WAV decoding) | PORT-ONLY — required because the port's font and audio decoders take paths |
| Desktop | `assets/` next to the executable | same | MATCH in effect |

## 4. Archive extraction (`src/platform/Archive.cpp`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Zip reading | hand-written central-directory reader | miniz-backed reader | MATCH in effect; both produce the same entry bytes |
| Zip64 end-of-central-directory | throws "ZIP64 packages are not supported" | same rule | FIXED/owned by chart-parity (see `chart.md` §11) |
| Zip64 per-entry sentinel | resolves the real sizes from the 0x0001 extra field, throwing only when that field is missing | same rule | FIXED/owned by chart-parity |
| Path traversal | canonicalise and reject entries escaping the root | same (`FileSystem::sanitizeEntryName`) | MATCH |
| Entry count / size limits | bounded | bounded | MATCH |

## 5. File system abstraction (`src/platform/FileSystem.cpp`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Portable joins | n/a | `joinPortable` normalises separators | PORT-ONLY |
| Legacy text encodings | Java readers assume UTF-8 with a GBK fallback path in the pack reader | `decodeLegacyText` falls back to GBK / Latin-1 | MATCH in effect |
| Temporary directory | `FileUtils` uses the cache dir | used only where the reference does | MATCH |
| Device description | `Build.MODEL + " - Android " + Build.VERSION.RELEASE` | model + release read from the platform through a shared-library query; falls back to a plain platform name | MATCH in format, PORT-ONLY on desktop |

## 6. File pickers (`src/platform/FileDialog.cpp`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Chart pack picker | `ACTION_OPEN_DOCUMENT` / `ACTION_OPEN_DOCUMENT_TREE` | native open-file / open-folder dialog on desktop; the document picker via a platform callback on Android | MATCH in effect |
| Result delivery | the chosen document is copied into the cache and handed to the native side tagged with its kind | same tagging scheme (`chartzip`, `replay`, `skin`, `chartdir`) | MATCH |
| Persistable permission | `takePersistableUriPermission` | same call on Android | MATCH |
| Size/entry limits | 512 MB input, 1 GB unpacked, 4096 entries | same three limits | MATCH |

## 7. GIF textures (`src/platform/Gif.cpp`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Decoder | third-party animated-GIF drawable library | self-contained decoder producing per-frame RGBA | MATCH in effect |
| Frame timing | library-managed playback | the renderer advances frames from `visualTimeSec` | MATCH — verify per-frame delay handling matches the GIF's own timing, recorded as a remaining risk |

## 8. Android Java bridge (`platform/android/app/src/main/java/.../MainActivity.java`)

| Aspect | Reference | Port | Verdict |
| --- | --- | --- | --- |
| Launcher activity | `ChartSelectActivity`, `singleInstance`, landscape, VIEW intent filters for `content://` zip/octet-stream and `file://` `.cgrp` / `.zip` / `.pez` | single `MainActivity` with the same launch mode and orientation, VIEW filters for `content://` zip / json / text / octet-stream, and `file://` filters for `.cgrp`, `.zip` and `.pez` | **FIXED** — the three `file://` extension filters were missing, so a package opened from a file manager never reached the importer. The importer itself already treats any non-JSON document as an archive, so no code change was needed beyond the declaration. |
| Incoming document | resolved on a worker thread, tagged by kind, handed to the native side | identical scheme and identical tag strings | MATCH |
| Argument passthrough | none (the reference is driven by intents) | a single `args` extra is split into native arguments, used by build verification and captures | PORT-ONLY (verification) |
| Picker bridge | `startActivityForResult` from the activity | same, returning the tagged path through the file-drop callback | MATCH |

## Cross-area requests

None outstanding.

## Android device verification, second run (the reported defects)

Re-verified after the fixes landed, on the same emulator (Pixel 5 profile, Android 12,
`1080x2340`, density 440 — a portrait panel, so the forced-landscape path is exercised too).

| Check | Result | Evidence |
| --- | --- | --- |
| Latest APK installs and starts | PASS | `adb install -r` Success, window focused |
| Forced landscape | PASS | surface is `2340x1080` |
| Settings page renders | PASS | `evidence/android-v2-settings.png`,
four tabs and both sliders with their value readouts |
| **Drag from the middle of a row scrolls** (reported defect #2) | PASS | two middle-drags scrolled from
界面缩放 down to 谱面流速/判定线粗细 — `evidence/android-v2-scrolled.png` |
| **Switch reacts to a tap** (reported defect #1) | PASS | tapping 挑战模式 turned it off: the thumb slid back and the list
re-laid out (`evidence/android-v2-switch-off.png`) |
| **Checked mark is visible when on** (reported defect #1) | PASS | 挑战模式 and 低分辨率模式 both show a white
check on the dark track left of the thumb — `evidence/android-v2-switch-on.png` |
| Low-resolution preview appears when toggled | PASS | enabling 低分辨率模式 drew the reduced-resolution preview region |

The check sits on the track's left end, which is where the reference puts it: the thumb slides
right when the switch is on, leaving the mark on the dark track where it stays legible. The
earlier port drew it inside the thumb, so it vanished exactly when the switch was on.

### What could not be checked here

- **No chart pack is installed on the device**, so the play path (chart select → gameplay →
  result) was not exercised. `开始游玩` stays on the home screen because there is nothing to
  play; that is the expected behaviour with an empty library, not a defect.
- This is still an **emulator with SwiftShader software rendering**. Real-GPU shader
  compilation, high-refresh presentation and real touch latency remain unverified.

## Android device verification (first run)

Verified on an emulator because no physical device was attached. The emulator is a Pixel 5
profile: Android 12, `1080x2340`, density 440 — a **portrait** panel, which makes it a direct
test of the forced-landscape constraint.

| Check | Result | Evidence |
| --- | --- | --- |
| APK installs | PASS | `adb install` returned Success |
| Application starts | PASS | process alive, window focused |
| Forced landscape | PASS | `dumpsys display` reports `mCurrentOrientation=1`, surface is `2340x1080` |
| Home screen renders | PASS | `evidence/android-home.png` |
| Settings opens | PASS | `evidence/android-settings.png` |
| Drag from the middle of a row scrolls | PASS | two middle-drags scrolled the list — reported defect #2 |
| Switch toggles on tap | PASS | a tap on a switch row toggled it and re-laid out the list |
| Checked mark is visible | PASS | `evidence/android-switch-on.png` — white check on the dark track, reported defect #1 |

The checked mark sits on the track's left end, so it stays visible after the thumb slides
right. That is what the reference does, and what the port previously got wrong.

### What this does not cover

This is an **emulator** with SwiftShader software GL, not a physical device. It proves the
Android build, the platform layer, the input paths and the interface rendering. It does **not**
cover real-GPU shader compilation, high-refresh-rate presentation, or real touch latency. A
physical-device run is still outstanding.

## Remaining risk

1. **No device attached during this pass.** The orientation fix is verified from the packaged
   manifest (`screenOrientation=0`) and from the source of the override that suppresses the
   toolkit's runtime re-request, but not by installing the APK and screenshotting it. Re-running
   `scripts\check-android-render.ps1 -Serial 127.0.0.1:5555` with an emulator online closes this.
2. The frame-rate hint and screen-on flag are intentionally not reproduced (§2); both are
   compositor/system conveniences with no effect on the drawn picture.
3. GIF per-frame delay handling was not compared against the reference library's playback timing.
