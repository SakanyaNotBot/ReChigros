# ReChigros

ReChigros 是一个节奏音游播放器的现代 C++ 跨平台重写。游戏逻辑、谱面解析、判定与渲染在
Windows、macOS、Linux、Android 与 iOS 上共用同一套源码，平台差异只存在于 CMake 条件与
`platform/` 包装工程中。

## 技术栈

| 层 | 技术 |
| --- | --- |
| 窗口、事件、输入、主循环 | SDL2 2.30.11 |
| 渲染 | OpenGL 3.3 Core（桌面）/ OpenGL ES 3.0（Android、iOS） |
| GL 加载 | glad |
| 音频 | miniaudio 0.11.21，单回调软件混音 |
| 谱面与配置 JSON | nlohmann/json 3.11.3 |
| 菜单与工具界面 | Dear ImGui 1.91.8 |
| 图像解码 | stb_image / stb_image_write / stb_truetype |
| ZIP 包解压 | miniz |
| 构建 | CMake ≥ 3.20、C++20、RAII |

依赖在配置阶段按固定版本与 SHA-256 校验下载，或从已解包的 `third_party/SDL` 就地使用，
构建过程不需要 Git clone。

## 目录结构

```text
assets/                     可执行文件同级资源（103 个文件）
include/rechigros/game/     谱面模型、判定、事件与时间线
include/rechigros/chart/    官方 / PEC / RPE 谱面解析
include/rechigros/render/   渲染管线公共接口（gl / batch / scene / fx / text / asset / math）
include/rechigros/audio/    音频引擎、解码器、混音器
include/rechigros/platform/ 资源、文件系统、归档
include/rechigros/app/      应用外壳与界面
src/core/                   谱面运行时与判定实现
src/chart/                  谱面解析实现
src/render/                 OpenGL / OpenGL ES 渲染实现
src/audio/                  miniaudio 音频实现
src/platform/               平台服务实现
src/app/                    SDL2 应用入口、界面与状态机
platform/android/           Gradle + NDK 工程
platform/ios/               Xcode 资源、Info.plist 与图标目录
tests/                      与参考实现对照的回归测试
docs/                       迁移清单与工程记录
```

## 桌面构建

需要 CMake ≥ 3.20、支持 C++20 的编译器，以及可用网络（首次配置下载依赖）。

Windows：

```powershell
build.bat desktop Release
```

Linux / macOS：

```sh
./build.sh desktop Release
```

产物位于 `build/desktop-release`。CMake 会把 `assets/` 复制到可执行文件同级目录；也可以用
`RECHIGROS_ASSETS` 环境变量覆盖资源根目录。也可以通过 CMake 预设构建：

```sh
cmake --preset desktop-release
cmake --build --preset desktop-release
```

只构建与平台无关的核心和谱面层（不需要 SDL，迭代最快）：

```powershell
build.bat core Debug
```

## Android 构建

需要 JDK 17、Android SDK 36、NDK `27.0.12077973` 与 CMake 3.22.1。

```powershell
build.bat android Release
```

APK 位于 `platform/android/app/build/outputs/apk/release/`。Gradle 构建 `arm64-v8a`、
`armeabi-v7a`、`x86_64` 三个 ABI，入口是 SDL2 的 `SDLActivity`，原生库为 `libmain.so`。
应用进入后台时挂起 miniaudio 设备并清理触点，回到前台恢复播放。以"打开方式"接收谱面包时，
`content://` 流会先复制到应用私有缓存，再经条目数、展开体积与路径穿越检查后交给共享逻辑。

未设置签名环境变量时使用 Android debug 证书生成可安装包；正式发布需提供
`RECHIGROS_KEYSTORE`、`RECHIGROS_STORE_PASSWORD`、`RECHIGROS_KEY_ALIAS`、
`RECHIGROS_KEY_PASSWORD`。

## iOS 构建

iOS 只能在 macOS + Xcode 上生成与签名：

```sh
./build.sh ios Release
```

该分支使用 `-DCMAKE_SYSTEM_NAME=iOS -G Xcode`，Bundle ID 为 `com.wuying.chigros`，
最低版本 iOS 13。真机运行前需在 Xcode 中选择开发团队。

在非 macOS 主机上可以静态校验 iOS 打包输入（Info.plist 键、图标尺寸、CMake 接线、
Bundle ID 一致性）：

```powershell
build.bat verify-ios
```

## 测试

桌面构建结束后可直接运行：

```powershell
build\desktop-release\tests\rechigros_core_tests.exe
build\desktop-release\tests\rechigros_chart_tests.exe
build\desktop-release\tests\rechigros_platform_tests.exe
build\desktop-release\tests\rechigros_audio_tests.exe
```

或通过 `ctest --test-dir build/desktop-release --output-on-failure` 一次运行全部。

渲染输入覆盖检查会把夹具谱面按不同输入各渲一次，断言每个输入确实改变了画面：

```powershell
build.bat verify-render
```

应用层另有端到端检查：无窗口跑真实主循环，验证进入游戏、结算、暂停恢复，以及回放录制与还原一致。

```powershell
build.bat verify-app
```

渲染层提供离屏自检路径，加载谱面、渲染若干帧并写出 PNG，无需可见窗口：

```powershell
build\desktop-release\src\render\rechigros_render_smoke.exe `
  --chart <谱面.json> --assets assets --out smoke.png --frames 3 --time 1.5
```

## 运行

```powershell
build\desktop-release\src\app\ReChigros.exe [谱面文件]
```

也可以直接把谱面文件拖进窗口。

## 迁移范围

参见 `MIGRATION.md`（逐区域状态）与 `docs/PORTING_INVENTORY.md`（参考单元与 C++ 单元对照）。

## 许可证

沿用原项目许可证，见 `LICENSE`。第三方依赖各自遵循其上游许可证，构建时下载的源码位于
`build/_deps`、`third_party/cache` 或 `third_party/SDL`。
