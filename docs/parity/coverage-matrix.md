# Per-function coverage matrix

Generated from `docs/compare/inventory.json` by `tools/parity/coverage_matrix.py`. One row per reference declaration.

Reference declarations: **2293** across 75 files.

| Status | Count | Meaning |
| --- | --- | --- |
| verified | 450 | named MATCH by a parity report |
| fixed | 177 | named by a parity report with a fix |
| differs | 0 | named with a remaining difference |
| excluded | 3 | deliberate exclusion, reason recorded |
| matched-by-name | 169 | a port symbol with the same name exists |
| unmatched | 1325 | no same-name port symbol; needs a manual verdict |

This matrix is an index. The authoritative per-function verdicts and the
evidence line for each one are in the topical reports (`chart.md`, `core.md`, `judging.md`,
`render.md`, `ui.md`, `gameplay.md`, `platform.md`, `audio.md`).


## `com\wuying\phigros\PhigrosApp.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 10 | `onCreate` | unmatched |  |

## `com\wuying\phigros\audio\NativeAudioEngine.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 11 | `NativeAudioEngine` | unmatched |  |
| 13 | `create` | verified |  |
| 19 | `delete` | fixed | `src\tools\frame_profile.cpp:74` `delete` (+2) |
| 25 | `setMusicData` | unmatched |  |
| 34 | `setMusicData` | unmatched |  |
| 39 | `setSfxData` | unmatched |  |
| 45 | `triggerSfx` | unmatched |  |
| 51 | `setPlaybackSpeed` | matched-by-name | `src\audio\AudioEngine.cpp:701` `setPlaybackSpeed` |
| 56 | `setSfxVolume` | matched-by-name | `src\audio\AudioEngine.cpp:697` `setSfxVolume` |
| 61 | `setLowPassEnabled` | matched-by-name | `src\audio\AudioEngine.cpp:709` `setLowPassEnabled` |
| 71 | `setMusicVolume` | matched-by-name | `src\audio\AudioEngine.cpp:693` `setMusicVolume` |
| 76 | `setMusicLooping` | matched-by-name | `src\audio\AudioEngine.cpp:705` `setMusicLooping` |
| 81 | `prepareDefaultSfxIfMissing` | unmatched |  |
| 86 | `start` | fixed |  |
| 91 | `pause` | fixed | `src\audio\AudioEngine.cpp:643` `pause` |
| 96 | `restart` | verified | `src\audio\AudioEngine.cpp:673` `restart` |
| 101 | `restartPaused` | matched-by-name | `src\audio\AudioEngine.cpp:681` `restartPaused` |
| 106 | `seekTo` | matched-by-name | `src\audio\AudioEngine.cpp:431` `seekTo` |
| 112 | `stop` | fixed | `src\audio\AudioEngine.cpp:648` `stop` |
| 117 | `getPlayheadSeconds` | matched-by-name | `src\audio\AudioEngine.cpp:459` `playheadSeconds` (+1) |
| 122 | `getMusicDurationSeconds` | matched-by-name | `src\audio\AudioEngine.cpp:728` `getMusicDurationSeconds` |

## `com\wuying\phigros\game\BpmTimeline.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 12 | `BpmTimeline` | matched-by-name | `src\core\BpmTimeline.cpp:8` `BpmTimeline` |
| 27 | `size` | fixed | `include\rechigros\game\BpmTimeline.hpp:8` `size` (+5) |
| 31 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 44 | `secToBeat` | verified | `src\core\BpmTimeline.cpp:28` `secToBeat` |
| 57 | `findSegmentByBeat` | unmatched |  |
| 75 | `findSegmentBySec` | verified |  |

## `com\wuying\phigros\game\Chart.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 48 | `updateUiTransform` | verified | `src\core\JudgeLine.cpp:188` `updateUiTransform` |
| 83 | `LineRuntimeResult` | unmatched |  |
| 94 | `estimateNoteCount` | unmatched |  |
| 109 | `floorPositionMonotonic` | fixed | `src\core\EventUtils.cpp:922` `floorPositionMonotonic` (+1) |
| 169 | `floorPositionForLine` | unmatched |  |
| 174 | `createSpeedLayerIndices` | unmatched |  |
| 179 | `floorPositionForLine` | unmatched |  |
| 202 | `initRuntime` | unmatched |  |
| 228 | `initRuntimeSequential` | unmatched |  |
| 299 | `initRuntimeParallel` | unmatched |  |
| 314 | `call` | fixed |  |
| 408 | `finalizeRuntime` | unmatched |  |

## `com\wuying\phigros\game\ChartLoader.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 78 | `ChartLoader` | verified |  |
| 167 | `computeNormalizedMd5` | verified |  |
| 199 | `tryApplyLineCsvTextures` | verified |  |
| 266 | `stripExt` | verified |  |
| 271 | `equalsIgnoreCase` | unmatched |  |
| 275 | `findUpwards` | fixed | `src\chart\ChartLoader.cpp:394` `findUpwards` |
| 287 | `resolveResource` | fixed |  |
| 297 | `resolveUp` | fixed |  |
| 347 | `getCell` | unmatched |  |
| 354 | `parseIntSafe` | unmatched |  |
| 362 | `parseFloatSafe` | unmatched |  |
| 370 | `parseCsvLine` | verified | `src\app\InfoFile.cpp:231` `parseCsvLine` (+1) |
| 410 | `tryLoadPrprExtra` | fixed |  |
| 570 | `parsePrprBpmSegments` | unmatched |  |
| 589 | `addPrprBpmSegment` | unmatched |  |
| 600 | `parsePrprVideo` | unmatched |  |
| 631 | `resolvePrprResource` | unmatched |  |
| 640 | `parsePrprScaleType` | unmatched |  |
| 655 | `parsePrprVideoAttachLine` | unmatched |  |
| 666 | `PrprFloatAnim` | unmatched |  |
| 672 | `parsePrprFloatAnim` | unmatched |  |
| 728 | `fillPrprLineEventEasing` | unmatched |  |
| 735 | `getAsJsonArray` | unmatched |  |
| 744 | `isPrprShaderFileName` | verified |  |
| 750 | `isNumberArray` | unmatched |  |
| 759 | `parseBeatTriple` | verified | `src\chart\ChartLoader.cpp:204` `parseBeatTriple` |
| 768 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 779 | `getAsString` | unmatched |  |
| 789 | `getAsDouble` | fixed |  |
| 804 | `getAsBoolean` | unmatched |  |
| 820 | `detectFormatFast` | verified |  |
| 865 | `looksLikeJsonObject` | verified | `src\chart\ChartLoader.cpp:56` `looksLikeJsonObject` |
| 908 | `tryApplyAttachUiFixFlag` | verified |  |

## `com\wuying\phigros\game\ClickEffectItem.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `ClickEffectItem` | fixed | `src\core\ClickEffectItem.cpp:5` `ClickEffectItem` |
| 26 | `ClickEffectItem` | fixed | `src\core\ClickEffectItem.cpp:5` `ClickEffectItem` |

## `com\wuying\phigros\game\Easing.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 12 | `Easing` | fixed | `src\chart\RpeChartParser.cpp:152` `parseEasing` (+4) |
| 14 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |
| 21 | `applyTween` | verified | `src\core\Easing.cpp:149` `applyTween` |
| 62 | `sineIn` | verified | `src\core\Easing.cpp:9` `sineIn` |
| 64 | `quadIn` | verified | `src\core\Easing.cpp:11` `quadIn` |
| 65 | `cubicIn` | verified | `src\core\Easing.cpp:12` `cubicIn` |
| 66 | `quartIn` | verified | `src\core\Easing.cpp:13` `quartIn` |
| 67 | `quintIn` | verified | `src\core\Easing.cpp:14` `quintIn` |
| 68 | `expoIn` | verified | `src\core\Easing.cpp:15` `expoIn` |
| 69 | `circIn` | verified | `src\core\Easing.cpp:17` `circIn` |
| 70 | `backIn` | fixed | `src\core\Easing.cpp:19` `backIn` |
| 71 | `elasticIn` | verified | `src\core\Easing.cpp:25` `elasticIn` |
| 72 | `bounceIn` | verified | `src\core\Easing.cpp:50` `bounceIn` |
| 73 | `sineOut` | verified |  |
| 75 | `quadOut` | verified |  |
| 76 | `cubicOut` | verified |  |
| 77 | `quartOut` | verified |  |
| 78 | `quintOut` | verified |  |
| 79 | `expoOut` | verified |  |
| 80 | `circOut` | verified |  |
| 81 | `backOut` | verified |  |
| 82 | `elasticOut` | verified |  |
| 83 | `bounceOut` | verified | `src\core\Easing.cpp:30` `bounceOut` |
| 103 | `sineInOut` | verified |  |
| 105 | `quadInOut` | verified |  |
| 106 | `cubicInOut` | verified |  |
| 107 | `quartInOut` | verified |  |
| 108 | `quintInOut` | verified |  |
| 109 | `expoInOut` | verified |  |
| 110 | `circInOut` | verified |  |
| 111 | `backInOut` | verified |  |
| 112 | `elasticInOut` | verified |  |
| 113 | `bounceInOut` | verified |  |
| 119 | `rpeRawEasingToTweenId` | verified | `src\core\Easing.cpp:235` `rpeRawEasingToTweenId` |
| 125 | `rpeEasingToTweenId` | verified | `src\core\Easing.cpp:241` `rpeEasingToTweenId` |
| 132 | `apply` | fixed | `src\core\Easing.cpp:248` `apply` (+2) |
| 138 | `applyTweenInt` | verified | `src\core\Easing.cpp:189` `applyTweenInt` |
| 179 | `intSineIn` | verified | `src\core\Easing.cpp:62` `intSineIn` |
| 181 | `intQuadIn` | verified | `src\core\Easing.cpp:64` `intQuadIn` |
| 182 | `intCubicIn` | verified | `src\core\Easing.cpp:65` `intCubicIn` |
| 183 | `intQuartIn` | verified | `src\core\Easing.cpp:66` `intQuartIn` |
| 184 | `intQuintIn` | verified | `src\core\Easing.cpp:67` `intQuintIn` |
| 185 | `intExpoIn` | verified | `src\core\Easing.cpp:68` `intExpoIn` |
| 186 | `intCircIn` | verified | `src\core\Easing.cpp:73` `intCircIn` |
| 187 | `intBackIn` | verified | `src\core\Easing.cpp:75` `intBackIn` |
| 188 | `intElasticIn` | verified | `src\core\Easing.cpp:90` `intElasticIn` |
| 189 | `elasticFAntideriv` | matched-by-name | `src\core\Easing.cpp:81` `elasticFAntideriv` |
| 198 | `intBounceIn` | verified | `src\core\Easing.cpp:119` `intBounceIn` |
| 200 | `bounceHAntideriv` | verified | `src\core\Easing.cpp:92` `bounceHAntideriv` |
| 226 | `intSineOut` | verified |  |
| 228 | `intQuadOut` | verified |  |
| 229 | `intCubicOut` | verified |  |
| 230 | `intQuartOut` | verified |  |
| 231 | `intQuintOut` | verified |  |
| 232 | `intExpoOut` | verified |  |
| 233 | `intCircOut` | verified |  |
| 234 | `intBackOut` | verified |  |
| 235 | `intElasticOut` | verified |  |
| 236 | `intBounceOut` | verified |  |
| 237 | `intSineInOut` | verified |  |
| 239 | `intQuadInOut` | verified |  |
| 240 | `intCubicInOut` | verified |  |
| 241 | `intQuartInOut` | verified |  |
| 242 | `intQuintInOut` | verified |  |
| 243 | `intExpoInOut` | verified |  |
| 244 | `intCircInOut` | verified |  |
| 245 | `intBackInOut` | verified |  |
| 246 | `intElasticInOut` | verified |  |
| 247 | `intBounceInOut` | verified |  |
| 248 | `clampedTween` | verified | `src\core\Easing.cpp:252` `clampedTween` |
| 270 | `intClampedTween` | verified | `src\core\Easing.cpp:268` `intClampedTween` |
| 296 | `buildBezierLut` | verified | `src\core\Easing.cpp:332` `buildBezierLut` (+1) |
| 306 | `buildBezierLut` | verified | `src\core\Easing.cpp:332` `buildBezierLut` (+1) |
| 317 | `lookupBezierLut` | verified | `src\core\Easing.cpp:348` `lookupBezierLut` |
| 330 | `bezierCacheKey` | unmatched |  |
| 339 | `cubicBezierYforX` | verified | `src\core\Easing.cpp:284` `cubicBezierYForX` |
| 398 | `bezierSampleX` | matched-by-name | `src\core\Easing.cpp:131` `bezierSampleX` |
| 403 | `bezierSampleY` | unmatched |  |
| 407 | `bezierSlopeX` | matched-by-name | `src\core\Easing.cpp:136` `bezierSlopeX` |

## `com\wuying\phigros\game\EventCursor.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 11 | `reset` | fixed | `include\rechigros\audio\Mixer.hpp:147` `reset` (+9) |

## `com\wuying\phigros\game\EventUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 9 | `EventUtils` | unmatched |  |
| 16 | `containsEventTime` | verified | `src\core\EventUtils.cpp:17` `containsEventTime` |
| 24 | `eventStarted` | verified | `src\core\EventUtils.cpp:23` `eventStarted` |
| 28 | `laterLineEventStarted` | matched-by-name | `src\core\EventUtils.cpp:25` `laterLineEventStarted` |
| 37 | `laterMoveEventStarted` | matched-by-name | `src\core\EventUtils.cpp:29` `laterMoveEventStarted` |
| 46 | `laterColorEventStarted` | matched-by-name | `src\core\EventUtils.cpp:33` `laterColorEventStarted` |
| 55 | `laterTextEventStarted` | matched-by-name | `src\core\EventUtils.cpp:37` `laterTextEventStarted` |
| 64 | `laterGifEventStarted` | matched-by-name | `src\core\EventUtils.cpp:41` `laterGifEventStarted` |
| 73 | `findEvent` | verified | `src\core\EventUtils.cpp:45` `findEvent` (+1) |
| 88 | `lerp` | fixed | `src\core\EventUtils.cpp:235` `lerp` |
| 92 | `getEventVal` | fixed | `src\core\EventUtils.cpp:244` `getEventVal` (+1) |
| 135 | `getMoveEventVal1` | verified | `src\core\EventUtils.cpp:320` `getMoveEventVal1` (+1) |
| 165 | `getMoveEventVal2` | matched-by-name | `src\core\EventUtils.cpp:391` `getMoveEventVal2` (+1) |
| 195 | `speedAt` | verified | `src\core\EventUtils.cpp:740` `speedAt` |
| 200 | `start` | fixed |  |
| 201 | `end` | fixed |  |
| 235 | `initSpeedEvents` | fixed | `src\core\EventUtils.cpp:766` `initSpeedEvents` |
| 314 | `nextSpeedEventStart` | unmatched |  |
| 323 | `computeFpForCumulative` | verified | `src\core\EventUtils.cpp:184` `computeFpForCumulative` |
| 341 | `getFloorPosition` | fixed | `src\core\EventUtils.cpp:842` `getFloorPosition` (+2) |
| 417 | `computeFpInEvent` | verified | `src\core\EventUtils.cpp:134` `computeFpInEvent` |
| 470 | `getFloorPosition` | fixed | `src\core\EventUtils.cpp:842` `getFloorPosition` (+2) |
| 475 | `getFloorPosition` | fixed | `src\core\EventUtils.cpp:842` `getFloorPosition` (+2) |
| 491 | `applyEasing` | verified | `src\chart\RpeChartParser.cpp:152` `parseEasing` (+4) |
| 495 | `applyEasing` | verified | `src\chart\RpeChartParser.cpp:152` `parseEasing` (+4) |
| 499 | `integrateBezierEasing` | verified | `src\core\EventUtils.cpp:104` `integrateBezierEasing` |
| 529 | `applyEasing` | verified | `src\chart\RpeChartParser.cpp:152` `parseEasing` (+4) |
| 566 | `applyRawRpeEasing` | verified |  |
| 597 | `getEventVal` | fixed | `src\core\EventUtils.cpp:244` `getEventVal` (+1) |
| 656 | `getMoveEventVal1` | verified | `src\core\EventUtils.cpp:320` `getMoveEventVal1` (+1) |
| 707 | `getMoveEventVal2` | matched-by-name | `src\core\EventUtils.cpp:391` `getMoveEventVal2` (+1) |
| 758 | `getColorVal` | verified | `src\core\EventUtils.cpp:462` `getColorVal` (+1) |
| 762 | `getColorValInto` | verified |  |
| 833 | `getTextVal` | fixed | `src\core\EventUtils.cpp:552` `getTextVal` (+1) |
| 882 | `getColorVal` | verified | `src\core\EventUtils.cpp:462` `getColorVal` (+1) |
| 914 | `getTextVal` | fixed | `src\core\EventUtils.cpp:552` `getTextVal` (+1) |
| 946 | `stripStaticTextMarkers` | verified | `src\core\EventUtils.cpp:678` `stripStaticTextMarkers` |
| 970 | `calculateTextValue` | verified | `src\core\EventUtils.cpp:692` `calculateTextValue` |
| 1031 | `getGifVal` | fixed | `src\core\EventUtils.cpp:647` `getGifVal` |

## `com\wuying\phigros\game\GameConstants.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 6 | `GameConstants` | verified |  |

## `com\wuying\phigros\game\GameGLSurfaceView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `GameGLSurfaceView` | unmatched |  |
| 33 | `enableMultisample` | unmatched |  |
| 37 | `enableUnlimitedFrameRate` | unmatched |  |
| 52 | `bindToRenderer` | unmatched |  |
| 55 | `SimpleOnGestureListener` | unmatched |  |
| 56 | `onDown` | unmatched |  |
| 60 | `onSingleTapConfirmed` | unmatched |  |
| 72 | `onDoubleTap` | unmatched |  |
| 102 | `onTouchEvent` | unmatched |  |
| 112 | `createContext` | unmatched |  |
| 127 | `destroyContext` | unmatched |  |
| 136 | `Es31ConfigChooser` | unmatched |  |
| 140 | `chooseConfig` | unmatched |  |
| 155 | `buildConfigAttribs` | unmatched |  |

## `com\wuying\phigros\game\GameRenderer.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 104 | `isMenuVisible` | unmatched |  |
| 109 | `setPracticeEnabled` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:56` `setPracticeEnabled` |
| 115 | `setPracticeSpeed` | matched-by-name | `src\app\GameScreen.cpp:872` `setPracticeSpeed` |
| 124 | `getPracticeSpeed` | matched-by-name | `src\app\GameScreen.cpp:872` `setPracticeSpeed` |
| 128 | `shouldResumeAudioOnActivityResume` | unmatched |  |
| 138 | `setReplayRecorder` | unmatched |  |
| 143 | `setReplayPlayback` | verified | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 150 | `sortReplayEntriesByTime` | verified |  |
| 159 | `isReplayPlayback` | matched-by-name | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 163 | `beginIntro` | verified |  |
| 171 | `beginIntroWithAudioResume` | verified |  |
| 178 | `scheduleIntroAutoResume` | verified |  |
| 184 | `cancelIntroAutoResume` | verified |  |
| 190 | `isIntroGenerationCurrent` | verified |  |
| 194 | `computeIntroLineScale` | matched-by-name | `src\render\scene\Transition.cpp:64` `introLineScale` |
| 228 | `elapsedIntroSeconds` | verified |  |
| 235 | `smoothStep01` | matched-by-name | `src\render\scene\HudPass.cpp:72` `smoothStep01` (+1) |
| 240 | `outroUiProgress` | verified |  |
| 248 | `elapsedOutroSeconds` | verified |  |
| 253 | `isInOutroLineCollapse` | verified |  |
| 260 | `shouldHideNonLineStageElementsForOutro` | verified |  |
| 265 | `shouldHideStoryboardElementsForOutro` | verified |  |
| 269 | `computeOutroLineCollapseScale` | verified | `src\render\scene\Transition.cpp:85` `outroLineCollapseScale` |
| 276 | `computeOutroPlainLineScaleX` | verified |  |
| 283 | `computeStageTextureAlpha` | matched-by-name | `src\render\scene\Transition.cpp:107` `stageTextureAlpha` |
| 306 | `computeSideArtworkAlpha` | matched-by-name | `src\render\scene\Transition.cpp:120` `sideArtworkAlpha` |
| 314 | `hasIntroAudioReleaseElapsed` | unmatched |  |
| 320 | `introDimMix` | unmatched |  |
| 335 | `outroDimMix` | matched-by-name | `src\render\scene\Transition.cpp:80` `outroDimMix` |
| 351 | `computeBackgroundDimAlpha` | matched-by-name | `src\render\scene\Transition.cpp:127` `backgroundDimAlpha` |
| 364 | `computeSideDimAlpha` | matched-by-name | `src\render\scene\Transition.cpp:135` `sideDimAlpha` |
| 375 | `updateOutroState` | fixed |  |
| 405 | `getCombo` | verified | `include\rechigros\game\JudgementEngine.hpp:90` `combo` |
| 414 | `getScore` | matched-by-name | `src\core\JudgementEngine.cpp:253` `score` |
| 438 | `getAccuracy` | verified | `src\core\JudgementEngine.cpp:265` `accuracy` |
| 442 | `getRealtimeAccuracy` | verified | `src\core\JudgementEngine.cpp:270` `realtimeAccuracy` |
| 446 | `getMaxCombo` | verified | `include\rechigros\game\JudgementEngine.hpp:91` `maxCombo` |
| 450 | `getPerfectCount` | unmatched |  |
| 454 | `getGoodCount` | unmatched |  |
| 458 | `getBadCount` | unmatched |  |
| 462 | `getMissCount` | unmatched |  |
| 466 | `getAccuracyInternal` | verified |  |
| 476 | `getRealtimeAccuracyInternal` | verified |  |
| 818 | `PreloadedGif` | verified |  |
| 1020 | `setJudgeLineWidth` | unmatched |  |
| 1443 | `applyBlockTextureWrap` | verified |  |
| 1454 | `applyTextureBilinearFilter` | verified |  |
| 1464 | `applyTexturePointFilter` | verified |  |
| 1474 | `maxFragTextureUnits` | verified |  |
| 1552 | `GameRenderer` | verified | `src\render\GameRenderer.cpp:1064` `GameRenderer` |
| 1726 | `sanitizeOfficialScreenDpi` | unmatched |  |
| 1735 | `sanitizeHitOffsetIndicatorMode` | unmatched |  |
| 1742 | `preloadGifTextures` | verified |  |
| 1759 | `decodeGifFrames` | verified |  |
| 1812 | `uploadPreloadedGifTextures` | verified |  |
| 1877 | `onDoubleTap` | unmatched |  |
| 1886 | `isInPauseHitArea` | unmatched |  |
| 1893 | `computeRevealTransformedRect` | unmatched |  |
| 1908 | `triggerPauseTouchFeedback` | unmatched |  |
| 1912 | `isBeforeGameplayStartOrIntroTransition` | unmatched |  |
| 1916 | `freezePlayheadForPause` | unmatched |  |
| 1924 | `clearPausePlayheadFreeze` | unmatched |  |
| 1929 | `shouldUsePausedPlayhead` | unmatched |  |
| 1937 | `shouldPauseGameplayJudgement` | unmatched |  |
| 1944 | `getPausedPlayheadSeconds` | unmatched |  |
| 1950 | `resetIntroAudioToPausedStart` | unmatched |  |
| 1958 | `enterPauseMenu` | unmatched |  |
| 1980 | `beginResumeCountdown` | unmatched |  |
| 2002 | `requestPauseMenu` | unmatched |  |
| 2010 | `onTouchEvent` | unmatched |  |
| 2117 | `handleUiTap` | unmatched |  |
| 2170 | `onSingleTap` | unmatched |  |
| 2187 | `restartInternal` | unmatched |  |
| 2340 | `applyPracticeSeek` | matched-by-name | `src\app\GameScreen.cpp:162` `applyPracticeSeek` |
| 2459 | `firstNoteIndexAtOrAfter` | unmatched |  |
| 2470 | `firstEffectIndexAtOrAfter` | unmatched |  |
| 2481 | `requestPracticeSeek` | unmatched |  |
| 2486 | `updatePracticeRepeat` | unmatched |  |
| 2505 | `resetNativeRenderBucketRuntimeState` | unmatched |  |
| 2519 | `transformTouchX` | fixed |  |
| 2527 | `officialFingerWorldScalePx` | unmatched |  |
| 2535 | `officialJudgePositionScalePx` | verified |  |
| 2542 | `officialFingerWorldX` | unmatched |  |
| 2546 | `officialFingerWorldY` | unmatched |  |
| 2550 | `tryJudgeInputDown` | unmatched |  |
| 2554 | `tryJudgeInputMove` | unmatched |  |
| 2558 | `tryJudgeAt` | unmatched |  |
| 2620 | `computeNoteHeadPosition` | unmatched |  |
| 2680 | `getPerfectWindowSec` | verified |  |
| 2684 | `practiceJudgeOverrideActive` | verified |  |
| 2689 | `tapPerfectWindowSec` | verified | `include\rechigros\game\JudgementEngine.hpp:57` `tapPerfectWindow` |
| 2693 | `tapGoodWindowSec` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:65` `tapGoodWindow` |
| 2697 | `setPracticePerfectWindowMs` | unmatched |  |
| 2703 | `setPracticeGoodWindowMs` | unmatched |  |
| 2709 | `getGoodWindowSec` | unmatched |  |
| 2713 | `getBadWindowSec` | unmatched |  |
| 2717 | `getOfficialFlickJudgeSpeed` | unmatched |  |
| 2721 | `officialFlickJudgeSpeed` | verified |  |
| 2726 | `getEarlyWindowSec` | unmatched |  |
| 2731 | `getJudgeWindowSec` | verified | `src\core\JudgementEngine.cpp:99` `judgeWindowSec` |
| 2744 | `getSpatialJudgeWindowSec` | verified |  |
| 2770 | `officialCheckNoteCandidateReplaces` | verified |  |
| 2795 | `officialCheckFlickCandidateReplaces` | verified |  |
| 2807 | `getLateMissLimitSec` | verified | `src\core\JudgementEngine.cpp:322` `lateMissLimitSec` (+1) |
| 2816 | `officialLateMissLimitSec` | verified |  |
| 2834 | `officialDragLocalJudged` | verified |  |
| 2838 | `calcJudgement` | verified |  |
| 2851 | `commitJudgement` | verified |  |
| 2960 | `chartTimeForJudgementDiff` | fixed | `src\core\JudgementEngine.cpp:143` `chartTimeForJudgementDiff` |
| 2965 | `recordHitOffsetIndicator` | verified |  |
| 2983 | `recordJudgementDetailOffset` | verified |  |
| 3007 | `judgementDetailRangeMs` | verified | `src\core\JudgementEngine.cpp:350` `judgementDetailRangeMs` |
| 3013 | `judgementDetailBinWidthMs` | matched-by-name | `src\core\JudgementEngine.cpp:355` `judgementDetailBinWidthMs` |
| 3017 | `buildPlayResult` | verified |  |
| 3038 | `buildReplayPlaybackResult` | verified |  |
| 3060 | `hasJudgementOffsetData` | verified |  |
| 3068 | `onSurfaceCreated` | unmatched |  |
| 3341 | `onSurfaceChanged` | unmatched |  |
| 3390 | `computeStageRect` | matched-by-name | `src\render\fx\BlockCompositePass.cpp:237` `setStageRect` |
| 3507 | `chartRevealCenterX` | unmatched |  |
| 3511 | `chartRevealCenterY` | unmatched |  |
| 3515 | `updateFrameChartRevealState` | unmatched |  |
| 3550 | `isChartRevealCameraActive` | matched-by-name | `src\app\GameScreen.cpp:80` `chartRevealCameraActive` |
| 3554 | `shouldRevealHiddenChartObjects` | unmatched |  |
| 3558 | `chartRevealHiddenFloorAlpha` | unmatched |  |
| 3568 | `currentChartRevealScale` | unmatched |  |
| 3573 | `chartRevealScreenX` | unmatched |  |
| 3579 | `chartRevealScreenY` | unmatched |  |
| 3585 | `computeRevealWorldBounds` | unmatched |  |
| 3602 | `onDrawFrame` | verified |  |
| 3727 | `refreshPauseSceneBlur` | unmatched |  |
| 3758 | `computeOfficialPauseBlurRadiusPx` | unmatched |  |
| 3764 | `drawPauseBackdrop` | unmatched |  |
| 3781 | `drawPauseBackdropTexture` | unmatched |  |
| 3812 | `shouldPostProcessAntialias` | unmatched |  |
| 3825 | `beginAntialiasFrame` | unmatched |  |
| 3835 | `bindAntialiasTarget` | unmatched |  |
| 3844 | `endAntialiasFrame` | unmatched |  |
| 3851 | `drawFxaaToDefaultFramebuffer` | unmatched |  |
| 3871 | `maybeAutoResumeAfterIntro` | unmatched |  |
| 3894 | `beginIntroAudioStartWatchdog` | unmatched |  |
| 3901 | `clearIntroAudioStartWatchdog` | unmatched |  |
| 3908 | `maybeRetryIntroAudioStart` | unmatched |  |
| 3943 | `releaseIntroAudioAtGameplayStart` | unmatched |  |
| 3955 | `releaseAudioForResumeFade` | unmatched |  |
| 3970 | `getResumeCountdownElapsedSec` | unmatched |  |
| 3978 | `getPauseMenuElapsedSec` | unmatched |  |
| 3986 | `computePauseBackdropIntroAlpha` | matched-by-name | `src\render\scene\PauseBackdrop.cpp:63` `pauseBackdropIntroAlpha` |
| 3995 | `computePauseButtonIntroAlpha` | matched-by-name | `src\app\GameScreen.cpp:644` `pauseButtonIntroAlpha` |
| 4004 | `computeContinueButtonAlpha` | matched-by-name | `src\app\GameScreen.cpp:650` `continueButtonAlpha` |
| 4010 | `computeResumeBackdropAlpha` | matched-by-name | `src\render\scene\PauseBackdrop.cpp:69` `resumeBackdropAlpha` |
| 4020 | `computeResumeBackdropBrightness` | matched-by-name | `src\render\scene\PauseBackdrop.cpp:79` `resumeBackdropBrightness` |
| 4026 | `computeResumeBackdropFlatten` | matched-by-name | `src\render\scene\PauseBackdrop.cpp:85` `resumeBackdropFlatten` |
| 4032 | `evaluateHermiteCurve` | matched-by-name | `src\render\scene\PauseBackdrop.cpp:30` `evaluateHermiteCurve` |
| 4065 | `finishResumeCountdown` | unmatched |  |
| 4077 | `drawResumeCountdown` | unmatched |  |
| 4108 | `drawOfficialCountdownDigit` | unmatched |  |
| 4118 | `officialCountdownAlpha` | unmatched |  |
| 4131 | `computeOfficialContinueBarX` | unmatched |  |
| 4145 | `easeOutBack` | unmatched |  |
| 4151 | `ensureCountdownTextures` | unmatched |  |
| 4170 | `computeResumeFadeAlpha` | unmatched |  |
| 4180 | `renderGameplayFrame` | unmatched |  |
| 4225 | `drawMessageEdgeMask` | unmatched |  |
| 4235 | `blockCompositeHasWork` | unmatched |  |
| 4252 | `renderSceneWithPrprEffects` | unmatched |  |
| 4452 | `drawHateBlackMaskOverlay` | unmatched |  |
| 4489 | `drawBackgroundAndBarsOnly` | unmatched |  |
| 4499 | `drawStageBackgroundOnly` | unmatched |  |
| 4556 | `officialRawBackgroundHidden` | unmatched |  |
| 4564 | `ensureKaleidoProgram` | unmatched |  |
| 4569 | `ensureDsGridProgram` | unmatched |  |
| 4574 | `isDesultorySignalsLevelEffects` | unmatched |  |
| 4586 | `dsGridOverlayVisible` | unmatched |  |
| 4620 | `drawGameplayObjects` | unmatched |  |
| 4715 | `drawDsGridOverlay` | unmatched |  |
| 4816 | `dsCanvasScaleFactor` | unmatched |  |
| 4829 | `drawOfficialKaleidoBackdrop` | unmatched |  |
| 4910 | `updateMessageStageMode` | unmatched |  |
| 4936 | `advanceMessageParticles` | unmatched |  |
| 4970 | `drawMessageParticles` | unmatched |  |
| 5054 | `createParticleGlowTexture` | unmatched |  |
| 5075 | `drawRevealScaledMasks` | unmatched |  |
| 5100 | `drawRevealOutsidePanel` | unmatched |  |
| 5156 | `initPrprVideoRuntimes` | unmatched |  |
| 5203 | `preparePrprVideoPlayer` | unmatched |  |
| 5219 | `Listener` | verified |  |
| 5220 | `onVideoSizeChanged` | unmatched |  |
| 5224 | `onPlaybackStateChanged` | unmatched |  |
| 5239 | `onPlayerError` | unmatched |  |
| 5257 | `setPrprVideoPlaybackSpeed` | unmatched |  |
| 5262 | `setPrprVideoPlaybackSpeedAll` | unmatched |  |
| 5268 | `releasePrprVideoRuntimes` | unmatched |  |
| 5272 | `releasePrprVideoRuntimes` | unmatched |  |
| 5279 | `release` | fixed | `src\app\TriGridBackdrop.cpp:260` `release` (+11) |
| 5283 | `pausePrprVideoRuntimes` | unmatched |  |
| 5292 | `resetPrprVideoRuntimes` | unmatched |  |
| 5310 | `drawPrprVideos` | unmatched |  |
| 5368 | `canPrprVideosPlay` | unmatched |  |
| 5374 | `syncPrprVideo` | unmatched |  |
| 5407 | `seekPrprVideo` | unmatched |  |
| 5411 | `seekPrprVideo` | unmatched |  |
| 5426 | `updatePrprVideoFrame` | unmatched |  |
| 5436 | `drawExternalVideoCentered` | unmatched |  |
| 5484 | `drawExternalTextureCentered` | unmatched |  |
| 5538 | `applyPrprEffectChain` | unmatched |  |
| 5558 | `applyPrprEffectPass` | unmatched |  |
| 5622 | `applyOfficialBloomPass` | unmatched |  |
| 5723 | `drawBloomStage` | unmatched |  |
| 5743 | `setUniform1f` | unmatched |  |
| 5748 | `setUniform2f` | unmatched |  |
| 5753 | `setUniform3f` | unmatched |  |
| 5758 | `setUniform1i` | unmatched |  |
| 5763 | `evalPrprVarScalar` | unmatched |  |
| 5776 | `ensureOfficialBloomPrograms` | unmatched |  |
| 5786 | `ensureVignetteNoiseProgram` | unmatched |  |
| 5791 | `buildPrprProgram` | unmatched |  |
| 5815 | `obtainBloomFbo` | unmatched |  |
| 5824 | `releaseBloomFbos` | unmatched |  |
| 5833 | `bindEffectSampler` | unmatched |  |
| 5845 | `bindVignetteNoiseIfNeeded` | unmatched |  |
| 5933 | `prprNoiseClockSec` | unmatched |  |
| 5940 | `applyPrprEffectChainFull` | unmatched |  |
| 5966 | `applyPrprEffectChainStageViewport` | unmatched |  |
| 6005 | `applyPrprEffectPassFull` | unmatched |  |
| 6032 | `applyPrprEffectPassViewport` | unmatched |  |
| 6076 | `applyPrprEffectPassCommon` | unmatched |  |
| 6140 | `evaluatePrprVarValue` | unmatched |  |
| 6147 | `clone` | unmatched |  |
| 6160 | `evalEventList` | unmatched |  |
| 6189 | `applyUniform` | matched-by-name | `src\render\gl\ShaderProgram.cpp:227` `uniform` |
| 6207 | `resetSmoothBiasHistory` | unmatched |  |
| 6218 | `invalidateSmoothClock` | unmatched |  |
| 6223 | `primeSmoothClock` | unmatched |  |
| 6235 | `cacheSmoothedPlayhead` | unmatched |  |
| 6241 | `addSmoothBias` | unmatched |  |
| 6257 | `getSmoothedPlayheadSeconds` | unmatched |  |
| 6368 | `clear` | verified | `src\audio\AudioEngine.cpp:66` `clear` (+5) |
| 6375 | `ensureRenderLineInfoCapacity` | unmatched |  |
| 6385 | `getRenderLineInfo` | unmatched |  |
| 6392 | `renderSceneDirect` | unmatched |  |
| 6563 | `drawAmetrineVignette` | unmatched |  |
| 6597 | `canUseNativeRenderCommands` | unmatched |  |
| 6607 | `canUseNativeTimeline` | unmatched |  |
| 6617 | `prepareNativeTimelineLineStates` | unmatched |  |
| 6669 | `applyNativeTimelineLineStates` | unmatched |  |
| 6727 | `resolveLineText` | unmatched |  |
| 6735 | `prepareNativeLineState` | unmatched |  |
| 6746 | `currentChartGlobalBeat` | unmatched |  |
| 6751 | `drawNativeHoldNotes` | unmatched |  |
| 6837 | `drawNativeNoteHeads` | unmatched |  |
| 6905 | `drawDebugOverlay` | matched-by-name | `src\app\GameScreen.cpp:276` `drawDebugOverlay` |
| 7032 | `normalizeAlpha01` | unmatched |  |
| 7042 | `isHitJudgement` | verified |  |
| 7046 | `applyYControlScale` | verified | `src\core\NoteGeometry.cpp:37` `applyYControlScale` (+1) |
| 7051 | `rpeControlHeightFromBasePx` | verified | `src\core\NoteGeometry.cpp:41` `rpeControlHeightFromBasePx` |
| 7069 | `getOrCreateDebugText` | unmatched |  |
| 7082 | `calcInclineValue` | verified | `src\core\NoteGeometry.cpp:53` `calcInclineValue` (+1) |
| 7103 | `combineInclineSinr` | verified | `src\core\NoteGeometry.cpp:61` `combineInclineSinr` (+1) |
| 7112 | `yOffsetToTransYPx` | verified | `src\core\NoteGeometry.cpp:70` `yOffsetToTransYPx` |
| 7120 | `ensureLineDrawOrder` | unmatched |  |
| 7149 | `getLineZOrder` | unmatched |  |
| 7156 | `drawFakeJudgeLine` | unmatched |  |
| 7217 | `isStoryboardJudgeLine` | unmatched |  |
| 7225 | `isLineAttachedToPrprVideo` | unmatched |  |
| 7235 | `drawJudgeLineVisualWithOutroLineCollapse` | unmatched |  |
| 7266 | `drawJudgeLineVisual` | unmatched |  |
| 7472 | `getOrLoadFileTexture` | unmatched |  |
| 7494 | `getOrCreateTextTexture` | unmatched |  |
| 7567 | `getTextPaint` | unmatched |  |
| 7600 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 7604 | `deleteGlTexture` | unmatched |  |
| 7619 | `invalidateAllCachedTextures` | unmatched |  |
| 7658 | `invalidateGl` | unmatched |  |
| 7662 | `invalidateHudDynamicText` | unmatched |  |
| 7674 | `ensureTextureValid` | unmatched |  |
| 7777 | `clearHudTextTextures` | unmatched |  |
| 7818 | `clearHudDynamicText` | unmatched |  |
| 7835 | `ellipsizeToWidth` | unmatched |  |
| 7839 | `ellipsizeToWidth` | unmatched |  |
| 7874 | `createHudTextTexture` | unmatched |  |
| 7878 | `createHudTextTexture` | unmatched |  |
| 7882 | `createHudTextTexture` | unmatched |  |
| 7886 | `createHudTextTexture` | unmatched |  |
| 7890 | `createHudTextTexture` | unmatched |  |
| 7983 | `isAmetrineComboBlocked` | unmatched |  |
| 7990 | `ametrineHudVignetteFactor` | unmatched |  |
| 8003 | `buildBlockedComboPattern` | unmatched |  |
| 8071 | `drawBlockedComboPattern` | unmatched |  |
| 8148 | `blockedComboNoiseSample` | unmatched |  |
| 8173 | `blockedComboNoise` | unmatched |  |
| 8181 | `measureHudTextWidth` | unmatched |  |
| 8204 | `drawHudText` | unmatched |  |
| 8227 | `updateHudDynamicText` | unmatched |  |
| 8326 | `drawHudDynamicText` | unmatched |  |
| 8343 | `updateHudTextTexturesIfNeeded` | unmatched |  |
| 8503 | `updateHudDisplayValues` | unmatched |  |
| 8526 | `approachHudDisplayValue` | unmatched |  |
| 8537 | `clampScoreInt` | unmatched |  |
| 8543 | `drawHudTexts` | unmatched |  |
| 8639 | `officialComboLabelOutroAlpha` | unmatched |  |
| 8656 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |
| 8660 | `officialHudUnitPx` | unmatched |  |
| 8664 | `officialHudHalfUiW` | unmatched |  |
| 8673 | `officialHudX` | unmatched |  |
| 8677 | `officialHudY` | unmatched |  |
| 8681 | `officialHudSize` | unmatched |  |
| 8685 | `officialHudBottomTextYOffsetPx` | unmatched |  |
| 8689 | `formatScore7` | unmatched |  |
| 8701 | `formatPercentHundredths` | unmatched |  |
| 8706 | `getAttachUiState` | unmatched |  |
| 8714 | `applyUiTransformAndDraw` | unmatched |  |
| 8726 | `applyUiTransformAndDrawUv` | unmatched |  |
| 8869 | `drawGameHudWithAnimations` | unmatched |  |
| 8906 | `sampleCurve` | matched-by-name | `src\render\scene\HudPass.cpp:77` `sampleCurve` (+1) |
| 8916 | `officialLevelStartEase` | unmatched |  |
| 8923 | `officialLevelOverRootScaleY` | unmatched |  |
| 8929 | `drawGameHudWithAnimationsClipped` | unmatched |  |
| 8942 | `drawGameHud` | unmatched |  |
| 8974 | `drawHitOffsetIndicator` | matched-by-name | `src\render\scene\HudPass.cpp:598` `drawHitOffsetIndicator` |
| 9053 | `drawHitOffsetLineIndicator` | unmatched |  |
| 9101 | `hitOffsetLineBottomSafety` | unmatched |  |
| 9105 | `hitOffsetBottomSafety` | unmatched |  |
| 9117 | `drawHitOffsetArc` | unmatched |  |
| 9142 | `drawHitOffsetRadialLine` | unmatched |  |
| 9159 | `drawHitOffsetLine` | unmatched |  |
| 9175 | `drawHitOffsetArrowLinear` | unmatched |  |
| 9190 | `drawHitOffsetArrow` | unmatched |  |
| 9205 | `hitOffsetArrowWidth` | unmatched |  |
| 9209 | `hitOffsetArrowHeight` | unmatched |  |
| 9218 | `drawHitOffsetText` | unmatched |  |
| 9245 | `hitOffsetColor` | unmatched |  |
| 9256 | `hitOffsetPointX` | unmatched |  |
| 9261 | `hitOffsetPointY` | unmatched |  |
| 9266 | `hitOffsetLineX` | unmatched |  |
| 9271 | `drawPauseTouchRing` | unmatched |  |
| 9304 | `drawFpsCounter` | matched-by-name | `src\render\scene\HudPass.cpp:750` `drawFpsCounter` |
| 9329 | `drawTextureAnchored` | unmatched |  |
| 9338 | `drawTextureAnchored` | unmatched |  |
| 9366 | `drawGifFrame` | unmatched |  |
| 9418 | `renderSceneToFbo` | unmatched |  |
| 9446 | `blurPass` | matched-by-name | `src\render\fx\PauseBackdropPass.cpp:90` `blurPass` |
| 9468 | `pauseScalableBlurPass` | unmatched |  |
| 9499 | `createBackgroundBlur` | unmatched |  |
| 9570 | `renderBackgroundToFbo` | unmatched |  |
| 9603 | `drawTimerLine` | unmatched |  |
| 9639 | `updateGameplay` | unmatched |  |
| 9676 | `syncPausedJudgementState` | fixed |  |
| 9687 | `replayRecordNowChartTimeSec` | verified |  |
| 9692 | `activateHoldAfterHead` | unmatched |  |
| 9712 | `trackActiveHold` | unmatched |  |
| 9720 | `useTrackedHoldOptimization` | unmatched |  |
| 9728 | `updateTrackedHoldNotes` | unmatched |  |
| 9771 | `getHoldBodyEffectIntervalSec` | verified |  |
| 9777 | `spawnDueHoldBodyHitEffects` | fixed |  |
| 9837 | `updateAutoplay` | fixed | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 9876 | `recordNewHoldPresses` | verified |  |
| 9893 | `updateReplayPlayback` | verified | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 9955 | `updateJudgeManualPhi` | fixed |  |
| 10496 | `getPhigrosTouchPos` | verified |  |
| 10553 | `getJudgeOffset` | unmatched |  |
| 10570 | `effectiveJudgeArea` | verified | `src\core\GameplaySession.cpp:138` `effectiveJudgeArea` |
| 10588 | `sanitizeJudgeArea` | verified | `src\core\NoteGeometry.cpp:74` `sanitizeJudgeArea` |
| 10593 | `computeNoteControlHeight` | unmatched |  |
| 10609 | `getJudgeDistance` | verified | `src\core\GameplaySession.cpp:172` `judgeDistance` |
| 10634 | `toLineLocalX` | unmatched |  |
| 10642 | `toLineLocalY` | unmatched |  |
| 10651 | `computeNoteLocalX` | unmatched |  |
| 10684 | `spawnHitEffect` | unmatched |  |
| 10700 | `spawnHoldHeadHitEffect` | unmatched |  |
| 10729 | `spawnHoldBodyHitEffect` | unmatched |  |
| 10733 | `hasHitEffectTint` | unmatched |  |
| 10737 | `hitEffectTintR` | unmatched |  |
| 10741 | `hitEffectTintG` | unmatched |  |
| 10745 | `hitEffectTintB` | unmatched |  |
| 10749 | `getLineRotDeg` | unmatched |  |
| 10763 | `computeNoteHeadPositionOnLine` | unmatched |  |
| 10798 | `spawnBadEffect` | unmatched |  |
| 10853 | `obtainHitEffect` | unmatched |  |
| 10861 | `obtainBadEffect` | unmatched |  |
| 10873 | `recycleHitEffect` | unmatched |  |
| 10879 | `recycleBadEffect` | unmatched |  |
| 10885 | `recycleAllHitEffects` | unmatched |  |
| 10892 | `recycleAllBadEffects` | unmatched |  |
| 10912 | `set` | fixed |  |
| 10922 | `obtainJudgeEvent` | unmatched |  |
| 10929 | `recycleJudgeEvents` | unmatched |  |
| 10961 | `FlickTracker` | verified | `src\core\JudgementEngine.cpp:28` `FlickTracker` |
| 10972 | `move` | fixed | `src\core\JudgementEngine.cpp:34` `move` |
| 10977 | `settleAtCurrentPosition` | matched-by-name | `src\core\JudgementEngine.cpp:39` `settleAtCurrentPosition` |
| 10993 | `advanceFrame` | matched-by-name | `src\core\JudgementEngine.cpp:47` `advanceFrame` |
| 11053 | `set` | fixed |  |
| 11092 | `set` | fixed |  |
| 11115 | `addQuadToBatch` | unmatched |  |
| 11143 | `putBatchInstance` | unmatched |  |
| 11165 | `setupVertexArrayObjects` | unmatched |  |
| 11175 | `createQuadVao` | unmatched |  |
| 11197 | `createBatchVao` | unmatched |  |
| 11243 | `uploadQuadUvBuffer` | unmatched |  |
| 11251 | `uploadBatchBuffer` | unmatched |  |
| 11262 | `flushBatch` | unmatched |  |
| 11266 | `flushBatch` | unmatched |  |
| 11301 | `flushHitFxBatch` | unmatched |  |
| 11330 | `noteWidthRatio` | verified | `src\render\GameRenderer.cpp:62` `noteWidthRatio` |
| 11349 | `hitFxDragIntegral` | unmatched |  |
| 11356 | `hitFxParticleTravel` | unmatched |  |
| 11373 | `drawClickEffects` | unmatched |  |
| 11543 | `getHistoricalLineState` | unmatched |  |
| 11554 | `shouldSkipReplayClickEffect` | unmatched |  |
| 11569 | `removeExpiredPrefix` | unmatched |  |
| 11578 | `drawBadEffects` | unmatched |  |
| 11619 | `drawHitEffects` | matched-by-name | `src\app\CalibrationScreen.cpp:345` `drawHitEffects` (+1) |
| 11716 | `drawTextureCover` | unmatched |  |
| 11763 | `drawOfficialBackgroundClip` | unmatched |  |
| 11801 | `drawOfficialSideBackgrounds` | unmatched |  |
| 11810 | `drawDeferredSidePanelsForDsChain` | unmatched |  |
| 11823 | `drawOfficialSideColumnsOverlay` | unmatched |  |
| 11853 | `drawSideVerticalGaps` | unmatched |  |
| 11864 | `drawOfficialSideBackgroundPanel` | unmatched |  |
| 11880 | `drawSideDimPanel` | unmatched |  |
| 11885 | `hasHorizontalSideBlanks` | unmatched |  |
| 11889 | `isStageFullscreen` | unmatched |  |
| 11899 | `enableStageScissor` | unmatched |  |
| 11931 | `disableStageScissor` | unmatched |  |
| 11935 | `withScreenScissor` | unmatched |  |
| 11977 | `drawButton` | unmatched |  |
| 11981 | `computePracticeLayout` | matched-by-name | `src\app\GameScreen.cpp:770` `computePracticeLayout` |
| 12041 | `computePracticeUiScale` | unmatched |  |
| 12048 | `pdp` | unmatched |  |
| 12053 | `btnRowTop` | unmatched |  |
| 12057 | `practiceText` | unmatched |  |
| 12069 | `drawPracticeTextCentered` | unmatched |  |
| 12073 | `drawPracticeTextCentered` | unmatched |  |
| 12084 | `drawPracticeTextLeft` | unmatched |  |
| 12094 | `drawPracticeTextRight` | unmatched |  |
| 12114 | `ensurePracticeUiTextures` | unmatched |  |
| 12125 | `createRoundedMaskTexture` | unmatched |  |
| 12136 | `createDiscTexture` | unmatched |  |
| 12146 | `drawRoundedRect` | matched-by-name | `src\app\GameScreen.cpp:341` `drawRoundedRect` |
| 12185 | `drawDisc` | unmatched |  |
| 12194 | `drawCapsule` | unmatched |  |
| 12202 | `practiceFormatTime` | matched-by-name | `src\app\GameScreen.cpp:698` `practiceFormatTime` |
| 12208 | `practiceFormatTimePrecise` | matched-by-name | `src\app\GameScreen.cpp:706` `practiceFormatTimePrecise` |
| 12216 | `drawPracticeMenu` | unmatched |  |
| 12321 | `drawCard` | matched-by-name | `src\app\GameScreen.cpp:347` `drawCard` |
| 12329 | `drawJudgeMenu` | unmatched |  |
| 12385 | `drawJudgeSliderRow` | unmatched |  |
| 12404 | `drawPracticeButton` | matched-by-name | `src\app\GameScreen.cpp:928` `drawPracticeButton` |
| 12419 | `practiceHandlesTouchDown` | unmatched |  |
| 12473 | `practiceHandlesTouchMove` | unmatched |  |
| 12510 | `updatePracticeFineScrub` | matched-by-name | `src\app\GameScreen.cpp:881` `updatePracticeFineScrub` |
| 12535 | `applyPracticeFineScrubFromTouch` | unmatched |  |
| 12543 | `applyPracticeSpeedFromTouch` | matched-by-name | `src\app\GameScreen.cpp:904` `applyPracticeSpeedFromTouch` |
| 12551 | `judgePanelVisible` | unmatched |  |
| 12556 | `applyPracticeJudgeFromTouch` | unmatched |  |
| 12570 | `applyPracticeScrubFromTouch` | matched-by-name | `src\app\GameScreen.cpp:898` `applyPracticeScrubFromTouch` |
| 12578 | `setPracticeRepeatPoint` | matched-by-name | `src\app\GameScreen.cpp:838` `setPracticeRepeatPoint` |
| 12601 | `togglePracticeRepeat` | matched-by-name | `src\app\GameScreen.cpp:862` `togglePracticeRepeat` |
| 12611 | `practiceJump` | unmatched |  |
| 12616 | `drawPauseButtons` | unmatched |  |
| 12623 | `drawButton` | unmatched |  |
| 12630 | `drawFullscreenTextureBackground` | unmatched |  |
| 12642 | `drawFullscreenTextureFbo` | unmatched |  |
| 12649 | `drawSolidRect` | unmatched |  |
| 12654 | `drawLine` | verified |  |
| 12667 | `drawTextureTopLeft` | unmatched |  |
| 12678 | `drawTextureCentered` | unmatched |  |
| 12725 | `isNoteRectInArea` | matched-by-name | `src\render\scene\NoteBatch.cpp:110` `isNoteRectInArea` |
| 12755 | `drawFullQuadWithCurrentProgram` | unmatched |  |
| 12796 | `rebuildTextureMetrics` | unmatched |  |
| 12811 | `getNoteWidthScale` | unmatched |  |
| 12817 | `getHoldUv` | unmatched |  |
| 12824 | `buildHoldUv` | matched-by-name | `src\render\scene\NoteBatch.cpp:204` `buildHoldUv` |
| 12882 | `getNoteHeadTexture` | unmatched |  |
| 12895 | `getNoteHeadTextureSameType` | unmatched |  |
| 12903 | `rebuildNoteHeadTextureDrawList` | unmatched |  |
| 12923 | `rebuildNativeRenderBuckets` | unmatched |  |
| 12964 | `buildNativeBucket` | unmatched |  |
| 13009 | `matchesNativeBucket` | unmatched |  |
| 13018 | `releaseNativeRenderBuckets` | unmatched |  |
| 13036 | `findNoteHeadTextureBucket` | unmatched |  |
| 13086 | `invalidateGl` | unmatched |  |
| 13098 | `isValidForContext` | unmatched |  |
| 13102 | `getGifFrameTexId` | unmatched |  |
| 13113 | `getGifFrameIndex` | unmatched |  |
| 13137 | `set` | fixed |  |
| 13138 | `width` | fixed | `include\rechigros\app\UiLayout.hpp:43` `width` (+5) |
| 13139 | `height` | verified | `include\rechigros\app\UiLayout.hpp:44` `height` (+5) |
| 13140 | `centerX` | matched-by-name | `include\rechigros\app\UiLayout.hpp:45` `centerX` (+1) |
| 13141 | `centerY` | matched-by-name | `include\rechigros\app\UiLayout.hpp:46` `centerY` (+1) |
| 13142 | `contains` | verified | `src\app\CalibrationScreen.cpp:293` `contains` |
| 13170 | `PrprVideoRuntime` | unmatched |  |
| 13183 | `ready` | fixed | `include\rechigros\app\TriGridBackdrop.hpp:24` `ready` (+11) |
| 13187 | `runOnPlayerThread` | unmatched |  |
| 13195 | `start` | fixed |  |
| 13212 | `pause` | fixed | `src\audio\AudioEngine.cpp:643` `pause` |
| 13225 | `markPlaybackBlocked` | unmatched |  |
| 13231 | `onPlaybackReleased` | unmatched |  |
| 13238 | `seekTo` | matched-by-name | `src\audio\AudioEngine.cpp:431` `seekTo` |
| 13250 | `setPlaybackSpeed` | matched-by-name | `src\audio\AudioEngine.cpp:701` `setPlaybackSpeed` |
| 13262 | `updateVideoSize` | unmatched |  |
| 13282 | `maybeSamplePlayerPosition` | unmatched |  |
| 13291 | `samplePlayerPosition` | unmatched |  |
| 13301 | `hasFreshPositionSample` | unmatched |  |
| 13307 | `release` | fixed | `src\app\TriGridBackdrop.cpp:260` `release` (+11) |
| 13346 | `FboTex` | unmatched |  |
| 13354 | `releaseFbos` | unmatched |  |
| 13386 | `ensurePrprFbos` | unmatched |  |
| 13402 | `deleteFbo` | unmatched |  |
| 13425 | `PrprShaderProgram` | unmatched |  |
| 13465 | `applyDefaults` | unmatched |  |
| 13486 | `getDefaultFloat` | unmatched |  |
| 13499 | `UniformInfo` | unmatched |  |
| 13507 | `initPrprShaderPrograms` | unmatched |  |
| 13560 | `warmUpPrprShaderPrograms` | unmatched |  |
| 13645 | `getPrprProgram` | unmatched |  |
| 13654 | `resolvePrprShaderSource` | unmatched |  |
| 13684 | `normalizePrprShaderKey` | unmatched |  |
| 13700 | `isPrprShaderFileName` | verified |  |
| 13706 | `prprPresetFileForKey` | unmatched |  |
| 13754 | `readAssetText` | unmatched |  |
| 13768 | `parsePrprUniformDefaults` | unmatched |  |
| 13808 | `preprocessPrprFragment` | unmatched |  |
| 13864 | `createWhiteTexture` | unmatched |  |
| 13886 | `createEmptyTexture` | unmatched |  |
| 13901 | `setLinearTextureParams` | unmatched |  |
| 13908 | `setLinearTextureParams` | unmatched |  |
| 13912 | `updateTextureWrap` | unmatched |  |
| 13921 | `uploadBitmapToBoundTexture` | unmatched |  |
| 13936 | `discardBoundFboColor` | unmatched |  |
| 13940 | `createFboForTexture` | unmatched |  |
| 13955 | `isFboComplete` | unmatched |  |
| 13966 | `loadTextureFromAssetsSafe` | unmatched |  |
| 13970 | `loadOfficialShaderTexture` | unmatched |  |
| 13995 | `flipBitmapVertical` | unmatched |  |
| 14011 | `loadStackedAtlasSlicesVFlip` | unmatched |  |
| 14044 | `loadTextureFromAssetsSafe` | unmatched |  |
| 14057 | `forceBitmapWhite` | unmatched |  |
| 14078 | `loadTextureFromFileSafe` | unmatched |  |
| 14084 | `loadTextureFromFileSafe` | unmatched |  |
| 14098 | `decodeBitmapDownsampled` | unmatched |  |
| 14116 | `loadGifTexture` | verified |  |
| 14137 | `uploadGifFramesFromPreloaded` | unmatched |  |
| 14190 | `uploadBitmapAsTexture` | unmatched |  |
| 14194 | `uploadBitmapAsTexture` | unmatched |  |
| 14218 | `loadResPackFromAssets` | unmatched |  |
| 14230 | `resolveFloat` | verified | `src\render\skin\SkinConfig.cpp:406` `resolveFloat` |
| 14240 | `resolveBool` | verified | `src\render\skin\SkinConfig.cpp:413` `resolveBool` |
| 14245 | `parseSkinColor` | verified |  |
| 14276 | `cropHoldBodyTexture` | verified |  |
| 14317 | `loadSkinTexture` | verified |  |
| 14329 | `createQuadPos` | unmatched |  |
| 14345 | `createNdcQuadPos` | unmatched |  |
| 14360 | `createQuadUv` | unmatched |  |
| 14377 | `updateQuadUv` | unmatched |  |
| 14401 | `updateQuadUvNdc` | unmatched |  |
| 14423 | `buildProgram` | matched-by-name | `src\app\TriGridBackdrop.cpp:124` `buildProgram` |
| 14448 | `compileShader` | unmatched |  |
| 14464 | `prepareShaderSource` | verified | `src\render\gl\ShaderProgram.cpp:123` `prepareShaderSource` |
| 14475 | `hasVersionDirective` | unmatched |  |
| 14485 | `upgradeLegacyVertexShaderPublic` | unmatched |  |
| 14490 | `upgradeLegacyVertexShader` | unmatched |  |
| 14497 | `upgradeLegacyFragmentShader` | unmatched |  |
| 14508 | `chooseLegacyFragmentOutputName` | unmatched |  |
| 14519 | `containsIdentifier` | unmatched |  |
| 14525 | `addGlslEs310FragmentHeader` | unmatched |  |
| 14572 | `shaderTypeName` | unmatched |  |
| 14832 | `updateOfficialBlocks` | unmatched |  |
| 14867 | `syncOfficialBlocksToTime` | unmatched |  |
| 14877 | `blockCompositeUsable` | unmatched |  |
| 14909 | `ensureBlockComposite` | unmatched |  |
| 15110 | `releaseBlockComposite` | unmatched |  |
| 15171 | `uploadBlockMaterialUniforms` | unmatched |  |
| 15236 | `updateTouchSlots` | unmatched |  |
| 15300 | `uploadBlockUniforms` | unmatched |  |
| 15325 | `putMaskVertex` | unmatched |  |
| 15333 | `emitMaskQuad` | unmatched |  |
| 15358 | `emitHoverQuad` | unmatched |  |
| 15372 | `renderBlockMasks` | unmatched |  |
| 15543 | `blockGlowRingWeight` | unmatched |  |
| 15555 | `runBlockPipelineInto` | unmatched |  |
| 15568 | `runBlockPrepasses` | unmatched |  |
| 15674 | `drawSolidRectRot` | unmatched |  |
| 15680 | `renderOfficialBlockComposite` | unmatched |  |
| 15715 | `drawBlockCompositeQuad` | unmatched |  |
| 15839 | `isTouchInsideBlock` | unmatched |  |
| 15846 | `processBlockedTouchesOfficial` | unmatched |  |
| 15872 | `updateLowPassFilterStateOfficial` | unmatched |  |
| 15882 | `setBlockAudioFilter` | unmatched |  |
| 15921 | `blockHash` | matched-by-name | `src\render\fx\BlockOverlay.cpp:52` `blockHash` |
| 15927 | `drawOfficialBlock` | unmatched |  |
| 16045 | `drawOfficialBlocks` | unmatched |  |
| 16089 | `updateOfficialChallenge` | unmatched |  |
| 16116 | `isOfficialChallengeFailed` | unmatched |  |
| 16121 | `getOfficialEffectName` | unmatched |  |

## `com\wuying\phigros\game\HitOffsetIndicator.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 26 | `HitOffsetIndicator` | fixed | `src\core\HitOffsetIndicator.cpp:9` `HitOffsetIndicator` |
| 34 | `reset` | fixed | `include\rechigros\audio\Mixer.hpp:147` `reset` (+9) |
| 51 | `addHit` | verified | `include\rechigros\game\JudgementEngine.hpp:115` `isHit` (+1) |
| 73 | `update` | fixed | `src\chart\Md5.cpp:84` `update` (+6) |
| 92 | `latestTextAlpha` | verified | `src\core\HitOffsetIndicator.cpp:32` `latestTextAlpha` |
| 99 | `markAge` | verified | `src\core\HitOffsetIndicator.cpp:36` `markAge` |
| 103 | `angleForDiff` | verified | `src\core\HitOffsetIndicator.cpp:37` `angleForDiff` |
| 108 | `judgementForOffset` | verified | `src\core\HitOffsetIndicator.cpp:38` `judgementForOffset` |
| 115 | `shouldShowCommitMark` | verified | `src\core\HitOffsetIndicator.cpp:41` `shouldShowCommitMark` |
| 121 | `fadeOutAlpha` | verified | `src\core\HitOffsetIndicator.cpp:42` `fadeOutAlpha` |
| 127 | `formatOffsetMs` | fixed | `src\core\HitOffsetIndicator.cpp:43` `formatOffsetMs` |
| 134 | `applyPointerMove` | verified | `src\core\HitOffsetIndicator.cpp:47` `applyPointerMove` |
| 150 | `easeOutCubic` | verified | `src\app\SettingsScreen.cpp:303` `easeOutCubic` |
| 156 | `clamp` | fixed |  |

## `com\wuying\phigros\game\JudgeLine.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 99 | `resolveFather` | verified | `src\core\JudgeLine.cpp:25` `resolveFather` |
| 107 | `sec2beat` | fixed | `src\core\JudgeLine.cpp:20` `sec2beat` |
| 112 | `globalSec2beat` | matched-by-name | `src\core\JudgeLine.cpp:22` `globalSec2beat` |
| 118 | `beat2sec` | fixed | `src\core\JudgeLine.cpp:23` `beat2sec` |
| 123 | `globalBeat2sec` | matched-by-name | `src\core\JudgeLine.cpp:24` `globalBeat2sec` |
| 167 | `NoteControlPoint` | unmatched |  |
| 170 | `NoteControlPoint` | unmatched |  |
| 174 | `NoteControlPoint` | unmatched |  |
| 188 | `calcNoteControl` | verified | `src\core\JudgeLine.cpp:183` `calcNoteControl` |
| 269 | `State` | fixed | `src\app\LibraryScreen.cpp:214` `state` |
| 320 | `getStateInto` | fixed |  |
| 503 | `fillState` | verified | `src\core\JudgeLine.cpp:48` `fillState` |
| 511 | `getState` | fixed | `src\app\LibraryScreen.cpp:214` `state` |
| 526 | `resetCursors` | verified | `src\core\JudgeLine.cpp:44` `resetCursors` |
| 557 | `mergeNotes` | verified | `src\core\JudgeLine.cpp:26` `mergeNotes` |
| 574 | `compare` | fixed |  |
| 583 | `compare` | fixed |  |
| 589 | `compare` | fixed |  |
| 595 | `compare` | fixed |  |
| 601 | `compare` | fixed |  |
| 607 | `compare` | fixed |  |
| 613 | `compare` | fixed |  |
| 620 | `compare` | fixed |  |
| 625 | `normalize` | verified | `src\core\JudgeLine.cpp:32` `normalize` |
| 690 | `initSpeedEventsPhi` | fixed | `src\core\JudgeLine.cpp:40` `initSpeedEventsPhi` |

## `com\wuying\phigros\game\MathUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 3 | `MathUtils` | verified |  |
| 5 | `rotatePoint` | verified |  |
| 12 | `clamp` | fixed |  |
| 16 | `clamp` | fixed |  |

## `com\wuying\phigros\game\MessageParticleField.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 112 | `sizeCurve` | verified | `src\core\MessageParticleField.cpp:32` `sizeCurve` |
| 127 | `start` | fixed |  |
| 132 | `update` | fixed | `src\chart\Md5.cpp:84` `update` (+6) |
| 163 | `spawn` | fixed | `src\render\fx\BadEffectField.cpp:17` `spawn` (+1) |
| 184 | `worldX` | verified |  |
| 187 | `alive` | unmatched |  |
| 189 | `lifeAlpha` | verified |  |
| 200 | `sizeAt` | verified | `src\core\MessageParticleField.cpp:131` `sizeAt` |
| 207 | `bandAlpha` | verified | `src\core\MessageParticleField.cpp:47` `bandAlpha` |
| 225 | `dotCount` | unmatched |  |
| 227 | `dotAt` | unmatched |  |
| 228 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |

## `com\wuying\phigros\game\NativeRenderCommands.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 64 | `NativeRenderCommands` | unmatched |  |
| 66 | `isAvailable` | matched-by-name | `src\platform\FileDialog.cpp:131` `available` (+3) |
| 174 | `LineState` | unmatched |  |
| 324 | `LineTimeline` | unmatched |  |
| 674 | `initNativeStore` | unmatched |  |
| 679 | `releaseNativeStore` | unmatched |  |
| 686 | `hasWhiteDefault` | unmatched |  |
| 693 | `countNonNullLayers` | unmatched |  |
| 702 | `countSpeedLayers` | unmatched |  |
| 711 | `hasSpeedLayers` | unmatched |  |
| 715 | `countNoteControls` | unmatched |  |
| 719 | `countLineEvents` | unmatched |  |
| 728 | `countMoveEvents` | unmatched |  |
| 737 | `countSpeedEvents` | unmatched |  |
| 746 | `countSpeedLayerEvents` | unmatched |  |
| 755 | `countColorEvents` | unmatched |  |
| 764 | `countSpeedBezierData` | unmatched |  |
| 775 | `countSpeedLayerBezierData` | unmatched |  |
| 784 | `countColorBezierData` | unmatched |  |
| 795 | `countGifEvents` | unmatched |  |
| 804 | `countLineBezierData` | unmatched |  |
| 813 | `countMoveBezierData` | unmatched |  |
| 822 | `bezierDataCount` | unmatched |  |
| 828 | `bezierDataCount` | unmatched |  |
| 849 | `LineEventWriter` | unmatched |  |
| 863 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 870 | `add` | fixed |  |
| 882 | `writeBezier` | unmatched |  |
| 917 | `MoveEventWriter` | unmatched |  |
| 933 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 940 | `add` | fixed |  |
| 954 | `writeBezier` | unmatched |  |
| 994 | `SpeedEventWriter` | unmatched |  |
| 1015 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1038 | `writeBezier` | unmatched |  |
| 1075 | `ColorEventWriter` | unmatched |  |
| 1093 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1138 | `GifEventWriter` | unmatched |  |
| 1148 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1170 | `NoteControlWriter` | unmatched |  |
| 1176 | `addAll` | matched-by-name | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1228 | `NoteBucket` | unmatched |  |
| 1267 | `setNote` | matched-by-name | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 1299 | `syncDynamicStates` | unmatched |  |
| 1316 | `resetRuntimeState` | unmatched |  |
| 1331 | `syncDynamicState` | unmatched |  |
| 1336 | `updateLoopStarts` | unmatched |  |
| 1362 | `initNativeStore` | unmatched |  |
| 1367 | `releaseNativeStore` | unmatched |  |

## `com\wuying\phigros\game\OfficialBlock.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 101 | `hasTransformEvents` | matched-by-name | `include\rechigros\game\OfficialBlock.hpp:62` `hasTransformEvents` |
| 105 | `update` | fixed | `src\chart\Md5.cpp:84` `update` (+6) |
| 240 | `safeDiv` | verified | `src\core\OfficialBlock.cpp:16` `safeDiv` |
| 248 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |
| 252 | `rotateAround` | verified | `src\core\OfficialBlock.cpp:40` `rotateAround` |
| 267 | `phase` | verified | `src\core\OfficialBlock.cpp:191` `phase` |
| 277 | `showProgress` | verified | `src\core\OfficialBlock.cpp:199` `showProgress` |
| 284 | `showAlpha` | verified | `src\core\OfficialBlock.cpp:204` `showAlpha` |
| 293 | `toLocal` | verified | `src\core\OfficialBlock.cpp:209` `toLocal` |
| 311 | `containsOriginal` | verified | `src\core\OfficialBlock.cpp:218` `containsOriginal` |
| 318 | `containsInset` | verified | `src\core\OfficialBlock.cpp:225` `containsInset` |
| 334 | `tryGetBlockingBlock` | verified | `src\core\OfficialBlock.cpp:238` `tryGetBlockingBlock` |
| 370 | `resetRuntime` | verified | `src\core\JudgeLine.cpp:266` `resetRuntime` (+1) |
| 379 | `syncToChartTime` | verified | `src\core\OfficialBlock.cpp:277` `syncToChartTime` |
| 404 | `evalMove` | verified | `src\core\OfficialBlock.cpp:53` `evalMove` |
| 425 | `easedProgress` | matched-by-name | `src\core\OfficialBlock.cpp:22` `easedProgress` |
| 444 | `findEventIndex` | verified | `src\core\OfficialBlock.cpp:28` `findEventIndex` |
| 460 | `corners` | verified | `src\core\OfficialBlock.cpp:293` `corners` |

## `com\wuying\phigros\game\OfficialChartParser.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `OfficialChartParser` | unmatched |  |
| 46 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 62 | `parseRoot` | verified | `include\rechigros\app\Replay.hpp:97` `root` (+2) |
| 100 | `call` | fixed |  |
| 151 | `parseBlockAreas` | matched-by-name | `src\chart\OfficialChartParser.cpp:125` `parseBlockAreas` |
| 245 | `parseJudgeLine` | matched-by-name | `src\chart\RpeChartParser.cpp:806` `parseJudgeLine` |
| 303 | `parseNotes` | matched-by-name | `src\chart\OfficialChartParser.cpp:44` `parseNotes` (+1) |
| 344 | `parseLineEvents1D` | unmatched |  |
| 372 | `parseMoveComponentEvents` | unmatched |  |
| 420 | `parseSpeedEventsOfficial` | unmatched |  |
| 448 | `unpackV1MoveEvents` | unmatched |  |
| 479 | `ensureCoverage` | verified | `src\chart\OfficialChartParser.cpp:28` `ensureCoverage` (+1) |
| 502 | `defaultSpeedEvents` | unmatched |  |
| 546 | `parseEventTime` | matched-by-name | `src\chart\RpeChartParser.cpp:105` `eventTime` |
| 556 | `getAsDouble` | fixed |  |
| 565 | `getAsInt` | unmatched |  |
| 574 | `getAsBool` | fixed |  |
| 588 | `getFirst` | unmatched |  |
| 597 | `asObj` | unmatched |  |
| 602 | `asArray` | unmatched |  |
| 612 | `copyLineEvent` | unmatched |  |

## `com\wuying\phigros\game\OfficialEase.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 25 | `OfficialEase` | verified |  |
| 47 | `toRpeEasing` | verified | `src\core\OfficialEase.cpp:62` `toRpeEasing` |
| 84 | `evaluate` | fixed | `src\core\OfficialEase.cpp:80` `evaluate` (+2) |
| 89 | `evaluate` | fixed | `src\core\OfficialEase.cpp:80` `evaluate` (+2) |
| 117 | `bezier01` | verified | `src\core\OfficialEase.cpp:29` `bezier01` |
| 142 | `sampleX` | unmatched |  |
| 146 | `sampleY` | unmatched |  |
| 150 | `derivativeX` | matched-by-name | `src\core\OfficialEase.cpp:28` `derivativeX` |

## `com\wuying\phigros\game\OfficialLevelEffects.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 100 | `OfficialLevelEffects` | unmatched |  |
| 108 | `isAvailable` | matched-by-name | `src\platform\FileDialog.cpp:131` `available` (+3) |
| 117 | `loadRoot` | unmatched |  |
| 127 | `loadByChart` | fixed |  |
| 159 | `loadByName` | unmatched |  |
| 166 | `readEffectTables` | fixed |  |
| 249 | `slashCount` | fixed |  |
| 259 | `slashAt` | fixed |  |
| 287 | `slashLightSetting` | verified |  |
| 299 | `evaluateTrack` | verified | `src\core\OfficialLevelEffects.cpp:99` `evaluateTrack` |
| 331 | `startValue` | verified |  |
| 333 | `endValue` | fixed |  |
| 334 | `track` | fixed | `src\core\OfficialLevelEffects.cpp:225` `track` (+1) |
| 342 | `hasTrack` | matched-by-name | `src\core\OfficialLevelEffects.cpp:225` `track` (+1) |
| 348 | `scalar` | fixed | `src\core\OfficialLevelEffects.cpp:235` `scalar` |
| 355 | `settingParams` | unmatched |  |
| 364 | `effectSettings` | unmatched |  |
| 371 | `settingFloat` | fixed | `src\core\OfficialLevelEffects.cpp:240` `settingFloat` |
| 383 | `isChallengeFailed` | fixed |  |
| 391 | `resetChallenge` | fixed |  |
| 398 | `onBadOrMiss` | fixed |  |
| 415 | `onBlockStateChanged` | fixed |  |
| 423 | `updateAmetrineVignette` | fixed |  |
| 449 | `ametrineVignette` | fixed |  |
| 464 | `PassSpec` | fixed |  |
| 467 | `PassSpec` | fixed |  |
| 474 | `buildPrprEffects` | fixed | `src\core\OfficialLevelEffects.cpp:246` `buildPrprEffects` |
| 613 | `toLineEvents` | fixed |  |
| 684 | `U` | fixed |  |
| 687 | `T` | fixed |  |
| 690 | `C` | fixed |  |
| 709 | `dogConsts` | unmatched |  |
| 725 | `slashUniforms` | unmatched |  |
| 737 | `slashTracks` | unmatched |  |
| 749 | `addSlashVars` | unmatched |  |
| 767 | `slashKeys` | unmatched |  |
| 773 | `posEvents` | unmatched |  |

## `com\wuying\phigros\game\PecChartParser.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `PecChartParser` | unmatched |  |
| 23 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 258 | `clampEase` | verified | `src\chart\PecChartParser.cpp:85` `clampEase` |
| 264 | `parseNote` | matched-by-name | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 307 | `toNotePosX` | unmatched |  |
| 311 | `stripComments` | unmatched |  |
| 317 | `parseDouble` | unmatched |  |
| 326 | `round4` | verified |  |
| 339 | `lb` | fixed |  |
| 352 | `SpeedPoint` | unmatched |  |
| 361 | `BpmPoint` | unmatched |  |
| 366 | `buildSpeedEvents` | fixed |  |
| 400 | `normalizeMoveEvents` | unmatched |  |
| 425 | `normalizeLineEvents` | unmatched |  |
| 447 | `applyAlphaSpecials` | verified | `src\chart\PecChartParser.cpp:183` `applyAlphaSpecials` |
| 488 | `assignVisibleTime` | verified | `src\chart\PecChartParser.cpp:136` `assignVisibleTime` |
| 545 | `convertNotesBeats` | unmatched |  |
| 573 | `convertMoveEventsBeats` | unmatched |  |
| 592 | `convertLineEventsBeats` | unmatched |  |
| 609 | `defaultMove` | verified |  |
| 622 | `defaultSpeed` | unmatched |  |
| 633 | `defaultLine` | verified |  |
| 651 | `BpmConverter` | verified | `src\chart\PecChartParser.cpp:55` `BpmConverter` (+1) |
| 670 | `baseBpm` | verified | `src\chart\PecChartParser.cpp:64` `baseBpm` (+1) |
| 677 | `bpmAtBeat` | matched-by-name | `src\chart\PecChartParser.cpp:66` `bpmAtBeat` |
| 685 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 695 | `beatToNormBeat` | verified | `src\chart\PecChartParser.cpp:71` `beatToNormBeat` |
| 702 | `findSegment` | verified |  |

## `com\wuying\phigros\game\PlayResult.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `PlayResult` | verified |  |
| 37 | `PlayResult` | verified |  |
| 71 | `isFullCombo` | verified | `include\rechigros\game\PlayResult.hpp:18` `isFullCombo` |
| 75 | `isAllPerfect` | verified | `include\rechigros\game\PlayResult.hpp:20` `isAllPerfect` |

## `com\wuying\phigros\game\PrprEffect.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `PrprEffect` | fixed |  |
| 35 | `isActive` | verified | `include\rechigros\game\PracticeJudgeWindows.hpp:22` `active` |
| 41 | `PrprVar` | unmatched |  |
| 49 | `ConstFloat` | verified |  |
| 53 | `ConstFloat` | verified |  |
| 62 | `ConstVec` | verified |  |
| 71 | `FloatEvents` | verified |  |
| 75 | `safeEvents` | verified |  |

## `com\wuying\phigros\game\PrprVideo.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `PrprVideo` | unmatched |  |
| 52 | `emptyEvents` | verified |  |
| 57 | `alphaAt` | matched-by-name | `src\render\fx\BadEffectField.cpp:8` `alphaAt` |
| 61 | `dimAt` | unmatched |  |
| 65 | `resetCursors` | verified | `src\core\JudgeLine.cpp:44` `resetCursors` |
| 72 | `eval` | fixed |  |
| 80 | `sanitizeScaleType` | matched-by-name | `include\rechigros\game\PrprEffect.hpp:48` `sanitizeScaleType` |
| 85 | `finiteOr` | verified |  |
| 89 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |

## `com\wuying\phigros\game\ReplayData.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `ReplayData` | unmatched |  |
| 48 | `toPlayResult` | matched-by-name | `src\app\Replay.cpp:139` `toPlayResult` |
| 54 | `fromPlayResult` | unmatched |  |

## `com\wuying\phigros\game\RpeChartParser.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 55 | `RpeChartParser` | verified |  |
| 126 | `call` | fixed |  |
| 172 | `buildBezierLuts` | verified |  |
| 184 | `buildBezierLutsForLine` | verified |  |
| 226 | `assignBezierLuts` | verified |  |
| 243 | `assignBezierLutsM` | verified |  |
| 260 | `assignBezierLutsL` | verified |  |
| 306 | `assignSpeedBezierLuts` | verified |  |
| 323 | `equalsAnyIgnoreCase` | verified |  |
| 393 | `parseAttachUiElementId` | verified |  |
| 413 | `parseJudgeLine` | matched-by-name | `src\chart\RpeChartParser.cpp:806` `parseJudgeLine` |
| 607 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 664 | `getEventTime` | matched-by-name | `src\chart\RpeChartParser.cpp:105` `eventTime` |
| 675 | `parseOffsetSeconds` | verified |  |
| 688 | `parseRpeVersion` | verified |  |
| 701 | `parseBpmList` | verified |  |
| 717 | `parseAlphaEvents` | matched-by-name | `src\chart\RpeChartParser.cpp:226` `parseAlphaEvents` |
| 733 | `parseLineEvents` | matched-by-name | `src\chart\OfficialChartParser.cpp:33` `parseLineEvents` (+1) |
| 798 | `parseMoveEvents` | fixed | `src\chart\OfficialChartParser.cpp:36` `parseMoveEvents` (+1) |
| 936 | `parseSpeedEvents` | matched-by-name | `src\chart\RpeChartParser.cpp:621` `parseSpeedEvents` |
| 994 | `buildLegacySpeedHeightLayer` | matched-by-name | `src\chart\RpeChartParser.cpp:562` `buildLegacySpeedHeightLayer` |
| 1047 | `applyLegacySpeedHeightMode` | matched-by-name | `src\chart\RpeChartParser.cpp:614` `applyLegacySpeedHeightMode` |
| 1061 | `LegacySpeedCursor` | verified |  |
| 1065 | `valueAt` | verified |  |
| 1114 | `valueBefore` | verified |  |
| 1162 | `addSpeedEvent` | matched-by-name | `src\chart\RpeChartParser.cpp:545` `addSpeedEvent` |
| 1185 | `parseScaleIntoLine` | verified |  |
| 1233 | `parseInclineEvents` | verified |  |
| 1248 | `applyStaticSkew` | verified |  |
| 1260 | `parseStaticSkew` | verified |  |
| 1273 | `parseGifEvents` | unmatched |  |
| 1314 | `parseColorEvents` | unmatched |  |
| 1364 | `parseTextEvents` | unmatched |  |
| 1414 | `parseNotesIntoLine` | unmatched |  |
| 1465 | `parseRpeNoteAbove` | unmatched |  |
| 1479 | `parseRpeAboveFlag` | unmatched |  |
| 1516 | `parseNote` | matched-by-name | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 1637 | `RpeNoteControlRaw` | unmatched |  |
| 1644 | `parseNoteControls` | unmatched |  |
| 1660 | `parseNoteControlsAny` | unmatched |  |
| 1672 | `parseNoteControlsAny` | unmatched |  |
| 1712 | `withValueFallbacks` | unmatched |  |
| 1723 | `mergeSpeedLayers` | matched-by-name | `src\chart\RpeChartParser.cpp:504` `mergeSpeedLayers` |
| 1777 | `SpeedLayerCursor` | verified |  |
| 1781 | `valueAtStart` | unmatched |  |
| 1826 | `valueBefore` | verified |  |
| 1869 | `speedEventAt` | unmatched |  |
| 1873 | `start` | fixed |  |
| 1874 | `end` | fixed |  |
| 1880 | `speedAt` | verified | `src\core\EventUtils.cpp:740` `speedAt` |
| 1885 | `speedAtLayersStart` | matched-by-name | `src\chart\RpeChartParser.cpp:362` `speedAtLayersStart` |
| 1894 | `speedAtLayersEnd` | matched-by-name | `src\chart\RpeChartParser.cpp:368` `speedAtLayersEnd` |
| 1901 | `addSampledMergedSpeedEvents` | matched-by-name | `src\chart\RpeChartParser.cpp:490` `addSampledMergedSpeedEvents` |
| 1916 | `addMergedSpeedEventFromVarying` | matched-by-name | `src\chart\RpeChartParser.cpp:459` `addMergedSpeedEventFromVarying` |
| 1952 | `speedEventProgress` | matched-by-name | `src\chart\RpeChartParser.cpp:390` `speedEventProgress` |
| 1962 | `subSpeedEasingWindow` | matched-by-name | `src\chart\RpeChartParser.cpp:399` `subSpeedEasingWindow` |
| 1977 | `hasSpeedBezier` | matched-by-name | `src\chart\RpeChartParser.cpp:375` `hasSpeedBezier` |
| 1983 | `evalSpeedBezier` | matched-by-name | `src\chart\RpeChartParser.cpp:380` `evalSpeedBezier` |
| 1995 | `buildSpeedSubBezierLut` | matched-by-name | `src\chart\RpeChartParser.cpp:411` `buildSpeedSubBezierLut` |
| 2015 | `addMergedSpeedEvent` | matched-by-name | `src\chart\RpeChartParser.cpp:429` `addMergedSpeedEvent` |
| 2053 | `ensureCoverageRotate` | unmatched |  |
| 2057 | `ensureCoverageAlpha` | unmatched |  |
| 2061 | `ensureCoveragePos` | unmatched |  |
| 2065 | `ensureCoverage1D` | unmatched |  |
| 2089 | `ensureCoverageMove` | unmatched |  |
| 2093 | `ensureCoverageMove` | unmatched |  |
| 2120 | `ensureCoverageSpeed` | unmatched |  |
| 2173 | `defaultMove` | verified |  |
| 2186 | `defaultSpeed` | unmatched |  |
| 2197 | `canPair` | unmatched |  |
| 2214 | `selectJudgeLineArray` | fixed |  |
| 2224 | `looksLikeJudgeLineArray` | unmatched |  |
| 2238 | `asObj` | unmatched |  |
| 2244 | `getAsArray` | unmatched |  |
| 2254 | `getAsObject` | unmatched |  |
| 2264 | `getFirst` | unmatched |  |
| 2273 | `getAsDouble` | fixed |  |
| 2285 | `getAsInt` | unmatched |  |
| 2297 | `getAsBool` | fixed |  |
| 2316 | `getAsString` | unmatched |  |
| 2326 | `resolvePath` | unmatched |  |
| 2337 | `toXNorm` | unmatched |  |
| 2342 | `toYRaw` | unmatched |  |
| 2347 | `toNotePosX` | unmatched |  |
| 2352 | `parseBeat` | verified | `src\chart\RpeChartParser.cpp:83` `parseBeat` |
| 2391 | `parseBezierPoints` | fixed | `src\core\EventUtils.cpp:16` `hasBezierPoints` |
| 2423 | `parseVec2` | fixed | `src\chart\RpeChartParser.cpp:235` `vec2` |
| 2443 | `parseRgb` | verified | `src\chart\RpeChartParser.cpp:672` `rgb` |
| 2483 | `clamp01f` | unmatched |  |
| 2493 | `BpmPoint` | unmatched |  |
| 2505 | `BpmConverter` | verified | `src\chart\PecChartParser.cpp:55` `BpmConverter` (+1) |
| 2524 | `baseBpm` | verified | `src\chart\PecChartParser.cpp:64` `baseBpm` (+1) |
| 2531 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 2541 | `beatToNormBeat` | verified | `src\chart\PecChartParser.cpp:71` `beatToNormBeat` |
| 2548 | `findSegment` | verified |  |
| 2565 | `toTimeline` | unmatched |  |

## `com\wuying\phigros\ui\AssetBitmaps.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 30 | `sizeOf` | unmatched |  |
| 42 | `AssetBitmaps` | unmatched |  |
| 44 | `preload` | verified |  |
| 58 | `get` | fixed | `src\render\text\TextRenderer.cpp:633` `get` |
| 81 | `cached` | verified |  |
| 87 | `decodeAndCache` | unmatched |  |
| 95 | `decode` | excluded | `src\app\UiText.cpp:140` `decode` |

## `com\wuying\phigros\ui\CalibrationActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 59 | `Runnable` | unmatched |  |
| 61 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 71 | `onCreate` | unmatched |  |
| 95 | `buildUi` | unmatched |  |
| 199 | `addStepButton` | unmatched |  |
| 209 | `makeButton` | unmatched |  |
| 221 | `matchWrap` | unmatched |  |
| 227 | `setAudioOffsetMs` | unmatched |  |
| 237 | `refreshValueViews` | unmatched |  |
| 261 | `finishWithResult` | unmatched |  |
| 270 | `onBackPressed` | verified |  |
| 275 | `onPause` | verified |  |
| 282 | `onResume` | verified |  |
| 296 | `onDestroy` | unmatched |  |
| 309 | `applyImmersiveMode` | unmatched |  |
| 325 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 329 | `clamp` | fixed |  |
| 366 | `CalibrationView` | unmatched |  |
| 376 | `setAudioOffsetMs` | unmatched |  |
| 381 | `setPanelWidthPx` | unmatched |  |
| 385 | `resetSmoothClock` | unmatched |  |
| 392 | `onDraw` | verified |  |
| 447 | `drawTap` | unmatched |  |
| 460 | `drawHitEffects` | matched-by-name | `src\app\CalibrationScreen.cpp:345` `drawHitEffects` (+1) |
| 510 | `drawTouchMarkers` | matched-by-name | `src\app\CalibrationScreen.cpp:409` `drawTouchMarkers` |
| 531 | `onTouchEvent` | unmatched |  |
| 550 | `currentNoteY` | unmatched |  |
| 563 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 567 | `currentLoopTime` | unmatched |  |
| 574 | `currentMarkerProgress` | unmatched |  |
| 578 | `rememberRenderedTapPosition` | unmatched |  |
| 587 | `judgeLineHalfWidth` | verified |  |
| 591 | `judgeLineY` | verified |  |
| 595 | `noteDrawProgressForLoopTime` | unmatched |  |
| 605 | `noteMarkerProgressForLoopTime` | unmatched |  |
| 612 | `tapYForProgress` | unmatched |  |
| 616 | `touchMarkerLineWidth` | unmatched |  |
| 620 | `clamp` | fixed |  |
| 626 | `perfectColor` | verified |  |
| 630 | `indicatorColor` | unmatched |  |
| 635 | `rgb` | fixed | `src\chart\RpeChartParser.cpp:672` `rgb` |
| 642 | `loadAssetBitmap` | unmatched |  |
| 651 | `splitHitFxFrames` | unmatched |  |
| 669 | `loadHitFxGrid` | unmatched |  |
| 686 | `TouchMarker` | unmatched |  |
| 704 | `reset` | fixed | `include\rechigros\audio\Mixer.hpp:147` `reset` (+9) |
| 713 | `getSeconds` | unmatched |  |
| 760 | `resetBiasHistory` | unmatched |  |
| 766 | `addBias` | unmatched |  |
| 792 | `CalibrationHitEffect` | unmatched |  |

## `com\wuying\phigros\ui\ChartSelectActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 244 | `onCreate` | unmatched |  |
| 267 | `onSaveInstanceState` | unmatched |  |
| 288 | `onNewIntent` | verified |  |
| 295 | `onResume` | verified |  |
| 306 | `refreshLocalChartsOnResume` | unmatched |  |
| 321 | `onPause` | verified |  |
| 330 | `onDestroy` | unmatched |  |
| 338 | `onWindowFocusChanged` | verified |  |
| 344 | `onBackPressed` | verified |  |
| 364 | `setupRoot` | unmatched |  |
| 394 | `restoreUiState` | unmatched |  |
| 423 | `renderRestoredScreen` | unmatched |  |
| 437 | `findLocalRecordById` | unmatched |  |
| 446 | `applyImmersiveMode` | unmatched |  |
| 467 | `renderHome` | verified |  |
| 571 | `applyHomeLayout` | verified |  |
| 653 | `openGithub` | verified |  |
| 661 | `menuButtonLp` | verified |  |
| 667 | `menuButton` | verified |  |
| 676 | `renderChartList` | verified |  |
| 721 | `OnChildAttachStateChangeListener` | unmatched |  |
| 722 | `onChildViewAttachedToWindow` | unmatched |  |
| 732 | `onChildViewDetachedFromWindow` | unmatched |  |
| 799 | `baseScreen` | unmatched |  |
| 807 | `addBackButton` | verified | `src\app\ScreenCommon.cpp:102` `backButton` |
| 839 | `preloadLocalCovers` | verified |  |
| 847 | `rememberLocalScrollPosition` | verified |  |
| 856 | `restorePendingLocalScroll` | verified |  |
| 865 | `addChartListPanel` | verified |  |
| 879 | `chartListRightMarginDp` | verified |  |
| 883 | `addChartTabs` | verified |  |
| 893 | `switchChartTab` | verified |  |
| 909 | `addSearchBox` | verified |  |
| 923 | `addChartActions` | verified |  |
| 942 | `setManageModeInPlace` | verified |  |
| 955 | `notifyVisibleChartItems` | unmatched |  |
| 966 | `updateVisibleLocalCards` | verified |  |
| 976 | `updateVisibleLocalCard` | verified |  |
| 984 | `applyVisibleLocalCard` | verified |  |
| 994 | `updateSelectionStateView` | verified |  |
| 1007 | `addOnlinePageControls` | unmatched |  |
| 1034 | `addSideIcon` | verified |  |
| 1048 | `addDetailTopIcon` | verified |  |
| 1062 | `renderSettings` | verified |  |
| 1119 | `OnPreDrawListener` | unmatched |  |
| 1122 | `onPreDraw` | unmatched |  |
| 1134 | `returnFromSettings` | verified |  |
| 1144 | `settingsRowWidth` | verified |  |
| 1150 | `applySettingsRowWidths` | verified |  |
| 1165 | `offsetSettingsRows` | verified |  |
| 1191 | `updateSettingsPreview` | verified |  |
| 1195 | `addSettingsTabs` | verified |  |
| 1216 | `buildVideoSettings` | verified |  |
| 1266 | `buildAudioSettings` | verified |  |
| 1283 | `buildReplaySettings` | verified |  |
| 1304 | `buildMoreSettings` | verified |  |
| 1311 | `settingsCard` | unmatched |  |
| 1323 | `syncSettingsToggles` | verified |  |
| 1332 | `addSwitch` | unmatched |  |
| 1349 | `addSliderRow` | unmatched |  |
| 1377 | `addEditRow` | unmatched |  |
| 1396 | `SimpleWatcher` | unmatched |  |
| 1397 | `afterTextChanged` | unmatched |  |
| 1403 | `addActionRow` | unmatched |  |
| 1420 | `addDualActions` | unmatched |  |
| 1435 | `addReplayRow` | matched-by-name | `src\app\SettingsScreen.cpp:558` `replayRow` |
| 1459 | `rowBase` | unmatched |  |
| 1466 | `smallButton` | unmatched |  |
| 1483 | `applyDetailBackground` | unmatched |  |
| 1514 | `clearDetailBackground` | unmatched |  |
| 1519 | `renderDetail` | unmatched |  |
| 1635 | `updateDetailTextSafety` | unmatched |  |
| 1648 | `DetailCanvasHost` | unmatched |  |
| 1660 | `textInset` | unmatched |  |
| 1664 | `updateTextSafety` | unmatched |  |
| 1696 | `onMeasure` | verified |  |
| 1713 | `onLayout` | verified |  |
| 1729 | `SettingsBackdrop` | unmatched |  |
| 1730 | `onDraw` | verified |  |
| 1742 | `addDetailText` | unmatched |  |
| 1751 | `showSearchDialog` | unmatched |  |
| 1781 | `showPageJumpDialog` | unmatched |  |
| 1801 | `showDetailInfoDialog` | unmatched |  |
| 1816 | `showSkinSelectDialog` | unmatched |  |
| 1829 | `deleteSelectedSkin` | unmatched |  |
| 1844 | `selectedSkinName` | unmatched |  |
| 1851 | `hitOffsetLabel` | matched-by-name | `src\app\SettingsScreen.cpp:370` `hitOffsetLabel` |
| 1858 | `cycleHitOffsetMode` | unmatched |  |
| 1864 | `loadOnlineIndex` | unmatched |  |
| 1899 | `confirmDownload` | unmatched |  |
| 1908 | `downloadChart` | unmatched |  |
| 1939 | `ensureOnlineCover` | unmatched |  |
| 1979 | `filterLocal` | unmatched |  |
| 1991 | `filterOnline` | unmatched |  |
| 2004 | `toggleLocalSelection` | unmatched |  |
| 2018 | `updateLocalSelectionState` | unmatched |  |
| 2024 | `toggleSelectAllLocal` | unmatched |  |
| 2037 | `confirmDeleteSelected` | unmatched |  |
| 2057 | `startSelectedRecord` | unmatched |  |
| 2061 | `startSelectedRecord` | unmatched |  |
| 2071 | `playReplay` | matched-by-name | `src\app\ResultScreen.cpp:378` `playReplay` |
| 2098 | `startPlay` | unmatched |  |
| 2102 | `startPlay` | unmatched |  |
| 2115 | `putPlaySettings` | unmatched |  |
| 2157 | `handleIncomingIntent` | unmatched |  |
| 2203 | `describeFile` | unmatched |  |
| 2211 | `initLaunchers` | unmatched |  |
| 2302 | `tryUpdateSongInfo` | unmatched |  |
| 2317 | `launchPickZip` | unmatched |  |
| 2321 | `launchPickMusic` | unmatched |  |
| 2325 | `launchPickChart` | unmatched |  |
| 2329 | `launchPickBg` | unmatched |  |
| 2333 | `launchPickSkin` | unmatched |  |
| 2337 | `launchPickReplayZip` | unmatched |  |
| 2341 | `selectLocalChart` | unmatched |  |
| 2352 | `getSelectedChartId` | unmatched |  |
| 2357 | `clearSelectedChartIfDeleted` | unmatched |  |
| 2369 | `onOnlineChartImported` | unmatched |  |
| 2374 | `getSelectedSkinIndex` | unmatched |  |
| 2378 | `setSelectedSkinIndex` | unmatched |  |
| 2382 | `getMusicFile` | unmatched |  |
| 2384 | `getChartFile` | unmatched |  |
| 2385 | `getBgFile` | unmatched |  |
| 2386 | `getSongName` | unmatched |  |
| 2387 | `getDifficulty` | unmatched |  |
| 2388 | `getChartOffsetMs` | unmatched |  |
| 2389 | `setLocalChartDetailVisible` | unmatched |  |
| 2392 | `updateStartButtonState` | unmatched |  |
| 2395 | `setSongName` | unmatched |  |
| 2400 | `setDifficulty` | unmatched |  |
| 2405 | `setChartOffsetMs` | unmatched |  |
| 2410 | `persistSelectedChartInfo` | unmatched |  |
| 2419 | `getAspectRatio` | unmatched |  |
| 2421 | `setAspectRatio` | unmatched |  |
| 2422 | `getKeyScale` | unmatched |  |
| 2423 | `setKeyScale` | unmatched |  |
| 2424 | `getScrollSpeed` | unmatched |  |
| 2425 | `setScrollSpeed` | unmatched |  |
| 2426 | `isMirrorX` | matched-by-name | `include\rechigros\render\math\Mat4.hpp:58` `mirrorX` |
| 2427 | `setMirrorX` | matched-by-name | `include\rechigros\render\math\Mat4.hpp:58` `mirrorX` |
| 2428 | `isAutoplay` | matched-by-name | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 2429 | `setAutoplay` | matched-by-name | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 2438 | `isChallengeMode` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:43` `challengeMode` |
| 2439 | `setChallengeMode` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:43` `challengeMode` |
| 2440 | `getBackgroundDim` | unmatched |  |
| 2441 | `setBackgroundDim` | unmatched |  |
| 2442 | `setJudgeLineWidth` | unmatched |  |
| 2443 | `isLowResMode` | unmatched |  |
| 2444 | `setLowResMode` | unmatched |  |
| 2445 | `isAntialias` | unmatched |  |
| 2446 | `setAntialias` | unmatched |  |
| 2447 | `isShowFps` | unmatched |  |
| 2448 | `setShowFps` | unmatched |  |
| 2449 | `isRealtimeAcc` | unmatched |  |
| 2450 | `setRealtimeAcc` | unmatched |  |
| 2451 | `isScoreJumpEffect` | unmatched |  |
| 2452 | `setScoreJumpEffect` | unmatched |  |
| 2453 | `isDisableShader` | unmatched |  |
| 2454 | `setDisableShader` | unmatched |  |
| 2455 | `isStoryMode` | unmatched |  |
| 2456 | `setStoryMode` | unmatched |  |
| 2457 | `isShowDebugInfo` | unmatched |  |
| 2458 | `setShowDebugInfo` | unmatched |  |
| 2459 | `isChartReveal` | unmatched |  |
| 2460 | `setChartReveal` | unmatched |  |
| 2469 | `isMultiPressHighlight` | unmatched |  |
| 2470 | `setMultiPressHighlight` | unmatched |  |
| 2471 | `isApfcIndicator` | unmatched |  |
| 2472 | `setApfcIndicator` | unmatched |  |
| 2473 | `isJudgementDetailDistribution` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:40` `setJudgementDetailDistribution` |
| 2474 | `setJudgementDetailDistribution` | matched-by-name | `include\rechigros\game\JudgementEngine.hpp:40` `setJudgementDetailDistribution` |
| 2475 | `isHitOffsetIndicator` | matched-by-name | `src\core\HitOffsetIndicator.cpp:9` `HitOffsetIndicator` |
| 2476 | `setHitOffsetIndicator` | matched-by-name | `src\core\HitOffsetIndicator.cpp:9` `HitOffsetIndicator` |
| 2477 | `getHitOffsetIndicatorMode` | unmatched |  |
| 2478 | `setHitOffsetIndicatorMode` | unmatched |  |
| 2484 | `getMusicVolumePct` | unmatched |  |
| 2485 | `setMusicVolumePct` | unmatched |  |
| 2486 | `getSfxVolumePct` | unmatched |  |
| 2487 | `setSfxVolumePct` | unmatched |  |
| 2488 | `getMusicSpeed` | matched-by-name | `src\core\GameplaySession.cpp:66` `setMusicSpeed` |
| 2489 | `setMusicSpeed` | matched-by-name | `src\core\GameplaySession.cpp:66` `setMusicSpeed` |
| 2490 | `getAudioOffsetMs` | unmatched |  |
| 2491 | `setAudioOffsetMs` | unmatched |  |
| 2492 | `isReplayEnabled` | unmatched |  |
| 2493 | `setReplayEnabled` | unmatched |  |
| 2494 | `loadPersistedSettings` | unmatched |  |
| 2534 | `getPrefs` | unmatched |  |
| 2539 | `isApfcIndicatorEnabled` | unmatched |  |
| 2545 | `editPrefs` | unmatched |  |
| 2549 | `isReadyToStart` | unmatched |  |
| 2555 | `iconButton` | matched-by-name | `src\app\UiStyle.cpp:192` `iconButton` |
| 2565 | `glassPanel` | matched-by-name | `src\app\UiLayout.cpp:182` `glassPanel` |
| 2575 | `rectPanel` | matched-by-name | `src\app\UiLayout.cpp:191` `rectPanel` |
| 2581 | `text` | fixed |  |
| 2585 | `text` | fixed |  |
| 2598 | `autoSingleLineText` | unmatched |  |
| 2604 | `uiTypeface` | unmatched |  |
| 2616 | `assetImage` | unmatched |  |
| 2622 | `loadAssetBitmap` | unmatched |  |
| 2631 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 2635 | `tryTakePersistableRead` | unmatched |  |
| 2642 | `hasLowerSuffix` | unmatched |  |
| 2646 | `errorMessage` | unmatched |  |
| 2672 | `clampPctToMul` | unmatched |  |
| 2676 | `sanitizeAspectRatio` | matched-by-name | `src\app\Settings.cpp:47` `sanitizeAspectRatio` |
| 2682 | `clampFinite` | unmatched |  |
| 2689 | `lerp` | fixed | `src\core\EventUtils.cpp:235` `lerp` |
| 2693 | `easeOutCubic` | verified | `src\app\SettingsScreen.cpp:303` `easeOutCubic` |
| 2698 | `clampInt` | unmatched |  |
| 2704 | `parseFloatSafe` | unmatched |  |
| 2713 | `parseIntSafe` | unmatched |  |
| 2722 | `formatFloat` | unmatched |  |
| 2726 | `fallback` | fixed |  |
| 2730 | `nonEmpty` | verified | `src\app\LibraryScreen.cpp:159` `nonEmpty` |
| 2734 | `shouldMarquee` | matched-by-name | `src\app\LibraryScreen.cpp:157` `shouldMarquee` |
| 2738 | `restartMarquee` | unmatched |  |
| 2745 | `stableId` | unmatched |  |
| 2754 | `shortDifficulty` | unmatched |  |
| 2760 | `gradeName` | matched-by-name | `src\app\ChartLibrary.cpp:410` `gradeName` |
| 2770 | `safeFileName` | unmatched |  |
| 2775 | `extensionOrJpg` | unmatched |  |
| 2839 | `beforeTextChanged` | unmatched |  |
| 2840 | `onTextChanged` | unmatched |  |
| 2855 | `ChartTabBar` | unmatched |  |
| 2859 | `ChartTabBar` | unmatched |  |
| 2877 | `setState` | matched-by-name | `src\app\LibraryScreen.cpp:214` `state` |
| 2884 | `setOnTabSelected` | unmatched |  |
| 2888 | `onDraw` | verified |  |
| 2927 | `performClick` | unmatched |  |
| 2933 | `onTouchEvent` | unmatched |  |
| 2944 | `drawSkewRect` | unmatched |  |
| 2957 | `tabProgress` | unmatched |  |
| 2964 | `selectedAmount` | unmatched |  |
| 2971 | `centerFor` | unmatched |  |
| 2975 | `widthFor` | verified |  |
| 2986 | `ChartCardAdapter` | unmatched |  |
| 2992 | `ChartCardAdapter` | unmatched |  |
| 3003 | `getItemId` | unmatched |  |
| 3013 | `findLocalPosition` | unmatched |  |
| 3021 | `onCreateViewHolder` | unmatched |  |
| 3090 | `onBindViewHolder` | unmatched |  |
| 3095 | `onBindViewHolder` | unmatched |  |
| 3100 | `bindHolder` | unmatched |  |
| 3149 | `getItemCount` | unmatched |  |
| 3154 | `applyLocalState` | unmatched |  |
| 3173 | `Vh` | verified |  |
| 3188 | `ChartCardFrame` | unmatched |  |
| 3191 | `onMeasure` | verified |  |

## `com\wuying\phigros\ui\CoverImageLoader.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `sizeOf` | unmatched |  |
| 28 | `CoverImageLoader` | unmatched |  |
| 30 | `load` | fixed | `src\app\CoverCache.cpp:189` `load` (+3) |
| 64 | `preload` | verified |  |
| 80 | `cacheKey` | verified |  |
| 85 | `decodeSampled` | unmatched |  |

## `com\wuying\phigros\ui\GameDialog.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 62 | `GameDialog` | unmatched |  |
| 83 | `Builder` | unmatched |  |
| 85 | `setTitle` | unmatched |  |
| 87 | `setTitle` | unmatched |  |
| 88 | `setMessage` | verified |  |
| 89 | `setMessage` | verified |  |
| 90 | `setView` | matched-by-name | `src\platform\Gif.cpp:30` `view` |
| 91 | `setItems` | verified |  |
| 96 | `setPositiveButton` | unmatched |  |
| 99 | `setPositiveButton` | unmatched |  |
| 102 | `setNegativeButton` | unmatched |  |
| 105 | `setNegativeButton` | unmatched |  |
| 108 | `setNeutralButton` | unmatched |  |
| 111 | `setNeutralButton` | unmatched |  |
| 114 | `setCancelable` | verified |  |
| 115 | `setOnDismissListener` | verified |  |
| 118 | `create` | verified |  |
| 124 | `show` | verified |  |
| 131 | `onCreate` | unmatched |  |
| 141 | `configureWindow` | unmatched |  |
| 177 | `show` | verified |  |
| 187 | `dismiss` | fixed |  |
| 205 | `AnimatorListenerAdapter` | unmatched |  |
| 206 | `onAnimationEnd` | unmatched |  |
| 213 | `finishDismiss` | unmatched |  |
| 222 | `runEnter` | unmatched |  |
| 234 | `AnimatorListenerAdapter` | unmatched |  |
| 235 | `onAnimationEnd` | unmatched |  |
| 241 | `applyProgress` | matched-by-name | `src\app\UiLayout.cpp:451` `progress` |
| 255 | `onStop` | unmatched |  |
| 261 | `buildContent` | verified |  |
| 378 | `weightedWrap` | unmatched |  |
| 386 | `styleCustomView` | verified |  |
| 401 | `text` | fixed |  |
| 411 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 420 | `ButtonSpec` | verified |  |
| 442 | `DialogPanel` | unmatched |  |
| 458 | `addButton` | unmatched |  |
| 464 | `panelWidth` | verified |  |
| 469 | `onMeasure` | verified |  |
| 503 | `onLayout` | verified |  |
| 522 | `rebuildPaths` | unmatched |  |
| 546 | `applySlant` | unmatched |  |
| 574 | `onDraw` | verified |  |
| 584 | `onTouchEvent` | unmatched |  |
| 595 | `ButtonCell` | unmatched |  |
| 618 | `onDraw` | verified |  |
| 632 | `dispatchSetPressed` | unmatched |  |
| 638 | `setPressed` | unmatched |  |
| 652 | `PressHighlight` | unmatched |  |
| 654 | `isStateful` | unmatched |  |
| 656 | `onStateChange` | unmatched |  |
| 665 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 682 | `setAlpha` | unmatched |  |
| 684 | `setColorFilter` | unmatched |  |
| 685 | `getOpacity` | unmatched |  |
| 693 | `ItemRow` | unmatched |  |
| 695 | `setShift` | unmatched |  |
| 708 | `onDraw` | verified |  |
| 723 | `SlantBox` | unmatched |  |
| 728 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 744 | `setAlpha` | unmatched |  |
| 746 | `setColorFilter` | unmatched |  |
| 747 | `getOpacity` | unmatched |  |

## `com\wuying\phigros\ui\GameTextOverlay.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `GameTextOverlay` | unmatched |  |
| 28 | `configure` | unmatched |  |
| 37 | `setColors` | unmatched |  |
| 42 | `captureExternalColor` | unmatched |  |
| 45 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 79 | `setAlpha` | unmatched |  |
| 80 | `setColorFilter` | unmatched |  |
| 81 | `getOpacity` | unmatched |  |
| 82 | `isStateful` | unmatched |  |
| 83 | `onStateChange` | unmatched |  |

## `com\wuying\phigros\ui\GameTextRenderer.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 26 | `invalidate` | matched-by-name | `include\rechigros\app\ChartLibrary.hpp:70` `invalidate` (+1) |
| 28 | `prepare` | verified | `src\app\TriGridBackdrop.cpp:217` `prepare` (+8) |
| 81 | `fits` | fixed |  |
| 95 | `build` | fixed | `src\render\fx\FullscreenPass.cpp:70` `build` (+1) |
| 106 | `height` | verified | `include\rechigros\app\UiLayout.hpp:44` `height` (+5) |
| 108 | `baseline` | fixed |  |
| 109 | `width` | fixed | `include\rechigros\app\UiLayout.hpp:43` `width` (+5) |
| 110 | `textSize` | verified |  |
| 111 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |

## `com\wuying\phigros\ui\GameTextView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `GameTextView` | unmatched |  |
| 29 | `GameTextView` | unmatched |  |
| 30 | `GameTextView` | unmatched |  |
| 48 | `setDesignTextSize` | unmatched |  |
| 56 | `setFitWidth` | unmatched |  |
| 60 | `setTextSize` | unmatched |  |
| 61 | `setTextSize` | unmatched |  |
| 67 | `getTextSize` | unmatched |  |
| 70 | `requestLayout` | unmatched |  |
| 74 | `onTextChanged` | unmatched |  |
| 82 | `hasFixedSize` | unmatched |  |
| 86 | `onConfigurationChanged` | unmatched |  |
| 90 | `displayText` | verified |  |
| 99 | `alignment` | verified |  |
| 106 | `prepare` | verified | `src\app\TriGridBackdrop.cpp:217` `prepare` (+8) |
| 115 | `onMeasure` | verified |  |
| 136 | `textTop` | verified |  |
| 142 | `getBaseline` | verified |  |
| 145 | `onDraw` | verified |  |
| 175 | `animationsEnabled` | unmatched |  |
| 180 | `drawCompoundDrawables` | unmatched |  |
| 192 | `setSelected` | unmatched |  |

## `com\wuying\phigros\ui\JudgementHistogramView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 41 | `JudgementHistogramView` | unmatched |  |
| 46 | `JudgementHistogramView` | unmatched |  |
| 51 | `init` | fixed |  |
| 57 | `setHistogram` | unmatched |  |
| 68 | `setTitle` | unmatched |  |
| 73 | `setContentInsets` | unmatched |  |
| 83 | `setHistogramTypeface` | unmatched |  |
| 88 | `setSourceBitmap` | unmatched |  |
| 94 | `onSizeChanged` | unmatched |  |
| 105 | `onDetachedFromWindow` | unmatched |  |
| 111 | `onDraw` | verified |  |
| 127 | `drawBlurredBackdrop` | matched-by-name | `src\app\ResultScreen.cpp:322` `drawBlurredBackdrop` |
| 142 | `drawPanel` | matched-by-name | `src\app\CalibrationScreen.cpp:511` `drawPanel` (+1) |
| 173 | `drawBars` | unmatched |  |
| 213 | `drawPeakTag` | unmatched |  |
| 257 | `drawAxisLabels` | unmatched |  |
| 272 | `effectiveMaxBarHeight` | unmatched |  |
| 277 | `ensureBlurredBitmap` | unmatched |  |
| 302 | `recycleBlurredBitmap` | unmatched |  |
| 311 | `boxBlur` | unmatched |  |
| 371 | `formatSignedMs` | unmatched |  |
| 376 | `formatAxisMs` | unmatched |  |
| 381 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 385 | `withAlpha` | unmatched |  |
| 389 | `clamp` | fixed |  |
| 395 | `pack` | fixed |  |

## `com\wuying\phigros\ui\LocalChartAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 33 | `LocalChartAdapter` | unmatched |  |
| 37 | `setItems` | verified |  |
| 47 | `setManageMode` | verified |  |
| 54 | `isManageMode` | unmatched |  |
| 58 | `selectedIds` | unmatched |  |
| 63 | `onCreateViewHolder` | unmatched |  |
| 70 | `onBindViewHolder` | unmatched |  |
| 97 | `getItemCount` | unmatched |  |
| 109 | `Vh` | verified |  |

## `com\wuying\phigros\ui\OnlineChartAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `Item` | fixed |  |
| 43 | `OnlineChartAdapter` | unmatched |  |
| 47 | `setItems` | verified |  |
| 53 | `addItems` | unmatched |  |
| 59 | `setQuery` | unmatched |  |
| 64 | `hasQuery` | unmatched |  |
| 68 | `visibleCount` | unmatched |  |
| 72 | `totalCount` | unmatched |  |
| 76 | `notifyItemUpdated` | unmatched |  |
| 85 | `onCreateViewHolder` | unmatched |  |
| 92 | `onBindViewHolder` | unmatched |  |
| 103 | `getItemCount` | unmatched |  |
| 108 | `filter` | fixed | `src\render\gl\Texture.cpp:69` `setFilter` |
| 122 | `buildMeta` | unmatched |  |
| 139 | `Vh` | verified |  |

## `com\wuying\phigros\ui\PlayActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 176 | `extraBool` | unmatched |  |
| 191 | `onCreate` | unmatched |  |
| 256 | `OnBackPressedCallback` | verified |  |
| 258 | `handleOnBackPressed` | unmatched |  |
| 280 | `setLoadingUi` | unmatched |  |
| 334 | `loadAndStart` | unmatched |  |
| 469 | `forceGc` | unmatched |  |
| 480 | `hintSurfaceFrameRateIfPossible` | unmatched |  |
| 501 | `Callback` | verified |  |
| 503 | `surfaceCreated` | unmatched |  |
| 513 | `surfaceChanged` | unmatched |  |
| 514 | `surfaceDestroyed` | unmatched |  |
| 518 | `buildOverlayUiOn` | unmatched |  |
| 591 | `startUiTicker` | unmatched |  |
| 594 | `Runnable` | unmatched |  |
| 595 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 625 | `stopUiTicker` | unmatched |  |
| 632 | `applyOverlayStageLayout` | unmatched |  |
| 687 | `officialHudUnitPx` | unmatched |  |
| 691 | `officialHudHalfUiW` | unmatched |  |
| 696 | `officialHudX` | unmatched |  |
| 700 | `officialHudY` | unmatched |  |
| 704 | `officialHudSize` | unmatched |  |
| 708 | `officialHudBottomTextYOffsetPx` | unmatched |  |
| 712 | `onBackPressed` | verified |  |
| 725 | `tryLoadSfxRawWav` | unmatched |  |
| 736 | `tryLoadSfxFromSkinDir` | unmatched |  |
| 744 | `tryLoadSfxFromSkinFile` | unmatched |  |
| 763 | `onPause` | verified |  |
| 775 | `onWindowFocusChanged` | verified |  |
| 788 | `onResume` | verified |  |
| 798 | `onDestroy` | unmatched |  |
| 816 | `onExit` | unmatched |  |
| 830 | `onFinished` | unmatched |  |
| 940 | `onStageRectChanged` | unmatched |  |
| 949 | `onResourcesReady` | unmatched |  |
| 975 | `applyImmersiveMode` | unmatched |  |
| 996 | `applyHighRefreshRate` | unmatched |  |

## `com\wuying\phigros\ui\ReplayManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 49 | `ReplayInfo` | unmatched |  |
| 59 | `ReplayManager` | unmatched |  |
| 61 | `getPrefs` | unmatched |  |
| 67 | `isReplayEnabled` | unmatched |  |
| 71 | `setReplayEnabled` | unmatched |  |
| 75 | `startRecording` | unmatched |  |
| 81 | `finishRecording` | unmatched |  |
| 100 | `clearCachedReplay` | unmatched |  |
| 105 | `getCachedReplayFile` | unmatched |  |
| 109 | `getReplaysRoot` | unmatched |  |
| 115 | `getReplayDir` | unmatched |  |
| 119 | `getChartFile` | unmatched |  |
| 131 | `getMusicFile` | unmatched |  |
| 143 | `getBgFile` | unmatched |  |
| 155 | `isReplayAlreadySaved` | unmatched |  |
| 171 | `getReplayData` | unmatched |  |
| 181 | `getReplayInfo` | unmatched |  |
| 292 | `getReplayList` | unmatched |  |
| 328 | `setFavorite` | unmatched |  |
| 338 | `deleteReplay` | unmatched |  |
| 497 | `isLikelyReplayPackage` | unmatched |  |
| 595 | `deleteRecursive` | unmatched |  |
| 626 | `isInsideDir` | unmatched |  |
| 636 | `relativizePath` | unmatched |  |
| 647 | `findFile` | unmatched |  |
| 661 | `findChartFileInDir` | unmatched |  |
| 677 | `findAudioFileInDir` | unmatched |  |
| 694 | `findImageFileInDir` | unmatched |  |

## `com\wuying\phigros\ui\ResultActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 81 | `iconIndex` | unmatched |  |
| 92 | `onCreate` | unmatched |  |
| 304 | `stylePanels` | unmatched |  |
| 323 | `illustrationScrim` | unmatched |  |
| 327 | `ShaderFactory` | unmatched |  |
| 328 | `resize` | excluded | `src\render\GameRenderer.cpp:1096` `resize` (+2) |
| 337 | `styleCornerButton` | unmatched |  |
| 350 | `styleChipButton` | unmatched |  |
| 358 | `bindPressFeedback` | unmatched |  |
| 373 | `setupModeTags` | unmatched |  |
| 405 | `formatMultiplier` | matched-by-name | `src\app\ResultScreen.cpp:285` `formatMultiplier` |
| 409 | `addModeTag` | unmatched |  |
| 436 | `ModeTagFlow` | unmatched |  |
| 441 | `onMeasure` | verified |  |
| 461 | `onLayout` | verified |  |
| 483 | `layoutRow` | unmatched |  |
| 494 | `setupResponsiveCanvas` | unmatched |  |
| 551 | `saveReplay` | matched-by-name | `src\app\ResultScreen.cpp:356` `saveReplay` |
| 588 | `applyPhigrosFont` | unmatched |  |
| 593 | `setupJudgementHistogramOverlay` | unmatched |  |
| 631 | `toggleJudgementHistogram` | unmatched |  |
| 639 | `showJudgementHistogram` | unmatched |  |
| 651 | `hideJudgementHistogram` | unmatched |  |
| 662 | `loadGradeImage` | unmatched |  |
| 670 | `loadButtonImage` | unmatched |  |
| 676 | `loadImageFromAssets` | unmatched |  |
| 691 | `setupFullscreen` | unmatched |  |
| 718 | `blurBackground` | unmatched |  |
| 748 | `gaussianBlur` | unmatched |  |
| 808 | `clamp` | fixed |  |
| 810 | `pack` | fixed |  |
| 811 | `allEase` | matched-by-name | `src\app\ResultScreen.cpp:63` `allEase` |
| 820 | `delayedSquare` | matched-by-name | `src\app\ResultScreen.cpp:74` `delayedSquare` |
| 827 | `levelSizeEase` | matched-by-name | `src\app\ResultScreen.cpp:81` `levelSizeEase` |
| 832 | `buttonEase` | matched-by-name | `src\app\ResultScreen.cpp:86` `buttonEase` |
| 845 | `AlphaTarget` | unmatched |  |
| 846 | `apply` | fixed | `src\core\Easing.cpp:248` `apply` (+2) |
| 848 | `childAlphaTargets` | unmatched |  |
| 860 | `startEnterAnimation` | unmatched |  |
| 922 | `onDestroy` | unmatched |  |
| 928 | `onWindowFocusChanged` | verified |  |
| 934 | `onBackPressed` | verified |  |

## `com\wuying\phigros\ui\SelectAudioFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 37 | `onCreate` | unmatched |  |
| 50 | `onCreateView` | unmatched |  |
| 56 | `onViewCreated` | unmatched |  |
| 83 | `SimpleWatcher` | unmatched |  |
| 84 | `afterTextChanged` | unmatched |  |
| 111 | `SimpleWatcher` | unmatched |  |
| 112 | `afterTextChanged` | unmatched |  |
| 138 | `SimpleWatcher` | unmatched |  |
| 139 | `afterTextChanged` | unmatched |  |
| 167 | `SimpleWatcher` | unmatched |  |
| 168 | `afterTextChanged` | unmatched |  |
| 190 | `onResume` | verified |  |
| 196 | `refreshFromActivity` | unmatched |  |
| 214 | `parseIntSafe` | unmatched |  |
| 224 | `parseFloatSafe` | unmatched |  |
| 234 | `clamp` | fixed |  |
| 238 | `clampInt` | unmatched |  |
| 242 | `formatFloat` | unmatched |  |
| 246 | `host` | unmatched |  |
| 252 | `beforeTextChanged` | unmatched |  |
| 253 | `onTextChanged` | unmatched |  |

## `com\wuying\phigros\ui\SelectChartFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `onCreateView` | unmatched |  |
| 40 | `onViewCreated` | unmatched |  |
| 64 | `SimpleWatcher` | unmatched |  |
| 65 | `afterTextChanged` | unmatched |  |
| 73 | `SimpleWatcher` | unmatched |  |
| 74 | `afterTextChanged` | unmatched |  |
| 82 | `SimpleWatcher` | unmatched |  |
| 83 | `afterTextChanged` | unmatched |  |
| 100 | `onResume` | verified |  |
| 106 | `refreshFromActivity` | unmatched |  |
| 153 | `host` | unmatched |  |
| 157 | `appendLoadedItem` | unmatched |  |
| 164 | `beforeTextChanged` | unmatched |  |
| 165 | `onTextChanged` | unmatched |  |

## `com\wuying\phigros\ui\SelectLocalChartsFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 57 | `onCreateView` | unmatched |  |
| 63 | `onViewCreated` | unmatched |  |
| 85 | `Listener` | verified |  |
| 87 | `onOpen` | verified |  |
| 91 | `onSelectionChanged` | unmatched |  |
| 100 | `OnScrollListener` | unmatched |  |
| 101 | `onScrolled` | unmatched |  |
| 126 | `onResume` | verified |  |
| 132 | `onDestroy` | unmatched |  |
| 138 | `refreshFromActivity` | unmatched |  |
| 163 | `openRecord` | unmatched |  |
| 177 | `handleBackPressed` | unmatched |  |
| 189 | `showRecordDetail` | unmatched |  |
| 198 | `populateDetail` | unmatched |  |
| 221 | `showList` | unmatched |  |
| 225 | `showList` | unmatched |  |
| 234 | `setManageMode` | verified |  |
| 240 | `updateManageActions` | unmatched |  |
| 257 | `updateStateText` | unmatched |  |
| 269 | `setHeaderCollapsed` | unmatched |  |
| 292 | `confirmDeleteSelected` | unmatched |  |
| 309 | `exportSelected` | unmatched |  |
| 329 | `attachTextWatchers` | unmatched |  |
| 332 | `SimpleWatcher` | unmatched |  |
| 333 | `afterTextChanged` | unmatched |  |
| 341 | `SimpleWatcher` | unmatched |  |
| 342 | `afterTextChanged` | unmatched |  |
| 349 | `SimpleWatcher` | unmatched |  |
| 350 | `afterTextChanged` | unmatched |  |
| 363 | `setTextIfDifferent` | unmatched |  |
| 370 | `runOnUiThread` | unmatched |  |
| 377 | `host` | unmatched |  |
| 383 | `beforeTextChanged` | unmatched |  |
| 384 | `onTextChanged` | unmatched |  |

## `com\wuying\phigros\ui\SelectOnlineChartsFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 70 | `onCreateView` | unmatched |  |
| 76 | `onViewCreated` | unmatched |  |
| 85 | `Listener` | verified |  |
| 87 | `onDownload` | unmatched |  |
| 91 | `onItemVisible` | unmatched |  |
| 100 | `OnScrollListener` | unmatched |  |
| 101 | `onScrolled` | unmatched |  |
| 119 | `TextWatcher` | unmatched |  |
| 120 | `beforeTextChanged` | unmatched |  |
| 121 | `onTextChanged` | unmatched |  |
| 122 | `afterTextChanged` | unmatched |  |
| 131 | `onResume` | verified |  |
| 139 | `onDestroy` | unmatched |  |
| 145 | `reloadOnline` | unmatched |  |
| 158 | `resetOnlineState` | unmatched |  |
| 167 | `fetchOnlineIndex` | unmatched |  |
| 205 | `applyOnlineIndex` | unmatched |  |
| 215 | `loadNextPage` | unmatched |  |
| 238 | `fetchVisibleItemCover` | unmatched |  |
| 253 | `confirmDownload` | unmatched |  |
| 262 | `downloadChart` | unmatched |  |
| 307 | `ensureCoverCached` | unmatched |  |
| 325 | `maybeLoadMoreForSearch` | unmatched |  |
| 332 | `updateProgress` | matched-by-name | `src\app\UiLayout.cpp:451` `progress` |
| 338 | `setStateMessage` | unmatched |  |
| 344 | `updateStateCount` | unmatched |  |
| 350 | `setHeaderCollapsed` | unmatched |  |
| 381 | `runOnUiThread` | unmatched |  |
| 388 | `extensionOrJpg` | unmatched |  |
| 401 | `safeFileName` | unmatched |  |
| 407 | `errorMessage` | unmatched |  |
| 438 | `host` | unmatched |  |

## `com\wuying\phigros\ui\SelectPagerAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 8 | `SelectPagerAdapter` | unmatched |  |
| 12 | `createFragment` | unmatched |  |
| 22 | `getItemCount` | unmatched |  |

## `com\wuying\phigros\ui\SelectReplayFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 60 | `onCreateView` | unmatched |  |
| 66 | `onViewCreated` | unmatched |  |
| 103 | `onResume` | verified |  |
| 109 | `enterSelectionMode` | unmatched |  |
| 113 | `enterSelectionMode` | unmatched |  |
| 123 | `exitSelectionMode` | unmatched |  |
| 130 | `handleExportClick` | unmatched |  |
| 146 | `handleDeleteClick` | unmatched |  |
| 166 | `refreshFromActivity` | unmatched |  |
| 173 | `refreshList` | unmatched |  |
| 196 | `updateActionButtons` | unmatched |  |
| 219 | `buildReplayItem` | unmatched |  |
| 343 | `toggleReplaySelection` | unmatched |  |
| 352 | `loadAssetBitmap` | unmatched |  |
| 360 | `playReplay` | matched-by-name | `src\app\ResultScreen.cpp:378` `playReplay` |
| 434 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 438 | `host` | unmatched |  |

## `com\wuying\phigros\ui\SelectVideoFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 71 | `onCreateView` | unmatched |  |
| 77 | `onViewCreated` | unmatched |  |
| 146 | `SimpleWatcher` | unmatched |  |
| 147 | `afterTextChanged` | unmatched |  |
| 171 | `SimpleWatcher` | unmatched |  |
| 172 | `afterTextChanged` | unmatched |  |
| 198 | `SimpleWatcher` | unmatched |  |
| 199 | `afterTextChanged` | unmatched |  |
| 251 | `onResume` | verified |  |
| 257 | `refreshSkinDropdown` | unmatched |  |
| 292 | `refreshFromActivity` | unmatched |  |
| 350 | `bindHitOffsetIndicatorModeDropdown` | unmatched |  |
| 363 | `syncAutoplayRevealSwitches` | unmatched |  |
| 370 | `applyPresetSelection` | unmatched |  |
| 392 | `updatePresetLabelForValue` | unmatched |  |
| 402 | `buildPresetLabels` | unmatched |  |
| 411 | `buildHitOffsetModeLabels` | unmatched |  |
| 419 | `getHitOffsetModeLabel` | unmatched |  |
| 425 | `getPresetLabel` | unmatched |  |
| 431 | `findPresetIndex` | unmatched |  |
| 438 | `getScreenAspect` | unmatched |  |
| 446 | `parseFloatSafe` | unmatched |  |
| 456 | `clamp` | fixed |  |
| 460 | `formatFloat` | unmatched |  |
| 464 | `host` | unmatched |  |
| 470 | `beforeTextChanged` | unmatched |  |
| 471 | `onTextChanged` | unmatched |  |
| 476 | `Filter` | fixed | `src\render\gl\Texture.cpp:69` `setFilter` |
| 477 | `performFiltering` | unmatched |  |
| 484 | `publishResults` | unmatched |  |
| 493 | `NoFilterStringAdapter` | unmatched |  |
| 498 | `getFilter` | matched-by-name | `src\render\gl\Texture.cpp:69` `setFilter` |

## `com\wuying\phigros\ui\SettingsPreviewView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 15 | `SettingsPreviewView` | unmatched |  |
| 20 | `update` | fixed | `src\chart\Md5.cpp:84` `update` (+6) |
| 27 | `onDraw` | verified |  |

## `com\wuying\phigros\ui\SettingsSliderView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 28 | `SettingsSliderView` | unmatched |  |
| 39 | `setOnValueChanged` | unmatched |  |
| 41 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 43 | `scale` | fixed |  |
| 44 | `capWidth` | unmatched |  |
| 45 | `thumbWidth` | unmatched |  |
| 46 | `endInset` | unmatched |  |
| 47 | `thumbGap` | unmatched |  |
| 49 | `trackLeft` | unmatched |  |
| 53 | `trackRight` | unmatched |  |
| 54 | `left` | fixed | `include\rechigros\render\math\Mat4.hpp:85` `left` |
| 58 | `right` | fixed | `include\rechigros\render\math\Mat4.hpp:88` `right` |
| 62 | `parallelogram` | unmatched |  |
| 74 | `onDraw` | verified |  |
| 96 | `updateValue` | matched-by-name | `src\chart\OfficialChartParser.cpp:20` `value` |
| 106 | `updateFromX` | unmatched |  |
| 111 | `onTouchEvent` | unmatched |  |
| 158 | `performClick` | unmatched |  |
| 160 | `onKeyDown` | unmatched |  |
| 172 | `onInitializeAccessibilityNodeInfo` | unmatched |  |
| 182 | `performAccessibilityAction` | unmatched |  |

## `com\wuying\phigros\ui\SettingsToggleView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `SettingsToggleView` | unmatched |  |
| 31 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 33 | `block` | fixed |  |
| 45 | `onDraw` | verified |  |
| 79 | `setChecked` | unmatched |  |
| 105 | `onDetachedFromWindow` | unmatched |  |
| 112 | `onInitializeAccessibilityNodeInfo` | unmatched |  |

## `com\wuying\phigros\ui\SkewFrameLayout.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 42 | `SkewFrameLayout` | unmatched |  |
| 47 | `SkewFrameLayout` | unmatched |  |
| 52 | `SkewFrameLayout` | unmatched |  |
| 57 | `init` | fixed |  |
| 70 | `setPanelColors` | unmatched |  |
| 76 | `setSkewDegrees` | unmatched |  |
| 82 | `setStrokeWidthDp` | unmatched |  |
| 89 | `setClipContentToSkew` | unmatched |  |
| 94 | `setStraightLeftEdge` | unmatched |  |
| 100 | `setStraightRightEdge` | unmatched |  |
| 106 | `setTopWider` | unmatched |  |
| 112 | `setAccentEdge` | unmatched |  |
| 120 | `onSizeChanged` | unmatched |  |
| 126 | `rebuildPath` | unmatched |  |
| 144 | `onDraw` | verified |  |
| 151 | `dispatchDraw` | unmatched |  |
| 181 | `setAntialiasedClip` | unmatched |  |
| 192 | `drawStroke` | unmatched |  |
| 198 | `drawAccent` | matched-by-name | `src\app\UiLayout.cpp:43` `drawAccent` |
| 234 | `dispatchTouchEvent` | unmatched |  |
| 243 | `containsPoint` | unmatched |  |
| 254 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |

## `com\wuying\phigros\ui\SkinManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 37 | `SkinEntry` | unmatched |  |
| 42 | `isBuiltin` | unmatched |  |
| 47 | `SkinManager` | verified |  |
| 49 | `getPrefs` | unmatched |  |
| 53 | `getSkinsRoot` | unmatched |  |
| 57 | `getSkinDir` | unmatched |  |
| 62 | `getSkinList` | unmatched |  |
| 82 | `getSelectedSkinIndex` | unmatched |  |
| 86 | `setSelectedSkinIndex` | unmatched |  |
| 137 | `isLikelySkinPackage` | unmatched |  |
| 173 | `deleteSkin` | unmatched |  |
| 197 | `loadSkinConfig` | verified | `src\render\skin\SkinConfig.cpp:159` `loadSkinConfig` |
| 215 | `getSkinConfig` | unmatched |  |
| 222 | `parseYmlConfig` | unmatched |  |
| 235 | `parseYmlContent` | verified |  |
| 272 | `stripYmlQuotes` | unmatched |  |
| 279 | `parseIntArray` | verified | `src\render\skin\SkinConfig.cpp:79` `parseIntArray` |
| 289 | `parseBool` | verified | `src\app\Settings.cpp:41` `parseBool` |
| 293 | `isSkinAssetName` | unmatched |  |
| 323 | `containsSkinConfigKeys` | unmatched |  |
| 338 | `deleteRecursive` | unmatched |  |

## `com\wuying\phigros\ui\TriGridBackgroundView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 23 | `TriGridBackgroundView` | unmatched |  |
| 27 | `TriGridBackgroundView` | unmatched |  |
| 32 | `init` | fixed |  |
| 45 | `Runnable` | unmatched |  |
| 46 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 52 | `updateRendering` | unmatched |  |
| 65 | `onAttachedToWindow` | unmatched |  |
| 69 | `onDetachedFromWindow` | unmatched |  |
| 72 | `onWindowVisibilityChanged` | unmatched |  |
| 75 | `onVisibilityChanged` | unmatched |  |
| 78 | `onPause` | verified |  |
| 79 | `onResume` | verified |  |
| 80 | `setBackgroundBitmap` | unmatched |  |
| 206 | `setPendingBackground` | unmatched |  |
| 213 | `TriGridRenderer` | unmatched |  |
| 221 | `onSurfaceCreated` | unmatched |  |
| 246 | `onSurfaceChanged` | unmatched |  |
| 253 | `onDrawFrame` | verified |  |
| 286 | `drawQuad` | matched-by-name | `src\app\TriGridBackdrop.cpp:281` `drawQuad` |
| 298 | `swapPendingBackground` | unmatched |  |
| 326 | `loadTexture` | matched-by-name | `src\app\TriGridBackdrop.cpp:172` `loadTexture` |
| 355 | `isPowerOfTwo` | matched-by-name | `src\app\TriGridBackdrop.cpp:114` `isPowerOfTwo` |
| 359 | `createProgram` | unmatched |  |
| 383 | `compileShader` | unmatched |  |

## `com\wuying\phigros\ui\UiTypography.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `UiTypography` | unmatched |  |
| 24 | `px` | fixed | `src\app\LibraryScreen.cpp:76` `px` |
| 28 | `preloadAsync` | unmatched |  |
| 45 | `gameTypeface` | unmatched |  |
| 68 | `loadGameTypeface` | unmatched |  |
| 76 | `applyTypeface` | unmatched |  |
| 90 | `useIndependentRenderer` | unmatched |  |
| 104 | `bindSingleLine` | unmatched |  |
| 140 | `TextWatcher` | unmatched |  |
| 141 | `beforeTextChanged` | unmatched |  |
| 142 | `onTextChanged` | unmatched |  |
| 143 | `afterTextChanged` | unmatched |  |
| 147 | `bindAllSingleLine` | unmatched |  |
| 167 | `fitSingleLine` | unmatched |  |
| 172 | `fitSingleLine` | unmatched |  |

## `com\wuying\phigros\ui\UiWarmup.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 36 | `UiWarmup` | unmatched |  |
| 38 | `start` | fixed |  |

## `com\wuying\phigros\util\ChartLibraryManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 48 | `ChartLibraryManager` | unmatched |  |
| 50 | `invalidateCache` | unmatched |  |
| 74 | `ChartRecord` | verified |  |
| 109 | `ImportResult` | unmatched |  |
| 115 | `getChartsRoot` | verified |  |
| 125 | `getLocalCharts` | unmatched |  |
| 145 | `scanLocalCharts` | unmatched |  |
| 163 | `loadRecord` | verified | `src\app\ChartLibrary.cpp:476` `loadRecord` |
| 350 | `deleteCharts` | verified |  |
| 392 | `recordBestResult` | matched-by-name | `src\app\ChartLibrary.cpp:705` `recordBestResult` |
| 413 | `recordPlayTime` | matched-by-name | `src\app\ChartLibrary.cpp:716` `recordPlayTime` |
| 421 | `getBestScore` | unmatched |  |
| 426 | `getBestAcc` | unmatched |  |
| 432 | `getLastPlayedAt` | unmatched |  |
| 437 | `findLocalChartIdForPath` | unmatched |  |
| 455 | `isComplete` | verified | `src\app\ChartLibrary.cpp:397` `complete` |
| 461 | `hasRequiredFiles` | unmatched |  |
| 468 | `mapToDest` | verified |  |
| 475 | `findInfoFile` | verified | `src\app\ChartLibrary.cpp:140` `findInfoFile` |
| 493 | `resolveExisting` | verified |  |
| 503 | `findChartFile` | matched-by-name | `src\app\ChartLibrary.cpp:156` `findChartFile` |
| 512 | `findAudioFile` | matched-by-name | `src\app\ChartLibrary.cpp:164` `findAudioFile` |
| 523 | `findByInfoName` | verified | `src\app\ChartLibrary.cpp:180` `findByInfoName` |
| 539 | `isChartCandidate` | verified |  |
| 559 | `isAudioCandidate` | verified |  |
| 565 | `isImageCandidate` | verified |  |
| 571 | `readHead` | verified | `src\app\ChartLibrary.cpp:20` `readHead` (+1) |
| 583 | `findImageFile` | matched-by-name | `src\app\ChartLibrary.cpp:172` `findImageFile` |
| 594 | `chooseShortestPath` | verified | `src\app\ChartLibrary.cpp:60` `chooseShortestPath` |
| 605 | `compareLocalCharts` | unmatched |  |
| 623 | `getRecordModifiedAt` | verified | `src\app\ChartLibrary.cpp:204` `recordModifiedAt` |
| 636 | `lastModified` | matched-by-name | `src\app\ChartLibrary.cpp:197` `lastModified` |
| 640 | `collectFiles` | unmatched |  |
| 706 | `deleteRecursive` | unmatched |  |
| 743 | `relativize` | verified | `src\app\ChartLibrary.cpp:29` `relativize` |
| 758 | `getScorePrefs` | unmatched |  |
| 763 | `getAccPrefs` | unmatched |  |
| 768 | `getPlayHistoryPrefs` | unmatched |  |
| 773 | `safeFileName` | unmatched |  |

## `com\wuying\phigros\util\FileUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `FileUtils` | verified |  |
| 20 | `queryDisplayName` | unmatched |  |
| 38 | `stripExtension` | verified | `src\chart\ChartLoader.cpp:379` `stripExtension` |

## `com\wuying\phigros\util\InfoFileUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 25 | `InfoFileUtils` | unmatched |  |
| 69 | `readInfoFile` | verified |  |
| 91 | `readInfoFromChartFile` | verified |  |
| 119 | `buildDifficultyLabel` | verified |  |
| 139 | `normalizeDifficultyText` | unmatched |  |
| 148 | `inferDifficultyPrefix` | unmatched |  |
| 292 | `fromKv` | verified |  |
| 341 | `getObj` | unmatched |  |
| 351 | `getStr` | verified |  |
| 368 | `safe` | verified |  |
| 373 | `parseIntOrNull` | unmatched |  |
| 383 | `getInt` | verified | `src\app\Settings.cpp:31` `parseInt` |
| 398 | `getBool` | verified | `src\app\Settings.cpp:41` `parseBool` |
| 418 | `firstNonEmpty` | matched-by-name | `src\app\InfoFile.cpp:46` `firstNonEmpty` |
| 427 | `stripQuotes` | verified | `src\app\InfoFile.cpp:26` `stripQuotes` (+2) |
| 440 | `trimDifficultyWhitespace` | fixed | `src\app\InfoFile.cpp:167` `trimDifficultyWhitespace` |
| 449 | `isDifficultyEdgeWhitespace` | matched-by-name | `src\app\InfoFile.cpp:130` `isDifficultyEdgeWhitespace` |
| 453 | `collapseNormalSpaceRuns` | verified | `src\app\InfoFile.cpp:111` `collapseNormalSpaceRuns` |
| 475 | `parseCsvLine` | verified | `src\app\InfoFile.cpp:231` `parseCsvLine` (+1) |

## `com\wuying\phigros\util\InfoJsonUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 13 | `InfoJsonUtils` | verified |  |
| 23 | `tryReadInfoJson` | verified |  |
| 49 | `firstString` | unmatched |  |

## `com\wuying\phigros\util\LanzouClient.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 57 | `pageUrl` | unmatched |  |
| 64 | `titleOrFileName` | unmatched |  |
| 69 | `levelOrFallback` | unmatched |  |
| 80 | `FolderSession` | unmatched |  |
| 93 | `Page` | verified |  |
| 103 | `ResolvedDownload` | unmatched |  |
| 109 | `LanzouClient` | unmatched |  |
| 368 | `rememberCookies` | unmatched |  |
| 379 | `addCookies` | unmatched |  |
| 397 | `cookieHeader` | unmatched |  |
| 426 | `parseJsVars` | unmatched |  |
| 437 | `resolveJsValue` | unmatched |  |
| 450 | `extractDescription` | unmatched |  |
| 471 | `extractDownloadPageUrl` | unmatched |  |
| 485 | `resolveDirectFromTp` | unmatched |  |
| 494 | `htmlBlockToText` | unmatched |  |
| 501 | `stripTags` | unmatched |  |
| 507 | `htmlDecode` | unmatched |  |
| 517 | `solveAcwScV2` | unmatched |  |
| 554 | `firstGroup` | unmatched |  |
| 600 | `valueOrEmpty` | unmatched |  |
| 605 | `valueOrDefault` | unmatched |  |

## `com\wuying\phigros\util\NativeAudioDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 10 | `NativeAudioDecoder` | unmatched |  |
| 22 | `decodeFile` | matched-by-name | `src\audio\Decoder.cpp:62` `decodeFile` |

## `com\wuying\phigros\util\OnlineIndexClient.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 32 | `OnlineIndexClient` | unmatched |  |
| 63 | `loadCachedOrBundled` | unmatched |  |
| 169 | `saveCache` | unmatched |  |
| 179 | `readCache` | unmatched |  |
| 190 | `readBundledFallback` | unmatched |  |
| 211 | `formatBytes` | unmatched |  |
| 224 | `numericChartId` | unmatched |  |
| 232 | `firstNonEmpty` | matched-by-name | `src\app\InfoFile.cpp:46` `firstNonEmpty` |

## `com\wuying\phigros\util\PcmDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 15 | `PcmDecoder` | unmatched |  |
| 23 | `DecodedAudio` | unmatched |  |
| 30 | `getNumFrames` | unmatched |  |
| 175 | `selectAudioTrack` | unmatched |  |
| 194 | `FloatArrayBuilder` | unmatched |  |
| 199 | `appendPcm` | unmatched |  |
| 222 | `ensureCapacity` | unmatched |  |
| 235 | `size` | fixed | `include\rechigros\game\BpmTimeline.hpp:8` `size` (+5) |
| 237 | `buffer` | excluded |  |

## `com\wuying\phigros\util\WavDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `WavDecoder` | unmatched |  |
| 204 | `u16` | fixed | `src\platform\Archive.cpp:30` `readU16` |

## `com\wuying\phigros\util\ZipPackUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 36 | `ZipPackUtils` | unmatched |  |
| 312 | `normalizeZipEntryName` | verified |  |
| 439 | `ZipEntryInfo` | unmatched |  |
| 447 | `isDirectory` | unmatched |  |
| 464 | `BoundedRafInputStream` | unmatched |  |
| 468 | `BoundedRafInputStream` | unmatched |  |
| 505 | `close` | verified |  |
| 510 | `readDummyByte` | unmatched |  |
| 517 | `deleteRecursive` | unmatched |  |
| 530 | `isInfoFile` | verified |  |
| 538 | `resolveInfoPath` | unmatched |  |
| 548 | `isAudioFile` | verified |  |
| 557 | `isImageFile` | verified |  |
| 564 | `readHead` | verified | `src\app\ChartLibrary.cpp:20` `readHead` (+1) |
| 575 | `looksLikeOfficialChartJson` | unmatched |  |
| 581 | `looksLikeRpeChartJson` | unmatched |  |
| 587 | `looksLikeAnyChartJson` | unmatched |  |
| 598 | `looksLikePhiEditText` | unmatched |  |
| 607 | `isLikelyChart` | verified |  |
| 615 | `findChartFileInExtracted` | unmatched |  |
| 635 | `findAudioFileInExtracted` | unmatched |  |
| 649 | `findImageFileInExtracted` | unmatched |  |
| 663 | `findByInfoNameInExtracted` | verified |  |
| 689 | `readInfoFileWithEncodingFallback` | unmatched |  |
| 736 | `readInfoFileWithCharset` | unmatched |  |
| 757 | `readJsonWithCharset` | unmatched |  |
| 780 | `readYamlLikeWithCharset` | unmatched |  |
| 801 | `readKeyValueTxtWithCharset` | unmatched |  |
| 827 | `readCsvWithCharset` | unmatched |  |
| 852 | `infoFromKvMap` | unmatched |  |
| 870 | `getFirstJsonStr` | unmatched |  |
| 886 | `firstNonEmptyStr` | unmatched |  |
| 895 | `readAllWithCharset` | unmatched |  |
| 912 | `stripYamlVal` | unmatched |  |
| 925 | `parseCsvLineSimple` | unmatched |  |
