# Porting inventory

Source baseline: `app/src/main/java/com/wuying/phigros` (40,861 lines across 89 files) plus
`app/src/main/cpp` (native audio and batch-building kernels) and `app/src/main/assets`.

Each row names the reference unit and the C++ unit that replaces it. `done` means the C++
unit reproduces the reference semantics and is covered by an assertion; `partial` means it
compiles and covers the common path; `todo` means it is not yet written.

## game/

| Reference | Reference lines | C++ unit | Status |
| --- | --- | --- | --- |
| `GameConstants.java` | 47 | `include/rechigros/game/GameConstants.hpp` | done |
| `MathUtils.java` | 16 | `include/rechigros/game/Easing.hpp` helpers | done |
| `Easing.java` | 369 | `src/core/Easing.cpp` | done |
| `BpmTimeline.java` | 86 | `src/core/BpmTimeline.cpp` | done |
| `EventCursor.java` | 14 | `include/rechigros/game/JudgeLine.hpp` | done |
| `LineEvent/MoveEvent/SpeedEvent/ColorEvent/TextEvent/GifEvent.java` | 20+21+27+20+14+16 | `include/rechigros/game/Event.hpp` | done |
| `EventUtils.java` | 979 | `src/core/EventUtils.cpp` | partial |
| `JudgeLine.java` | 613 | `src/core/JudgeLine.cpp` | partial |
| `Note.java` | 80 | `include/rechigros/game/Note.hpp` | done |
| `Chart.java` | 408 | `src/core/Chart.cpp` | partial |
| `ClickEffectItem.java` | 43 | `src/core/ClickEffectItem.cpp` | done |
| `HitOffsetIndicator.java` | 152 | `src/core/HitOffsetIndicator.cpp` | done |
| `PlayResult.java` | 73 | `include/rechigros/game/PlayResult.hpp` | done |
| `PrprEffect.java` | 68 | `include/rechigros/game/PrprEffect.hpp` | partial |
| `PrprVideo.java` | 82 | `include/rechigros/game/PrprVideo.hpp` | todo |
| `OfficialBlock.java` | 433 | `src/core/OfficialBlock.cpp` | todo |
| `OfficialEase.java` | 144 | `src/core/OfficialEase.cpp` | todo |
| `OfficialLevelEffects.java` | 733 | `src/core/OfficialLevelEffects.cpp` | todo |
| `MessageParticleField.java` | 214 | `src/core/MessageParticleField.cpp` | todo |
| `ReplayData.java` | 74 | `include/rechigros/game/ReplayData.hpp` | todo |
| `SkinConfig.java` | 25 | `include/rechigros/game/SkinConfig.hpp` | todo |
| `ResPackInfo.java` | 11 | `include/rechigros/game/ResPackInfo.hpp` | todo |
| `GameRenderer.java` judgement region | ~700 | `src/core/GameplaySession.cpp`, `src/core/JudgementEngine.cpp` | partial |
| `GameRenderer.java` render region | ~13900 | `src/render/**` | todo |
| `NativeRenderCommands.java` | 1245 | `src/render/batch/**` | todo |
| `GameGLSurfaceView.java` | 162 | `src/app/Application.cpp` (SDL3 surface) | todo |

## chart/

| Reference | Reference lines | C++ unit | Status |
| --- | --- | --- | --- |
| `ChartLoader.java` | 816 | `src/chart/ChartLoader.cpp` | partial |
| `OfficialChartParser.java` | 543 | `src/chart/OfficialChartParser.cpp` | partial |
| `PecChartParser.java` | 634 | `src/chart/PecChartParser.cpp` | partial |
| `RpeChartParser.java` | 2308 | `src/chart/RpeChartParser.cpp` | partial |

## audio/

| Reference | Reference lines | C++ unit | Status |
| --- | --- | --- | --- |
| `audio_engine.cpp` | 910 | `src/audio/AudioEngine.cpp` | partial |
| `audio_decoder.cpp` | 211 | `src/audio/Decoder.cpp` | todo |
| `NativeAudioEngine.java` | 123 | `include/rechigros/audio/AudioEngine.hpp` | partial |
| `NativeAudioDecoder.java` | 32 | `src/audio/Decoder.cpp` | todo |
| `PcmDecoder.java` | 206 | `src/audio/Decoder.cpp` | todo |
| `WavDecoder.java` | 202 | `src/audio/Decoder.cpp` | todo |

## ui/

The Android view hierarchy is replaced by an ImGui screen stack with the same
information architecture and the same interactions.

| Reference | Reference lines | C++ unit | Status |
| --- | --- | --- | --- |
| `ChartSelectActivity.java` | 2937 | `src/app/screens/ChartSelectScreen.cpp` | todo |
| `PlayActivity.java` | 943 | `src/app/screens/PlayScreen.cpp` | todo |
| `ResultActivity.java` | 853 | `src/app/screens/ResultScreen.cpp` | todo |
| `CalibrationActivity.java` | 715 | `src/app/screens/CalibrationScreen.cpp` | todo |
| `GameDialog.java` | 688 | `src/app/ui/Dialog.cpp` | todo |
| `ReplayManager.java` | 638 | `src/app/ReplayManager.cpp` | todo |
| `SkinManager.java` | 303 | `src/app/SkinManager.cpp` | todo |
| `GameTextRenderer.java` / `GameTextView.java` / `GameTextOverlay.java` | 110+192+82 | `src/render/text/**` | todo |
| `JudgementHistogramView.java` | 351 | `src/app/screens/ResultScreen.cpp` | todo |
| `TriGridBackgroundView.java` | 368 | `src/render/scene/TriGrid.cpp` | todo |
| `SkewFrameLayout.java` | 232 | `src/app/ui/SkewPanel.cpp` | todo |
| `SettingsSliderView.java` / `SettingsToggleView.java` / `SettingsPreviewView.java` | 187+109+58 | `src/app/ui/Controls.cpp` | todo |
| `UiTypography.java` / `UiWarmup.java` | 180+53 | `src/app/ui/Typography.cpp` | todo |
| `SelectLocalChartsFragment.java` | 351 | `src/app/screens/ChartSelectScreen.cpp` | todo |
| `SelectOnlineChartsFragment.java` | 402 | `src/app/screens/ChartSelectScreen.cpp` | todo |
| `SelectReplayFragment.java` | 389 | `src/app/screens/ChartSelectScreen.cpp` | todo |
| `SelectVideoFragment.java` | 461 | `src/app/screens/ImportScreen.cpp` | todo |
| `SelectAudioFragment.java` | 232 | `src/app/screens/ImportScreen.cpp` | todo |
| `SelectChartFragment.java` | 146 | `src/app/screens/ImportScreen.cpp` | todo |
| `SelectPagerAdapter.java` / `OnlineChartAdapter.java` / `LocalChartAdapter.java` | 23+125+102 | `src/app/screens/ChartSelectScreen.cpp` | todo |
| `CoverImageLoader.java` / `AssetBitmaps.java` | 99+95 | `src/render/TextureCache.cpp` | todo |

## util/

| Reference | Reference lines | C++ unit | Status |
| --- | --- | --- | --- |
| `ChartLibraryManager.java` | 704 | `src/app/ChartLibrary.cpp` | todo |
| `InfoFileUtils.java` / `InfoJsonUtils.java` | 467+59 | `src/app/ChartMetadata.cpp` | todo |
| `ZipPackUtils.java` | 856 | `src/platform/ZipArchive.cpp` | todo |
| `FileUtils.java` | 63 | `src/platform/FileSystem.cpp` | todo |
| `LanzouClient.java` | 552 | `src/platform/net/LanzouClient.cpp` | todo |
| `OnlineIndexClient.java` | 215 | `src/platform/net/OnlineIndexClient.cpp` | todo |

## assets

`assets/` mirrors the reference asset tree byte for byte, except that the reference
stores it under `app/src/main/assets` and here it sits next to the executable.
103 files, 24.3 MB.
