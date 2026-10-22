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
| 依赖 | SDL2 2.30.11、OpenGL 3.3 Core / GLES 3.0、miniaudio、Dear ImGui、nlohmann/json、stb、miniz |
| 构建 | CMake ≥3.15 + Ninja，`build.bat` 一键 |
| 首方代码量 | 约 22,800 行 C++（参考 Java 约 40,000 行） |
| Git | 有仓库，主分支 `master`，最新提交 `94f3a27` |

**方向**：把 Java 音游逐功能移植为跨平台 C++。要求功能、逻辑、性能不低于原项目，界面与参考视觉一致。

---

## 二、环境与构建（重要）

### 工具链路径（本机固定，不在 PATH 上）

```
CMake:   D:\Studio-SDK\cmake\3.22.1\bin\cmake.exe
Ninja:   D:\Studio-SDK\cmake\3.22.1\bin\ninja.exe
MSVC:    C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat
Android: ANDROID_SDK_ROOT=D:\Studio-SDK，NDK 27.0.12077973，JDK 17
adb:     D:\Studio-SDK\platform-tools\adb.exe
```

MSVC 需要在 `cmd /c "call vcvars64.bat && ..."` 里调用，无 vswhere。

### 常用命令

```powershell
cd E:\ReChigros

# 桌面构建 + 跑测试
build.bat desktop Release

# 应用端到端验证（隐藏窗口跑主循环 + 字体覆盖检查）
build.bat verify-app

# 渲染矩阵（18 个用例，验证每个渲染输入确实改变画面）
build.bat verify-render

# 安卓 APK
build.bat android Release
# 产物：platform\android\app\build\outputs\apk\release\app-release.apk

# iOS 打包检查（不需要 macOS，只验证工程完整性）
build.bat verify-ios
```

### 依赖下载

需要代理：`$env:HTTPS_PROXY='http://127.0.0.1:7899'`（以及 `HTTP_PROXY`）。CMake 首次配置会 FetchContent 下载 SDL2。

### 自建构建目录

```powershell
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake='D:\Studio-SDK\cmake\3.22.1\bin\cmake.exe'
cmd /c "call `"$vcvars`" >nul 2>&1 && `"$cmake`" -S . -B build\myprobe -G Ninja -DCMAKE_BUILD_TYPE=Release -DRECHIGROS_SDL_SOURCE=E:/ReChigros/third_party/SDL && `"$cmake`" --build build\myprobe --parallel"
```

---

## 三、调试工具与手段（很有用，先读这节）

### 1. 渲染冒烟工具

```powershell
build\myprobe\src\render\rechigros_render_smoke.exe `
  --chart tests\fixtures\plain.json --assets assets `
  --out build\out.png --frames 1 --time 1.0
```

支持参数：`--chart --out --assets --frames --width --height --time --skin --song --pause --pause-elapsed --dim --mirror --scroll --key-scale --force-bad --compare`

`--compare 另一张.png` 会输出两图差异百分比，用于验证某个输入确实影响画面。

### 2. 应用隐藏窗口批量运行（**最有用**）

```powershell
# 跑 N 帧并退出，打印状态摘要
build\myprobe\src\app\ReChigros.exe tests\fixtures\plain.json --frames 900

# 其他开关
--expect-result              验证进入结算
--expect-pause               验证暂停/恢复状态机
--record-replay <file>       录制回放
--replay-playback <file>     回放并比对成绩
--autoplay / --practice / --challenge / --mirror
--mistime <ms>               触摸延迟，用于产生非 Perfect 判定
--practice-perfect <ms>      练习模式 perfect 窗口
--practice-good <ms>         练习模式 good 窗口
--capture-frame <png>        把当前帧存成 PNG
```

**`--capture-frame` 是验证游玩画面最可靠的手段**。注意：游戏界面要等 140 帧后才会捕获（等 intro 结束），否则截到的是入场动画。

### 3. 安卓设备调试

模拟器已在跑（雷电），通过 `127.0.0.1:5555` 连接：

```powershell
$adb='D:\Studio-SDK\platform-tools\adb.exe'
& $adb -s 127.0.0.1:5555 install -r 'E:\ReChigros\platform\android\app\build\outputs\apk\release\app-release.apk'
& $adb -s 127.0.0.1:5555 shell am force-stop com.wuying.chigros
& $adb -s 127.0.0.1:5555 shell am start -n com.wuying.chigros/.MainActivity
& $adb -s 127.0.0.1:5555 shell screencap -p /sdcard/s.png
& $adb -s 127.0.0.1:5555 pull /sdcard/s.png E:\ReChigros\build\s.png
& $adb -s 127.0.0.1:5555 logcat -d | Select-String 'SDL/APP'
```

**⚠️ 坐标陷阱（我踩了三次）**：截图是 1920×1080，但显示给你时会缩放（约 1067×600）。点击坐标必须按 `1920/1067 ≈ 1.8` 换算。例：截图里按钮在 (1008,492)，实际要点 **(1814,886)**。

**⚠️ `adb shell input tap` 缺陷**：点击太快（按下/释放同帧）会导致某些按钮无响应。用 `input swipe x y x y 150` 制造按压时长。

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
| 全新克隆构建 | 验证通过 | 从 `git clone` 跑通桌面+测试+APK+iOS 检查 |
| Android APK | 成功 | 三 ABI、v2 签名、26.9 MB |
| iOS 打包 | 47 项检查通过 | 编译需 macOS |

### 功能模块

已完成的模块见 `MIGRATION.md` 的「Area status」表，绝大多数标 `done`。关键的有：

- 谱面解析：PEC / RPE / 官方格式，格式由内容前缀判断而非扩展名
- 判定引擎：窗口、相位、长条结算、挑战模式计分、练习模式窗口覆盖
- 渲染器：判定线、音符、长条、打击特效、HUD、暂停背景、bloom、故事板着色器链、章节 9 特效
- 音频：miniaudio 单回调混音、解码、低通、播放位置
- UI：主界面、谱面库、详情、结算、校准、设置
- 回放与设置持久化
- 红区（OfficialBlock）内联渲染
- 打空音符飞散残留

### 本次会话新修的关键缺陷

| 缺陷 | 根因 | 提交 |
| --- | --- | --- |
| 头文件改动后不重编，运行崩溃 | MSVC 中文环境 `/showIncludes` 输出被 Ninja 解析失败，头文件依赖未记录。已在 CMake 里让每个目标文件依赖全部公共头文件（`include/*.hpp` + `src/*.hpp`） | 早期 |
| 界面文字模糊 | DPI 在字体创建后才查询；字形按 20px 栅格化后被放大 4 倍。改用 `RasterizerDensity` 按界面缩放栅格化 | 早期 |
| 中文全显示为问号 | 打包后资源不是文件，字体读不到回退 ASCII 字体。改用 `AssetManager::materialize` | `1f99785` |
| 立绘/图标/封面不显示 | `CoverCache` 用 `stbi_load(文件路径)`，APK 内资源不是文件。改走 `AssetManager` + `stbi_load_from_memory` | `1f99785` |
| 少数汉字显示方块 | 字体范围缺 `轴`(U+8F74)、`频`(U+9891)。追加码点。**注意**：`AddFontFromFileTTF` 只保存指针，范围数组必须 `static` 否则悬空崩溃 | `dfbad9b` |
| 难度标签显示方块 | `normalizeDifficultyText` 主动把空格转成排版空格 U+2002 | `5d7f01c` / `92fe1b3` |
| 输入框无法输入 | 从未调用 `SDL_StartTextInput()`，软键盘不弹、字符事件不产生 | `aa33892` |
| 快速点击「开始游玩」无反应 | `pressStartSec` 只在按住跨帧时赋值，点击太快则永不触发。改用 `IsItemActivated()` | `d0816eb` |
| 音频停止时谱面冻结 | `playheadSec` 完全依赖音频位置，流停止则卡住。改为未播放时用帧时钟推进 | `c96f9a2` |
| 游戏画面无法截图验证 | `captureFramebufferIfRequested` 被 `if (screen != Game)` 排除 | `315d77b` |
| 场景不绑定目标/视口 | 无特效时依赖调用者状态 | `7191fb5` |
| 安卓无法导入谱面 | `onCreate` 里在 `super.onCreate` 前处理 intent，Activity 未 attach | `cb1fd43` |
| 打击特效偏大 18% | `respack.json` 缺 `hitFxScale: 0.85` | `d19f71b` 附近 |
| 长条材质硬编码 | `holdRepeat/holdKeepHead/holdCompact` 恒为 false，切片写死 | `89de03b` |
| 回放不记录成绩 | 写文件前未把最终成绩写进 meta | 早期 |

---

## 五、未完成事项

### 1. 红区屏幕空间合成（已知最难，三次失败）

**现状**：红区只绘制为静态剪影。参考项目会在红区内把画面替换成"撕裂"效果的位移重采样。

**已有的**：
- `fx::BlockCompositePass` 模块已实现且编译通过（`include/rechigros/render/fx/BlockCompositePass.hpp`）
- 两个着色器随资源发布且与参考逐字节一致：`assets/official_shaders/block_composite.glsl`、`block_prepass.glsl`
- 门限与发光环权重有单元断言（`TransitionTests`）
- 掩码栅格化代码已写好（六类区域 → 六个通道）

**为什么没接通**：从 `drawScene` 驱动合成会让整屏变白，**即使把合成着色器换成极简 `texture(scene,uv)` 直通也一样**，说明问题在帧流程机制而非着色器。

**已通过实测排除的原因**（不要重复）：
- 离屏路由本身（用普通 blit 回显验证过，正常）
- 掩码绘制
- 着色器编译
- 缺采样器（共 8 个，`noiseMap` 容易漏）
- 缺全屏 VAO（`FullscreenQuad::draw()` 不自动 `ensure()`）
- `Framebuffer::resize()` 遗留绑定（会污染后续绘制）
- `prepare()` 开头调 `release()` 摧毁已分配目标

**建议方法**：先写独立探针程序，只创建 GL 上下文、只跑 prepass+composite、把中间目标 dump 成 PNG 逐个检查，确认 pass 本身正确后再接 `drawScene`。**不要**继续在完整帧里猜。

详见 `MIGRATION.md` 的「Remaining work: the block screen-space composite」。

### 2. 安卓端游玩画面不呈现

**现象**：安卓上游戏逻辑完整运行（能跑完、能进结算页、判定计数正确），但**游玩画面不显示**，而 ImGui 覆盖层（如 MISS 提示）正常显示。桌面端同一份渲染代码输出完全正常。

**观察到的线索**：
```
E EGL_emulation: eglSurfaceAttrib: error 0x3009 (EGL_BAD_MATCH)
W OpenGLRenderer: Failed to set EGL_SWAP_BEHAVIOR on surface
D HostConnection: glGetError exceeded.     ← 每帧刷屏
V GraphicsEnvironment: angle_gl_driver_selection_pkgs=[]
```

模拟器使用 **ANGLE** 转换层，且持续 GL 错误。

**判断**：大概率是模拟器图形栈问题，不是产品缺陷。但**未在真机验证**。

**下一步**：用真机装 APK、点播放按钮、截图。这是最快的定论方式。若真机也异常，则需换路径排查（比如用 `renderOffscreen` 直接输出 PNG，绕开屏幕呈现）。

### 3. 官方解析器完整对齐

`MIGRATION.md` 标 `in progress`：格式 1/3 的移动打包、音符字段覆盖度、短谱外推。

### 4. iOS 实际编译

仅做了打包完整性检查（47 项），**未在 macOS 上编译**。需要 macOS 环境。

---

## 六、明确排除的功能（已论证，不要实现）

见 `MIGRATION.md` 的「Deliberate exclusions」：

| 项 | 排除理由 |
| --- | --- |
| 故事板视频层 | 参考用平台视频管线（仅移动端有），且无任何随包内容使用 |
| 在线谱面目录 | 依赖第三方托管服务，故意做成占位符 |
| 移动端原生 UI 工具包 | 本移植用 ImGui 绘制等效界面 |
| 红区屏幕空间合成 | 见上文，唯一保留的渲染缺口 |

---

## 七、踩坑清单（务必读）

### 构建类

1. **改头文件必须整库重建**。虽然 CMake 已做头文件依赖，但改的是成员布局时，稳妥做法是删掉构建目录重配。这个坑在本项目里造成了至少四次"莫名其妙的运行时崩溃"。
2. **`build.bat android` 曾把 Gradle 命令发到仓库根目录**（已修，用 `-p` 指定项目目录）。若再出现 `Directory does not contain a Gradle build`，检查这个。
3. **Android 依赖 androidx 不可用**。`MainActivity` 用的是 `startActivityForResult`，不是 `ActivityResultLauncher`。gradle 里没有 androidx 依赖。

### 编码类

4. **多行字符串替换会静默失败**。PowerShell 的 `.Replace()` 对多行文本常因缩进不匹配而不生效，且不报错。**改源码一律用 `edit` 工具**，改完 grep 确认。
5. **`std::filesystem::temp_directory_path()` 在非 ASCII 用户名下抛异常**（本机用户名是 `无影`）。测试里用 `current_path()` 或带 `error_code` 的重载。
6. **`AddFontFromFileTTF` 的 GlyphRanges 只存指针**，传入的数组必须活到图集构建完成（用 `static`）。
7. **ImGui 字体范围**：`GetGlyphRangesChineseSimplifiedCommon()` 约 2500 字，覆盖 `0x2000-0x206F` 标点区。缺字要手动追加码点，不要用 Full（21000 字会撑爆图集）。

### 平台类

8. **打包构建的资源不是文件**。任何读资源的地方都必须走 `AssetManager::readBinary` / `materialize`，不能直接 `fopen`/`stbi_load`。
9. **安卓 adb 点击坐标要乘 1.8**（见第三节）。
10. **`adb shell input tap` 太快会导致按钮无响应**，用 `input swipe` 加时长。

### 测试类

11. **渲染矩阵采样稀疏**（每 5 行、每 20 字节），只能判断"有无差异"，不能验证数值精度。精度类断言应写成单元测试。
12. **单点采样可能撞上巧合**（如两个用例在某时刻位置正好重合）。多时间点验证。

---

## 八、关键文件索引

| 路径 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 顶层构建，含头文件依赖保障 |
| `scripts/build.ps1` | 一键构建脚本主体 |
| `scripts/verify-app-loop.ps1` | 应用端到端验证 |
| `scripts/verify-render-matrix.ps1` | 渲染矩阵 |
| `scripts/check-font-coverage.py` | 字体覆盖检查 |
| `cmake/Dependencies.cmake` | 依赖拉取 |
| `src/app/Application.cpp` | 主循环、事件、ImGui 初始化、帧捕获 |
| `src/app/GameScreen.cpp` | 游玩逻辑、时钟推进、结束判定 |
| `src/render/GameRenderer.cpp` | 渲染总调度 `drawScene` |
| `src/core/JudgementEngine.cpp` | 判定 |
| `src/core/JudgeLine.cpp` | 判定线状态机 |
| `src/chart/` | 三种格式解析器 |
| `src/platform/AssetManager.cpp` | 打包资源读取 |
| `platform/android/app/src/main/java/com/wuying/chigros/MainActivity.java` | 安卓入口、导入、选择器桥接 |
| `MIGRATION.md` | 逐模块移植状态与论证 |
| `README.md` | 面向使用者的说明 |

---

## 九、验证命令速查

接手后建议先跑这些确认环境：

```powershell
cd E:\ReChigros
build.bat verify-app        # 应用循环 + 字体覆盖
build.bat verify-render     # 渲染矩阵
build.bat android Release   # APK
build.bat verify-ios        # iOS 打包检查
```

期望结果：前两个 exit 0，APK 成功产出，iOS 47 项通过。

---

## 十、给接手者的建议优先级

1. **先跑验证命令**确认环境可用，再动代码
2. **安卓呈现问题**：争取真机确认，这是投入产出比最高的一步
3. **官方解析器对齐**：影响谱面兼容性，可独立推进且有测试支撑
4. **红区合成**：最难，建议放在最后，且务必先用独立探针验证 pass 本身
