# 交接文档

本文件面向接手本项目的下一个会话。它记录**当前真实状态**、**如何构建与调试**、**已知未完成事项**，以及踩过的坑。

> 原则：本文只写经过实测验证的结论。标注「已验证」的都有实际输出证据；标注「未验证」的请自行确认，不要当成事实。

---

## 零之一、铁律：禁止照录屏拟合（违反即返工）

**行为只能来自源码，不能来自录像。**

历史事故：暂停恢复时画面会「先变清晰、再被压暗模糊一瞬间、才最终清晰」。这条假行为是照着
带异象的录屏逐帧猜出来的：录屏里那一帧确实暗了一下，于是实现里就照着写了一帧压暗。真实原因
不是画面逻辑，而是录屏本身在那几帧掉帧、后处理链在恢复时多跑了一遍。照着录屏写，等于把别人
的渲染故障复制进了自己的渲染器。

所以本轮把判定顺序固定成下面四步，任何一步缺证据都不许动代码：

1. **先找源码。** 每一条行为都要能指到参考实现的**文件 + 行号区间**，并且这段源码要能独立读懂：
   谁触发、什么条件、什么数值、什么时候结束。只找到「大概是这个意思」不算，必须读到分支和常量。
2. **再读文档里的实测结论。** 项目文档里记录过从二进制反汇编、从真机取帧得到的结论，它们比
   源码注释更接近运行时真实值，优先级高于猜测。文档与源码冲突时，以能复现的一方为准，并在
   本文写明冲突。
3. **然后才实现。** 实现只复刻第 1、2 步的结论。源码里没有的过渡、缓动、补间、闪烁，一律不写。
   「看起来应该有个动画」不是理由。
4. **最后用运行取证。** 截图、像素断言、音频测量、逐帧时间戳，任选其一但要能复现。跑不出来
   就等于没修。**禁止**用「代码已按源码实现」代替运行证据。

补充两条判断纪律：

- **怀疑测量方法本身。** 判断「没有设备」「没有效果」「没触发」之前，先怀疑探测手段：命令是否
  真的连上了、路径是否指到了另一份资源、数据根里是不是装着别的谱面、截图那一帧是否还没进入
  效果时间窗。本项目已经多次因为探测方法错误而误判了整轮结论（例如 adb 未 `start-server`
  时报「无设备」，数据根里第一首谱面被按曲名子串选中导致以为效果没触发）。
- **不写元注释。** 代码注释只描述行为本身，不写「已遵守某规则」「按某实现移植」这类句子，也不
  出现参考项目的名字。

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

## 零之四、本轮（2026-10-26）八个问题的结论与实测

本轮全部结论都以**运行输出/像素断言/计时**为准。每条都给出复现命令与数字，
换机器后重跑应得到同数量级的结果。

### 1. 第九章异象不触发

**根因：效果表现在只能从真实文件读，而 Android 的资产在安装包内。**

- `OfficialLevelEffects::load` 用 `std::ifstream` 直接打开
  `official_effects/chapter9_phase2.json`。桌面树里它是文件，能读；安装包里它不是文件，
  只有平台资产访问能读，于是表加载返回 `nullptr`，**每首歌都静默失去效果链**。
- 白名单摘要匹配、`storyMode` 默认值、加载器传参都不是原因：桌面捕获路径一直能选中
  （`capture-level-effect: HateLevelEffects hideHud=1 song=ハテ`）。
- 修法：核心库接收一个资产读取器，由应用在初始化时安装，走 `platform::AssetManager::readBinary`；
  桌面树没有读取器时仍走原来的文件路径。
- 回归：`tests/LevelEffectTests.cpp` 新增用例，在文件系统**故意读不到**表的前提下由读取器提供字节，
  断言表能加载、读取器被问了正确的相对路径、选出的效果可用。把读取器分支关掉这两条断言即失败。

#### 1.1 补充：剧情模式没有透传给加载器（本轮复查发现，已修）

初版只修了资产读取。「所有异象是否都实现了」的复查又发现两处真缺陷：

- **storyMode 只传给了渲染器，没传给加载器。** gameEnter 调 loadFromFile 时用默认
  storyMode=true，于是效果链**总是**挂上，渲染端只挡了对象层（Hate 背景黑幕、Message 粒子）。
  结果是：**剧情模式关闭时，ハテ / DS / Message 的 shader 链照跑、HUD 照样被隐藏**。
  参考实现的做法是 loadByChart(context, md5, storyMode) 在加载时就返回 nullptr（非 Message 时），
  链根本不挂。现在按此修：设置透传进加载器，Message 照旧保留。
- **Message 的 rawImageHideTime（=109 s）没实现。** 参考实现里到这一刻 Message 的画布
  从 16:9 letterbox 变为**覆盖整个视口**（原始背景被不透明 kaleido 遮罩盖住）。现在由渲染器
  从效果表推出该时刻，向后 seek 会自动把 letterbox 放回来。

实测（捕获路径 --screen game-nostory 关剧情模式，HUD 用「近白像素」判别，不会被红/黄噪点干扰）：

| 谱面 | 剧情开 | 剧情关 |
|---|---|---|
| ハテ IN | HateLevelEffects hideHud=1，HUD 隐藏（白墨 0/0/0） | none hideHud=0，HUD 显示（114/149/59） |
| Desultory Signals | DesultorySignalsLevelEffects hideHud=1，HUD 隐藏 | none hideHud=0，HUD 显示（836/1486/1409） |
| Message SP | MessageLevelEffects hideHud=1，HUD 隐藏 | **仍为** MessageLevelEffects hideHud=1（与参考一致） |

Message 舞台切换实测（1:1 舞台 + 16:9 窗口）：切换前亮带横向只覆盖 x=633..1286（1.7% 列），
切换后 x=0..1919（100%）；侧栏接缝切换前在 x≈424、切换后消失。

**测量陷阱**：批处理里 --frame-stats 的 now_sec 是程序时钟（帧数/60），而谱面时钟跟随**音频实时**——
一轮 7006 帧只跑约 35 秒墙钟，所以「第 7000 帧」并不等于谱面 116 秒。按谱面时间取证必须跑够真实时长
（约 110 s 墙钟才到 109 s），或用帧驱动的数据。

#### 1.2 Ametrine 与 Petrichor 的现状

- **Ametrine**：本机没有它的谱面（白名单摘要 677a9f… / 76c0b07… 在仓库里找不到），摘要路径无法在本机
  验证。机制部分（唯一 simpleVignette 通道、radius 0.24 / smoothness 0.55、2 s 渐暗、窗口内 Bad/Miss
  判失败、失败后保持、干净则散开、HUD 不隐藏）已由 tests/LevelEffectTests.cpp 新增用例钉住。
- **Petrichor**：效果表里有条目，但参考实现的白名单里也没有它的摘要，即它按摘要永远选不中；
  这与移植版一致，不是缺失。

### 2. 噪域触点照亮变成整屏红块

**根因：触点反馈在两条渲染路径上都画了。**

- 合成路径由着色器按手指位置点亮噪域，本身不画任何内联色块；内联回退路径才画「脉冲」色块。
  移植版在合成路径之后**又**画了一次脉冲，而脉冲半径按区域尺寸算
  （`max(halfW, halfH)`，窄高区域可到整屏），于是一次触摸就把整幅画面刷红。
- 修法：脉冲只在回退路径画（与它所属的剪影同处），合成路径不画。
- 实测（`tests/fixtures/tall_zone.json`，高窄区域 + 回放注入一次触摸，同帧对比）：

  | | 画面变化像素 | 占比 | 平均 RGB 变化 |
  |---|---|---|---|
  | 修前 | 921130 | 99.9% | 27.50 → 109.29 |
  | 修后 | 97656 | 10.6% | 27.54 → 28.63 |

- 合成路径的探针每个中间目标校验和修前修后**完全一致**
  （08_composite=1292924955、09_presented=1292924955、09b_touch=3607153101），说明着色器路径没被动过。
- 注：`max(halfW,halfH)` 本身不是错，它是官方回退路径的写法；错的是它跑在了合成路径上。

### 3. AUTOPLAY / REPLAY 时触点进入判定

**根因：`gamePointerDown` 没有 AUTOPLAY 分支。**

- 参考实现的触摸入口在处理完界面点击后有一行 `if (autoplay && !replayPlayback) return;`，
  即自动演示时手指根本进不了判定；回放时触摸仍被跟踪（用于噪域反馈与低通），但判定由回放条目驱动。
- 移植版只在回放时提前返回，自动演示时把真实触点喂进了会话，于是噪域被手指点亮、音频被压。
- 修法：按键/移动/抬起三处都挡掉自动演示；暂停控件在更早的位置处理，仍然可用。
- 实测：自动演示运行 `pointerEvents=12 → 0`；手动运行仍为 6；回放回放成绩 633333 与录制一致。
- 顺带修掉一个真正的边沿缺陷：音频低通与关卡状态原来靠调用方比较手指列表来判边沿，
  而「没有噪域」时这个列表永远是空，抬指那次的下降沿传不出去。现在由会话自己发布该边沿。

### 4. 分数跳动效果

**根因：动画状态是函数内静态变量，从不复位。**

- 显示值插值（每帧逼近目标、步长 = 已过时间 / 50 ms 窗口）本身写得对，但状态挂在
  HUD 绘制函数的 `static` 上：第一次运行之后它一直留着上一次的值，而且新一局开场把视觉时钟清零后
  `lastUpdateSec` 还是旧值，增量判据永远不成立，插值系数退化为 1 —— 数字直接跳到位，看起来就是没有效果。
- 修法：状态移进帧输入结构（每局自有），新谱面/重开/练习跳转时复位；帧增量按墙钟取并钳在 100 ms 内。
- 实测（plain 谱面，开关开/关逐帧对比）：第 60、100 帧无差异；第 80、120、160、180 帧
  分数框分别有 1501 / 655 / 3730 / 3225 个像素不同；同一帧重复截图 byte-identical。
- 回归：`tests/TransitionTests.cpp` 新增用例钉住「60 Hz 一帧走完剩余差距的三分之一」、
  收敛、复位、以及关闭时的直通行为。

### 5. 暂停恢复倒计时结束后二次压暗

**根因：参考实现里根本不存在这个状态，移植版照着录屏把它造出来了。**

- 参考实现的暂停状态只有「菜单」「倒计时」两个赋值点；`PAUSE_FADEOUT` 常量虽然声明了，
  但**没有任何一行把状态设成它**，`resumeFadeOutStartNs` 也从未被赋过非负值 —— 整个分支不可达。
  倒计时结束（3.0 s）时 `finishResumeCountdown()` 直接清掉菜单状态，下一帧就是正常游玩。
- 移植版多了一个 0.25 s 的淡出窗口，并且把已经走到 3.0 s 的进度重置回 0.25 s 再喂给
  背景曲线（该曲线在 0–1.5 s 恒为完全不透明），于是画面在已经变清晰之后**又被整块压暗**，
  0.25 s 后突然消失。这正是用户看到的那一瞬间。
- 修法：删掉这个状态（枚举、分支、常量、渲染端模式），倒计时结束即恢复播放并 seek 回暂停位置。
- 实测（逐帧亮度）：48.22 → 65.80 → 82.87 → 88.43 → 89.31 → 89.83 并保持，**没有任何一帧回落**。

### 6. ImGui → ObjectivelyMVC

**结论：在现有交付工具链上无法完成，原因是构建层而不是工作量。**

- 入口清单已量化并可重跑：`python tools/ui/imui_inventory.py` → 合计 **348** 个入口
  （界面 273、绘制层 52、外壳 23；其中直接 `ImGui::` 调用 244）。
- 构建配置对两个交付目标**强制关闭**该框架（`CMakeLists.txt` 里 `if(MSVC OR ANDROID) ... OFF`），
  打开会直接 FATAL_ERROR，因为框架是 GNU C 且依赖 POSIX `regex.h`。
- 三条绕行路径本轮都实测过：WSL gcc 能编译出三个框架库，但其 SDL 没有 GPU 后端，
  跑起来报 `SDL_CreateGPUDevice: No supported SDL_GPU backend found`，无法出图；
  NDK clang 对 MSVC ABI 能编过 70 个源文件中的 66 个，卡在 `regex.h`；
  本机没有 MinGW/clang-cl。
- 迁移顺序、每批验收标准、以及「先做 SettingsScreen（目标实现 `src/app/MvcSettings.c` 已存在）」
  的理由写在 `docs/ui-migration.md`。**本轮没有迁移任何界面**，因为无法在可出图的环境里验证。

### 7. 启动黑屏与切屏卡顿

**根因：每次启动都把所有已安装谱面从头读一遍。**

- 目录预热要读每张谱面的内嵌元数据来补全 `info.json` 缺的字段。元数据块先在头 256 KB 里找，
  找不到就把**整个文档**解析一遍 —— 而官方格式谱面根本没有元数据块，于是每次启动都在读
  并解析几 MB 到十几 MB 的谱面，只为确认「没有」。
- 顺带两条：谱面文件经 `ostringstream` 读入多复制了一遍；身份摘要按字节逐个喂哈希。
- 修法：先做有界探测（头窗口 + 其后 1 MB）找元数据块，没有就不解析；文件读入单缓冲；
  摘要改为分块喂入。
- 实测（12 个包，同一命令）：

  | 阶段 | 修前 | 修后 |
  |---|---|---|
  | 构建库列表（capture screen setup） | 2698.7 ms | 11–61 ms |
  | 到第一帧总计 | 3849 ms | 1041–1798 ms |
  | 16 MB 谱面解析 | 333 ms | 220 ms |

- 摘要值未变：白名单谱面仍选中 `HateLevelEffects`。

### 8. 从详情页开始游玩卡顿并瞬间跳入场

**根因：整段加载发生在按下的那一帧里，屏幕上没有任何提示。**

- 一次启动 = 读谱面 + 解码音频 + 建会话，实测 **505–512 ms 全部压在一帧内**，
  期间画面冻在详情页，随后入场动画已经跑了几帧。
- 参考实现的做法是：先把加载面板设成内容视图（深色渐变 + 居中卡片 + 不确定进度条 + 两行文案），
  再把谱面和音频的解析/解码**放到工作线程**，GL 视图加在面板**后面**，资源就绪后才移除面板。
- 修法照此：界面上的开始入口改走加载路径，谱面解析与音频解码都在工作线程完成，
  主线程只在结果就绪的那一帧安装缓冲区并建会话；加载面板按参考实现的样式绘制
  （渐变、卡片、旋转弧、`加载中... / (正在解析谱面与解码音频)`）。
- 实测：阻塞式启动 505–512 ms → 延后启动在消费帧只花 **76.8–77.9 ms**；
  截帧显示第 3、10 帧是加载面板，第 30 帧已经是带 HUD 的游玩画面。
- 回放播放与结算页重试也走同一条加载路径；无头验证保留同步入口，以便下一帧就能检查运行状态。

### 本轮遗留

- 界面迁移未动（见第 6 条），需要先决定工具链方向。
- 延后启动的消费帧仍有 ~77 ms，其中大头是封面图的同步解码；渲染端的纹理缓存只有同步接口，
  要彻底消掉需要给渲染器加「按像素上传」的异步入口。
- 真机（Android）本轮未接上，`adb devices` 为空；第 1 条的设备侧结论来自
  「文件流读不到安装包内资产」这一机制与桌面侧的读取器回归测试，仍需真机复核。

## 零之五、章节九真谱面复核与资源层逐字节比对（2026-10-26 晚）

### 1. 真谱面（E:/Chigros-Reference/shader_dump/chart）与白名单的对应

| 谱面 | EZ | HD | IN | AT/SP |
|---|---|---|---|---|
| Ametrine | 无（未命中） | AmetrineLevelMod | AmetrineLevelMod | AT 无 |
| Desultory Signals | 有 | 有 | 有 | 有 |
| ハテ | 无 | 无 | HateLevelEffects | AT 无 |
| Message | — | — | — | MessageLevelEffects（SP） |

- 与参考白名单逐条一致；Ametrine 的 hideHud=0，其余三首 hideHud=1。
- Ametrine 暗角实测（同谱面同时刻、只关效果做对照）：
  桌面 角部中位数 11.0 → 3.0、中心 19.0 不变；蓝叠设备 11.0 → 6.0、中心 16.0→17.0。
  结论：暗角存在、只在 44–86 s 窗口内出现、只压暗四周、HUD 保持可见。

### 2. 蓝叠（BlueStacks 5 中国版）接入

- 安装目录 C:/Program Files/BlueStacks_nxt_cn（数据在 E:/BlueStacks_nxt_cn，实例 Pie64，adb 端口 5555）。
- adb connect 127.0.0.1:5555 → SM-S908E / Android 9 / x86_64 / 2400x1080。
- 设备端启动参数走 intent extra args；数据根用应用外部目录。注意 adb push 目录/ 会把内容摊平，
  必须推到 <root>/packs 才形成库结构（否则库为空并静默回退到默认库）。
- 设备端 A/B（截图 build/shots/dev_hate_in2.png / dev_hate_ez.png）：
  ハテ IN 显示完整异象且 HUD 全部消失；ハテ EZ 无异象、HUD 完整。
  这直接证明效果表已能从安装包内读到（问题 1 的根因修复在 Android 上有效）。

### 3. 资源层逐字节比对（参考 App 的 APK 对照）

从设备拉取参考包 com.wuying.phigros 的 base.apk 解包后比对：

- prpr 着色器 26 个：代码逐行一致（去掉注释后 0 处差异；5 个文件的差异只是注释按措辞规则改写）。
- official_shaders（block_composite/prepass）、效果表 chapter9_phase2.json（MD5 83e802f8…）、
  official_fonts/blocked_ui.ttf（MD5 51458c49…）、14 张 official_textures 全部一致。

### 4. 修掉的第三处偏差：DS 网格与固定侧栏的绘制顺序

- 参考实现里 DS 的网格是 2000 单位画布，会溢出 16:9 舞台；因此它把固定侧栏推迟到
  网格与游玩对象之后绘制，让侧栏盖住溢出的网格。移植版侧栏随背景一起画，网格压在侧栏上。
- 修法：背景阶段在网格可见时跳过侧栏，网格之后再补一次侧栏 pass。
- 实测（1:1 舞台 + 16:9 窗口、网格完全不透明时）：侧栏内白色网格像素 0，舞台内 25605。

### 5. 审计：与参考一致的部分（证据方式）

- 判定窗口 0.080 / 0.180 / 0.220；挑战 0.040 / 0.090 / 0.140 + 帧时长抖动项。
- 评分 acc*900000 + maxCombo*100000/n（挑战 acc*1e6），四舍五入；准确率权重 0.65。
- FlickTracker 逐行一致（三帧历史、0.01 阈值、投影速度、新 flick 5 倍门限、flicked/stopped 锁存）。
- GameConstants 全量一致（PGR 0.05625/0.6、RPE 1350/900、HEIGHT_RATIO 0.83175、SPEED_RATIO、
  命中粒子 15..35 / 半径 0.2 / 厚度 0.2、判定色与 alpha）。
- 音频：音量映射 0..5（参考 0..500% /100）、hold 头用 tap 音、低通开启值 22000 Hz、播放速度钳制。

### 6. 仍未验证 / 风险

- 参考实现的音频 DSP 在编译好的 native 库里，低通关闭截止频率（移植版取 1500 Hz）与重采样质量
  无法与源码逐字比对，只能听感/频谱间接比较。
- 蓝叠是 x86_64 + 软件 GL（emugl），不代表真机 GPU 的精度与性能。
- 本轮审计集中在章节九 + 判定/计分/音频常数；其余界面交互未逐项复核。
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

### 十二、真机验证（2026-10-25 深夜，Redmi 23049RAD8C / Android 15 / 骁龙）

设备 `88be2138` 首次接入，之前所有结论都建立在模拟器上，现在是真机数据。

**真机录制已完全跑通**：

| 项 | 实测 |
|---|---|
| 分辨率/编码 | 1920×1080 H.264 |
| 611 帧耗时 | **7.14 秒**（≈85 帧/秒，高于 60fps 实时） |
| 抽帧画面 | 颜色正确（红音符/蓝判定线/HUD 正常） |
| 文件大小 | 2.58 MB |

**GL 端 YUV 转换在真机上生效**，Android 那边不再做 RGBA→YUV 循环。

### 真机上踩的三个坑（都已修复，记录以备后续）

模拟器上全部通过的代码，到真机上连续三次失败，每次都是**读设备实际报的参数**才定位：

1. **提交 size 传 0 → 编码器丢弃全部帧**（`MediaCodec discarded an unknown buffer`）。
   文件能生成、能播放、但是空白，特征极易误判成“编码器不工作”。必须传 `w*h*3/2`。
2. **色度是交错的**：真机 plane1/plane2 的 `rowStride=1920`、`pixelStride=2`，
   即 U/V 共用一块 buffer 交替存放（模拟器不是这样）。必须成对写入。
3. **U/V 来自两个独立源平面**：把同一个值写进两个通道会让**整幅画面变绿**。

教训：模拟器编码器与真机编码器在 plane 布局上**没有保证一致**，
凡是涉及 Image/Plane 的代码必须在真机上验一遍，不能只看模拟器。

---

## 十三、真机吞吐优化（2026-10-26）

### 最终数据（Redmi 23049RAD8C / Android 15 / 骁龙 8 Gen 1 `taro`，611 帧 1920×1080）

| 阶段 | 轮次起点 | 最终 |
|---|---|---|
| 总耗时 | 7.14 s | **5.84 s** |
| capture（绘制+转换） | 9.29 ms/帧 | 5.4 ms/帧 |
| encodeWait | 0.25 ms | 0.4 ms |
| encoderWrite | 10.47 ms | 6.9 ms |

三次测量：5.89 / 5.84 / 5.84 s，画面抽帧验证正确。

### 关键判断依据：先拆开再优化

加了 `[capture] draw=Xms convert=Yms/frame` 插桩后才看清：**绘制只要 0.22ms，转换占 7.5ms**。
没有这个数据之前，很容易误以为瓶颈在渲染。

### 做了且有效的

1. **三次平面回读合并为一次等待**（`Yuv420Pass::convert`）：三个 `glReadPixels` 先连续发起、写入各自 PBO，
   再统一 `glMapBufferRange` 取回。convert 7.5 → 5.4 ms。
2. **垂直翻转移进着色器**（`kLumaFragment`/`kChromaFragment` 采样 `1.0 - uv.y`）：
   回读字节已经是编码器要的顺序，省掉整帧逐行 memcpy。
3. **编码器输入缓冲改为轮询** `dequeueInputBuffer(0L)`（原 10ms 阻塞）：encoderWrite 10.3 → 6.9 ms。
4. **帧通过复用的 direct ByteBuffer 传递**，不再每帧新建 3MB Java 数组。
5. **色度按行批量读**，不再逐字节 `source.get(i)`。

### 试过但失败的（都已回退，勿重试）

- **单张 `width × height*1.5` R8 纹理 + viewport 分区**（理论省两次读）：
  convert 确实降到 5.71ms，但画面错误（绿屏 / 画面分左右两半 / 文字倒置），
  调整两轮布局均失败。**三平面独立 FBO 是已验证正确的结构。**
- **硬件编码器**（桌面端测的）：libx264 6.32ms/帧，h264_nvenc 6.89，h264_qsv 16.14，amf 缺 dll。
  本机硬编不比软编快，已回退。换机器需重测。

### 下一步可能性（未做）

- `capture` 5.4ms 与 `encoderWrite` 6.9ms 已接近，重叠后理论下限约 7ms/帧 ≈ 4.3s。
  再压需要降编码负载（码率/分辨率，会损画质）或换更快的转换路径。

- 桌面端仍有一个未验证项：`capture` 阶段用 PBO 但**延迟一帧**的契约与编码器需求冲突，
  目前 Yuv420Pass 走同步路径；若要让桌面也吃到重叠收益，需要重新设计这个时序。
  （2026-10-26 下午已实现双 PBO + 双 plane 目标的延迟一帧契约，见第十四节；收益不显著。）

---

## 十四、录制音频 / 设置接线 / 弹窗顺序 / 真机 AAC（2026-10-26 下午）

本轮全部结论都以实际运行、ffprobe、PCM 频谱和像素断言为准，不看「代码里写了什么」。

### 用户实测缺陷 → 根因 → 验证

| # | 现象 | 根因（实测定位） | 验证证据 |
|---|---|---|---|
| 1 | 录像没有 note 打击音 | 混音只合成正弦音，从未调用皮肤/内置音效；`recordingHitSound()` 写了却没被调用 | 探针皮肤 1733Hz 能量 2574 vs 关闭皮肤 27.1；缺音频皮肤与内置完全同谱 |
| 2 | 关掉打击音后 BGM 也没了 | `noteHitsound` 同时门控整条音轨 | 真实页面点关后成品仍有 AAC，每秒 RMS 0.0849 |
| 3 | 音乐倍速下音画不同步 | 只把总帧数除以 speed，`chartTime` 未缩放；音乐也未重采样 | 2x 时音轨 6.19→4.69s，频谱质心 3018→7124Hz |
| 4 | 录像使用不了部分设置 | `input.keyScale`、HUD 揭示相机从未赋值；`input.practice.autoPlay` 未设置 | 探针皮肤品红 `click.png` 出现在成品；reveal 值已接线 |
| 5 | hold 首个打击特效位置不对 | 头特效用当前帧 line 状态，而非事件时刻 | 改动后强差异（>12/255）仅 433–964 px，集中在判定线 |
| 6 | 渲染进度弹窗被设置项遮住 | 全屏窗口共用 `NoBringToFrontOnFocus`，获得焦点也不提升显示顺序 | 面板内设置值盘像素 15103 → 0；取消键与进度条可见 |
| 7 | 安卓 AAC 轨仍未写入容器 | `prepareAudioTrack` 只在 start 时触发；PCM scratch 是 direct buffer 却调用 `array()`；inputDone 后直接 break 未等 EOS | 真机 probe PASS：93 包/47616 B/1.984s；生产路径 426 包/9.088s |
| 8 | 立体声右声道提前出声 | `adelay=<ms>` 只延迟第一个声道 | 两声道首个非零样本差 0.0 ms |

### 关键教训

1. **时间基准必须自己算一遍再下结论。** `PGR_BEAT=1.875`、`beat2sec = beat*1.875/bpm`；
   `hold.json` 的 `time:96` 在 120 BPM 下是 **1.5 s**，对应视频第 **155** 帧（含 1.083 s 开场）。
   按 96/32 当成 3 s 会得出完全错误的判读。
2. **md5 一致/不一致都要解释。** 纯 hold 谱面逐帧 md5 与基线完全一致，证明双缓冲未错帧；
   混合谱面三次 md5 不同，来源是预生成 burst 用 `random_device` 播种的既有随机性，不是回归。
3. **只改输入不改时间轴等于没修。** 倍速必须同时作用于 chartTime、session 帧长和音频重采样，
   漏一个都会让打击音逐步漂移。
4. **探针尺寸可能是设备侧的坑。** 真机 `c2.qti.avc.encoder` 对 64×64 返回 `BAD_VALUE`，
   探针改 1280×720 才 PASS；这不是音频链路的问题。
5. **可见窗口验证要加前景断言。** 两个代理同时跑可见窗口时，`SetForegroundWindow` 失败会让脚本
   截到别人的窗口并点错目标；现在脚本在截图/点击前硬断言目标 PID 仍是前台窗口。

### 性能：capture 收益不显著，编码 preset 才是量级收益

**capture（未获显著收益）**：`Yuv420Pass` 改为双 PBO + 双 plane 目标后，新旧二进制交替 A/B
各 5 次中位数：capture 2.27 → 1.97 ms，encodeWait 1.88 → 2.04 ms，合计 4.15 → 4.01 ms。
差异约 0.14 ms，小于逐次波动，**不构成性能结论**；保留它的理由是结构上不再阻塞下一帧绘制，
且输出与单缓冲路径**逐字节相同**（多次录制 md5 一致）。

**编码 preset（有量级收益）**：外部编码器默认用 libx264 的 `medium`，而编码就在录制关键路径上。
同一批 371 帧原始帧，各 preset 的 ffmpeg 自身耗时：

| preset | 耗时 |
|---|---|
| medium（原先的默认） | 1.99 s |
| faster | 1.62 s |
| veryfast（现默认） | **1.20 s** |
| superfast | 1.03 s |
| ultrafast | 0.73 s |

录制内部：`encoderWrite` 5.7 → **1.45 ms/帧**，`encodeWait` 2.2 → **1.2 ms/帧**（三次稳定）。
画质代价：与同帧 medium 编码 SSIM 0.9987（28.8 dB）；与实际录像整体 SSIM 0.9980，且文件小 16%。
`preset` 做成设置项而非常量，方便换机器后回调；VP9 分支不传（其质量参数不吃 preset）。

`rechigros_record_check` 增加反向断言：喂一个不存在的 preset 时编码器进程必须失败，
以此证明该参数确实进入命令行——否则拼错参数会被静默忽略，上面的时间就什么都没测到。

### 渲染页用户路径实测（本轮补充）

用真实鼠标操作（启动 → 点「开始渲染」→ 观察 → 点「取消」/等待完成）验证弹窗全流程，
不只看代码：

| 路径 | 修复前 | 修复后 |
|---|---|---|
| 点「取消」 | 界面提示「已取消渲染」，但输出目录留下 **262,192 字节的 mp4**，ffprobe **解析不出任何流**（未完成容器没有索引，播放器打不开） | 文件被删除，不留残骸 |
| 正常完成 | — | 3.3 秒完成，界面回到「开始渲染」，产物 731 帧视频 + 音轨，可播放 |

原因：`cancel()` 停了编码线程、删了临时音轨，却没删编码器正在写的容器。
现在取消时若确实在录制，会一并删除该输出文件，避免用户把它当成有效录像。

### 录制失败路径：崩溃与残留文件（本轮，部分修复）

用真实场景复现（录制过程中从外部强杀 `ffmpeg.exe`），对照旧二进制与本轮修改：

| 指标 | 修改前 | 修改后 |
|---|---|---|
| 进程崩溃 | **3/3**（`0xC0000409`，即 `std::terminate`） | **1/3**（`0xC0000005`） |
| 留下损坏 mp4 | **3/3**（`moov atom not found`，播放器打不开） | **0/3** |

已确认的两个根因：

1. **失败返回时编码线程仍是 joinable**（实测 `state=3 thread_joinable=1`）。
   带着 joinable 的 `std::thread` 销毁对象会触发 `std::terminate`。
   现在所有失败分支统一走 `failAndRelease()`：置停止位 → 中止编码器 → join → 释放资源 → 删除半成品。
2. **取消/失败后残留未完成容器**。已统一由 `discardIncompleteOutput()` 删除。

**大幅改善但未根除**（提交 029eea4 及后续）。`0xC0000005` 的主因是**流关闭与写帧之间的竞态**：
写线程持有一帧阻塞写时，另一线程关闭了同一个管道流，等于把流从写线程脚下抽走。

修法：给流加锁，并在取锁**之前**发布“正在关闭”标志；写循环在帧边界检查该标志后返回，
所以只有写线程真正离开管道后才会关闭它。

实测（录制中从外部强杀 `ffmpeg.exe`，同一脚本反复跑）：

| 阶段 | 结果 |
|---|---|
| 修改前 | 8 次里 **3 次崩溃**（`0xC0000005`），且 8 次都留残缺文件 |
| 加 join/清理 | 8 次里 3 次崩溃，**0 次残留文件** |
| 加关闭握手 | 18 次连续全部正常失败退出 |
| 再调整 kill/close 顺序 | 同一“慢轮询”场景 **6 次里 2 次仍崩溃**（比之前 4/6 好） |

**残留问题**：在“子进程被外部强杀”这一非用户路径上，仍有约 1/3 概率出现 `0xC0000005`。
用户路径（点「取消」）已复测：**文件被正确删除、进程正常**。普通录制不受影响（370 帧 + 音轨，ctest 8/8）。

**为什么停在这里**：这是外部强杀 `ffmpeg.exe` 的场景，用户不会触发；而继续追它需要能挂调试器的环境。
已记录的结论是“显著改善但未根除”，不要当成已解决。

**教训**：跨线程共享的对象（这里是 SDL 的 `SDL_IOStream*`）不能只在析构处“补一次 join”就算安全，
要保证“取锁/置标志的顺序”让另一线程能主动退出。我先前按“先 abort 再 join”与“先 join 再 abort”
各试一次都只是把崩溃率降低，真正的修法是让写线程自己看到关闭请求。

### 已知未解决

- `rechigros_import_check.exe` 有 2 项失败（plain replay 无法创建回放目录）。
  **修改前的旧二进制同样失败**，属既有环境/权限问题，非本轮回归。
- `platform/android/app/build.gradle` 保留了 `testInstrumentationRunner` 与 `testBuildType 'release'`，
  供真机探针使用；若后续不需要 instrumentation 测试，可去掉这两行。
- hold 身体首爆的「半拍」相位现由 `tests/CoreTests.cpp` 的可执行断言固定（头事件时刻 = note.sect，
  首个身体爆发严格晚于头、且在一个半拍内；注入「头帧即身体爆发」会失败），但**仍未与参考录像逐帧对齐比较**。
- hold 头特效现按 `burst.timeSec` 还原历史 line 状态；plain 与 official 两种解析路径都已抽帧确认
  头特效出现在打击帧（第 155 帧），但未在真实官方谱面（18.5s 起才有 hold）上做同样确认。
- 安卓真机本轮未连接，`capture 5.4 / encoderWrite 6.9 ms` 的指标**未随 preset 改动重测**；
  真机成品（AAC 426 包/9.088s）是移动 preset 之前的构建。重连设备后应复测这两项。

### 偏移（offset）语义：本轮修掉的真实缺陷

参考实现里 `chartTime = playhead − chart.offset − userOffsetSec`，即偏移只把**谱面**
在音乐内平移，**不改变录音长度**。移植版有两处不一致：

1. **打击音按 `note.sect / speed` 排布，没有加偏移**：设了 200ms 校准的玩家，
   每一击都比画面上的音符早/晚 200ms 响起。
   实测（无音乐 fixture）：修复前 2.083 / 2.583 / 3.583 与 0 偏移完全相同；
   修复后 2.283 / 2.783 / 3.783，与音符到达判定线的时刻一致。
   带音乐谱面用高通滤波定位瞬态，同样整体后移 200ms（1.087→1.287 等）。
2. **无音乐时把偏移算进结束点**：这是正确的（没有播放头可依，只能以最后音符 + 偏移），
   带音乐时长度**不随偏移变化**（实测 731 帧 vs 731 帧），与参考语义一致。

另确认 `prepareRenderChart → gameEnter` 已正确设置 `userOffsetSec = (chartOffsetMs + audioOffsetMs)/1000`，
从详情页直接进渲染页也不会丢失偏移。

### 视频设置项实测（本轮补充）

| 设置 | 验证方式 | 结果 |
|---|---|---|
| 编码器 | 同一谱面录 `.265` 与 `.mp4` | `hevc,Main` vs `h264`，都带 AAC；设置确实到达编码器 |
| 分辨率 | 命令行默认走索引 0 | 1920×1080（`--record` 无法改分辨率，走界面才会变） |
| 帧率 | 默认 60 | 371 帧 / 6.183s 一致 |
| preset | 见上 | H.264 veryfast 1.20s；H.265 veryfast 3.05s（medium 4.86s），两者都受益 |

注意：`--record <path>.265` 只用来选择编码器，最终文件名**总是追加 `.mp4`**，
所以产物是 `h265.mp4` 而不是 `.265`。这是既有命名规则，不是失败。

### 本轮修掉的一个自造回归（记录以防再犯）

上一轮为了覆盖“谱面比音乐长”的情况，把视频内容长度改成 `max(音乐时长, 谱面结束)`，
但同一轮没有同步音轨长度公式。实测后果：9 秒音乐的谱面录成 **视频 12.18s / 音轨 10.18s**，
尾部 2 秒无声。

现已统一：视频与音轨都用 `recordingContentSeconds()`（音乐结束 → 除以倍速），
退场由 `playhead > 音乐结束` 触发（与交互路径一致，交互路径本来就以音乐结束收尾）。
实测同谱面：视频 12.18s / 音轨 12.18s；第 600 帧处于退场中（均值亮度 84）、
第 640 帧为稳定模糊背景（119），说明延长的部分是退场动画而不是冻结帧；
与源音乐相关性 0.78（旧版 0.61，因其截掉了结尾）。

**教训**：任何“时长/时间轴”改动必须同时检查四个量——视频总帧数、音频轨长度、
退场触发点、以及帧内的 chartTime/playhead。只改其中一个一定会造成音画不同步。


---

## 十五、本轮（2026-10-27）五个缺陷：根因、数字、踩到的坑

本轮全部结论都有**设备实测数字**或**逐帧像素证据**。设备是蓝叠（`127.0.0.1:5555`，
`ro.build.version.sdk=28` / Android 9 / x86_64 / 2400x1080，软件 GL）。
**注意：不要用 AVD 配置文件推断设备系统版本**——本机同时存在多个 AVD，实际连上的这台是 SDK 28。

### 1. 安卓端「开始渲染」后画面静止卡死

**根因**：音乐轨在**第一帧视频之前**就必须进容器（muxer 只接受未启动前的轨道），
而这段编码跑在**处理按钮的那一帧里**。`VideoEncoder::openPlatform` →
`platformenc::addAudioTrack` → Java `setAudioTrack` → `prepareAudioTrack` 同步编码整首歌。

三次实测（同一命令）：

| 次 | `audio track loaded` | `audio encoded … eos=true` | 主线程被占 |
|---|---|---|---|
| 1 | 11:45:26.197 | 11:46:35.308 | **69.11 s** |
| 2 | 11:50:12.027 | 11:51:20.573 | **68.55 s** |
| 3 | 11:54:25.710 | 11:55:40.042 | **74.33 s** |

两条日志 **pid/tid 相同**（都是主线程）。卡死期间：

- `mp4` **恒为 0 字节**（muxer 要等视频轨格式，而帧只能等这次调用返回）
- `pcm` 恒为 **67,993,600 字节** = 8,499,200 帧 × 2ch × 4B @48 kHz = 177.06 s
- `ps -A -o TIME` 在 65 s 墙钟里只涨了 **4 s** → **阻塞，不是慢**

**修法**：编码移到独立线程；`audioTrackState()` 暴露 0/1/2/3，`RecordSession::step()`
在 `Preparing` 时返回 `true`（"还有活"）但**不产帧**，界面因此能画进度条。结果收集到局部变量、
最后一次性加锁发布，代际计数器让被取消的 worker 丢弃结果且不 join。

**修后实测**：CPU 从卡死时的 00:00:05 变成持续增长（6→9→12→15→19→21→24 s），
进度弹窗出现并显示 `20 / 10689 帧 0%`，`mp4` 从第一次 muxer 写入起就有内容。

**踩到的坑**：`audioTrackState()` 最初写成 `synchronized`，和帧交接用同一把类锁，
**单帧里测到 638 ms 的 monitor contention**。改为读 volatile 字段、不加锁。

### 2. 结尾 HUD 退场动画

**参考里根本没有「整体渐隐」**（`GameRenderer.java`）：

| 内容 | 行 | 代码 |
|---|---|---|
| 退场是以舞台中心为轴的 Y 缩放 | 8857-8859 | `y = rootY + (y-rootY)*rootScaleY; h *= rootScaleY;` |
| 缩放曲线 1 → 1.1875 | 8925-8929 | `1 + (START-1)*smoothstep(t)` |
| HUD 透明度**与退场无关** | 8903 | `float hudAlpha = introStarted ? 1f : 0f;` |
| COMBO 标签额外淡出 | 8641-8646 | `1 - smoothstep(t * 40/35)` |
| 准确率共用该 alpha | 8599 | `hudAlpha * officialComboLabelOutroAlpha(...)` |

`drawHudTexts`（8545-8639）全函数体里只有 4 处 alpha，其中 3 处是上面那个标签 alpha。

**移植侧缺陷**：`GameScreen.cpp:2072-2084` 把 `outroProgress` 写死 0、只乘 0.40 s 的 alpha；
`RecordSession.cpp` 更是从未赋值。`HudPass.cpp` 的实现**本来就是对的**，只是没人喂值。

**修法**：`outroProgress = clamp(elapsed / (40/60), 0, 1)`，alpha 恒 1。

**逐帧证据**（录制产物抽帧，顶部 15% 条带）：若在淡出，`maxInk` 会下降；若只是移出画面，它保持。

| 帧 | 墨迹数 | maxInk | 底部条带 |
|---|---|---|---|
| 173 | 4018 | **255** | 518 |
| 178 | 2735 | **255** | 165 |
| 183 | 1201 | **255** | 0 |
| 187 | 165 | **254** | 0 |

**反向对照**（把 `RecordSession` 改回旧写法重新录制）：每帧恒为 **1823 / 255 / 535**，
HUD 一动不动。这就是"整体渐隐"的真身——比渐隐更糟，因为淡出窗口起点 1.7 s 已在录制结束之后。

### 3. 校准工具打不开 / 退不出设置页（两个独立缺陷）

**(a) 打不开**：校准音频用 `platform::assetRoot() / "audio" / "calibration.wav"` 走
`decodeFile` → `ma_decoder_init_file`，而打包构建里它**不是文件**。
`unzip -l app-release.apk` 确认 `assets/audio/calibration.wav`（352,844 B）在包里，
但文件路径打不开。改用 `AssetManager::materialize`。

**(b) 退不出**：`SettingsScreen.cpp` 打开校准时执行 `context.settingsReturn = Screen::Settings`，
**覆盖了设置页自己的返回目标**。于是校准页返回 → 进设置页，设置页返回 → 又回设置页。
修法：给校准页独立字段 `CalibrationRuntime::returnTo`，不再复用它人的目标。

### 4. SDL3 文件框架与导出目录

**先读头文件得到的硬结论**（SDL 3.4.18，`third_party/SDL3/include/SDL3/SDL_version.h`）：

- `SDL_GetUserFolder` 在 Android 上是**空桩**：`src/filesystem/android/SDL_sysfilesystem.c`
  里对所有 `SDL_Folder` 都 `SDL_Unsupported(); return NULL;`。
  → `SDL_FOLDER_VIDEOS/DOWNLOADS` **拿不到**导出目录，这是后端没写，不是参数问题。
- `SDL_Storage` **没有 Android/SAF 后端**：`src/storage/` 只有 `generic` 和 `steam`；
  Android 上 `SDL_OpenUserStorage` 等价于 `SDL_GetPrefPath`（即原来的私有目录），
  **换 Storage 换不来任何新位置/新权限**，所以没有迁移 56 处 `std::filesystem` 调用点。
- Android `SDL_ShowOpenFolderDialog` 直接 `SDL_Unsupported()`，所以两个"选目录/选文件夹"
  入口在该平台如实报告不可用。
- 对话框 filter 是**扩展名列表**（`json;pec;txt`），`validate_list` 明确拒绝 `*.json` 和
  `*.*`；旧的 glob 写法全部改写，"所有文件"改成不传 filter。

**已迁移**：`src/platform/FileDialog.cpp` 删掉手写 Windows 壳代码（COM 公寓 /
`GetOpenFileNameW` / `SHBrowseForFolderW` / 手写双 NUL 多选解析）与 Activity 自建请求路径，
两个平台都走 `SDL_ShowOpenFileDialog` / `SDL_ShowOpenFolderDialog`。

**导出目录**：`Environment.DIRECTORY_MOVIES/Chigros` 与 `DIRECTORY_DOWNLOADS/Chigros`
（参考实现的 `EXPORT_DIR` 就是 `"Chigros"`，`ui/ReplayManager.java:38`）。
三个设备侧缺陷必须同时修才能写进去：

1. **权限没声明**：`AndroidManifest.xml` 原来只有 `INTERNET`。加
   `WRITE_EXTERNAL_STORAGE`（`maxSdkVersion="28"`）+ 首次使用时请求。
2. **目录没校验可写性**：存在但拒绝写的目录看起来可用，会在录制末尾才失败。现在用探针文件
   试写，不可写则回退私有目录。
3. **路径没落地**：实测生效——`/sdcard/Movies/Chigros` 于 13:20 创建，
   `dumpsys` 显示 `WRITE_EXTERNAL_STORAGE: granted=true`，
   渲染页"输出目录"显示 `/storage/emulated/0/Movies/Chigros`。

### 5. 冷启动黑屏

**量化**（三冷启动，`am force-stop` 间隔）：

| 阶段 | 修前中位数 | 修后中位数 |
|---|---|---|
| `SDL_Init` | 72.1 ms | 78.5 ms |
| `GL context + ImGui` | 644.4 ms | 613.5 ms |
| `renderer + shaders` | **2827.6 ms** | **2278.2 ms** |
| `glyph cache` | 50.1 ms | 45.6 ms |
| **到第一帧总计** | **3913.3 ms** | **3231.2 ms** |

**拆开渲染器**后才看清钱花在哪：`text=60ms textures=2656ms shaders=13ms`——
**97% 是纹理加载**，其中绝大部分是解码（纯 CPU，不需要 GL 上下文）。桌面逐资源探针
（`RECHIGROS_TEXTURE_LOG=1`）：`hit_fx.png` 解码 141.9 ms、`background.jpg` 67.9 ms、
`WireNoise.png` 28.4 ms。

**修法**：启动纹理的解码放到 worker 线程，并且**与字形层初始化重叠**
（`beginPrefetchAssets` / `awaitPrefetchAssets`）。

**这里踩了一个坑**：第一版 start 完立刻 await，四个忙线程把字形层从 60 ms 顶到 **570 ms**，
渲染器阶段反而更慢（2498 → 2458 ms，等于没赚）。**必须重叠，不能只是换个顺序。**

**剩余归因**：窗口在 ~300 ms 就显示（`Displayed … +273..+326ms`），所以 2.85–3.23 s 里
约 **2.5–2.9 s 是窗口已存在之后的黑屏**。现在最大块是 `textures`（1.8–2.2 s，设备解码比桌面慢
一个量级）和 `GL context + ImGui`（195–623 ms，模拟器软件 GL 抖动 430 ms）。

### 本轮踩到的坑（新增，务必记住）

1. **修完必须做反向对照。** HUD 退场那条如果只报"墨迹数下降"，淡出也能满足；
   加上 `maxInk` 才能区分"移出画面"与"变淡"。反向对照（改回旧代码）则是证明
   "这个改动确实起了作用"的唯一手段。
2. **`ps -A -o TIME` 冻结是判断"阻塞 vs 慢"最快的手段。** 65 s 墙钟只涨 4 s 就是阻塞。
3. **等待一个线程完成的工作，不要用与高频路径共享的锁。** 638 ms 的 monitor contention
   就出在这个细节上。
4. **"目录存在"不等于"目录可写"。** 分区存储之前的系统上，公共目录存在但拒绝写入。
5. **加解码线程要让它重叠在别的阶段下面。** 立刻 join 只会把并行变成串行的额外开销。
6. **设备系统版本要从设备读，不要从 AVD 配置推断。** 本机多 AVD，
   实际连的是 SDK 28 而不是 API 31。
7. **装完 APK 要核对 `dumpsys package … lastUpdateTime`。** 本轮有两次"诊断日志没出现"，
   根因是 APK 根本没装上（时间戳没变），白等了一轮。


### 补充：编码线程在设备上仍可能被平台写入器卡住（本轮实测）

修掉主线程占用后，**又暴露出一个独立的卡死**，必须记录：

**现象**：设备录制跑到中途，`mp4` 停在 4,239,512 字节不再增长，`ps -A -o TIME` 冻住。
逐线程采样 25 秒墙钟：

```
tid  name         before  after  delta
19892 SDLActivity      22     25      3
19914 SDLThread       311    311      0
total ticks delta over 25 s: 3 = 0.03 s CPU
```

**整个进程 25 秒只跑了 0.03 秒 CPU**，所有线程 state=S（睡眠）。这是死锁，不是慢。

**机制**（可从日志时序与线程状态推出）：平台的 `MPEG4Writer` 在自己的线程里卡住
（`Received total/0-length (13/0) buffers` 之后就再没有输出），而 JNI 调用
（`queueFrame` → Java `queueFrame` 是 `synchronized`）正阻塞在它持有的桥接锁上；
于是编码线程卡在 JNI 里，帧队列不再被消费，主线程的 `queueSpace_.wait` 永远等不到空位。

**已做**：给帧队列等待加了 20 秒上限，超时报告 "the encoder stopped accepting frames"
并走失败清理。**正常编码器远达不到这个上限**（桌面同一队列 1.44 ms/帧进、1.44 ms/帧出，
端到端仍 `RECORD PASS frames=281`）。

**未做 / 未验证**：平台写入器**自身**为什么停住没有查明——需要能挂原生调试器的环境
（`/proc/<pid>/task/<tid>/stack` 与 `syscall` 在非 root 的蓝叠上都是 Permission denied）。
现在它表现为**可报告的失败**而不是冻结的窗口，但不是"已解决"。

**另注**：设备端每帧视频只有 ~12–572 帧进容器（`Received total/0-length (13/0) buffers`、
`(573/0)`），而总帧数是 10689。这与"软件 GL 模拟器上 `glMapBufferRange` 回读极慢"一致，
**不代表真机行为**；真实吞吐要按第十四节的真机数据（5.84 s / 611 帧）为准。



---

## 十六、真机验证（2026-10-27，Redmi 23049RAD8C / Android 15 / SDK 35 / arm64-v8a / root）

设备 `88be2138`，`ro.board.platform=taro`（骁龙 8 Gen 1），1080x2400，root 可用（KernelSU）。
这是**真 GPU + 真硬件编码器**，与蓝叠的软件 GL 完全不同，**以本节数字为准**。

### 1. BUG 1 在真机上不复现（修复有效）

15:20:39 从渲染页开始录制，全程界面在画：进度弹窗显示
`正在渲染 / 3234 / 10689 帧 30%` 且进度条推进，进程 CPU 稳步增长。

| | 模拟器（修复前） | 真机（修复后） |
|---|---|---|
| 主线程被占 | **69.11 / 68.55 / 74.33 s** | 0 |
| 进度弹窗 | 从未画出 | 画出并持续更新 |
| 产物 | 0 字节 mp4 + 68 MB pcm | 完整 126.5 MB mp4 |

`[record] finished … frames=10689`，ffprobe：

```
video: h264, 1920x1080, 60/1, duration 178.116667
audio: aac, duration 177.066667
container duration 178.149667, size 126541621, bit_rate 5682485
```

**注意**：模拟器上那个"平台写入器卡住"的次级死锁**在真机上没有复现**，
所以 20 s 有界等待在真机上没有被触发过；它是安全网，不是已证明的必要路径。

### 2. BUG 4 分区存储：export 目录必须走媒体服务

Android 15 上直接 `createNewFile` 写 `/storage/emulated/0/Movies` 被拒：

```
W ReChigrosImport: public directory is not writable: /storage/emulated/0/Movies
W ReChigrosImport: java.io.IOException: Operation not permitted
W ReChigrosImport:   at java.io.File.createNewFile(File.java:1022)
```

**这是本轮的关键教训**：第 4 节那套「查目录可写性」在 SDK 28 上够用，
在 SDK 35 上**必须**改成「私有暂存 + 完成后交给媒体服务」：

```
I ReChigrosImport: published 20261027-153624.mp4 to Movies/Chigros
I SDL/APP: [record] finished Movies/Chigros/20261027-153624.mp4 frames=10689
```

媒体库查询（相册/文件管理器读的就是这条）：

```
Row: 422 _display_name=20261027-153624.mp4, relative_path=Movies/Chigros/, _size=126386153
```

### 3. BUG 2 HUD 退场：真机产物逐帧验证

用真机自己录的 10689 帧录像抽末段（168 s 起，60 fps）。判据：
**淡出会让 maxInk 下降；位移出画面则它保持 255。**

| 帧 | 顶部 15% 墨迹 | maxInk | 底部 15% 墨迹 |
|---|---|---|---|
| 500 | 2792 | **255** | 532 |
| 505 | 1999 | **255** | 250 |
| 510 | 1077 | **255** | 0 |
| 515 | 350 | **255** | 0 |
| 517 | 224 | **255** | 0 |

约 0.28 s 内顶部从 2792 掉到 224、底部从 532 掉到 0，最亮像素**全程 255**。
即"移出画面"而非"渐变消失"，与参考的 Y 缩放一致。

### 4. BUG 5 冷启动：真机比模拟器快 3 倍

| 阶段 | run 1 | run 2 | run 3 |
|---|---|---|---|
| SDL_Init | 21.3 | 39.8 | 26.7 |
| GL context + ImGui | 111.7 | 102.9 | 93.0 |
| audio device | 51.0 | 90.6 | 80.6 |
| **renderer + shaders** | **747.0** | **500.1** | **525.2** |
| glyph cache | 44.6 | 39.9 | 38.1 |
| library warm-up | 80.7 | 70.3 | 59.4 |
| **到第一帧总计** | **1151.1** | **906.6** | **896.7** |

渲染器细分（真 GPU）：`textures` 396–553 ms、`text` 58–192 ms、`shaders` 2.8–3.4 ms。

对比模拟器同改动后（总计 2851–3311 ms、textures 1784–2203 ms）：
**真机快 3 倍，纹理阶段快 4–5 倍**。**之前用模拟器数字判断"纹理是瓶颈"这个结论
在真机上依然成立（仍是最大项），但绝对量级完全不同，不要交叉引用。**

`Displayed … +579..+759ms` → 窗口约 0.6 s 出现，首帧 0.9–1.15 s，
**剩余黑屏 300–550 ms**。真机上 `GL context + ImGui` 只有 93–112 ms
（模拟器 195–660 ms），说明模拟器那块抖动也是软件 GL 的产物。

### 5. BUG 3 校准页在真机上正常

`build/shots/rn/calibration.png`：面板完整、判定线上有音符图、打击特效贴图解析成功。
修复前报「校准音频加载失败」并立刻退回。

### 6. 真机游玩画面

`build/shots/rn/game_mid.png`：HUD 完整（62 连击 / AUTOPLAY、分数 0059104、
曲名 ハテ EZ、难度 Lv.EZ）、判定线、音符、打击特效齐备。

### 本节未完成

- 设备在会话后段掉线（`device not found`，重连无效）。**BUG 3(b) 的返回路径
  只做了代码核对与模拟器验证，尚未在真机上点过。**
- 真机 root 可用，但没用到 `/proc/<pid>/task/<tid>/stack` 的原生栈取证——
  因为 BUG 1 在真机上没有复现。
- Android 15 的 `WRITE_EXTERNAL_STORAGE` 不再生效，`ensureSharedStoragePermission`
  在 SDK>28 时直接返回 true；实际走的是媒体服务路径，权限分支在真机上未被覆盖。



### 7. BUG 3(b) 的设置页返回路径：补上了一个能真正抓住缺陷的测试

原修复（`calibration.returnTo` 独立字段）本身是对的，但**最初写的回归测试是废的**：
它直接设置 context 字段再调 `exitCalibration`，而缺陷在**点击处理函数**里。
反向对照证明：把 bug 加回去，测试**照样通过**。

改成走真实指针路径后：
1. 音频页会滚动，且**只有视口内的行才登记矩形**——不滚动时只登记 5 个，校准行不在其中；
2. 所以测试先滚到该行，再向页面要它刚登记的矩形，按矩形中心点击；
3. 按下与松开必须跨帧（一帧内完成不会登记点击）。

反向对照（把 `context.settingsReturn = Screen::Settings` 加回去）：

```
[FAIL] opening calibration leaves the settings page's target alone  (settingsReturn=6)
[FAIL] the settings page can still be left after a calibration attempt
SELF-TEST FAIL (2 of 56 checks failed)
```

`settingsReturn=6` 就是 `Screen::Settings`，即"退不出设置页"的确切机制。
修复在位时 **SELF-TEST PASS (56/56)**、ctest 8/8。

**教训**：写回归测试时先做一次反向对照。测试直接摆弄状态、而缺陷在交互路径上，
这样的测试永远不会失败，也永远抓不到回归。

---

## 十七、六个后续缺陷（2026-10-27）

### 1. 音频准备耗时与进度：AAC 调度是瓶颈，不是样本转换

**复现**：蓝叠 SDK 28 / x86_64，数据根 `refdata-hate-ez`，进入渲染页后点击开始：

```
adb -s 127.0.0.1:5555 shell "am start -n com.wuying.chigros/.MainActivity -e args '--data-root /sdcard/Android/data/com.wuying.chigros/files/refdata-hate-ez --screen render:ハテ --frames 100000 --fixed-delta'"
adb -s 127.0.0.1:5555 shell "input swipe 518 886 518 886 120"
```

修前第 5 秒和第 30 秒都显示 `0 / 10689 帧 0%`（`build/shots/bug1-before-5s.png`、
`bug1-before-30s.png`）。输入是 8,499,200 帧、48 kHz、双声道 float PCM。

**先量化再优化**：同一旧算法加阶段计时后，得到：

| 阶段 | 修前 | 修后实测范围 |
|---|---:|---:|
| 读 PCM | 384 ms | 96–132 ms |
| float→short | **284 ms** | 480–551 ms |
| AAC 编码与队列等待 | **67,411 ms** | **1,926–3,215 ms** |
| muxer 写整轨音频 | 548 ms | 483–678 ms |
| 点击→首个 muxer 视频帧（完整墙钟） | **70.799 / 71.114 s** | **4.538 / 5.900 s** |

所以「逐样本转换占大头」这个线索被实测否定。根因是旧循环每取出一个 AAC 包，
就重新进入 `dequeueInputBuffer(10ms)`，而不是先排空已经可用的输出；每个包都可能多吃一次
输入槽等待。修为有输入就提交，随后一次排空全部可用输出；仅在输入全部提交后阻塞等输出。
float→short 同时改成 little-endian `FloatBuffer` + 编码器输入的 native `ShortBuffer`，
但这不是主要收益来源。AAC profile、192 kbps、声道顺序、量化公式、PTS 公式均未改：
修前修后都是 **8300 包 / 4,249,601 字节 / 首 PTS 1,083,000 μs / 末 PTS 178,128,332 μs**。

参考源码没有这条录制音频准备管线；`PcmDecoder.java:201-209` 只提供解码后的 NIO 批量读取
写法。此项不改变任何可见时序语义，不根据录像或猜测新增行为。

**进度**：worker 以已提交 PCM 帧数发布 0..999 千分比，完整发布后才是 1000；经 JNI /
`VideoEncoder` / `RecordSession` 到 `RenderScreen`。音频阶段显示真实百分比，视频阶段仍显示
`renderedFrames / totalFrames`。连拍证据：

- `build/shots/bug1-seq-3.png`：10%
- `build/shots/bug1-seq-4.png`：40%
- `build/shots/bug1-seq-5.png`：64%
- `build/shots/bug1-seq-6.png`：91%
- `build/shots/bug1-seq-7.png`：进入视频阶段，18 / 10689 帧

**反向对照**：只把 worker 的进度发布固定为 0，保留其余优化，同一真实点击路径在
`build/shots/bug1-reverse-5.png`、`bug1-reverse-6.png` 连续显示「正在编码音频 0%」，
随后进入视频 18 帧；恢复后才是上面的递增序列。不是直接给界面塞一个假进度。

**回归与未解决项**：桌面 ctest 8/8、自检 56/56；Android Release 构建成功。既有
`VideoEncoderBridgeProbe` 已补「等音频就绪再送视频、进度单调递增、AAC 包数/时长」断言，
但此蓝叠镜像安装 9 KB 测试 APK 后会让主机退出（主 APK 正常），所以**探针未在本机执行**，
不能记为通过。模拟器后续仍可能报 `the platform encoder stopped accepting frames` 并删除
未完成 mp4；这与第 15 节记录的平台写入器问题相同，本项只验收音频准备和首帧，不宣称
模拟器整段录制已经修好。真机本轮不在线，需重连后复核硬件 AAC 与完整产物。

### 2. 录制音画同步：开场静音必须进样本，不能只写起始 PTS

**同步参考（完整函数已读）**：

- `easy-phi-main/test_files/test_shared.hpp:35-95` 初始化视频 `time_base={1,fps}`、音频
  `time_base={1,sampleRate}`，两帧的 PTS 都从 0 开始；`writeAudio`（97-115）按样本数推进音频，
  `writeVideoFrame`（117-130）按帧推进视频，`flushStream`（150-156）只负责时间基换算与交错写包。
- `open_rpe_recorder.cpp:586-617` 先混出最终 PCM，再以 `frameCut / fps` 逐帧绘制。
  对齐依据在样本和帧本身，不依赖封装器猜测相对起点。
- `Phi-Recorder-main/src-tauri/src/render.rs:568-592` 把音乐放入输出样本数组，
  `618-622` 按音符时间放打击音；`875-954` 再映射音视频流。
- 原项目 `GameRenderer.java:565-566` 的开场延迟为 **1083 ms**，`4263-4265` 的谱面时间是
  `musicPos - chart.offset - userOffsetSec`。

**测量数据根**：`build/sync-data-{zero,plus,minus}/packs/sync`，单张 9 秒合成谱面，
BPM 120，分数每次命中都变化。必须直接传入 `chart.json`，不能用 `--screen render`：
截图 harness 的 `applyReferenceSettings` 会把 `audioOffsetMs` 重置为 0，造成假对照。

桌面命令（三个数据根各一次）：

```
build/desktop-release/src/app/ReChigros.exe --data-root build/sync-data-zero \
  build/sync-data-zero/packs/sync/chart.json --window 1280x720 --fixed-delta \
  --record build/sync-before-zero.mp4
```

移动端把 `packs` 与 `settings.txt` 推到设备后，用同一参数和 `--record` 输出到应用外部目录。
分析脚本是 `build/measure_sync.py`、`build/measure_score.py`：前者读 ffprobe、首包、音频瞬态，
后者从右上角分数变化确定真实判定帧。COMBO 区域会被飞过的音符污染，已弃用该探针。

**桌面结论**：现有 `adelay=1083|1083` 已把静音写进样本，无需改桌面路径。
0 ms 时打击音与分数帧的误差为 **0.2–3.5 ms**；+200 ms 的判定帧整体 +12 帧、打击瞬态
约 +202.9 ms，-200 ms 整体 -12 帧、约 -200.0 ms；三条容器音轨均为 **12.183 s**
（解码后均 12.202667 s），校准没有改变音乐长度。

**移动端根因与反向对照**：旧真机产物 `build/shots/rn_record.mp4` 是视频起点 0、
音频起点 1.083 s，原始 PCM 本身没有这段静音。保留输入槽修复、只恢复旧 PTS 路径录得
`build/sync-reverse-android.mp4`：首打击瞬态 **0.046479 s**，首次分数判定 **1.083333 s**，
即声音早 **1036.854 ms**；muxer 还把首音频包归一化到 0。这就是原缺陷的完整复现。

另有 AAC priming：仅把静音放进 PCM 后，打击音仍晚 **46.146 ms**。设备输出格式没有
delay/padding 字段；从开场静音中取 **2048 帧**吸收 priming、末尾补回等量静音，既不丢音乐，
也不改变输入总帧数。最终 `build/sync-primed-android.mp4`：

| 指标 | 结果 |
|---|---|
| 视频 / 音频 start_time | **0 / 0** |
| 视频 / 音频首包 PTS | **0 / 0** |
| 首打击瞬态 vs 首次分数判定 | 1.086812 vs 1.083333，**晚 3.479 ms** |
| 后续打击误差 | 约 **0.6–10.6 ms**，均 <16.7 ms |
| 完整录制 | `frames=731`，19,201,654 B |
| 反向对照 | **早 1036.854 ms** |

移动端 +200/-200 ms 也分别移动判定帧 ±12 帧和打击音约 ±200 ms，音轨长度不变；
证据在 `build/sync-offsets-android-measure.txt`。

**顺带必须修的测量阻断**：`submitFrame` 原来做 64 次零等待轮询，模拟器尚未来得及释放
输入槽就拒绝第二帧，成品被删除。改为先轮询，再排空输出并做总共至多 100 ms 的短等待，
短谱面才得以完整生成。它不是用放宽同步判据换来的成功。

**回归与限制**：ctest 8/8、自检 56/56，Android 主包与测试包均编译成功，最终安装
`lastUpdateTime=21:53:29`。测试 APK 仍因第 1 条所述蓝叠主机问题无法执行。2048 帧 priming
已在软件 AAC 上实测，**真机硬件 AAC 尚未复核**。录制日志为 731 帧而 ffprobe 视频为 715 帧，
说明尾部捕获/收集仍有独立差异，留待噪域与捕获路径项排查；前 6 秒同步测量不受尾帧影响。

### 3. HUD 退场：标签要淡出，数字与其余元素只做 Y 缩放

**逐元素源码结论**（`GameRenderer.java`）：根变换在 8857-8859，缩放曲线在 8925-8929，
`hudAlpha` 在 8903 只取决于开场，并非整体淡出。各元素如下：

| 元素 | 参考位置 | 应有行为 |
|---|---|---|
| 暂停键 | 8951-8959 + 根变换 | 只随根 Y 缩放/平移 |
| 进度条 | 8962-8963 + 根变换 | 只随根 Y 缩放/平移 |
| 分数 | 8586-8595 | 只随根 Y 缩放/平移 |
| 准确率 | 8596-8603 | Y 缩放 + 与标签共用退场 alpha |
| COMBO 数字 | 8625-8630 | **只随根 Y 缩放/平移，不用标签 alpha** |
| COMBO 标签 | 8632-8637 + 8641-8646 | Y 缩放 + `1-smoothstep(t*40/35)` 淡出 |
| 曲名 | 8562-8570 | 只随根 Y 缩放/平移 |
| 难度 | 8573-8583 | 只随根 Y 缩放/平移 |

移植侧 `HudPass.cpp` 已计算 `comboAlpha` 并用于准确率，但标签绘制仍只传 `textAlpha`。
这就是本项根因：不是根缩放缺失，也不是整体渐隐，而是**标签专用 alpha 没接到标签上**。
修复只在 `HudElement::ComboLabel` 的调用上乘 `comboAlpha`，其他元素不动。

**复现与修复数字**：`build/sync-before-zero.mp4` 第 640 帧中，数字和其余 HUD 已离开，
RECORD 仍是全亮白字。修复后 `build/hud-after-zero.mp4` 同帧标签消失。中央标签区域：

| 帧 | 修前近白像素 / max | 修后近白像素 / max |
|---|---:|---:|
| 610 | 1380 / 255 | 1360 / 255 |
| 620 | 1474 / 255 | 991 / 255 |
| 630 | 1013 / 255 | 473 / 255 |
| 640 | **578 / 255** | **0 / 122** |

Android 同项对照：旧 580 / 255，新 **0 / 122**（`build/hud-android-measure.txt`）。
这区分了淡出与位移：其他元素在仍可见时 max 保持 255，标签则提前变暗并清除。

**四种标签场景**：

- RECORD：`hud-after-01.png` 至 `hud-after-04.png`，逐帧淡出。
- AUTOPLAY：`hud-autoplay-f1950.png` 中途完整，`hud-autoplay-f2100.png` 已清除。
- REPLAY：有效判定回放（14 notes、16 entries，实际 combo=14）在
  `hud-replay-valid-f1950.png` 完整，`hud-replay-valid-f2100.png` 已清除；两次回放成绩均与录制一致。
- COMBO：`rechigros_render_smoke --outro 0` 的 `hud-smoke-combo-0.png` 完整，
  `--outro 0.875` 的 `hud-smoke-combo-875.png` 清除。该参数只用于诊断真实 HUD 渲染。

注意 combo<3 时本来就不画标签，不能用“全 Miss 回放标签消失”冒充退场证据；上述 REPLAY
特意使用有效判定条目，确保标签先出现。所有场景最终都走同一个 `ComboLabel` alpha 调用。

**反向对照与回归**：暂时撤掉 `comboAlpha` 后，`--outro 0.875` 的
`hud-smoke-combo-reverse.png` 仍清晰显示 COMBO，证明探针会失败；恢复后同一帧无标签。
ctest 8/8、自检 56/56，Android 重录成功，最终安装 `lastUpdateTime=22:13:36`。

### 4. 捕获路径：视觉时钟、尾队列与真平面色度

**素材与判据**：本机可访问目录和蓝叠公共目录都没有 TrueHome / True World 录像或谱面；
参考私有目录当前也拒绝 `run-as`，因此指定曲目的逐帧对照**未完成**，不能用其他录像冒充。
改用 `build/capture-redzone/chart.json`（未激活红区淡入，enableTime=2.6s）和
`build/capture-prpr/chart.json`（official_vignette_plus），去掉音符以排除随机粒子。
比较红区/中心区域 SHA-256、RGB 中位数、实际解码帧数，不做整帧原始像素差。

**根因 A：离线捕获仍在读墙钟。** 交互的 `visualTimeSec` 本来就是实时钟，离线则按帧号推进；
旧实现却直接读 `steady_clock`，所以同一视觉时刻的淡入取决于机器速度。参考
`GameRenderer.java:14845-14866` 的实时更新只用于交互；冻结捕获应使用可复现的视觉时钟。
现统一为 `input.visualTimeSec`，新谱面重置 `blockUpdateLastSec`。

**根因 B：prpr 冻结噪声没接参考规则。** 参考 `GameRenderer.java:5910-5940` 明确：
实时用会话时间，冻结捕获用 `chartTimeSec + FROZEN_NOISE_LEAD_SEC`，且该常量为 **3.0**。
移植却只按 prepare 后的墙钟生成噪声。现捕获开始/离屏 PNG 启用 `chartTimeSec+3.0`，
结束恢复实时时钟，释放链时清除开关。

**重复捕获对照**（`build/capture-structural-{after,reverse,android}.txt`）：恢复旧时钟后，
红区与 prpr 两组重复捕获的区域校验和全部不同；修复后桌面与 Android 均逐值一致。
例：红区第 90 帧，旧 A/B 中位数为 [145,132,131] / [133,130,127]，修后两次均为
[158,135,133]。这是同刻结构一致性，不是“看起来差不多”。

**根因 C：正常完成和取消共用一个停止位。** `drainEncoder()` 设置 `encoderStop_` 后，
写线程弹出一帧立即 break 并清空剩余队列，最多丢 16 帧。新增 `encoderDraining_`：
正常完成排空后退出，取消/失败仍立即停止。基线实际解码为桌面 722/731、Android 715/731；
恢复旧关闭逻辑为 730/731，修复后桌面和 Android 都是 **731/731**。

**根因 D：编码桥把交错 U/V 当成所有设备的布局。** 蓝叠首帧实测：
`luma=1920/1、chromaU=960/1、chromaV=960/1`，即真平面；旧代码却把 U/V 交错写进 U，
造成整屏绿色。现 `pixelStride==1` 时分别写两个 plane，交错布局仍按原样成对写。
`build/redzone-old-f90.png` 是旧绿色异常，`build/redzone-planar-f90.png` 已恢复；
`build/redzone-input-yuv.png` 与 `build/redzone-interactive.png` 同场景一致，证明剩余
桌面/模拟器明暗差不是编码器新增失真。临时 plane 日志和 YUV dump 均已删除。

**其他 pass 推广检查**：场景 FBO、bloom、prpr 主 pass、DS 网格、kaleido、暂停背景/HUD
模糊都按当前输出或源纹理尺寸重建/计算，未发现噪域同类旧窗口尺寸缓存。`block_probe`
仍通过：合成与上屏 checksum 1292924955、touch checksum 3607153101、framebuffer/viewport/
blend 复原通过。shader 的 `time`/`timeY`、screen/8 mask 与调用方 viewport 未被改动。

**回归与未验证**：ctest 8/8、自检 56/56。重复捕获、尾帧和真平面修复在桌面与蓝叠均实跑。
真机的交错 U/V 分支未能在本轮真机上复核；指定 TrueHome 素材也不可用，不能用这些结果
推断该曲目或所有硬件编码器均已覆盖。最终无探针验收见 `build/capture-final.log`：
`frames=731` 且 `nb_read_frames=731`，APK `lastUpdateTime=22:55:29`。

