# 交接文档

本文件面向接手本项目的下一个会话。它记录**当前真实状态**、**如何构建与调试**、**已知未完成事项**，以及踩过的坑。

> 原则：本文只写经过实测验证的结论。标注「已验证」的都有实际输出证据；标注「未验证」的请自行确认，不要当成事实。

---

## 零之二、逐函数判定已清零（本轮）

`docs/parity/coverage-matrix.md` 的未判定项现在是 **0**：

| 状态 | 数量 | 含义 |
|---|---|---|
| verified | 1525 | 报告判定为 MATCH |
| fixed | 196 | 报告判定为已修复 |
| differs | 0 | 无遗留差异 |
| excluded | 403 | 有理由的架构性排除 |
| matched-by-name | 0 | — |
| unmatched | 0 | — |

合计 2124 个参考声明，**每一个都有明确判定和依据**。重新生成为：

```
python tools/parity/coverage_matrix.py
python tools/parity/unadjudicated.py     # 输出应为 0
```

### 清零过程中发现的两类工具缺陷（已修）

这两条是踩过的坑，重跑判定前务必确认修复还在：

1. `coverage_matrix.py` 不认识 `DELIBERATE-EXCLUSION` / `DIFFERENT BY DESIGN` 两种判定写法，
   导致报告里写好的判定不被计入（一度虚高 183 项「未判定」）。
2. 同一个 `normalise()` 既用于声明名、又用于整行文本。它的 `split("::")[-1]` 对声明名是对的，
   对整行却是灾难：只要行内出现 `std::vector` 之类限定名，行首待判定的名字就被截掉
   （一度虚高 113 项）。现已有独立的 `normalise_row()`。

**教训**：工具在报「还剩 N 项」时，先验证工具本身的计数是否正确，再投入时间。

### 清零过程中修掉的真实缺陷（新增，均已提交）

| 缺陷 | 影响 |
|---|---|
| 掩码顶点索引当浮点索引用 | 第二个掩码的几何被整体丢弃，噪域触点光晕永远不可见 |
| 谱面帧间隔钳制为 0..0.1 | 参考是「非正或 >0.25 回退到 1/60」，移植版零间隔会让谱面停一帧、卡顿帧追赶不足 |
| 着色器源查找缺两层回退 | 谱面写 `/my_shader` 或 `vignette.glsl` 时静默丢失特效 |
| HUD 半宽未钳到 16:9 | 宽屏上分数/连击/曲名被推到参考不存在的位置 |
| fit-width 只迭代 3 次 | 收不敛，文字比参考小（字体「模糊」的真正原因之一） |
| 指示器模式越界用 clamp | 参考是越界归零，移植版把非法值变成「线形指示器」 |

## 零之三、本轮（2026-10-25）修复记录

本轮全部结论都以**实际运行/截图/ffprobe 数字**为准，不看「代码里写了什么」。

### 用户实测缺陷 → 根因 → 验证

| # | 现象 | 根因（实测定位） | 验证证据 |
|---|---|---|---|
| 1 | 长按谱面卡片无反应 | 卡片手势只有按下/拖动/抬起三段，没有按住计时；管理模式的唯一入口是右下角编辑牌 | 自检 52/52，含 `holding a library card enters manage mode and selects it`；三条反向对照证明断言会失败 |
| 1b | 卡片上半部分点击无效 | **既有缺陷**：命中检测把内容空间 Y 与屏幕空间行顶比较，两者只在 gridT==0 时相等 | 反向对照：恢复旧命中检测则 `library card tap opens the detail page` FAIL |
| 2 | 详情页进渲染页点开始渲染提示 no chart is loaded | 渲染页从不加载它描述的谱面 | 自检 4 条断言 + 交互路径渲染出 522 帧 |
| 3 | 点开始渲染报「管道正在被关闭」 | **默认输出目录是非 ASCII 路径，而所有交给编码器的路径都来自 path::string()**，Windows 上按 ANSI 代码页窄化，编码器打不开输出后立即退出 | 最小程序实证：.string() 时 SDL 打不开、u8string() 能打开；修复后 RECORD PASS frames=192 |
| 4 | 录制视频没有音乐和打击音 | 音轨只写了解码后的音乐；step() 里 takeSoundEvents() 的返回值被丢弃 | volumedetect：修复前无音频流，修复后 mean=-34.5dB / max=-9.1dB |
| 5 | 录制视频 Hold 无打击特效 | RecordSession::step() 从不取 takeHoldHeadBursts/takeHoldBodyBursts，交互路径取了 | 抽帧可见 hold 头部爆发特效 |
| 6 | 录制到歌曲结束就停，HUD 未退场完 | 时长少算了开场 lead（chartTime 减了 lead，但总帧数没加它） | 帧数 10384 增至 10449（+65 帧约 1.08 s = lead）；末帧 HUD 已消失 |
| 7 | 录制视频右侧有黑边 | 录制期间窗口 resize 会调用 renderer 的 resize()，把离屏目标按窗口尺寸重建，场景按窗口布局却按录制尺寸回读 | 非黑像素 bbox 从 1196x682（0.392）变为 1920x1080（1.000） |
| 8 | 输出目录显示乱码 | 同一 path::string() 问题，显示侧 | 截图显示目录正确 |
| 9 | COMBO 标签只能选固定四项 | 该行是循环切换的四选项 | 改为可输入文本框 |
| 10 | 「开始渲染」「返回」文字糊成马赛克 | 字体无粗体字面，合成粗体按整像素 2D 膨胀（取最大覆盖）；16 px 下约等于把笔画加粗三分之一，字腔被填平 | 实心像素占比 53.9% 升至 61.8%，软边 46.1% 降至 38.2% |

### 关键教训（本轮新增，务必记住）

1. **path::string() 在 Windows 上不是 UTF-8。** 任何要交给 SDL、子进程、JSON 或 archive 名字的
   窄字符串都必须走 platform::pathToUtf8()。反过来，fopen 要的是 ANSI 窄路径，那里必须用
   _wfopen(path.wstring())。**这两个方向相反**，混用会得到「能打开但写不进去」或「打不开」两种
   不同故障。
2. **离屏录制必须独占渲染尺寸。** 捕获期间任何 resize() 都会让场景布局与回读尺寸不一致，表现为
   视频里出现黑边而不是崩溃，很难从「画面看起来正常」发现。
3. **编码器音轨在开录前就要生成**（编码器先于第一帧启动），所以打击音的时刻表必须来自谱面本身，
   不能来自「帧回调里攒的事件」。
4. **合成粗体不要用整像素膨胀。** 界面字号 16-24 px 时 1 px 膨胀约为笔画的三分之一，笔画会粘连；
   应按覆盖率分数加粗。
5. **--record 这类批处理不能在 initialize() 里跑完整个编码**，否则窗口全程不响应，系统记为
   Application Hang。

### 录制功能验收命令

```powershell
.\build\desktop-release\src\app\ReChigros.exe --data-root build\capture-data `
  --screen render:Hate --record 'D:\无影\Videos\check.mp4'
& E:\ffmpeg\bin\ffprobe.exe -v error -show_entries stream=codec_type,duration `
  -of default=noprint_wrappers=1 'D:\无影\Videos\check.mp4'
```

## 零、接手第一件事

**工作树当前是干净的。** 第 5～10 轮成果已提交为 `77c4009`；本轮又追加了两个提交：

| 提交 | 内容 |
| --- | --- |
| `c5a2ce5` | 设置页开关的命中与勾选标记、高 DPI 字形物理像素映射、噪域合成三处修正 + 探针扩展 |
| `2bbea97` | RPE note type 语义、判定线速度初值、音符控制点边界对齐参考 |
| `3a01478` | 勾选标记改回参考行为、行上拖动可滚动、启动黑屏 4692→约 1300 ms |
| `2dcb06a` | 固化 ObjectivelyMVC 三件套源码并接入 CMake |
| `cb1736c` | MVC 运行时冒烟测试与工具链守卫 |
| `82b9781` | **方案 B：整树改用 WSL GCC 构建**（框架是 GNU C，MSVC 编不了） |
| `5b9e458` | **界面缩放设置项**，解决「手机/高分屏上界面太小看不清」 |

### 构建方式在 82b9781 之后分成了两套（重要）

| 用途 | 命令 | 说明 |
| --- | --- | --- |
| Windows 桌面（GL 渲染、界面、工具） | `build.bat desktop Release` | MSVC；`RECHIGROS_WITH_OMVC` 默认 OFF |
| 含 ObjectivelyMVC 的整树 | `wsl bash build/wsl-build.sh` | WSL GCC 13.3；首次先跑 `build/wsl-deps.sh` |
| 安卓 | `build.bat android Release` | NDK clang，不受影响 |

`build/wsl-deps.sh` 在无 root 的 WSL 里把 X11/xcb/ttf/image/alsa/pulse/gbm 的开发与运行时
文件解包到 `~/wsl-prefix`，脚本幂等可重跑。

**验证状态（重要）**：

| 项 | 结果 |
| --- | --- |
| 整树编译链接 | `EXIT=0` |
| 自检 / ctest | 37/37、8/8 |
| **MVC 运行时** | **`SMOKE PASS`，Vulkan 后端，RenderDevice / WindowController / Theme / View 全部就绪** |
| **MVC 真实出帧** | **`SMOKE PASS`**：像素回读 872 个差异像素、110 个字形覆盖像素 |

WSL 没有桌面显示，但 **`SDL_VIDEODRIVER=offscreen` 能创建 GPU 设备**，MVC 因此可以在
WSL 里真正初始化并出图。跑法：

```bash
wsl -e bash -lc 'cd /mnt/e/ReChigros; export LD_LIBRARY_PATH=$HOME/wsl-prefix/lib;   SDL_VIDEODRIVER=offscreen build/wsl-release/src/tools/rechigros_omvc_smoke'
```

注意四件事：

1. `SDL_VIDEODRIVER=dummy` **不行**（没有 GPU 后端），必须用 `offscreen`。
2. `RenderDevice::initWithWindow` 会自己认领窗口，调用方**不要**再调
   `SDL_ClaimWindowForGPUDevice`，否则报 "Window already claimed" 并在拆除时崩溃。
3. **`beginFrame` 之前必须先 `setFramebuffer`**，否则断言 "no framebuffer set" 直接崩。
   离屏渲染的正确用法是 `createFramebuffer` + `acquireCommandBuffer` +
   `controller renderTo:commands framebuffer:`，不要走 `beginFrame`（它只 resize
   窗口的呈现目标）。
4. 框架内部用 `GPU_Assert` 直接终止进程，**没有返回值可判断**，所以每次接线改动都要
   实际跑一遍冒烟程序，不能只看编译通过。

### 设置页迁移到 ObjectivelyMVC：骨架已就位

| 项 | 状态 |
| --- | --- |
| 框架三件套编译进应用 | **已完成**（WSL GCC 树 `BUILD=0`） |
| C / C++ 桥接分层 | **已完成**：`MvcSettings.c`（纯 C，含框架头）+ `MvcSettingsBridge.cpp`（只转发 C ABI） |
| 设置页结构（tabs / 行 / Checkbox） | **已实现并编译通过** |
| 像素级验证接入 | **已完成**：`rechigros_omvc_settings_probe` → `PROBE PASS`（8 行、543 亮像素） |

**关键设计**：框架头文件绝不能进 C++ 编译单元——它把 `interface` 当结构体成员名，和
Windows SDK 的同名宏冲突。所以分成 C 实现 + C++ 转发两层，C++ 侧只看到普通函数声明。

**踩到的坑（都已解决，记录避免重犯）**：
- `$(Class, method)` 只用于**实例**；类方法要用 `$$(Class, method)`。
- 三个框架静态库必须**全部显式链接**（`objectivelymvc objectivelygpu objectively`）。
  只写 `objectivelymvc` 时，`release` 等符号在链接期缺失。
- `MSVC` 树上 `RECHIGROS_WITH_OMVC` 默认 OFF，MVC 相关源文件条件编译，不影响 Windows 构建。
- **首帧构建条件不能只比 tab 号**：`tab` 默认 0、首次调用也传 0，`if (tab != current)` 恒假，
  `buildList` 永远不执行，页面空白但一切"成功"。要用独立的 `built` 标志。

**设置页探针**（`rechigros_omvc_settings_probe`）是这块的验收工具：

```bash
wsl -e bash -lc 'cd /mnt/e/ReChigros; export LD_LIBRARY_PATH=$HOME/wsl-prefix/lib; \
  SDL_VIDEODRIVER=offscreen build/wsl-release/src/tools/rechigros_omvc_settings_probe'
```

它打印行数、变化像素数、明亮像素数，三项都非零才 `PROBE PASS`。改动设置页后必须跑它。

### MVC 出图：已打通（这是接界面迁移的前提）

冒烟程序 `rechigros_omvc_smoke` **`SMOKE PASS`**，且是**像素级证明**：framebuffer 回读得到
`differing=872`（变化像素）、`glyphCoverage=110`（字形覆盖像素）。

关键教训：**一条什么都不画的渲染通道同样会"成功返回"**。所以这个冒烟程序不只检查返回值，
它把 framebuffer 回读出来数像素；两者都非零才算过。以后改动接线必须跑它。

接线必须和官方 `Examples/Hello.c` 一致，我是对照它才修好的：

| 我最初写错的 | 正确的 |
| --- | --- |
| 硬编码 `R8G8B8A8_UNORM` | `getSwapchainTextureFormat()` 取真实格式 |
| 硬编码 640×360 目标 | `SDL_GetWindowSizeInPixels()` 的实际尺寸 |
| 没有 depth attachment | 补 `D16_UNORM` |
| 自建 `renderTo` + 手动 submit | `beginFrame` → `controller render` → `endFrame` |

另外三个容易踩的点：

1. **主题会覆盖手工设置的 `View::backgroundColor`**，所以不能靠"设个背景色看有没有变"
   判断渲染是否生效；要数字形。
2. `Renderer::endFrame` 用 `LOADOP_LOAD` **不清屏**。
3. `View::draw` 断言 `self->window`，该指针由 `moveToWindow` 递归下发，构造时不会设置。

**给接手者的建议**：这个框架的绘制链路没有返回值、靠 `GPU_Assert` 直接终止进程，从零猜
代价很高。**先读 `build/omvc-probe/ObjectivelyMVC-main/Examples/Hello.c`**（随源码下载，本地
就有），按它接线，再用像素回读验证。

开工先跑 `git status --short` 确认干净，再按第九节验证环境。

之前出过两次把源文件压成一行的操作事故，其中一次靠 `git checkout` 恢复，
连带回退了整轮改动。**没有提交就等于没有保险。**

第 5～10 轮的成果摘要（详细证据见 `docs/parity/`）：

| 轮次 | 内容 |
| --- | --- |
| 5～6 | 六项用户反馈缺陷（导入/切页/详情卡顿、判定线出舞台、暂停状态机），列表首帧 2869ms → 5ms |
| 7 | 参考缺陷修复（皮肤颜色解析、偏移量舍入）、音乐倍速接线、绘制时钟 seek 重置 |
| 8 | 回放存储去重按内容比较、调试开关改为谱面几何 overlay |
| 9～10 | 渲染导出功能（离屏渲染 + H.264/AAC 编码 + 进度条 + 取消 + 命令行入口） |

---

## 一、项目概况

| 项 | 值 |
| --- | --- |
| 位置 | `E:\ReChigros` |
| 参考实现（只读） | `E:\Chigros`（Java/Android 原项目） |
| 参考素材（只读） | `E:\Chigros-Reference` |
| 语言/标准 | C++20 |
| 依赖 | SDL3 3.4.18、OpenGL 3.3 Core / GLES 3.0、miniaudio、Dear ImGui、nlohmann/json、stb、miniz |
| 构建 | CMake ≥3.15 + Ninja，`build.bat` 一键 |
| 首方代码量 | 约 33,600 行 C++（参考 Java 约 40,000 行） |
| Git | 主分支 `master`，最近提交见 `git log --oneline -1` |
| 文档 | `MIGRATION.md`（模块状态）、`docs/COVERAGE_AUDIT.md`（功能差距审计，已标注修复项）、`docs/UI_PORTING_GUIDE.md`（界面移植规范） |

**方向**：把 Java 音游逐功能移植为跨平台 C++。功能与逻辑不得低于原项目，界面与参考设备的视觉逐屏一致。

**状态摘要（2026-10-22 第四轮反馈收尾）**：

- 构建、八个测试套件、应用自检（30/30）、应用循环（11/11）、渲染矩阵（19 用例，含两个皮肤用例）、界面字形覆盖、文件导入检查（30/30）、红区合成探针：**全部通过**。
- 安卓游玩画面不呈现的问题：**已定位并修复，模拟器上已验证**（见第四节）；APK 已用当前源码重新构建并在雷电模拟器上复验。
- 第九章红区（噪域）屏幕空间合成：**已完成并通过独立探针验证**（见第四节）。
- 界面：主界面、列表/搜索/管理/导入、四个设置页、校准页、谱面详情、结算页、游玩 HUD、暂停、恢复倒计时、练习面板与判定面板均已按设备截图逐屏校准，元素位置基本在 0–3 px 内。
- **第二轮反馈的游玩画面修复**：背景改为按谱面插画解析并经过模糊层；舞台按设置宽高比计算，两侧为固定模糊侧栏（参考 `drawOfficialSideColumnsOverlay` 行为）；开场按参考节奏重排（1083 ms 音频缓冲、装饰判定线曲线、HUD 进入）；结尾按参考构建行为（无舞台缩放，HUD 淡出后全屏模糊过渡）；暂停背景模糊与计数数字（80 px em）按实机帧校准；**删除了自行添加的屏幕中央 "PERFECT/GOOD/BAD/MISS" 文字与底部 "±ms" 文字**，改为参考的判定偏移指示器（扇形刻度盘 + 指针）。FPS 读数移到参考位置。
- **歌曲信息与资源包**：`info.txt/yml/json` 解析按参考 `FieldInfo` 字段集、别名、类型强制与包检测顺序完全对齐（参考只有 11 个字段，未额外发明）；`SkinConfig` 全套字段（判定线/音符颜色、hit_fx 缩放/时长/旋转/着色、粒子、长条样式、贴图覆盖与回退顺序）已移植并生效，导入/选择皮肤后下一局即应用。旧编码（GBK）文本与压缩包内文件名在桌面端自动回退解码。
- **性能**：官方谱面解析并行化（稠密谱面 680–870 → 302–350 ms）、prepareRuntime 按行并行 + 单调 floor 游标（6.5 → 2.6–3.3 ms）、note 按类型时间窗遍历（4–6×）、红区推进窗口化（0.184 → 0.0057 ms/帧，32×）+ 掩码顶点复用、会话侧红区遍历移除、缓存红区窗口查询；--frame-stats 新增 GPU/swap/wall 列与 --swap-interval 诊断，空闲 gameplay 帧跳过 ImGui。安静主机同状态设备对比：端口呈现节奏 SF 均值 4.99 ms（p99 8.33，80% 落在 240 Hz 单 tick）vs 参考应用 6.56–6.91 ms（p99 10.81–12.50，44–46% 单 tick），上一版本 5.48 ms；帧内 CPU 3.63 ms（旧 3.79）在 4.17 ms 帧预算内。

**第四轮反馈修复（2026-10-22 晚，已提交 5ab0de / SDL3 迁移 11c8e08）**：

- **SDL2 → SDL3 迁移完成**：SDL3 3.4.18（官方发布包，滑哈值固定在 cmake/Dependencies.cmake），imgui 后端切到 imgui_impl_sdl3，Android 层改用 org.libsdl.app 与 libSDL3.so，Windows/Android/iOS 构建链路全部走通（iOS 仍需 macOS 生成 Xcode 工程）。
- **长按条打击特效**：与参考一致——条尾体爆发按参考计时器（每半拍一次、在当前位置），判定头特效改为在当时头部实际位置（提前击中在判定线上方），不再一律画在判定线上。
- **手动玩法卡顿**：根因是 HUD 数值每次命中都重新光栅化文字并上传贴图；改为字符集条带（按字号只光栅化一次）后，无头 tap 账测帧渲染 3.80/6.66/15.60 → **0.93/1.65/2.47 ms**，布局像素完全一致（max delta 0）；另取消了每帧图像集缓存的字符串分配与重复几何评估。
- **结算/暂停/游戏背景模糊**：游戏背景改用参考的 5 抽头 16 遍高斯（高频粒度 -25%）；暂停改为四级可缩放模糊金字塔（颗粒度 0.491 → 0.120，-76%）；结算背景改为 1/3 分辨率三次双轴盒式模糊（参考算法，块状纹理消失）。
- **谱面导入持久化**：Android 选择器/文件管理器打开的包现在以“种类标签”传给原生层，导入到应用自有 iles/app/packs（持久）、刷新列表并打开详情页（与参考一致），不再直接开始游戏；导入缓存副本在安装后清理。
- **交付物**：rtifacts/deliverables/ 下 windows/ReChigros-windows-x64.zip、ndroid/ReChigros-android.apk、ios/ReChigros-ios-source.zip + BUILD_IOS.md。
- **已知问题**：应用外部文件管理器“打开方式”启动谱面包时，模拟器的 libgui/SurfaceFlinger 在 SIGSEGV 上不稳定（纯启动 12/12 与应用内导入稳定；已尝试窗口标志、延迟导入均无效，可能需将导入移到工作线程）。
- 文件导入：谱面包（压缩包/文件夹/散文件）、回放、皮肤资源包三条流程已实现并按参考文案提示；`rechigros_import_check` 30 项检查通过。
- 功能：回放判定条目模型、无头 JSON 谱面按普通格式解析、`useAttachUiFix` 生效、长按条半拍爆发、难度文本 U+2002 规范化、实践判定窗口贯通、舞台宽高比默认与清洗——`docs/COVERAGE_AUDIT.md` 的 S1/S2 项已清零（余 S3 重构项 #13/#14 已在性能工作中大部分处理）。

---

## 二、环境与构建（重要）

### 工具链路径（本机固定，不在 PATH 上）

```
CMake:   D:\Studio-SDK\cmake\3.22.1\bin\cmake.exe
Ninja:   D:\Studio-SDK\cmake\3.22.1\bin\ninja.exe
MSVC:    C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat
Android: ANDROID_SDK_ROOT=D:\Studio-SDK，NDK 27.0.12077973，JDK 17
adb:     D:\Studio-SDK\platform-tools\adb.exe
Python:  C:\Users\无影\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe（含 Pillow，用于截图比对）
```

MSVC 需要在 `cmd /c "call vcvars64.bat && ..."` 里调用，无 vswhere。

### 常用命令

```powershell
cd E:\ReChigros

build.bat desktop Release          # 桌面构建 + 七个测试套件
build.bat verify-app               # 应用端到端验证（隐藏窗口主循环 + 字体覆盖）
build.bat verify-render            # 渲染矩阵（19 用例，含皮肤用例）
build.bat android Release          # 安卓 APK
build.bat verify-ios               # iOS 打包检查（不需要 macOS）
build\desktop-release\src\app\rechigros_import_check.exe   # 导入流程批处理检查
```

### 单个界面截图（界面移植的核心工具）

```powershell
build\desktop-release\src\app\ReChigros.exe `
  --data-root E:\ReChigros\build\capture-data --screen library `
  --window 1920x1080 --frames 60 --fixed-delta `
  --capture-frame E:\ReChigros\build\shots\cpp_library.png
```

- `--screen` 取值：`home`、`library`、`library-online`、`library-manage`、`settings-video/audio/replay/more`、`detail`、`result`、`calibration`、`game`、`game-pause`、`game-practice`、`game-practice-pause`、`game-countdown`。
- 可以带谱面选择器：`--screen detail:Message`、`--screen result:Hate`、`--screen game:Hate`（按曲名子串匹配）。
- `--data-root build/capture-data` 已预置与参考设备完全相同的 8 个谱面包（名称/难度/封面一致），用于逐卡片对比。
- `--fixed-delta` 让帧时钟固定为 1/60 秒，`--frames N` + `--capture-frame` 的截图可复现；`--capture-at-frame N` 指定截图发生在第 N 帧（游玩 HUD 要在开场动画之后截图时必用）；`--capture-time T` 固定 GPU 背景动画时间（主界面/设置页用 `--capture-time 4` 与参考截图对齐，其余界面先扫 0–30 秒找背景相位最接近的 T 再比较，否则整屏差异主要来自背景动画相位）；`--aspect 1.0` 让游玩界面按窄舞台渲染（复现两侧固定模糊侧栏）。
- `--frame-stats path.csv` 记录每帧的实际间隔与交换前 CPU 时间，并在结束时打印 `FRAME-STATS ... p95/p99/max over16/over33/over100` 摘要；桌面与安卓（intent args）都可用，是判断卡顿来源的主要手段。
- 截图后与 `build/ref/java_*.png`（参考设备 1920x1080 截图，索引见 `build/ref/README.md`）比较；动画类对比素材在 `build/ref-video/`（开场、暂停、倒计时、结算过渡、判定偏移指示器手动游玩、窄舞台侧栏，含逐帧 PNG 与 README 索引）；`build/tools/compare_screens.py` 输出总体/分区差异与对比拼图。

### 依赖下载

需要代理：`$env:HTTPS_PROXY='http://127.0.0.1:7899'`（以及 `HTTP_PROXY`）。CMake 首次配置会 FetchContent 下载 SDL3。

### 自建构建目录（并行工作时避免互相干扰）

```powershell
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='D:\Studio-SDK\cmake\3.22.1\bin\cmake.exe'
cmd /c "call `"$vcvars`" >nul 2>&1 && `"$cmake`" -S . -B build\myprobe -G Ninja -DCMAKE_BUILD_TYPE=Release -DRECHIGROS_SDL_SOURCE=E:/ReChigros/third_party/SDL3 && `"$cmake`" --build build\myprobe --parallel"
```

---

## 三、调试工具与手段（很有用，先读这节）

### 1. 界面截图与参考截图

- 参考设备截图：`build/ref/java_*.png`（1920x1080，索引与拍摄条件见 `build/ref/README.md`）。
- 本项目截图：用第二节的命令生成到 `build/shots/`。
- 比对：`build/tools/compare_screens.py REFERENCE.png CAPTURE.png --out build/shots/cmp.png --crop x0,y0,x1,y1`。
- 元素测量：`build/tools/measure.py`（按行/列扫描颜色段、阈值取文字墨迹包围盒）。实测坐标应与参考截图相差在 0–2px 内。

### 2. 渲染冒烟工具

```powershell
build\desktop-release\src\render\rechigros_render_smoke.exe `
  --chart tests\fixtures\plain.json --assets assets `
  --out build\out.png --frames 1 --time 1.0
```

支持 `--chart --out --assets --frames --width --height --time --skin --song --pause --pause-elapsed --dim --mirror --scroll --key-scale --force-bad --compare`。`--compare 另一张.png` 输出差异百分比。

### 3. 红区合成独立探针（改渲染前先跑它）

```powershell
build\desktop-release\src\tools\rechigros_block_probe.exe --assets assets --out-dir build\probe
```

它把红区合成的每个中间目标 dump 成 PNG，并断言帧状态复原、`presented == off-screen composite`。改 `fx::BlockCompositePass`/`GameRenderer` 的合成接线后必须重跑。

### 4. 应用隐藏窗口批量运行

```powershell
build\desktop-release\src\app\ReChigros.exe tests\fixtures\plain.json --frames 900
# --expect-result / --expect-pause / --record-replay / --replay-playback
# --autoplay / --practice / --challenge / --mirror
# --mistime <ms> / --practice-perfect <ms> / --practice-good <ms>
# --capture-frame <png> / --gl-debug
```

### 5. 安卓设备

模拟器（雷电）常驻：`127.0.0.1:5555`；参考应用包名 `com.wuying.phigros`（**不要卸载/清数据**），本项目包名 `com.wuying.chigros`。

```powershell
$adb='D:\Studio-SDK\platform-tools\adb.exe'
& $adb -s 127.0.0.1:5555 install -r 'E:\ReChigros\platform\android\app\build\outputs\apk\release\app-release.apk'
& $adb -s 127.0.0.1:5555 shell am force-stop com.wuying.chigros
& $adb -s 127.0.0.1:5555 shell am start -n com.wuying.chigros/.MainActivity
& $adb -s 127.0.0.1:5555 shell screencap -p /sdcard/s.png
& $adb -s 127.0.0.1:5555 pull /sdcard/s.png E:\ReChigros\build\s.png
```

- 截图 1920x1080，adb 坐标直接用（不要换算）；点击用 `input swipe x y x y 150` 制造按压时长。
- 设备端门禁：`scripts\check-android-render.ps1 -Serial 127.0.0.1:5555`（安装当前 APK、直接进游玩、拉取应用内截图与系统截图、失败即报错）。
- GLSL ES 静态校验：`scripts\check-gles-shaders.ps1`。
- 应用内截图：`MainActivity` 支持启动参数 `--es args "--frames 300 --autoplay --capture-frame <path> <chart.json>"`（见 `platform/android/.../MainActivity.java` 的 `getArguments` 覆盖）。

### 6. 参考应用私有数据（截图对齐用）

`adb exec-out run-as com.wuying.phigros tar -cf - files` 可导出参考应用的谱面库/设置。注意：`adb shell` 的 stdout 重定向在 PowerShell 会破坏二进制，必须用 `cmd /c "... > file"`；向应用私有目录写文件要用 `adb shell run-as <pkg> sh -c "cat > /data/data/<pkg>/shared_prefs/xxx.xml"` 配合 stdin 管道（SELinux 不允许 app 读 `/data/local/tmp`）。

---

## 四、已完成并有验证证据

### 构建与测试

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 桌面构建 | 完成 | 干净构建 0 错误 0 警告 |
| 八个测试套件 | 通过 | `ctest` 8/8（core/chart/audio/platform/transition/level_effect/app_data/skin） |
| 应用自检 | `SELF-TEST PASS` | 含每屏无头绘制 |
| 渲染矩阵 | **26 用例**通过 | `build.bat verify-render` |
| 应用循环 | 11 项通过（含 pause-cycle） | `build.bat verify-app` |
| 字体覆盖 | 233 个非 ASCII 字符全有字形 | 已接入 `verify-app` |
| 界面截图工具 | 完成 | 每个界面可在 1920x1080 下确定性截图 |
| GLSL ES 校验 | **43/43** 通过 | `scripts/check-gles-shaders.ps1` |
| 安卓设备渲染门禁 | 通过（BlueStacks 2400x1080） | APK 安装运行、横屏、intent-filter 均已验 |
| 红区合成探针 | 通过 | 帧状态复原、presented == composite |
| iOS 打包 | **48 项**检查通过 | 编译需 macOS |
| 录像编码器 | 通过 | `rechigros_record_check.exe`；端到端 10360 帧 + AAC 音轨 |

### 本次会话修复的关键缺陷

| 缺陷 | 根因 | 修复 |
| --- | --- | --- |
| 安卓游玩画面完全不显示（只剩上层 ImGui 提示） | ① `TextRenderer::initialize` 用文件路径打开 `res/font.ttf`，APK 内资源不是文件 → 渲染器初始化失败、`render()` 直接返回，画面停留在上一帧；② GLSL ES 片元着色器没有默认浮点精度，凡自带 `out vec4` 的着色器全部编译失败（约 30/35 个），所有绘制静默失效 | 字体改走 `AssetManager::materialize`；着色器归一化时无条件注入 precision 块；着色器编译失败改为可诊断日志 |
| 第九章红区只画静态剪影 | 合成的帧流程缺陷：合成绘制前未重新绑定调用方 framebuffer、GL 状态未保存/恢复、掩码按全分辨率分配且从未栅格化（着色器按 screen/8 采样）、`prepare()` 丢失已分配尺寸、stage rect 从未下发 | 重写 `BlockCompositePass` + `BlockOverlay::collectMaskQuads`，`GameRenderer` 中完成接线；独立探针验证 |
| 界面文字模糊、尺寸与参考不一致 | 文字尺寸直接乘布局缩放（应乘显示密度）；项目字体图集固定尺寸放大 | 新增按最终像素尺寸栅格化的字形缓存（`UiText.*`）；布局/文字/面板/动画统一到 `UiLayout.*` |
| 主界面几何与参考不符 | 面板右边缘应为斜边而非直边、标题字号偏大 1.5 倍、角色图水平偏移、缺少 GitHub 图标资源 | 逐项按参考源码与设备截图修正；主界面元素墨迹框已与参考对齐到 1px |
| 练习模式判定窗口无效 | `JudgementEngine` 的 practice 覆盖从未被判定路径使用（会话自己用原始窗口比较） | 判定、空间衰减、检测窗口、late-miss 限制统一走 practice-aware 窗口 |
| RPE 解析丢失 | 颜色对象写法、组合 `scaleEvents`、move 别名、纯文字事件线颜色 | 均已按参考实现补齐 |

### 功能模块

下列模块的**主体功能已在位并通过门禁**，但「能跑」不等于「逐函数对等」——
逐项判定进度见第五节：

- 谱面解析：PEC / RPE / 官方格式，格式由内容前缀判断而非扩展名
- 判定引擎：窗口、相位、长条结算、挑战模式计分、练习模式窗口覆盖
- 渲染器：判定线、音符、长条、打击特效、HUD、暂停背景、bloom、故事板着色器链、章节 9 特效（含红区屏幕空间合成）
- 音频：miniaudio 单回调混音、解码、低通、播放位置
- UI 基础设施：内置字体的逐字号栅格化、731x415 设计画布 + 密度单位换算、斜切面板/标签栏/滑块等控件、逐屏截图工具
- 主界面
- 回放与设置持久化
- 打空音符飞散残留

---

## 五、未完成事项

### 1. 逐函数比对（主线任务，工作量最大）

这是整个任务的核心目标，**远未完成**。

`docs/parity/coverage-matrix.md`：参考共 **2312** 个声明

| 状态 | 数量 | 含义 |
| --- | --- | --- |
| verified | 298 | 报告明确判定 MATCH |
| fixed | 165 | 报告记录已修复 |
| excluded | 3 | 有意排除，理由已记录 |
| matched-by-name | 188 | 存在同名符号（**不等于行为一致**） |
| **unmatched** | **1470** | 无同名符号，需人工判定 |

**不要把 1470 当成"已完成"或"自动证明对等"。它是工作清单。**

已被证伪的旧说法：`MIGRATION.md` 与旧 `COVERAGE_AUDIT.md` 声称「S1/S2 已清零、
基本完成」，`docs/parity/REPORT.md` §7 已逐条推翻——实际在谱面解析、核心模型、
判定、渲染、界面、平台层都发现了玩家可见缺陷。

**有效做法**（已被证明可行，见 `docs/parity/gameplay.md` 与 `judging.md`）：

1. 不要做名称比对。移植版命名风格差异大，`tools/parity/method_crossref.py`
   只能匹配 68/544，噪声太大。
2. 按**行为单元**核对：读参考实现 → 判断它想要什么 → 对照移植版 → 写结论 + 双侧 file:line 进对应报告。
3. 已核对通过的范例可直接引用（不必重做）：
   - `HitOffsetIndicator` 全算法（常量、缓动、舍入、commit mark）
   - 分数追赶 `approachDisplayValue`
   - 多押高亮宽度/UV
   - 谱面揭秘相机（0.20/0.30/1.0s）
   - 舞台 letterbox 规则
   - 低分辨率/抗锯齿尺寸
4. 数值常量已系统性扫过：`tools/parity/constant_values.py` 比对 188 个数值常量，
   只有 1 个不在移植版（`HOLD_DESTROY_DELAY`，参考里声明后从未被读取）。

### 2. 噪域打击特效的扭曲（用户反馈第 6 项，已修，待真机复核）

用户原话：「噪域上好像没有打击特效及其粒子的扭曲图案？参考项目可不是这样的」。

已修复三处与参考不一致的行为，证据见 `docs/parity/game_renderer_residual.md`
的 "Follow-up: zone hit-effect distortion" 一节：

1. glow ping-pong 目标由双线性改为点采样（参考 `GameRenderer.java:15011-15034`）。
2. 预处理结束后同时恢复 blend 的开启和关闭状态（`15597-15599`、`15665-15673`）。
3. 合成使用调用方原始 viewport，不再强制覆盖全屏（`15562-15567`、`15711-15714`）。

独立探针已扩展为真实亮核 + 偏移粒子场景并全部通过：预处理后 blend 正确恢复为关闭，
上屏与离屏合成差异 0.000000%，扭曲区内可见粒子被重采样。**仍需真机同帧对照**，
因为点采样与双线性的差别正是稀疏渲染矩阵分辨不出来的部分。

仍未接线：触摸 hover 的动态 uniform（移植版恒为 `touchCount=0`、
`touchShineValue=0`，参考在 `15246-15321`、`15807-15814` 上传实时触点）。

### 3. 渲染页的路径输入

输出目录仍是点击弹原生目录选择器，不能在页面内直接输入路径。文件名已支持输入。

### 3b. 界面尺寸与清晰度（界面缩放，已完成并提交）

用户原话：「页面缩放不合适，在手机上这种缩放很容易看不清，在电脑高分辨屏幕上也容易看不清」。

根因已量化：界面密度是常量 `kDensityRatio = 1.75 / (1080/415)`，等于**参考设备**的密度，
与真实设备无关。所以任何屏幕上都只有「参考设备那一种物理大小」。参考图 1920x1080 下设置页
一屏 4 行，而移植版同一台手机上是 7 行，看起来就是整体缩小、文字变细。

修复：新增 `Settings::uiScale`（0.85–2.0，默认 1.0），在设置页「画面」分组第一项以
「界面缩放」滑块暴露；`Canvas::dp()` 乘以该系数，所以整屏行高、字号、控件一起放大，
行内比例不变。已截图验证：**1.0 时每行约 190 px，1.6 时约 300 px**。

刻意保留 `kDensityRatio` 的参考值作为 1.0 基线，这样 1.0 下的截图仍可与参考逐像素比对。

**试过但已回退**：给字形覆盖率加 gamma（0.72）以提亮笔画。实测参考图有 4136 个接近纯白
的像素、移植版只有 727 个（5.7 倍差距）说明确实偏淡，但 gamma 校正后只从 727 变到 728，
证明笔画偏淡的主因是**字号本身偏小**而不是覆盖率曲线。该改动已完整回退，未留在代码里。

### 3a. 设置页本轮修复（已完成，有回归）

- **开关不可点击**：滚动分派先于控件消费按压，而基线未把开关整行登记进
  `controlRects`，于是按压被判成空白拖动。现在整行已登记（`SettingsScreen.cpp`，
  `drawSwitch` 末行），点击/行外释放/拖动三条路径都有自检。
- **开启态没有 ✓**：参考用三段矢量路径绘制（`SettingsToggleView.java:47-71`），
  移植版此前完全缺失。现按参考的三点坐标绘制，并随 `thumbX` 移动、随进度淡入。
  这里**故意不照抄参考的缺陷**：参考的白色勾画在白色滑块上且固定在轨道左端，完全
  开启时不可见；移植版改用轨道深色并跟随滑块（REPORT §4a 已记录）。
- **字体更糊**：定位为高 DPI 下界面按逻辑像素栅格化字形、再被 framebuffer 放大。
  `UiLayout.cpp` 现在按 `DisplayFramebufferScale` 放大后的物理像素栅格化，再换算回
  逻辑单位，并在设备像素网格上对齐；布局与命中坐标不变。自检锁定该换算，
  **但真机 1.25x/2x 输出的实际观感仍需真机截图确认**。

### 4. 平台验证缺口（本轮状态）

本轮重新构建了安卓 APK（`platform/android/app/build/outputs/apk/release/app-release.apk`，含本轮
全部改动，三种 ABI 的 `libSDL3.so`/`libmain.so`、dex 与 assets 齐全，manifest 仍为
`screenOrientation="landscape"`）。**但那只是编译和打包验证，不是真机运行验证**：
本轮 `adb devices` 始终为空，没有可用模拟器或真机，因此高 DPI 字体映射、噪域点采样
差异、设置页开关在触屏上的手感这三项都**没有设备证据**，不要当成已验。

### 4a. 平台验证缺口

- **Android**：APK 的横屏与 intent-filter 已从打包产物验证，也在 BlueStacks 上装过。
  但真机（非高通 GPU / mediump 回退路径）未验证。模拟器当前未启动
  （`adb devices` 为空），需先开 BlueStacks 再跑
  `scripts\check-android-render.ps1 -Serial 127.0.0.1:5555`。
- **iOS**：只做打包结构检查（48 项），**从未编译**（需要 macOS）。
- **macOS / Linux**：源码层面支持，未实编译。

### 5. 结尾过渡时序

参考在 HUD 退出后有约 2 秒整屏模糊过渡，端口目前是 HUD 退出后直接进结算页入场。
结算页背景本身是模糊插画，但缺少中间阶段。

### 6. 性能

设备安静 A/B 无 >33ms 帧。与参考同机的呈现节奏仍有约 10% 差距
（端口 5.81ms vs 参考 5.22ms），帧内 CPU 已低于帧间隔，剩余更像呈现/合成/GPU 路径。
下一轮候选见 `build/agent-core/perf-report.md`。

**注意**：第 5～10 轮改了渲染与界面路径（异步贴图、scissor、暂停四态、
遮盖层），性能数字需要重测后再引用。

---

## 六、明确排除的功能（已论证，不要实现）

见 `MIGRATION.md` 的「Deliberate exclusions」：

| 项 | 排除理由 |
| --- | --- |
| 故事板视频层 | 参考用平台视频管线（仅移动端有），且无任何随包内容使用 |
| 在线谱面目录 | 依赖第三方托管服务，故意做成占位符 |
| 移动端原生 UI 工具包 | 本移植用 ImGui 绘制等效界面 |

---

## 七、踩坑清单（务必读）

### 构建 / 多会话并行类

1. **改头文件必须整库重建**。虽然 CMake 已做头文件依赖，但改成员布局时稳妥做法是删掉构建目录重配。历史上有多次"莫名其妙运行时崩溃"由此而来。
2. **多会话并行时共享源码树会短暂不可链接**：别的会话正在写某个 `.cpp` 时链接会报该文件的符号缺失。这通常是暂时的，等对方写入完成再构建；不要为此去改别人的文件。
3. **每个会话用自己的构建目录**（`build/<名字>`），不要同时写 `build/desktop-release`。
4. **`build.bat android` 曾把 Gradle 命令发到仓库根目录**（已修，用 `-p` 指定项目目录）。
5. **Android 依赖 androidx 不可用**，`MainActivity` 用 `startActivityForResult`。

### 界面 / 渲染类

6. **界面坐标有两套单位**：布局用 731x415 设计画布 × `min(w/731,h/415)`；文字与 `dp()` 用显示密度（参考设备 1920x1080/280dpi → 1.75）。在 1080p 下 `1dp = 0.67246` 设计单位。把布局轴上的数字误乘密度（或反之）会造成 1.5 倍级别的错位——主界面角色图就踩过这个坑（`14*scale` 是布局单位，不是 `dp(14)`）。
7. **文字尺寸不能乘布局缩放后直接画**：需要按最终像素尺寸栅格化（`ui::drawLabel` 已处理）。ImGui 内建字体只用于文本输入等控件，界面正文不要用它。
8. **参考设备实际渲染的文字常比源码数字推算的小**（主界面"开始游玩"标签实际是 28 密度单位而非源码文本对应的 38；结算/详情的 XML 字号同样偏小）。界面移植的判定标准是**参考截图上的墨迹包围盒**，不是源码数字×密度。
9. **安卓 APK 内资源不是文件**：任何读资源的地方都必须走 `AssetManager`（`readBinary`/`materialize`），不能 `fopen`/`stbi_load` 路径。
10. **GLSL ES 片元着色器没有默认 float 精度**：自带 `out vec4` 的片元着色器必须在 `#version 300 es` 后注入 `precision highp/mediump float;`，否则整段程序编译失败且绘制静默无效。改着色器统一走 `ShaderProgram` 的归一化，不要手写版本行。
11. **`Framebuffer::resize()` 遗留绑定会污染后续绘制**；离屏 pass 结束要恢复调用方 framebuffer、viewport、blend、colour mask、scissor、depth。
12. **红区掩码按 `screen/8` 点采样**，掩码目标必须按该分辨率分配，否则覆盖率恒为 0。
13. **`AddFontFromFileTTF` 的 GlyphRanges 只存指针**，数组必须活到图集构建完成（用 `static`）。
14. **ImGui 字体范围**：`GetGlyphRangesChineseSimplifiedCommon()` 约 2500 字；缺字手动追加码点，不要用 Full。

### 安卓调试类

15. **adb 截图为 1920x1080，输入坐标直接用**；`input tap` 太快会丢按压，用 `input swipe x y x y 150`。
16. **PowerShell 的 `>` 重定向会破坏 adb 输出的二进制**（导出 tar/截图必须 `cmd /c`）。
17. **SELinux 不允许应用读 `/data/local/tmp`**；写应用私有文件要用 stdin 管道进 `run-as ... sh -c "cat > ..."`。
18. 参考应用是"视觉基准"：可以改它的设置（shared_prefs）来暴露不同界面，但**不要卸载/清数据**。

### 测试类

19. **渲染矩阵采样稀疏**（每 5 行、每 20 字节），只能判断"有无差异"，不能验证数值精度。精度类断言应写成单元测试。
20. **单点采样可能撞上巧合**（两个用例在某时刻位置正好重合），多时间点验证。
21. **`std::filesystem::temp_directory_path()` 在非 ASCII 用户名下抛异常**（本机用户名是 `无影`），测试里用 `current_path()` 或带 `error_code` 的重载。

### 编辑与工具类（第 5～10 轮新增，代价最大）

22. **绝对不要用 PowerShell 做代码行切片。** 曾用
    `Get-Content` + 数组切片 + `Set-Content` 删函数，把 `GameScreen.cpp`（1900 行）
    **压成 1 行**。改代码一律用 edit 工具的精确字面替换，不要用：
    - `Get-Content`/`Set-Content` 配行号范围
    - `-replace` 批量替换代码
    - `clang-format` 去"修复"已损坏的文件（它会把 `#include` 塞进行内，更糟）

    万一换行丢失，`tools/reline.py` 可按 token 边界安全重排。
23. **从 bash 调 PowerShell 脚本会假失败。** PowerShell 脚本里的 stderr 输出会被
    包成 `NativeCommandError`，触发 `$ErrorActionPreference='Stop'`。一律用
    `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\xxx.ps1` 直接调。
24. **ffmpeg 的 stderr 不能接管道。** 录像时推帧期间没人在读，编码器写满自己的错误
    管道后停止接收帧，报「管道正在被关闭」。必须写文件、结束时再读。
25. **`writeFrame` 这类写循环改完必须核对花括号完整。** 曾漏掉 while 的闭合括号，
    导致每帧只写第一个 1MB 分片，剩余数据从未发出——表现和上一条一模一样。
26. **参考项目本身有缺陷，不要照抄。** 用户明确要求发现缺陷要修。已修的记录在
    `docs/parity/REPORT.md` §4a（皮肤颜色解析、偏移量舍入）。判断标准：先问
    「参考这个行为是不是它想要的」，再决定照抄还是修正。
27. **注释不得出现其他项目名。** Chigros / Phigros / Java / Android / ported from
    一律不许；也不要写「已遵守注释规则」这类元说明。注释解释**为什么**，不解释**是什么**。
28. **横竖屏是强制横屏，不要改。** 参考所有 activity 都是 `landscape`。已在
    Android manifest + `Activity.setOrientationBis` 覆盖、iOS plist 双横屏 +
    `UIRequiresFullScreen`。**绝不要改成竖屏或跟随系统。**
29. **构建并发上限 2。** 16GB 内存跑 4 个并发构建把宿主搞崩过一次。
    `C1060` / `mspdbcore.dll` / `LLVM ERROR: out of memory` / `.obj Permission denied`
    都是资源耗尽或并发冲突，等 60 秒串行重试，不是代码错误。

---

## 八、关键文件索引

| 路径 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 顶层构建，含头文件依赖保障 |
| `scripts/build.ps1` | 一键构建脚本主体 |
| `scripts/check-gles-shaders.ps1` | GLSL ES 静态校验（43 个着色器） |
| `scripts/check-android-render.ps1` | 安卓设备渲染门禁 |
| `scripts/verify-app-loop.ps1` | 应用端到端验证 |
| `scripts/verify-render-matrix.ps1` | 渲染矩阵 |
| `scripts/check-font-coverage.py` | 字体覆盖检查 |
| `cmake/Dependencies.cmake` | 依赖拉取 |
| `src/app/Application.cpp` | 主循环、事件、ImGui 初始化、帧捕获、`--screen` 截图入口 |
| `src/app/CaptureHarness.cpp` | 每个界面的代表状态与参考设置 |
| `src/app/UiText.cpp` | 逐字号字形栅格化缓存（界面文字清晰度的关键） |
| `src/app/UiLayout.cpp` | 设计画布/密度换算、斜切面板、文字排版、标签栏、动画插值 |
| `src/app/CommonScreens.cpp` | 主界面、toast、导航 |
| `src/app/LibraryScreen.cpp` / `SettingsScreen.cpp` / `DetailScreen.cpp` / `ResultScreen.cpp` / `CalibrationScreen.cpp` / `GameScreen.cpp` | 各界面 |
| `src/render/GameRenderer.cpp` | 渲染总调度 `drawScene` 与帧流程 |
| `src/render/scene/HudPass.cpp` | 游玩 HUD 绘制 |
| `src/render/fx/BlockCompositePass.cpp` | 红区屏幕空间合成 |
| `src/tools/block_probe.cpp` | 红区合成独立探针 |
| `src/core/JudgementEngine.cpp` | 判定、窗口、练习覆盖 |
| `src/core/JudgeLine.cpp` | 判定线状态机 |
| `src/chart/` | 三种格式解析器 |
| `src/platform/AssetManager.cpp` | 打包资源读取 |
| `platform/android/app/src/main/java/com/wuying/chigros/MainActivity.java` | 安卓入口、导入、选择器桥接、启动参数 |
| `src/app/SmoothingClock.cpp` | 绘制时钟（60 帧 bias 滚动平均，±10ms 钳制） |
| `src/app/RecordSession.cpp` | 录像会话：逐帧离屏渲染 + 编码驱动 + 进度/取消 |
| `src/app/VideoEncoder.cpp` | ffmpeg 管道封装、音轨第二路输入、诊断日志 |
| `src/app/RenderScreen.cpp` | 渲染设置页 + 进度条遮盖层 |
| `src/render/scene/ChartDebugOverlay.cpp` | 谱面调试 overlay（判定线/note 位置标记） |
| `tools/parity/constant_values.py` | 数值常量系统性核对（188 项） |
| `tools/parity/coverage_matrix.py` | 重建覆盖矩阵 |
| `tools/reline.py` | 换行丢失时的安全恢复工具 |
| `docs/parity/REPORT.md` | **权威总报告**（取代旧审计与 MIGRATION 的结论） |
| `docs/parity/*.md` | 分领域逐项证据（chart/core/judging/render/ui/gameplay/platform/audio） |
| `docs/parity/coverage-matrix.md` | 2312 行逐声明索引（1470 项待判定） |
| `build/ref/` | 参考设备界面截图与索引 |
| ~~`docs/COVERAGE_AUDIT.md`~~ | **已作废**，其「S1/S2 已清零」结论被 `docs/parity/REPORT.md` §7 推翻 |
| ~~`MIGRATION.md`~~ | **部分过时**，结论以 `docs/parity/` 为准 |

---

## 九、验证命令速查

接手后建议先跑这些确认环境：

```powershell
cd E:\ReChigros
git add -A && git commit -m "..."      # 第一步！先保住现有工作
build.bat desktop Release              # 构建 + 八个测试套件
build.bat verify-app                   # 应用循环（含 pause-cycle）+ 字体覆盖
build.bat verify-render                # 渲染矩阵 26 项
build.bat verify-ios                   # iOS 打包检查 48 项
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\check-gles-shaders.ps1   # 43 个着色器
.\build\desktop-release\src\tools\rechigros_block_probe.exe --assets assets --out-dir build\probe
.\build\desktop-release\src\tools\rechigros_record_check.exe build\record-out
```

期望结果：全部 exit 0，探针打印 PASS，录像检查产出可读 mp4。
设备在线时再跑 `scripts\check-android-render.ps1 -Serial 127.0.0.1:5555`。

单项验证（截图核对）：

```powershell
.\build\desktop-release\src\app\ReChigros.exe --data-root build\capture-data `
  --screen "detail:Hate" --window 1920x1080 --frames 200 --fixed-delta `
  --capture-at-frame 180 --capture-frame build\shots\x.png
```

录像端到端（需 `E:\ffmpeg\bin\ffmpeg.exe`）：

```powershell
.\build\desktop-release\src\app\ReChigros.exe --data-root build\capture-data `
  --screen "game:Hate" --window 1280x720 --frames 90 --fixed-delta --autoplay `
  --record build\out.mp4
# 打印 RECORD PASS，产物含 H.264 视频 + AAC 48kHz 立体声音轨
```

---

## 十、给接手者的建议优先级

1. **先提交现有工作**（见第零节），再跑第九节验证确认环境。
2. **回读 `docs/parity/REPORT.md`**，特别是 §4a（参考缺陷修复）和 §6（剩余风险），
   建立对"哪些结论可信"的正确认识。
3. **主线：逐函数比对**（第五节第 1 项）。这是任务的核心，1470 项待判定。
   按行为单元推进，每核完一项就把结论和双侧 file:line 写进对应领域报告。
   优先做用户点过名但尚未核完的：噪域打击特效、皮肤字段、舞台机制、
   谱面揭秘/调试、挑战模式判定、低分辨率/抗锯齿、分数跳动、多押高亮、
   AP/FC 指示器、判定偏移指示器、剧情模式、音乐倍速、延迟校准、校准工具。
4. **平台验证**：安卓真机、iOS 实编译。
5. **收尾**：全量性能基准重测（第 5～10 轮改了渲染与界面路径）。

可用的人力分工方式（第 5～7 轮用过，有效）：按领域拆 4 个 teammate——
chart / core / judging / render，各自拥有不重叠的源码目录与报告文件，
Lead 负责跨领域接线、合并结论、跑门禁。注意**同时最多 4 个活跃子代理**，
且并发构建上限 2。

---

## 十一、录制性能：实测数据与已做/未做

用户要求提升 Windows / iOS / Android 三端录制性能，参考 `Phi-Recorder-main`（Rust+前端，
用硬件编码器 h264_nvenc/qsv/amf/vaapi）与 `easy-phi-main`（C++ 单头库，PBO 异步回读 +
独立编码线程 + 直出 YUV420）。

### 已做（提交 2c13132）

| 改动 | 实测收益 |
|---|---|
| 编码移到独立线程 + 有界队列（16 帧） | 渲染与编码重叠，不再交替 |
| PBO 异步回读（`gl::AsyncReadback`） | capture 阶段降到 2.5ms/帧 |
| 整帧一次写入（原按 1MB 分块） | encoderWrite 13.15 → 8.63 ms/帧 |

**合并效果**：同一谱面 281 帧，6.78s → 4.44s（20.8 → 15.8 ms/帧，**-34.5%**）。
输出帧数 281、音轨 221 帧均正确，ctest 8/8、自检 52/52、录像检查 PASS。

录制现在会打印分阶段耗时（`[perf] capture=… encodeWait=… encoderWrite=…`），
以后优化前先看这个，不要再靠猜。

### 试过但**否决**的

**硬件编码器不适配本机**，实测（1080p，120 帧）：

| 编码器 | ms/帧 | 可用 |
|---|---|---|
| libx264（现用） | 6.32 | 是 |
| h264_nvenc | 6.89 | 是 |
| h264_qsv | 16.14 | 是 |
| h264_amf | — | 否（缺 amfrt64.dll） |

结论：本机硬件编码不比软件快，加上去是负优化，已回退。
**换机器后应重新测一遍**，有独显且驱动正常时 nvenc 可能反超。

### 尚未做（下一步优先级）

1. **RGBA→YUV420 转换移进 GL**（easy-phi 的做法）。ffmpeg 收到 rgba 后每帧要自己转，
   实测 yuv420p 直入比 rgba 快 1.6ms/帧（6.32 → 4.73）。做法是加一个把捕获纹理转成
   3 个平面的着色器，再喂 `-pix_fmt yuv420p`。
2. **启动开销占 26%**（1.55s of 5.86s）。`launch: audio` 阶段实际包含渲染器初始化
   （编译 shader、上传纹理），是最大的一块，还没拆开。
3. **Android 端未验证性能**。MediaCodec 路径已跑通，但编码器仍是模拟器软件实现，
   真机硬编未测。
4. **iOS 从未编译过**，无性能数据。
