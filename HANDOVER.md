# 交接文档

本文件面向接手本项目的下一个会话。它记录**当前真实状态**、**如何构建与调试**、**已知未完成事项**，以及踩过的坑。

> 原则：本文只写经过实测验证的结论。标注「已验证」的都有实际输出证据；标注「未验证」的请自行确认，不要当成事实。

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

**状态摘要（2026-10-22 第二轮反馈收尾）**：

- 构建、八个测试套件、应用自检（30/30）、应用循环（11/11）、渲染矩阵（19 用例，含两个皮肤用例）、界面字形覆盖、文件导入检查（30/30）、红区合成探针：**全部通过**。
- 安卓游玩画面不呈现的问题：**已定位并修复，模拟器上已验证**（见第四节）；APK 已用当前源码重新构建并在雷电模拟器上复验。
- 第九章红区（噪域）屏幕空间合成：**已完成并通过独立探针验证**（见第四节）。
- 界面：主界面、列表/搜索/管理/导入、四个设置页、校准页、谱面详情、结算页、游玩 HUD、暂停、恢复倒计时、练习面板与判定面板均已按设备截图逐屏校准，元素位置基本在 0–3 px 内。
- **第二轮反馈的游玩画面修复**：背景改为按谱面插画解析并经过模糊层；舞台按设置宽高比计算，两侧为固定模糊侧栏（参考 `drawOfficialSideColumnsOverlay` 行为）；开场按参考节奏重排（1083 ms 音频缓冲、装饰判定线曲线、HUD 进入）；结尾按参考构建行为（无舞台缩放，HUD 淡出后全屏模糊过渡）；暂停背景模糊与计数数字（80 px em）按实机帧校准；**删除了自行添加的屏幕中央 "PERFECT/GOOD/BAD/MISS" 文字与底部 "±ms" 文字**，改为参考的判定偏移指示器（扇形刻度盘 + 指针）。FPS 读数移到参考位置。
- **歌曲信息与资源包**：`info.txt/yml/json` 解析按参考 `FieldInfo` 字段集、别名、类型强制与包检测顺序完全对齐（参考只有 11 个字段，未额外发明）；`SkinConfig` 全套字段（判定线/音符颜色、hit_fx 缩放/时长/旋转/着色、粒子、长条样式、贴图覆盖与回退顺序）已移植并生效，导入/选择皮肤后下一局即应用。旧编码（GBK）文本与压缩包内文件名在桌面端自动回退解码。
- **性能**：官方谱面解析并行化（稠密谱面 680–870 → 302–350 ms）、prepareRuntime 按行并行 + 单调 floor 游标（6.5 → 2.6–3.3 ms）、note 按类型时间窗遍历（4–6×）、红区推进窗口化（0.184 → 0.0057 ms/帧，32×）+ 掩码顶点复用、会话侧红区遍历移除、缓存红区窗口查询；--frame-stats 新增 GPU/swap/wall 列与 --swap-interval 诊断，空闲 gameplay 帧跳过 ImGui。安静主机同状态设备对比：端口呈现节奏 SF 均值 4.99 ms（p99 8.33，80% 落在 240 Hz 单 tick）vs 参考应用 6.56–6.91 ms（p99 10.81–12.50，44–46% 单 tick），上一版本 5.48 ms；帧内 CPU 3.63 ms（旧 3.79）在 4.17 ms 帧预算内。
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
build\myprobe\src\render\rechigros_render_smoke.exe `
  --chart tests\fixtures\plain.json --assets assets `
  --out build\out.png --frames 1 --time 1.0
```

支持 `--chart --out --assets --frames --width --height --time --skin --song --pause --pause-elapsed --dim --mirror --scroll --key-scale --force-bad --compare`。`--compare 另一张.png` 输出差异百分比。

### 3. 红区合成独立探针（改渲染前先跑它）

```powershell
build\agent-block\src\tools\rechigros_block_probe.exe --assets assets --out-dir build\probe
```

它把红区合成的每个中间目标 dump 成 PNG，并断言帧状态复原、`presented == off-screen composite`。改 `fx::BlockCompositePass`/`GameRenderer` 的合成接线后必须重跑。

### 4. 应用隐藏窗口批量运行

```powershell
build\myprobe\src\app\ReChigros.exe tests\fixtures\plain.json --frames 900
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
| 桌面构建 | 完成 | 干净构建 0 错误 |
| 六个测试套件 | 通过 | core / chart / audio / platform / transition / level_effect |
| 应用自检 | `SELF-TEST PASS (30/30)` | 含每屏无头绘制 |
| 渲染矩阵 | 18 用例通过 | `build.bat verify-render` |
| 应用循环 | 11 项通过 | `build.bat verify-app` |
| 字体覆盖 | 233 个非 ASCII 字符全有字形 | 已接入 `verify-app` |
| 界面截图工具 | 完成 | 每个界面可在 1920x1080 下确定性截图 |
| GLSL ES 校验 | 35/35 通过 | `scripts/check-gles-shaders.ps1` |
| 安卓设备渲染门禁 | 通过 | `ANDROID-RENDER PASS`（应用内截图 + 系统截图） |
| 红区合成探针 | 通过 | 帧状态复原、presented == composite |
| iOS 打包 | 47 项检查通过 | 编译需 macOS |

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

已完成的模块见 `MIGRATION.md` 的「Area status」表，绝大多数标 `done`。关键的有：

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

### 1. 界面（基本完成，剩余细节）

主界面、列表/搜索/管理/导入、四个设置页、校准页、谱面详情、结算页、游玩 HUD、暂停（含 HUD 变暗保留）、恢复倒计时、练习面板与判定面板均已按设备截图/参考帧校准。已知仍可继续打磨的残余（按影响排序）：

- **结尾过渡时序**：参考在 HUD 退出后有约 2 秒的整屏模糊过渡，端口目前是 HUD 退出后直接进入结算页入场（结算页背景本身是模糊插画，但缺少中间阶段）。属于结算页/过渡时序修改。
- **主界面标题文字**：端口显示 `ReChigros`，参考显示 `Chigros`（产品命名差异，非几何差异）。
- **暂停 HUD 亮度**：已按参考补上（HUD 以 0.27 倍 alpha 重绘于分级底图之上，score/song/icon 亮度与参考误差 ~10% 内）。
- **结算页模式标签**：设备对比用的参考截图与端口跑的是不同模式（挑战开关），标签数量不同，不是设计差异；如需最终确认应以同模式参考帧比较。
- 菜单类截图的整屏差异主要来自背景动画相位；逐元素 ink 对比才是有意义的指标（桌面端已完成，元素位置基本 0–3 px）。

### 2. 功能差距（S1/S2 已清零，余 S3 重构项）

`docs/COVERAGE_AUDIT.md` 的 S1/S2 项均已修复或标注（回放判定条目模型、无头 JSON 谱面、`useAttachUiFix`、长条身体反馈、难度文本、回放 meta、实践判定窗口、默认宽高比等）。剩余为 S3 重构：`NoteGeometry` 的单一速度基准、`prepareRuntime` 进一步并行/缓存（#13/#14 的主要收益已在性能工作中落地）。

### 3. 平台验证

- **安卓**：雷电模拟器（宿主 GPU 直通）上构建、安装、渲染门禁与帧时间 A/B 均通过；**真机（非高通 GPU / mediump 回退路径）仍未验证**。
- **iOS**：仅做打包完整性检查（47 项，0 警告），**未在 macOS 上编译**。
- **macOS / Linux**：CMake 与源码层面支持，未在对应系统实编译。

### 4. 性能

已完成正式基准（见 `MIGRATION.md` 性能行与 `HANDOVER` 状态摘要）：桌面与设备帧时间均可复测（`--frame-stats`），设备安静 A/B 无 >33 ms 帧。仍待处理：

- 与参考应用同机的 SurfaceFlinger 呈现节奏差距约 10%（端口 5.81 ms vs 参考 5.22 ms）；帧内 CPU 已低于帧间隔，剩余更像呈现/合成与 GPU 路径，下一轮应用 GPU timer query + SurfaceFlinger 时间戳定位。
- 下一轮候选（详见 `build/agent-core/perf-report.md`）：`anyZoneVisible` 全量扫描缓存、复合掩码向量提出帧外、GameScreen 渲染输入字符串拷贝、HUD 字符串格式化、纯游玩帧跳过 ImGui、交换/队列深度对比。

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

---

## 八、关键文件索引

| 路径 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 顶层构建，含头文件依赖保障 |
| `scripts/build.ps1` | 一键构建脚本主体 |
| `scripts/check-gles-shaders.ps1` | GLSL ES 静态校验（35 个着色器） |
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
| `docs/COVERAGE_AUDIT.md` | 逐模块功能差距（权威） |
| `docs/UI_PORTING_GUIDE.md` | 界面移植规范与截图对比流程 |
| `build/ref/` | 参考设备界面截图与索引 |
| `MIGRATION.md` | 逐模块移植状态与论证（部分过时，以审计为准） |

---

## 九、验证命令速查

接手后建议先跑这些确认环境：

```powershell
cd E:\ReChigros
build.bat desktop Release       # 构建 + 六个测试套件
build.bat verify-app            # 应用循环 + 字体覆盖
build.bat verify-render         # 渲染矩阵
build.bat android Release       # APK
build.bat verify-ios            # iOS 打包检查
scripts\check-gles-shaders.ps1  # GLES 静态校验
build\agent-block\src\tools\rechigros_block_probe.exe --assets assets --out-dir build\probe
```

期望结果：全部 exit 0，APK 产出，探针打印 PASS。设备在线时再跑 `scripts\check-android-render.ps1 -Serial 127.0.0.1:5555`。

---

## 十、给接手者的建议优先级

1. **先跑验证命令**确认环境可用，再动代码。
2. **界面逐屏移植**：按 `docs/UI_PORTING_GUIDE.md`，先截图（`--screen`）再与 `build/ref/` 对比，元素包围盒对到 0–2px 再继续下一屏。
3. **功能差距**：从 `docs/COVERAGE_AUDIT.md` 的 S1 项开始（回放模型 → 无头 JSON → `useAttachUiFix` → 长条身体反馈）。
4. **平台验证**：安卓真机、iOS 实编译；补性能基准。
