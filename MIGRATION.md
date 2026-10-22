# Migration tracker

Source baseline: `E:\Chigros` — 40,024 lines of Java across 89 files, 4,771 lines of native
C++, and 95 packaged asset files. The C++ port reproduces the same architecture, the same
class decomposition and the same behaviour, replacing the Android platform layer with SDL2
and OpenGL.

Current size: about **25,000 lines** of first-party C++ (excluding vendored headers) against
40,024 lines of Java. Line count is a rough proxy only — heavily duplicated Java plumbing
(JNI marshalling, view boilerplate) collapses in C++, while the renderer expands.

> **Status accuracy.** The tables below were written before a full source comparison was
> run. `docs/COVERAGE_AUDIT.md` is now the authoritative account of functional parity: it
> lists every area as matches / partial / missing / differs with file and line evidence, and
> contradicts several rows below. Where the two disagree, the audit is correct. Interface
> parity is tracked separately in `docs/UI_PORTING_GUIDE.md`.

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
| Render input coverage | `build.bat verify-render` | eighteen cases: every renderer input and effect path measurably changes the frame |
| Application loop | `build.bat verify-app` | nine hidden-window runs: gameplay, result, pause cycle, replay record/playback with an exact match, and the autoplay, challenge, practice and mirror modes |
| Interface font coverage | `build.bat verify-app` | every one of the 233 non-Latin characters the screens can draw has a glyph in the shipped font |
| One-click build | `build.bat desktop` | configures, builds and runs the test suite from a clean tree |
| Android APK | `gradlew :app:assemble` | `app-release.apk`, 3 ABIs, v2 signed |
| Android on device | `scripts/check-android-render.ps1` | `ANDROID-RENDER PASS`: scene renderer initialises, every GLES shader compiles, in-app capture and system screenshot show the play field |
| GLES shader validation | `scripts/check-gles-shaders.ps1` | 35/35 shaders valid for GLSL ES 3.00 |
| Interface capture harness | `ReChigros.exe --screen <name> --window 1920x1080 --capture-frame …` | captures every screen at the reference resolution for comparison against `build/ref/` |
| Chapter-9 red-zone composite | `rechigros_block_probe.exe` + in-app capture | all intermediate targets dumped and verified; presented frame equals the off-screen composite; desktop tests pass |
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
| HitOffsetIndicator | done | angle/format/commit-mark assertions |
| OfficialBlock (red zone) | done | parsed into the chart, advanced per frame, and drawn as a noise-displaced silhouette with edge, fill, glow and sparks; blocks judging |
| OfficialEase | done | curve evaluation ported |
| PEC parser | done | offset, commands, visible-time walk, multi-BPM |
| Chart library / UI screens | done | library, detail, game, result, calibration, settings, self-test |
| Replay model | done | judgement entries (tap/drag/hold-press/hold-release/flick, keyed by note index) recorded from the judging path and re-applied on playback without synthesising touches, so a replay judges the same on any device and viewer settings stay display-only; timing metadata (early/late split, standard deviation, 19-bin offset histogram) saved and merged back for the settlement screen; legacy touch-sample files still read; asserted in `CoreTests` and `AppDataTests` |
| Settings persistence | done | key/value file; load-time clamps for every playback setting, including the stage aspect (default 16:9, 0.5..5.0 clamp, 0 = follow the window) |
| miniaudio engine | done | single-callback mixer, decoder, low-pass, playhead |
| Hit effects | done | judgement-tinted spritesheet + debris; atlas grid and skin colours read from `respack.json` |
| Skin layers | done | the selected skin directory is layered over the built-in pack; only the fields a skin specifies change |
| Hold bodies | done | body slice cropped from the hold sheet using the `holdAtlas` heights |
| Judge-line pass / note pass / background / HUD | done | visible in the render smoke output |
| RPE parser | done | note/event/model parity; speed layers are kept per layer and also merged into one curve, asserted against the per-layer integration |
| Official parser | done | format 1/3 move packing, block areas, note fields and event coverage; format-version normalisation asserted in `ChartParserTests` |
| Plain JSON chart schema | done | a JSON object with none of the identifying headers binds by field name and keeps `formatVersion` at 0, so hold geometry takes the non-official branch; no official-parser coercions (bpm clamp, `isCover`/`invertRotation` defaults, packed coordinates, block areas); asserted in `ChartParserTests` |
| `useAttachUiFix` from pack metadata | done | `info.yml`/`info.yaml`/`info.json` searched up to six levels above the chart and applied to the chart, nearest file wins; asserted in `ChartParserTests` |
| Hold body feedback | done | per-hold half-beat burst timer, one burst per interval at the position the hold occupies, rendered through the existing hit-effect pass; asserted in `CoreTests` and covered by a render-smoke capture |
| Difficulty label text | done | space runs collapse and become U+2002 EN SPACE, default label `SP Lv.?`; asserted in `AppDataTests` |
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
| Chapter-9 remaining objects (block screen-space composite) | done | masks rasterised at screen/8, edge/glow chain at screen/4, composite into the caller's target, full GL state save/restore; HUD joins the frame so it tears with the scene; probe and tests cover it. |
| Android APK | done | 3 ABIs, v2 signed, verified on the emulator with the device gate script |
| Interface foundation | done | per-size glyph rasteriser, design-canvas/density layout layer, exact skewed-panel and tab widgets, screen capture harness |
| Start screen | done | ported with the same geometry, typography and motion as the deployed interface |
| Other interface screens | in progress | chart list, settings, chart detail, settlement, gameplay/pause/practice HUD are being ported screen by screen against device captures |
| Practice judgement windows | done | the override now participates in classification, spatial falloff, per-type detection window and late-miss limit |
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

## Packaged builds

A build that ships its assets inside a package cannot open them as files, and three separate
things depend on that difference:

- **Images** are read through the asset layer and decoded from memory. Opening them by path
  silently yields nothing, which is how the character art, icons and covers all disappeared
  on device while working on the desktop.
- **The interface font** is materialised to a readable path before the atlas is built.
  Without that the atlas falls back to an ASCII-only font and every non-Latin character draws
  as a placeholder.
- **The example chart** is copied into the library on first run, because the library folder
  lives in private storage the package cannot address.

Importing uses the platform document picker, which grants access to the chosen files without
the application holding broad storage permission. A chosen folder is copied recursively
through the document resolver, since the grant covers the tree rather than a path.

## Notes

- `nvidia`/driver specifics and absolute luminance comparison are out of scope: this port
  compares animation shapes, monotonicity and endpoints instead.
- The APK is signed with the Android debug certificate unless `RECHIGROS_KEYSTORE`,
  `RECHIGROS_STORE_PASSWORD`, `RECHIGROS_KEY_ALIAS` and `RECHIGROS_KEY_PASSWORD` are set.
- Comments in this repository reference no other project, and carry no attribution or
  provenance notes.

## Chapter-9 red-zone screen-space composite

The composite is wired and verified. The defects that kept it from rendering were frame
plumbing, not shaders:

- the composite draw ran without re-binding the caller's framebuffer, so it wrote into the
  last intermediate target instead of the presented frame;
- no GL state survived the pass (blend, colour mask, viewport, scissor, depth);
- masks were allocated at full resolution and never rasterised, while both shipped shaders
  sample them as `screen / 8` point targets;
- `prepare()` lost the allocated size when rebuilding shaders;
- the stage rect was never handed to the pass.

The module now rasterises the six zone classes into two additive mask targets at `screen/8`,
builds the coverage and edge/glow chain at `screen/4`, saves and restores the full frame
state, and writes the composite into the caller's target. Frames with a visible zone take
the off-screen route; the storyboard chain runs first and is copied back, the HUD is drawn
into the finished frame (so it tears with the scene), and the pause re-render also
composites. `src/tools/block_probe.cpp` renders a synthetic scene through every stage and
asserts the frame state is restored and the presented frame matches the off-screen result.

## Deliberate exclusions

These are out of scope on purpose. Each was checked against the reference before deciding.

| Area | Why it is excluded |
| --- | --- |
| Storyboard video layer | The reference plays videos through a platform video pipeline (a surface-backed external texture) that only exists on the mobile platform. No bundled content uses it: there is no `videos` entry in any packaged file, and the video files in the reference tree are capture footage, not shipped assets. Supporting it would mean adding a full video decoder for content nobody ships. The chart model still parses the video entries, so a later port only has to add the decoder. |
| Online chart catalogue | The browser feature depends on third-party hosting services. It is deliberately a placeholder so the application never calls an unverified endpoint on the user's behalf. |
| Mobile-only UI toolkit | The reference draws its screens with native widgets. This port draws equivalent screens with an immediate-mode UI toolkit instead, so the widget classes have no C++ counterpart by design; the feature they represent does exist. |

## 第二轮反馈修复记录（游玩画面与资源包）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| 背景解析与模糊 | done | 游玩背景取自谱面插画，经 scene/BackgroundBlur 降采样 + 可分离模糊（对应参考 createBackgroundBlur），并按开场/结束的透明度曲线绘制；不再固定使用内置背景图 |
| 舞台与两侧模糊遮罩 | done | 舞台矩形按设置宽高比与 min(目标, 窗口) 规则计算；舞台不满屏时绘制黑隙 + 模糊插画侧栏 + 0.7 侧边压暗（对应 drawOfficialSideColumnsOverlay）；HUD 相对舞台布局 |
| 开场动画 | done | 1083 ms 音频缓冲、装饰判定线按采样曲线展开并保持 10 帧、真实判定线在进入游玩前隐藏、HUD 按 40 帧进入曲线 |
| 结尾动画 | done | 以实机参考帧为准：无舞台缩放；HUD 淡出后全屏模糊过渡到结算页，并隐藏非判定线元素 |
| 暂停 HUD | done | 暂停背景模糊包含 HUD 的冻结帧，模糊强度、计数数字（80 px em）与暂停图标位置按实机帧校准 |
| 自行添加的判定文字 | removed | 删除屏幕中央 PERFECT/GOOD/BAD/MISS 与底部 ±ms 文本（参考不存在） |
| 判定偏移指示器 | done | 扇形刻度盘（红/黄/绿区间、中心刻度、三层标记辉光、指针缓动、长按头标记），自动/回放时抑制，几何与参考手动游玩帧误差 ≤3 px |
| FPS 计数 | done | 移至参考位置（左下 "N FPS"，12 dp，0.5 透明底板） |
| 歌曲信息解析 | done | 与参考 11 字段、别名、类型强制、包检测顺序与回退一致；难度 EN SPACE 规范化；旧 GBK 文本回退解码（桌面） |
| 资源包（皮肤）配置 | done | SkinConfig 全字段解析/合并/校验，皮肤贴图优先回退内置资源，判定线颜色/透明度、hit_fx 缩放/时长/旋转/着色、粒子开关、长条样式生效，选择或导入后下一局应用 |
| 渲染性能 | done | 官方谱面解析并行化（680–870→302–350 ms）、prepareRuntime 并行 + 单调 floor 游标（6.5→2.6–3.3 ms）、note 时间窗遍历（4–6×）、红区窗口化推进（32×）+ 掩码顶点复用、会话红区遍历移除、缓存窗口查询；--frame-stats 诊断列；安静主机同状态设备：本端口 SF 4.99 ms / 单 tick 80% vs 参考 6.56–6.91 ms / 44–46%，上一版本 5.48 ms；帧 CPU 3.63 ms < 4.17 ms 预算 |
