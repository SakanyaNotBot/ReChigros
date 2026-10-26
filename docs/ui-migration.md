# 界面迁移：从 ImGui 到 ObjectivelyMVC

本文回答三件事：现在有多少界面绘制入口、按什么顺序迁移、以及当前工具链能不能承接这次迁移。
所有数字都来自仓库内的检查脚本，可以重跑复现。

```
python tools/ui/imui_inventory.py --json build/ui-inventory.json
```

---

## 一、入口清单（实测）

脚本把入口分成三类：**screen**（各个界面）、**foundation**（界面绘制层本身）、
**infrastructure**（程序外壳与自检，不算界面）。

| 文件 | 类别 | 入口数 |
|---|---|---|
| src/app/LibraryScreen.cpp | screen | 59 |
| src/app/RenderScreen.cpp | screen | 59 |
| src/app/SettingsScreen.cpp | screen | 56 |
| src/app/UiStyle.cpp | foundation | 28 |
| src/app/DetailScreen.cpp | screen | 27 |
| src/app/ResultScreen.cpp | screen | 27 |
| src/app/GameScreen.cpp | screen | 19 |
| src/app/CalibrationScreen.cpp | screen | 14 |
| src/app/CommonScreens.cpp | screen | 12 |
| include/rechigros/app/UiWidgets.hpp | foundation | 11 |
| src/app/UiLayout.cpp | foundation | 11 |
| src/app/Application.cpp | infrastructure | 8 |
| src/app/ScreenCommon.cpp | infrastructure | 8 |
| src/app/SelfTest.cpp | infrastructure | 7 |
| include/rechigros/app/UiStyle.hpp | foundation | 2 |

按类别：screen **273**、foundation **52**、infrastructure **23**，合计 **348**。

按入口类型：

| 入口 | 次数 | 含义 |
|---|---|---|
| `ImGui::` | 244 | 直接调用即时模式界面库 |
| `ui::drawLabel` | 59 | 绘制一行文本 |
| `beginFullscreenWindow` | 11 | 每个界面开一块全屏绘制层 |
| `endFullscreenWindow` | 16 | 与之配对 |
| `ui::drawImage` | 7 | 贴图 |
| `ui::drawText` / `drawTextCentered` | 5 | 单点文本 |
| `ui::drawBackdrop` | 3 | 背景动画 |
| `ui::measureText` | 2 | 文本测量 |
| `ui::iconButton` | 1 | 图标按钮 |

这组数字说明一件重要的事：**界面并不是直接长在 ImGui 上的**。真正跨界的只有 244 个
`ImGui::` 调用，而且集中在少数几个文件里；文本、面板、按钮、贴图已经有自己的封装。
迁移的实际工作是把这些封装换成视图对象的组装，而不是逐个改写 244 处调用。

---

## 二、当前工具链能不能承接迁移（关键结论）

**不能，而且这是构建配置层面就决定的，不是工作量问题。**

界面框架是 GNU C 写的。仓库里的构建脚本对此有明确处理（`CMakeLists.txt`）：

```cmake
if(MSVC OR ANDROID)
  set(RECHIGROS_WITH_OMVC_DEFAULT OFF)
else()
  set(RECHIGROS_WITH_OMVC_DEFAULT ON)
endif()
option(RECHIGROS_WITH_OMVC "Build the ObjectivelyMVC interface framework"
  ${RECHIGROS_WITH_OMVC_DEFAULT})
```

也就是说，**两个实际交付目标（Windows 桌面用 MSVC、Android 用 NDK）上，这个框架默认
就是关闭的**；打开会直接配置失败（`cmake/ObjectivelyMVC.cmake`）：

```cmake
if(MSVC)
  if(RECHIGROS_GNU_C_COMPILER AND EXISTS "${RECHIGROS_GNU_C_COMPILER}")
    set(OMVC_GNU_C_TOOLCHAIN TRUE)
  else()
    message(FATAL_ERROR
      "The interface framework is GNU C, which MSVC cannot compile. "
      "Set -DRECHIGROS_GNU_C_COMPILER=<path to clang.exe> or build with a GNU toolchain.")
  endif()
endif()
```

本轮实测了三条可能的绕行路径，结论如下。

### 路径一：WSL GCC（唯一能完整编译框架的路径）

Ubuntu 24.04 / gcc 13.3 可用，`build/wsl-release` 里**三个框架静态库已经构建成功**：

```
build/wsl-release/libobjectively.a
build/wsl-release/libobjectivelygpu.a
build/wsl-release/libobjectivelymvc.a
build/wsl-release/src/tools/rechigros_omvc_smoke
build/wsl-release/src/tools/rechigros_omvc_settings_probe
```

但框架的渲染设备走 SDL_GPU，而这个 WSL 的 SDL3 构建没有可用的 GPU 后端：

```
$ SDL_VIDEODRIVER=dummy ./build/wsl-release/src/tools/rechigros_omvc_smoke
ERROR: ObjectivelyGPU/RenderDevice.c::450::init:
       SDL_CreateGPUDevice: No supported SDL_GPU backend found!
```

WSL 里有 Vulkan 加载器和 lavapipe 软件实现（`libvulkan.so.1` 的符号可解析，
`/usr/share/vulkan/icd.d/lvp_icd.json` 存在），但 SDL 的 GPU-Vulkan 后端没有编译进去，
强制指定驱动也只会得到 `SDL_HINT_GPU_DRIVER vulkan unsupported`。
**结论：WSL 能编译、不能出图。**没有画面就无法做本文要求的「每批可截图对比」。

### 路径二：用 NDK 的 clang 在 MSVC 主机构建框架

Android NDK 27 自带 clang 18，支持 `--target=x86_64-pc-windows-msvc`。实测可以编译
框架的绝大多数源码（70 个源文件里 **66 个成功**），两个卡点：

1. **Windows SDK 把 `interface` 定义成宏**，而框架把 `interface` 当作结构体成员名。
   用一个强制包含的头（先 `winsock2.h`、再 `windows.h`、再 `#undef interface`）可以绕过。
2. **`regex.h` 不存在**。框架的 `Objectively/Regexp.h` 直接 `#include <regex.h>`，
   剩下 4 个失败文件（`Style.c`、`Theme.c`、`View.c`、`View+JSON.c`）全部卡在这里。
   Windows 没有 POSIX 正则头，需要另写一层兼容实现。

### 路径三：MinGW / clang-cl

本机没有独立的 MinGW 或 clang-cl 工具链（`gcc`/`clang` 均不在 PATH，Visual Studio 的
LLVM 目录只有运行库没有编译器）。安装工具链属于环境变更，不在本轮范围内。

### 结论

**在现有交付工具链上，ObjectivelyMVC 无法构建；在能构建它的环境里，它无法出图。**
所以本轮的界面迁移只能做到「把清单和顺序做到可执行」，不能假装迁完一批。
要继续推进，需要先做一个明确的工程决定，二选一：

- **A. 为 Windows 与 Android 引入 GNU C 兼容工具链**（Windows 侧补 clang + POSIX 正则兼容层；
  Android 侧 NDK 本身就是 clang，主要缺 POSIX 头，需补兼容层），然后按下面的顺序迁移。
- **B. 保留 ImGui，把「全部替换」降级为「把界面绘制集中到可替换的一层」**，即保留 `ui::`
  这一层并把它定义成稳定的视图接口，以后换实现只改这一层。

---

## 三、迁移顺序

顺序的判据是**依赖方向**和**每批能否独立验收**，不是文件大小。

**第一批：SettingsScreen（56 处）**
目标实现已经存在——`src/app/MvcSettings.c`（309 行）已经把设置页用框架写出来了，
包含四个分页、开关行与样式表。这一批的工作量是接线而不是重写，因此它是唯一能最快
证明「构建路径可通」的一批。验收：`--screen settings-video/audio/replay/more` 四个
截图与现 ImGui 版逐项对比。

**第二批：ResultScreen（27）+ DetailScreen（27）**
只读展示为主，互不共享状态，可以并行。结果页含直方图与磨砂背景，是框架
`CollectionView`／自定义绘制能力的试金石。

**第三批：LibraryScreen（59）**
列表、滚动、封面缓存、管理模式多选。这一批决定框架的列表能力够不够用；
不够用要在这里暴露，而不是拖到后面。

**第四批：RenderScreen（59）+ CalibrationScreen（14）**
录制进度条与模态弹窗；校准页有自定义刻度绘制与拖动输入。

**第五批：GameScreen 覆盖层（19）+ CommonScreens（12）**
暂停菜单、Toast、倒计时必须与渲染器的帧同步，必须放在最后，
因为它依赖前面所有批次的输入路由稳定下来。

**贯穿项：foundation（52）**
`UiStyle`/`UiLayout`/`UiWidgets` 被每个界面使用，不能一次性替换。
每批迁移时把该批用到的原语抽成视图接口，最后一批完成后删除空壳。
判断标准：`imui_inventory.py` 里 foundation 的计数归零。

每批的验收条件相同，缺一不收：

1. 构建通过，`ctest` 全绿；
2. 该界面能截图（`--screen <name>`），且与迁移前的同屏截图做像素对比，
   差异只允许出现在字体光栅化与抗锯齿上，布局坐标必须一致（`build/tools/measure.py`
   量文字墨迹包围盒，允许 0–2 px）；
3. `imui_inventory.py` 里该文件的入口数归零；
4. 交互项（开关、拖动、按压）逐项手动过一遍，并在提交信息里写清测了哪些。
