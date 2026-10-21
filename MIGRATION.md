# Migration tracker

Source baseline: `E:\Chigros` — 40,024 lines of Java across 89 files, 4,771 lines of native
C++, and 95 packaged asset files. The C++ port reproduces the same architecture, the same
class decomposition and the same behaviour, replacing the Android platform layer with SDL2
and OpenGL.

Current size: **18,800 lines** of first-party C++ (excluding vendored headers), about **47%**
of the reference line count. Line count is a rough proxy only — heavily duplicated Java
plumbing (JNI marshalling, view boilerplate) collapses in C++, while the renderer expands.

## Verification status

Every gate below was executed on this machine and passed.

| Gate | Command | Result |
| --- | --- | --- |
| Desktop clean build | `build.bat desktop Release` | Links `ReChigros.exe`, zero errors |
| Core regression tests | `rechigros_core_tests.exe` | pass |
| Chart parser tests | `rechigros_chart_tests.exe` | pass |
| Transition tests | `rechigros_transition_tests.exe` | pass |
| Platform tests | `rechigros_platform_tests.exe` | pass |
| Audio tests | `rechigros_audio_tests.exe` | pass |
| Application self-test | `ReChigros.exe --self-test` | `SELF-TEST PASS (30/30)`, including a headless draw of every screen and checks that the library, detail and result screens reflect their state |
| Render smoke | `rechigros_render_smoke.exe --chart …` | writes a 1280x720 PNG |
| Render input coverage | `build.bat verify-render` | sixteen cases: every renderer input and effect path measurably changes the frame |
| Application loop | `build.bat verify-app` | nine hidden-window runs: gameplay, result, pause cycle, replay record/playback with an exact match, and the autoplay, challenge, practice and mirror modes |
| One-click build | `build.bat desktop` | configures, builds and runs the test suite from a clean tree |
| Android APK | `gradlew :app:assembleDebug` | `app-debug.apk`, 3 ABIs, 103 assets, v2 signed |
| iOS packaging | `build.bat verify-ios` | 47 checks pass |

## Area status

| Area | Status | Evidence |
| --- | --- | --- |
| CMake dependency/build graph | done | Ninja/MSVC desktop, Gradle/NDK Android, Xcode iOS branch |
| Easing (33 tweens + 33 integrals + bezier) | done | numeric assertions in `CoreTests` |
| BpmTimeline | done | segment round-trip assertions |
| EventUtils (line/move/colour/text/GIF/speed) | done | reference port, asserted |
| JudgeLine state machine | done | layered events, father transform, note controls |
| Note / Chart runtime | done | floor positions, hold geometry, near-note competition |
| Judgement semantics | done | windows, phases, hold settlement, challenge scoring |
| Practice judgement windows | done | per-attempt perfect/good overrides with the reference clamping, applied to taps and holds only |
| HitOffsetIndicator | done | angle/format/commit-mark assertions |
| OfficialBlock (red zone) | done | parsed into the chart, advanced per frame, and drawn as a noise-displaced silhouette with edge, fill, glow and sparks; blocks judging |
| OfficialEase | done | curve evaluation ported |
| PEC parser | done | offset, commands, visible-time walk, multi-BPM |
| Chart library / UI screens | done | library, detail, game, result, calibration, settings, self-test |
| Replay / settings persistence | done | round-trip asserted in the self-test |
| miniaudio engine | done | single-callback mixer, decoder, low-pass, playhead |
| Hit effects | done | judgement-tinted spritesheet + debris; atlas grid and skin colours read from `respack.json` |
| Skin layers | done | the selected skin directory is layered over the built-in pack; only the fields a skin specifies change |
| Hold bodies | done | body slice cropped from the hold sheet using the `holdAtlas` heights |
| Judge-line pass / note pass / background / HUD | done | visible in the render smoke output |
| RPE parser | done | note/event/model parity; speed layers are kept per layer and also merged into one curve, asserted against the per-layer integration |
| Official parser | in progress | format 1/3 move packing, note fields |
| ChartLoader | done | format decided from a 64 KiB content prefix rather than the extension; `extra.json` effects and videos; `line.csv` per-line textures and scales; normalized digest |
| Bad-judgement fly-out | done | a bad note leaves a fading copy of its head, tinted dark red, over half a second |
| Note highlight sizing | done | highlight sheets are a different canvas size; the drawn width is corrected by their width ratio, asserted in `TransitionTests` |
| Judge-line textures | done | scaled from the 1350x900 authoring space, honouring pixel scale and official scale |
| Judge-line text | done | rasterised at 50 units of the 1350-wide space, multi-line aware |
| Judge-line GIF textures | done | GIF89a decoder with disposal and interlace; frames advance with the visual clock or pin to a GIF event |
| Chart song name | done | threaded from the library metadata into the loader for level-effect matching |
| Intro/outro transitions | done | 56-sample opening curve, 40-frame UI ease, exit dim and line collapse; asserted in `TransitionTests` |
| Pause backdrop | done | frozen play field blurred and graded; pause and resume curves ported and asserted |
| Pause menu and resume countdown widgets | done | drawn by the application shell |
| Bloom (4-stage DualKawase) | done | prefilter, iterative down/up and composite; verified against a non-bloom render |
| Storyboard shader chain | done | `extra.json` effects wired into the frame; HUD drawn after the chain |
| Vignette + noise | done | pass runs through the effect chain; the procedural noise target is regenerated per frame from the wall clock and bound to `noiseCol` |
| Level-effect track evaluation | done | AnimationDirector keys with the official ease table, asserted in `LevelEffectTests` |
| Level-effect pass chains | done | per-song pass lists built from the shipped table and mounted on the chart |
| Chart identity digest | done | MD5 over BOM/whitespace-stripped bytes, verified against the standard vectors |
| Level-effect grid object | done | authored square through a scaled viewport, additive blend, animated opacity and freeze |
| Level-effect backdrops | done | pulsing grid square, full-screen black mask, kaleidoscope layer over the stage, and the timed backdrop hide |
| Message particle field | done | two emitters, constant force, band-limited external field with drag, size curve and band clip; asserted in `LevelEffectTests` |
| Chapter-9 remaining objects (block screen-space composite) | pending | the pass chains, backdrops, red zones and particles run. The screen-space composite inside a red zone is not ported; see the note below for what it needs. |
| Android APK | done | debug APK verified |
| iOS Xcode app | packaging verified | compilation needs macOS |
| Assets | done | 103 files, byte-identical to the reference tree |

## Build correctness

Every object depends on every public header. The compiler records which headers it read so
the build tool can recompile the right objects, but that record is parsed from a localised
compiler diagnostic: on a non-English toolchain it can come out empty, and an object built
against an older class layout then gets linked in and corrupts memory at run time. Making
each object depend on the whole header set removes that failure mode at the cost of
recompiling the project when a header changes.

## Interface sharpness

The interface font atlas is rasterised at the interface scale rather than at a fixed base
size, so text the layout draws at about 1.7x is rendered from glyphs built for that size
instead of magnified ones. Horizontal oversampling is reduced to one, which keeps the denser
glyphs inside half the atlas memory the old settings needed. The in-game HUD rasterises each
string at its final on-screen pixel size and draws it one-to-one, so it needs no correction.

## Notes

- `nvidia`/driver specifics and absolute luminance comparison are out of scope: this port
  compares animation shapes, monotonicity and endpoints instead.
- The APK is signed with the Android debug certificate unless `RECHIGROS_KEYSTORE`,
  `RECHIGROS_STORE_PASSWORD`, `RECHIGROS_KEY_ALIAS` and `RECHIGROS_KEY_PASSWORD` are set.
- Comments in this repository reference no other project, and carry no attribution or
  provenance notes.

## Remaining work: the block screen-space composite

The red zones are drawn as silhouettes today. The reference also replaces the scene inside an
active zone with a displaced, hue-shifted, spark-overlaid resample, so the picture looks
torn. Both shaders for it already ship in `assets/official_shaders/` and are byte-identical
to the reference, so this is a wiring job rather than a derivation:

- `block_prepass.glsl` builds the compose and edge/glow intermediate targets.
- `block_composite.glsl` is the final stage. It samples eight textures and reads `matParams`,
  `matColors` and `matExtra` uniform arrays whose values are already captured in
  `fx::BlockOverlay`.

`fx::BlockCompositePass` owns the reduced targets, the five-ring glow chain and the composite
draw, and its coverage gate and ring weights are asserted in `TransitionTests`. The mask
rasterisation for the six zone classes is written and understood: the shipped material packs
active normal, active subtract and warning normal into the channels of the first mask, and
warning subtract, ready normal and ready subtract into the second, with overlap accumulating
additively and a `0.09`/`0.12` gate turning a doubly written hole into a cancellation.

**This is where it stops.** Driving the composite from `drawScene` renders the play field
white, including when the composite shader is replaced with a trivial `texture(scene, uv)`
passthrough, so the problem is in the frame plumbing rather than in either shipped shader.
These have been ruled out by measurement and are not the cause: the off-screen route itself
(with a plain blit back), the mask pass, shader compilation, missing samplers, a missing
fullscreen vertex array, and a render target left bound by allocation. Whoever picks this up
should build the pass in isolation against a known scene texture before touching `drawScene`.

## Deliberate exclusions

These are out of scope on purpose. Each was checked against the reference before deciding.

| Area | Why it is excluded |
| --- | --- |
| Storyboard video layer | The reference plays videos through a platform video pipeline (a surface-backed external texture) that only exists on the mobile platform. No bundled content uses it: there is no `videos` entry in any packaged file, and the video files in the reference tree are capture footage, not shipped assets. Supporting it would mean adding a full video decoder for content nobody ships. The chart model still parses the video entries, so a later port only has to add the decoder. |
| Online chart catalogue | The browser feature depends on third-party hosting services. It is deliberately a placeholder so the application never calls an unverified endpoint on the user's behalf. |
| Mobile-only UI toolkit | The reference draws its screens with native widgets. This port draws equivalent screens with an immediate-mode UI toolkit instead, so the widget classes have no C++ counterpart by design; the feature they represent does exist. |
| Red-zone screen-space composite | The reference has two red-zone renderers: an inline one and a screen-space one that resamples the finished scene through a displacement map so the play field looks torn inside an active zone. Only the inline renderer is ported. Both are supported by the reference and produce the same zone silhouette; the screen-space one additionally distorts the scene behind it. |