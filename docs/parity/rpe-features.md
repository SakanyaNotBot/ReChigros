# RPE 渲染特性清单

本清单只描述游玩、回放与离线渲染的行为；编辑器命令、布局、撤销、协作、导出和诊断不属于画面功能。依据为只读目录 `rpe-next-main/src/core` 与 `src/ui` 的字段和完整求值函数，原有游玩视觉另核对原实现。不能将某字段“已读取”当成它已正确出帧。

状态：**核心运行时已实现，验证边界见文末**。每项仍有未覆盖的组合时明确标为未测，不能以清单代替实现。

## 本轮实测证据

| 项 | 实测 |
|---|---|
| 轨迹 | 参数圆 `progress=0.25` 得 `(0,200)`、切线 `-180°`；极坐标圆得 `(0,100)`；同一 seed 逐位一致、换 seed 改变；一个事件同时驱动判定线 X/Y/旋转 |
| 独立噪域 | 默认 fixture 中心 `(160,80,80)`、边缘 `(255,84,84)`，外部保持场景；两 Pass 皮肤中心 `(25,229,76)`，命名 `masked` 目标被第二 Pass 采样 |
| 噪声生命周期 | `ready`、`active`、`hiddenAfter` 与旋转后四角由单元测试断言 |
| shader | 50 个内置名称均加载进真实 `PrprChain`，再加 1 个 `paintEvents` 共 **51 个 effect / 0 编译失败**；GLSL ES 静态检查 **88/88** |
| 自定义打击音 | 无音乐谱面录制首音 **1200.0 Hz / peak 22998**，证明命中路径使用包内资源而非 880 Hz 默认音 |
| 解析 | 负 Hold/负流速符号、`skewControl.skew`、父线默认继承、自定义音效路径、噪域六类轨道、全局事件保留均有断言 |

**门禁**：桌面 ctest **8/8**、应用自检 **61/61**、渲染矩阵 **27/27**、应用循环 **13 项**
与字体 **245 字符** 全通过；GLSL ES **88/88**。

**设备实测**：蓝叠 SDK 28（2400×1080，软件 GL）上，默认 RPE 噪域中心
`(154,74,73)`、边缘 `(255,84,84)`；两 Pass 皮肤中心 `(25,229,76)`。APK
`lastUpdateTime=2026-10-28 19:08:55`。

**明确未测**：默认/自定义噪域均在 640×360 桌面 fixture 与蓝叠实测；尚未对真实 RPE 整曲、
`noClear` 历史在暂停/seek 的组合、`lowPerformance` 双通道、单通道输出和外部纹理逐一截图验证。
全局事件只保留定义和值，参考实现本身只有注册与占位类型，没有可据以实现的画面映射；
不得编造映射。RPE 音符的 `yOffset`、剪切和负 Hold 已有数学/解析断言，但仍缺与真实 RPE
整曲逐帧的几何对照。蓝叠为软件 GL，不覆盖真机驱动的多 Pass/纹理格式差异。

## 元数据、时间与资源

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| `META.RPEVersion` | `chart.mjs:32-35`、`scene.mjs:122-131` | 控制 ≥163 的父线旋转默认继承 | 两版本、显式 true/false 对照 |
| `META.offset` | `hitsounds.mjs:6-18` | 谱面在音乐内平移，不改变音乐长度 | 正负偏移下的打击音与判定时刻 |
| 名称、作曲、谱师、插画、曲绘、难度、歌曲、背景 | `chart.mjs:32-35` | 参与库展示和资源选择，不凭空改变几何 | 资源加载与页面显示 |
| `BPMList` 与拍三元组 | `beat.mjs`、`tempo.mjs` | 全曲变速时间轴，拍值按规定量化 | 多 BPM、分子分母、负时间段边界 |
| 判定线 `bpmfactor` | `scene.mjs:27-45` | 每条线独立换算时间与速度积分 | 不同 factor 的同秒对照 |
| 判定线贴图、GIF、文字字体与锚点 | `preview.mjs:512-565` | 实际贴图尺寸/文字排版和锚点定位，不是统一判定线长宽 | 静态图、动画帧、文本与缩放 |
| `attachUI` | `preview.mjs:333-350` | 附着 HUD 元素并保持贴图原色 | 七种元素 |
| `hitsound` / `hitSound` | `hitsounds.mjs:6-18,33-49,74-105` | 按谱面包路径解码；默认 tap/drag/flick；Hold 用 tap；失败可回退 | 实时与录制各一次，解码失败对照 |

## 音符

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| Tap / Hold / Flick / Drag | `chart.mjs:13-15`、`preview.mjs:352-404` | 四类素材、判定、声音与爆发 | 四类自动演示及回放 |
| `startTime` / `endTime` | `scene.mjs:41-50` | 头尾使用全局 BPM 和线 factor | 多 BPM 的头尾位置 |
| `above` | `chart.mjs:52-54`、`scene.mjs:76-90` | 上下侧符号与素材方向 | 双侧出帧 |
| `isFake` | `scene.mjs:47-51`、`hitsounds.mjs:11-17` | 可绘制但不判定、不发声 | 假音符计数与出帧 |
| `speed`（正、零、负） | `scene.mjs:46-50,76-90` | 零速度静止绘制，负速度反向；不能隐藏或取绝对值 | 三速度逐帧位置 |
| 负长度 Hold | `scene.mjs:41-50,87` | 保留头尾时间、floor 与长度的符号 | 未到头、到头后、反向长度出帧 |
| 激活 Hold | `scene.mjs:76-90` | 头固定到 `yOffset`，尾按当前 floor 求值 | 激活前后两帧 |
| `size` / `sizeControl` | `scene.mjs:76-90` | 非 Hold 等比缩放；Hold 横向缩放，不施加控制曲线的 Y 缩放 | 非 Hold 与 Hold 宽高 |
| `alpha` / `alphaControl` | `scene.mjs:88-90` | 音符透明度乘控制曲线 | 多距离采样 |
| `visibleTime` | `scene.mjs:65-73` | 限制出现前的可见窗口，默认 999999 | 窗口边界 |
| `yOffset` | `scene.mjs:41-45,76-90` | 进入头尾 floor，激活后固定；距离再乘速度和侧向 | 多速度、激活 Hold、负速度 |
| `posControl` | `scene.mjs:83-90` | 只修改非 Hold 横向坐标 | 横向轨迹 |
| `yControl` | `scene.mjs:83-90` | 只修改非 Hold 的纵向距离 | 普通音符与 Hold 对照 |
| `skewControl` | `scene.mjs:83-90`、`preview.mjs:399-402` | 非 Hold 顶点剪切，角度为 positionX×控制值；不是宽度缩放，不作用于 Hold | 上下边缘剪切量与纹理形状 |
| `tint` / `color` | `preview.mjs:389-392` | 音符 RGB 着色 | 彩色音符像素 |
| `tintHitEffects` | `preview.mjs:431-436` | 爆发图和粒子独立着色 | 命中爆发区域 |
| `judgeArea` 与控制 | `preview.mjs:380-386`、现有判定模型 | 空间命中区按控制曲线变化 | 同一触点的命中/未命中 |
| 多押高亮 | `preview.mjs:356-359` | 同刻音符使用高亮素材但保持逻辑宽度 | 同刻与不同刻 |
| `zOrder` 与线/Note 分层 | `scene.mjs:105`、`preview.mjs:333-355` | 稳定 z 排序、线与 Hold/普通音符分层 | 重叠区域 |
| `isCover` | `scene.mjs:65-73` | 穿过线之后的头部裁剪规则 | 穿线前后 |
| `father` / `rotateWithFather` | `scene.mjs:108-136` | 累计父线平移和旋转；默认继承取决于版本；防止循环 | 显式/默认/多代父线 |
| `anchor` | `preview.mjs:512-548` | 贴图和文本围绕锚点绘制 | 非中心锚点 |

## 事件层与扩展事件

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| 多 `eventLayers` | `scene.mjs:27-38,54-63` | 各层移动、旋转、透明度叠加，速度分别积分后相加 | 两层不同事件 |
| `moveXEvents` / `moveYEvents` | `events.mjs:5-43` | 起始前使用回退、区间内缓动、结束后保持 | 多点采样 |
| `rotateEvents` | `scene.mjs:54-63` | 角度事件参与线和父线变换 | 旋转后顶点 |
| `alphaEvents` | `scene.mjs:36,65-73` | 透明度及负值隐藏语义 | 正/负 alpha 边界 |
| `speedEvents` | `events.mjs:46-91` | 线性精确积分、其他曲线分段数值积分、支持负值 | 同秒 floor 校验 |
| 29 种缓动及裁剪区间 | `easing.mjs`、`events.mjs:21-25` | easingType、easingLeft/right 全部传入同一求值器 | 边界与中点 |
| 三次贝塞尔 | `events.mjs:21-25` | 使用四点曲线而非线性替代 | 非对称曲线 |
| `scaleXEvents` / `scaleYEvents` | `scene.mjs:36-38`、`preview.mjs:512-516` | 判定线两轴缩放 | 两轴不同值 |
| `colorEvents` | `scene.mjs:36-38`、`preview.mjs:516-544` | RGB 插值及默认贴图着色 | 彩色判定线 |
| `textEvents` | `events.mjs:26-39`、`preview.mjs:552-565` | 普通/前缀/后缀文本插值、`%P%` 数值插值、多行文字 | 多时间点与整数/小数 |
| `inclineEvents` | `scene.mjs:86`、`preview.mjs:355-369` | 改变非 Hold 横向偏移，不作用于 Hold | 倾角与距离关系 |
| `paintEvents` | `shader.mjs:96-145` | 转换为所属线的后处理事件，保留参数与顺序 | 实际中间目标 |
| `gifEvents` | `scene.mjs:36-38`、现有 GIF 资源路径 | 控制动画帧位置 | 同秒帧索引 |
| `Name`、`Group`、`numOfNotes`、`linkgroup`、`inst` | `chart.mjs:22-39`、`events.mjs:5-43` | 编辑元数据；不能伪造额外画面行为。`inst` 不改变已明确的零时长事件结果 | 确认无静默几何副作用 |

## 独立 RPE 噪域

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| `blockAreaList` 生命周期 | `noise-areas.mjs:73-110` | appear ≤ t < disappear | 两端边界 |
| 矩形与反向顶点 | `noise-areas.mjs:89-101` | bottomLeft/topRight 取规范四角，可负坐标 | 顶点校验 |
| `activeIntervals` | `noise-areas.mjs:73-87` | 激活区间裁剪到生命周期、同起点合并 | 多区间与空区间 |
| 零时长激活 | `noise-areas.mjs:73-95` | 瞬时激活进入正确序列，不被过滤 | 单帧状态 |
| `readyFlash` | `noise-areas.mjs:106-114,140-146` | 布尔/数值时长、就绪闪烁强度 | 闪烁相位 |
| 普通/反转覆盖 | `noise-preview.mjs:87-109` | 普通并集；反转区域扣除重叠后叠加，不是简单红色 XOR | 三区域覆盖 |
| 激活/禁用分层 | `noise-preview.mjs:66-83` | disabled（含 ready）与 active 两组各自覆盖并扣除场景 | 不同状态重叠 |
| 默认颜色与边缘 | `noise-areas.mjs:13-15`、`noise-preview.mjs:115-159` | appearance/active/edge 颜色与透明度，边缘平滑 | 中位数与区域墨迹 |
| 移动 | `noise-areas.mjs:102-128` | 以区域中心为基准的 moveX/moveY | 位移后的顶点 |
| 旋转、双轴缩放 | `noise-areas.mjs:47-67,115-128` | 支持不同锚点和事件累计变换 | 多事件、多锚点 |
| `anchor`、`transformStart/End/Step` | `noise-areas.mjs:47-67,119-127` | 保留前后矩阵，瞬时变换链 | 连续/瞬时事件 |
| `Name`、`editorPositionX` | `noise-areas.mjs:184-192` | 名称与编辑辅助信息，不改变覆盖范围 | 不误当位移 |
| `layerId` / `noiseAreaLayers` | `noise-skin.mjs:2-18` | 按声明顺序分别绘制被使用的层 | 多层顺序 |
| `noiseAreaOptions.ignoreTripleInversion` | `chart.mjs:82-85`、`official-chart.mjs:13` | 编辑转换选项；预览没有额外画面分支 | 不擅自反转 |

## 噪域皮肤与自定义着色器图

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| `skin` 路径与默认回退 | `noise-skin.mjs:28-49`、`noise-preview.mjs:32-84` | `shader/...` 相对包根，错误时回退普通噪域 | 有效/缺失资源 |
| 标量、vec2、vec4 参数 | `noise-skin.mjs:76-90`、`noise-skin-renderer.mjs:173-185` | 从 main.glsl 注释读取默认值，层参数覆盖 | 参数实际 uniform |
| `channel: low` | `noise-skin.mjs:20-26`、`noise-skin-renderer.mjs:119-125` | 有低性能定义才切换，否则使用 normal | 双通道对照 |
| `screenTexture` / `blockMask` | `noise-skin-renderer.mjs:146-205` | 场景和几何 mask 分别采样，不能混用官方红区材质 | 中间目标 |
| mask 六位域 | `noise-skin.mjs:173-175`、`noise-skin-renderer.mjs:146-162` | active/非 active/ready 的普通与反转覆盖位分别可采样 | 位域校验 |
| mask 尺寸、目标尺寸与 texel | `noise-skin.mjs:104-113`、`noise-skin-renderer.mjs:119-143` | scaleX/Y 独立分配、采样尺寸和逆尺寸正确 | 非等比窗口 |
| 命名 targets | `noise-skin.mjs:132-169` | r8/rg8/rgba8、nearest/linear、noClear、独立缩放 | 每格式中间目标 |
| 多 Pass 与 inputs | `noise-skin.mjs:145-169`、`noise-skin-renderer.mjs:196-209` | 按图顺序采样 `$screen` 和中间目标，不能读写同一附件 | 多 Pass 校验和 |
| 单通道输出 | `noise-skin.mjs:150-168`、`noise-skin-renderer.mjs:196-208` | `target.r/g/b/a` 只写指定通道，其他通道保留 | colour mask 验证 |
| `noClear` 历史 | `noise-skin-renderer.mjs:173-189` | 历史保存/恢复；暂停同刻重用；回退、大步 seek、参数改变时重置 | 重复、seek 对照 |
| `$screen` 输出 | `noise-skin-renderer.mjs:196-211` | 屏幕 ping-pong，最终回到调用方目标 | 上屏一致性 |
| 外部纹理 | `noise-skin.mjs:92-102`、`noise-skin-renderer.mjs:99-111` | 相对路径、flipY、clamp/repeat/mirror、nearest/linear | 纹理方向与边界 |
| 线性/sRGB、straight/premult | `noise-skin-renderer.mjs:12-13,99-111` | 按规定色彩转换和预乘 | 像素值 |
| `time` | `noise-preview.mjs:70-73`、`noise-skin-renderer.mjs:203` | 与场景相同的音乐时间，不使用墙钟 | 同刻重复 |
| `touchCount` / `touchPoints` | `noise-skin-renderer.mjs:31,203` | 预览无触点传空；交互路径若提供触点则保留同一坐标约定 | 空触点与触点对照 |
| `touchReleaseDuration` | `noise-skin.mjs:22-27,86-90` | 非负参数，可覆盖，不能伪造预览没有的状态 | 参数边界 |

## 轨迹（版本 210）

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| `trajectory.version` 与采样 | `curve-trajectory.mjs:77-145,176-181`、`scene.mjs:30-34` | moveX 轨迹可派生 moveY 和旋转，不能只按普通事件丢弃 | 三轴采样 |
| 参数式 / 极坐标 | `curve-trajectory.mjs:86-110` | 独立起止表达式，参数/角度坐标 | 两种模式 |
| X/Y 独立缓动 | `curve-trajectory.mjs:94-107` | easingX、easingY 分开求值 | 不同缓动 |
| 起止裁剪、缩放、旋转 | `curve-trajectory.mjs:94-110` | trimStart/End、scaleX/Y、rotation | 顶点和包围盒 |
| `alignStart` | `curve-trajectory.mjs:112-138` | 首点对齐 authored startX/Y | 对齐前后 |
| 切线旋转与连续圈数 | `curve-trajectory.mjs:112-134` | 切线角度，1024 点参考，360° 解卷绕 | 跨 ±180° |
| `rotationExpression` | `curve-trajectory.mjs:91-110` | 显式方向表达式及变量替换 | 表达式采样 |
| 星形/随机多边形 | `curve-trajectory.mjs:97-103` | 顶点插值、点数钳制、半径、inner | 顶点数量与形状 |
| seed / randomness | `curve-trajectory.mjs:79-110` | 确定性随机形状，不允许墙钟随机 | 重复与不同 seed |
| 参数与数学表达式 | `batch-script.mjs:4-77` | 变量、pi、true/false、函数、运算优先级、幂、比较、逻辑、三元 | 表达式与错误输入 |
| 预设形状 | `curve-trajectory.mjs:11-39` | line/circle、polygon-3/4/5/6/8/12、arc、rectangle、diamond、star、ellipse、heart、spiral、rose、infinity、lissajous、parabola、sine、damped-wave、cycloid、astroid、cardioid、limacon、log-spiral、hypotrochoid、epitrochoid、superellipse、random-wave、random-polygon、random-loop | 由通用求值器逐形状覆盖，不为预设写视觉特判 |

## 后处理与全局事件

| 特性 | 源码依据 | 应有行为 | 验收 |
|---|---|---|---|
| 根 `effects` / `shaderEvents` / `META.effects` | `shader.mjs:96-110`、`shader-events.mjs:33-43` | 与 paintEvents 合并，line 循环映射，排序后运行 | 三处来源同刻 |
| `global` 与 `order` | `shader.mjs:81-93,109-145`、`preview.mjs:93-118` | 舞台 shader → UI/噪域 → 全局 shader，order/start 决定顺序 | 中间目标与层次 |
| shader 时间线参数 | `shader.mjs:46-79,137-145` | 常量、向量、逐事件曲线、line bpmfactor、默认值 | 多参数同刻 |
| 50 种内置 shader | `shader.mjs:5-10`、`assets/rpe/shaders` | chromatic/circle_blur/fisheye/glitch/grayscale/noise/pixel/radial_blur/shockwave/vignette（含 `_2`）；liquid/flowing/image_noise/snow/glow_effect/flip/night_vision/scanline/rain/rainbow；flare/underwater/fog/kaleidoscope/emboss/sobel/oil_painting/distortion/hologram/burn；camera/lightning/old_tv/bokeh/neon/heat_distortion/rays/color_shift/wave/two_tone | 全部编译、运行非空；pr 文件仅作对应备用资源 |
| 全局事件版本 240 | `global-events.mjs:4-45,47-103` | 保留类型声明、五类值（number/text/boolean/color/json）、默认/重叠/事件曲线；未知类型不得静默改名 | 通用求值与序列化边界 |
| 全局扩展的画面映射 | `global-events.mjs:47-120` | 参考当前只有注册机制和占位类型，不存在可凭空推断的 shader/几何映射；不得自拟视觉行为 | 若后续出现具体注册类型再逐项补齐 |

## 实现状态汇总

| 组 | 状态 |
|---|---|
| 负 Hold/负速度、`yOffset`、`skewControl`、父线默认值 | 已进入解析与绘制路径；解析/符号/时间基有测试，整曲逐帧对照未做 |
| 自定义打击音实时与离线 | 已接两条路径；离线 1200Hz 实测，实时设备听测未做 |
| 独立噪域运行时 | 生命周期、变换、激活/反转 mask 已实现；默认与两 Pass 皮肤桌面 fixture 实测 |
| 噪域皮肤完整图 | 参数、命名 target、多 Pass、纹理/通道代码已实现；noClear、low 通道、单通道、外部纹理未逐项实测 |
| paintEvents 与 shader 排序 | 已接根 effects/shaderEvents/META/paint；51 effect 真实加载、0 编译失败 |
| 50 种 shader | 资源与兼容层已接入；全部真实链接；视觉结果未逐种与参考截图比对 |
| 轨迹求值器与三轴 | 已接入；参数/极坐标/随机/切线/三轴有数值测试；全部预设未逐项视觉验收 |
| 全局事件 | 原始定义和值保留；参考没有画面映射，不能实现未定义视觉 |

验证原则不变：先真实调用路径复现，修改后实测；反向恢复缺陷时断言或结构化指标应失败。
剩余未测项必须按上表边界交付，不能写成“全部通过”。
