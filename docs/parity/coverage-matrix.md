# Per-function coverage matrix

Generated from `docs/compare/inventory.json` by `tools/parity/coverage_matrix.py`. One row per reference declaration.

Reference declarations: **2124** across 75 files.

| Status | Count | Meaning |
| --- | --- | --- |
| verified | 1525 | named MATCH by a parity report |
| fixed | 196 | named by a parity report with a fix |
| differs | 0 | named with a remaining difference |
| excluded | 403 | deliberate exclusion, reason recorded |
| matched-by-name | 0 | a port symbol with the same name exists |
| unmatched | 0 | no same-name port symbol; needs a manual verdict |

This matrix is an index. The authoritative per-function verdicts and the
evidence line for each one are in the topical reports (`chart.md`, `core.md`, `judging.md`,
`render.md`, `ui.md`, `gameplay.md`, `platform.md`, `audio.md`).


## `com\wuying\phigros\PhigrosApp.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 10 | `onCreate` | excluded |  |

## `com\wuying\phigros\audio\NativeAudioEngine.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 11 | `NativeAudioEngine` | excluded |  |
| 13 | `create` | excluded |  |
| 19 | `delete` | fixed | `src\tools\frame_profile.cpp:74` `delete` (+2) |
| 25 | `setMusicData` | verified |  |
| 34 | `setMusicData` | verified |  |
| 39 | `setSfxData` | verified |  |
| 45 | `triggerSfx` | verified |  |
| 51 | `setPlaybackSpeed` | verified | `src\audio\AudioEngine.cpp:701` `setPlaybackSpeed` |
| 56 | `setSfxVolume` | verified | `src\audio\AudioEngine.cpp:697` `setSfxVolume` |
| 61 | `setLowPassEnabled` | verified | `src\audio\AudioEngine.cpp:709` `setLowPassEnabled` |
| 71 | `setMusicVolume` | verified | `src\audio\AudioEngine.cpp:693` `setMusicVolume` |
| 76 | `setMusicLooping` | verified | `src\audio\AudioEngine.cpp:705` `setMusicLooping` |
| 81 | `prepareDefaultSfxIfMissing` | verified |  |
| 86 | `start` | fixed |  |
| 91 | `pause` | fixed | `src\audio\AudioEngine.cpp:643` `pause` |
| 96 | `restart` | excluded | `src\audio\AudioEngine.cpp:673` `restart` |
| 101 | `restartPaused` | verified | `src\audio\AudioEngine.cpp:681` `restartPaused` |
| 106 | `seekTo` | verified | `src\audio\AudioEngine.cpp:431` `seekTo` |
| 112 | `stop` | fixed | `src\audio\AudioEngine.cpp:648` `stop` |
| 117 | `getPlayheadSeconds` | verified | `src\audio\AudioEngine.cpp:459` `playheadSeconds` (+1) |
| 122 | `getMusicDurationSeconds` | verified | `src\audio\AudioEngine.cpp:728` `getMusicDurationSeconds` |

## `com\wuying\phigros\game\BpmTimeline.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 12 | `BpmTimeline` | verified | `src\core\BpmTimeline.cpp:8` `BpmTimeline` |
| 27 | `size` | fixed | `include\rechigros\game\BpmTimeline.hpp:8` `size` (+5) |
| 31 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 44 | `secToBeat` | verified | `src\core\BpmTimeline.cpp:28` `secToBeat` |
| 57 | `findSegmentByBeat` | fixed |  |
| 75 | `findSegmentBySec` | verified |  |

## `com\wuying\phigros\game\Chart.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 48 | `updateUiTransform` | verified | `src\core\JudgeLine.cpp:188` `updateUiTransform` |
| 83 | `LineRuntimeResult` | verified |  |
| 94 | `estimateNoteCount` | verified |  |
| 109 | `floorPositionMonotonic` | fixed | `src\core\EventUtils.cpp:922` `floorPositionMonotonic` (+1) |
| 169 | `floorPositionForLine` | verified |  |
| 174 | `createSpeedLayerIndices` | verified |  |
| 179 | `floorPositionForLine` | verified |  |
| 202 | `initRuntime` | verified |  |
| 228 | `initRuntimeSequential` | verified |  |
| 299 | `initRuntimeParallel` | verified |  |
| 314 | `call` | fixed |  |
| 408 | `finalizeRuntime` | verified |  |

## `com\wuying\phigros\game\ChartLoader.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 78 | `ChartLoader` | verified |  |
| 167 | `computeNormalizedMd5` | verified |  |
| 199 | `tryApplyLineCsvTextures` | verified |  |
| 266 | `stripExt` | verified |  |
| 271 | `equalsIgnoreCase` | verified |  |
| 275 | `findUpwards` | fixed | `src\chart\ChartLoader.cpp:394` `findUpwards` |
| 287 | `resolveResource` | fixed |  |
| 297 | `resolveUp` | fixed |  |
| 347 | `getCell` | verified |  |
| 354 | `parseIntSafe` | verified |  |
| 362 | `parseFloatSafe` | verified |  |
| 370 | `parseCsvLine` | verified | `src\app\InfoFile.cpp:231` `parseCsvLine` (+1) |
| 410 | `tryLoadPrprExtra` | fixed |  |
| 570 | `parsePrprBpmSegments` | verified |  |
| 589 | `addPrprBpmSegment` | verified |  |
| 600 | `parsePrprVideo` | verified |  |
| 631 | `resolvePrprResource` | verified |  |
| 640 | `parsePrprScaleType` | verified |  |
| 655 | `parsePrprVideoAttachLine` | verified |  |
| 666 | `PrprFloatAnim` | verified |  |
| 672 | `parsePrprFloatAnim` | verified |  |
| 728 | `fillPrprLineEventEasing` | verified |  |
| 735 | `getAsJsonArray` | verified |  |
| 744 | `isPrprShaderFileName` | verified |  |
| 750 | `isNumberArray` | verified |  |
| 759 | `parseBeatTriple` | verified | `src\chart\ChartLoader.cpp:204` `parseBeatTriple` |
| 768 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 779 | `getAsString` | verified |  |
| 789 | `getAsDouble` | fixed |  |
| 804 | `getAsBoolean` | verified |  |
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
| 189 | `elasticFAntideriv` | verified | `src\core\Easing.cpp:81` `elasticFAntideriv` |
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
| 330 | `bezierCacheKey` | excluded |  |
| 339 | `cubicBezierYforX` | verified | `src\core\Easing.cpp:284` `cubicBezierYForX` |
| 398 | `bezierSampleX` | verified | `src\core\Easing.cpp:131` `bezierSampleX` |
| 403 | `bezierSampleY` | verified |  |
| 407 | `bezierSlopeX` | verified | `src\core\Easing.cpp:136` `bezierSlopeX` |

## `com\wuying\phigros\game\EventCursor.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 11 | `reset` | fixed | `include\rechigros\audio\Mixer.hpp:147` `reset` (+9) |

## `com\wuying\phigros\game\EventUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 9 | `EventUtils` | fixed |  |
| 16 | `containsEventTime` | verified | `src\core\EventUtils.cpp:17` `containsEventTime` |
| 24 | `eventStarted` | verified | `src\core\EventUtils.cpp:23` `eventStarted` |
| 28 | `laterLineEventStarted` | verified | `src\core\EventUtils.cpp:25` `laterLineEventStarted` |
| 37 | `laterMoveEventStarted` | verified | `src\core\EventUtils.cpp:29` `laterMoveEventStarted` |
| 46 | `laterColorEventStarted` | verified | `src\core\EventUtils.cpp:33` `laterColorEventStarted` |
| 55 | `laterTextEventStarted` | verified | `src\core\EventUtils.cpp:37` `laterTextEventStarted` |
| 64 | `laterGifEventStarted` | verified | `src\core\EventUtils.cpp:41` `laterGifEventStarted` |
| 73 | `findEvent` | verified | `src\core\EventUtils.cpp:45` `findEvent` (+1) |
| 88 | `lerp` | fixed | `src\core\EventUtils.cpp:235` `lerp` |
| 92 | `getEventVal` | fixed | `src\core\EventUtils.cpp:244` `getEventVal` (+1) |
| 135 | `getMoveEventVal1` | verified | `src\core\EventUtils.cpp:320` `getMoveEventVal1` (+1) |
| 165 | `getMoveEventVal2` | verified | `src\core\EventUtils.cpp:391` `getMoveEventVal2` (+1) |
| 195 | `speedAt` | verified | `src\core\EventUtils.cpp:740` `speedAt` |
| 200 | `start` | fixed |  |
| 201 | `end` | fixed |  |
| 235 | `initSpeedEvents` | fixed | `src\core\EventUtils.cpp:766` `initSpeedEvents` |
| 314 | `nextSpeedEventStart` | verified |  |
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
| 707 | `getMoveEventVal2` | verified | `src\core\EventUtils.cpp:391` `getMoveEventVal2` (+1) |
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
| 27 | `GameGLSurfaceView` | excluded |  |
| 33 | `enableMultisample` | verified |  |
| 37 | `enableUnlimitedFrameRate` | verified |  |
| 52 | `bindToRenderer` | verified |  |
| 55 | `SimpleOnGestureListener` | verified |  |
| 56 | `onDown` | excluded |  |
| 60 | `onSingleTapConfirmed` | verified |  |
| 72 | `onDoubleTap` | verified |  |
| 102 | `onTouchEvent` | verified |  |
| 112 | `createContext` | verified |  |
| 127 | `destroyContext` | verified |  |
| 136 | `Es31ConfigChooser` | verified |  |
| 140 | `chooseConfig` | verified |  |
| 155 | `buildConfigAttribs` | verified |  |

## `com\wuying\phigros\game\GameRenderer.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 104 | `isMenuVisible` | verified |  |
| 109 | `setPracticeEnabled` | verified | `include\rechigros\game\JudgementEngine.hpp:56` `setPracticeEnabled` |
| 115 | `setPracticeSpeed` | verified | `src\app\GameScreen.cpp:872` `setPracticeSpeed` |
| 124 | `getPracticeSpeed` | verified | `src\app\GameScreen.cpp:872` `setPracticeSpeed` |
| 128 | `shouldResumeAudioOnActivityResume` | excluded |  |
| 138 | `setReplayRecorder` | verified |  |
| 143 | `setReplayPlayback` | verified | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 150 | `sortReplayEntriesByTime` | verified |  |
| 159 | `isReplayPlayback` | verified | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 163 | `beginIntro` | verified |  |
| 171 | `beginIntroWithAudioResume` | verified |  |
| 178 | `scheduleIntroAutoResume` | verified |  |
| 184 | `cancelIntroAutoResume` | verified |  |
| 190 | `isIntroGenerationCurrent` | verified |  |
| 194 | `computeIntroLineScale` | verified | `src\render\scene\Transition.cpp:64` `introLineScale` |
| 228 | `elapsedIntroSeconds` | verified |  |
| 235 | `smoothStep01` | verified | `src\render\scene\HudPass.cpp:72` `smoothStep01` (+1) |
| 240 | `outroUiProgress` | verified |  |
| 248 | `elapsedOutroSeconds` | verified |  |
| 253 | `isInOutroLineCollapse` | verified |  |
| 260 | `shouldHideNonLineStageElementsForOutro` | verified |  |
| 265 | `shouldHideStoryboardElementsForOutro` | verified |  |
| 269 | `computeOutroLineCollapseScale` | verified | `src\render\scene\Transition.cpp:85` `outroLineCollapseScale` |
| 276 | `computeOutroPlainLineScaleX` | verified |  |
| 283 | `computeStageTextureAlpha` | verified | `src\render\scene\Transition.cpp:107` `stageTextureAlpha` |
| 306 | `computeSideArtworkAlpha` | verified | `src\render\scene\Transition.cpp:120` `sideArtworkAlpha` |
| 314 | `hasIntroAudioReleaseElapsed` | verified |  |
| 320 | `introDimMix` | verified |  |
| 335 | `outroDimMix` | verified | `src\render\scene\Transition.cpp:80` `outroDimMix` |
| 351 | `computeBackgroundDimAlpha` | verified | `src\render\scene\Transition.cpp:127` `backgroundDimAlpha` |
| 364 | `computeSideDimAlpha` | verified | `src\render\scene\Transition.cpp:135` `sideDimAlpha` |
| 375 | `updateOutroState` | fixed |  |
| 405 | `getCombo` | verified | `include\rechigros\game\JudgementEngine.hpp:90` `combo` |
| 414 | `getScore` | verified | `src\core\JudgementEngine.cpp:253` `score` |
| 438 | `getAccuracy` | verified | `src\core\JudgementEngine.cpp:265` `accuracy` |
| 442 | `getRealtimeAccuracy` | verified | `src\core\JudgementEngine.cpp:270` `realtimeAccuracy` |
| 446 | `getMaxCombo` | verified | `include\rechigros\game\JudgementEngine.hpp:91` `maxCombo` |
| 450 | `getPerfectCount` | verified |  |
| 454 | `getGoodCount` | verified |  |
| 458 | `getBadCount` | verified |  |
| 462 | `getMissCount` | verified |  |
| 466 | `getAccuracyInternal` | verified |  |
| 476 | `getRealtimeAccuracyInternal` | verified |  |
| 818 | `PreloadedGif` | verified |  |
| 1020 | `setJudgeLineWidth` | verified |  |
| 1443 | `applyBlockTextureWrap` | verified |  |
| 1454 | `applyTextureBilinearFilter` | verified |  |
| 1464 | `applyTexturePointFilter` | verified |  |
| 1474 | `maxFragTextureUnits` | verified |  |
| 1552 | `GameRenderer` | fixed | `src\render\GameRenderer.cpp:1064` `GameRenderer` |
| 1726 | `sanitizeOfficialScreenDpi` | verified |  |
| 1735 | `sanitizeHitOffsetIndicatorMode` | fixed |  |
| 1742 | `preloadGifTextures` | verified |  |
| 1759 | `decodeGifFrames` | verified |  |
| 1812 | `uploadPreloadedGifTextures` | verified |  |
| 1877 | `onDoubleTap` | verified |  |
| 1886 | `isInPauseHitArea` | verified |  |
| 1893 | `computeRevealTransformedRect` | verified |  |
| 1908 | `triggerPauseTouchFeedback` | verified |  |
| 1912 | `isBeforeGameplayStartOrIntroTransition` | verified |  |
| 1916 | `freezePlayheadForPause` | verified |  |
| 1924 | `clearPausePlayheadFreeze` | verified |  |
| 1929 | `shouldUsePausedPlayhead` | verified |  |
| 1937 | `shouldPauseGameplayJudgement` | verified |  |
| 1944 | `getPausedPlayheadSeconds` | verified |  |
| 1950 | `resetIntroAudioToPausedStart` | verified |  |
| 1958 | `enterPauseMenu` | verified |  |
| 1980 | `beginResumeCountdown` | verified |  |
| 2002 | `requestPauseMenu` | verified |  |
| 2010 | `onTouchEvent` | verified |  |
| 2117 | `handleUiTap` | verified |  |
| 2170 | `onSingleTap` | verified |  |
| 2187 | `restartInternal` | verified |  |
| 2340 | `applyPracticeSeek` | verified | `src\app\GameScreen.cpp:162` `applyPracticeSeek` |
| 2459 | `firstNoteIndexAtOrAfter` | verified |  |
| 2470 | `firstEffectIndexAtOrAfter` | verified |  |
| 2481 | `requestPracticeSeek` | verified |  |
| 2486 | `updatePracticeRepeat` | verified |  |
| 2505 | `resetNativeRenderBucketRuntimeState` | verified |  |
| 2519 | `transformTouchX` | fixed |  |
| 2527 | `officialFingerWorldScalePx` | verified |  |
| 2535 | `officialJudgePositionScalePx` | verified |  |
| 2542 | `officialFingerWorldX` | verified |  |
| 2546 | `officialFingerWorldY` | verified |  |
| 2550 | `tryJudgeInputDown` | verified |  |
| 2554 | `tryJudgeInputMove` | verified |  |
| 2558 | `tryJudgeAt` | verified |  |
| 2620 | `computeNoteHeadPosition` | verified |  |
| 2680 | `getPerfectWindowSec` | verified |  |
| 2684 | `practiceJudgeOverrideActive` | verified |  |
| 2689 | `tapPerfectWindowSec` | verified | `include\rechigros\game\JudgementEngine.hpp:57` `tapPerfectWindow` |
| 2693 | `tapGoodWindowSec` | verified | `include\rechigros\game\JudgementEngine.hpp:65` `tapGoodWindow` |
| 2697 | `setPracticePerfectWindowMs` | verified |  |
| 2703 | `setPracticeGoodWindowMs` | verified |  |
| 2709 | `getGoodWindowSec` | verified |  |
| 2713 | `getBadWindowSec` | verified |  |
| 2717 | `getOfficialFlickJudgeSpeed` | verified |  |
| 2721 | `officialFlickJudgeSpeed` | verified |  |
| 2726 | `getEarlyWindowSec` | verified |  |
| 2731 | `getJudgeWindowSec` | verified | `src\core\JudgementEngine.cpp:99` `judgeWindowSec` |
| 2744 | `getSpatialJudgeWindowSec` | verified |  |
| 2770 | `officialCheckNoteCandidateReplaces` | verified |  |
| 2795 | `officialCheckFlickCandidateReplaces` | verified |  |
| 2807 | `getLateMissLimitSec` | verified | `src\core\JudgementEngine.cpp:322` `lateMissLimitSec` (+1) |
| 2816 | `officialLateMissLimitSec` | verified |  |
| 2834 | `officialDragLocalJudged` | verified |  |
| 2838 | `calcJudgement` | verified |  |
| 2851 | `commitJudgement` | fixed |  |
| 2960 | `chartTimeForJudgementDiff` | fixed | `src\core\JudgementEngine.cpp:143` `chartTimeForJudgementDiff` |
| 2965 | `recordHitOffsetIndicator` | verified |  |
| 2983 | `recordJudgementDetailOffset` | verified |  |
| 3007 | `judgementDetailRangeMs` | verified | `src\core\JudgementEngine.cpp:350` `judgementDetailRangeMs` |
| 3013 | `judgementDetailBinWidthMs` | verified | `src\core\JudgementEngine.cpp:355` `judgementDetailBinWidthMs` |
| 3017 | `buildPlayResult` | verified |  |
| 3038 | `buildReplayPlaybackResult` | verified |  |
| 3060 | `hasJudgementOffsetData` | verified |  |
| 3068 | `onSurfaceCreated` | excluded |  |
| 3341 | `onSurfaceChanged` | excluded |  |
| 3390 | `computeStageRect` | verified | `src\render\fx\BlockCompositePass.cpp:237` `setStageRect` |
| 3507 | `chartRevealCenterX` | verified |  |
| 3511 | `chartRevealCenterY` | verified |  |
| 3515 | `updateFrameChartRevealState` | verified |  |
| 3550 | `isChartRevealCameraActive` | verified | `src\app\GameScreen.cpp:80` `chartRevealCameraActive` |
| 3554 | `shouldRevealHiddenChartObjects` | verified |  |
| 3558 | `chartRevealHiddenFloorAlpha` | verified |  |
| 3568 | `currentChartRevealScale` | verified |  |
| 3573 | `chartRevealScreenX` | verified |  |
| 3579 | `chartRevealScreenY` | verified |  |
| 3585 | `computeRevealWorldBounds` | verified |  |
| 3602 | `onDrawFrame` | verified |  |
| 3727 | `refreshPauseSceneBlur` | verified |  |
| 3758 | `computeOfficialPauseBlurRadiusPx` | verified |  |
| 3764 | `drawPauseBackdrop` | verified |  |
| 3781 | `drawPauseBackdropTexture` | verified |  |
| 3812 | `shouldPostProcessAntialias` | verified |  |
| 3825 | `beginAntialiasFrame` | verified |  |
| 3835 | `bindAntialiasTarget` | verified |  |
| 3844 | `endAntialiasFrame` | verified |  |
| 3851 | `drawFxaaToDefaultFramebuffer` | verified |  |
| 3871 | `maybeAutoResumeAfterIntro` | verified |  |
| 3894 | `beginIntroAudioStartWatchdog` | verified |  |
| 3901 | `clearIntroAudioStartWatchdog` | verified |  |
| 3908 | `maybeRetryIntroAudioStart` | verified |  |
| 3943 | `releaseIntroAudioAtGameplayStart` | verified |  |
| 3955 | `releaseAudioForResumeFade` | verified |  |
| 3970 | `getResumeCountdownElapsedSec` | verified |  |
| 3978 | `getPauseMenuElapsedSec` | verified |  |
| 3986 | `computePauseBackdropIntroAlpha` | verified | `src\render\scene\PauseBackdrop.cpp:63` `pauseBackdropIntroAlpha` |
| 3995 | `computePauseButtonIntroAlpha` | verified | `src\app\GameScreen.cpp:644` `pauseButtonIntroAlpha` |
| 4004 | `computeContinueButtonAlpha` | verified | `src\app\GameScreen.cpp:650` `continueButtonAlpha` |
| 4010 | `computeResumeBackdropAlpha` | verified | `src\render\scene\PauseBackdrop.cpp:69` `resumeBackdropAlpha` |
| 4020 | `computeResumeBackdropBrightness` | verified | `src\render\scene\PauseBackdrop.cpp:79` `resumeBackdropBrightness` |
| 4026 | `computeResumeBackdropFlatten` | verified | `src\render\scene\PauseBackdrop.cpp:85` `resumeBackdropFlatten` |
| 4032 | `evaluateHermiteCurve` | verified | `src\render\scene\PauseBackdrop.cpp:30` `evaluateHermiteCurve` |
| 4065 | `finishResumeCountdown` | verified |  |
| 4077 | `drawResumeCountdown` | verified |  |
| 4108 | `drawOfficialCountdownDigit` | verified |  |
| 4118 | `officialCountdownAlpha` | verified |  |
| 4131 | `computeOfficialContinueBarX` | verified |  |
| 4145 | `easeOutBack` | verified |  |
| 4151 | `ensureCountdownTextures` | verified |  |
| 4170 | `computeResumeFadeAlpha` | verified |  |
| 4180 | `renderGameplayFrame` | verified |  |
| 4225 | `drawMessageEdgeMask` | verified |  |
| 4235 | `blockCompositeHasWork` | verified |  |
| 4252 | `renderSceneWithPrprEffects` | verified |  |
| 4452 | `drawHateBlackMaskOverlay` | verified |  |
| 4489 | `drawBackgroundAndBarsOnly` | verified |  |
| 4499 | `drawStageBackgroundOnly` | verified |  |
| 4556 | `officialRawBackgroundHidden` | verified |  |
| 4564 | `ensureKaleidoProgram` | verified |  |
| 4569 | `ensureDsGridProgram` | verified |  |
| 4574 | `isDesultorySignalsLevelEffects` | verified |  |
| 4586 | `dsGridOverlayVisible` | verified |  |
| 4620 | `drawGameplayObjects` | verified |  |
| 4715 | `drawDsGridOverlay` | verified |  |
| 4816 | `dsCanvasScaleFactor` | verified |  |
| 4829 | `drawOfficialKaleidoBackdrop` | verified |  |
| 4910 | `updateMessageStageMode` | verified |  |
| 4936 | `advanceMessageParticles` | verified |  |
| 4970 | `drawMessageParticles` | verified |  |
| 5054 | `createParticleGlowTexture` | verified |  |
| 5075 | `drawRevealScaledMasks` | verified |  |
| 5100 | `drawRevealOutsidePanel` | verified |  |
| 5156 | `initPrprVideoRuntimes` | excluded |  |
| 5203 | `preparePrprVideoPlayer` | excluded |  |
| 5219 | `Listener` | excluded |  |
| 5220 | `onVideoSizeChanged` | excluded |  |
| 5224 | `onPlaybackStateChanged` | excluded |  |
| 5239 | `onPlayerError` | excluded |  |
| 5257 | `setPrprVideoPlaybackSpeed` | excluded |  |
| 5262 | `setPrprVideoPlaybackSpeedAll` | excluded |  |
| 5268 | `releasePrprVideoRuntimes` | excluded |  |
| 5272 | `releasePrprVideoRuntimes` | excluded |  |
| 5279 | `release` | fixed | `src\app\TriGridBackdrop.cpp:260` `release` (+11) |
| 5283 | `pausePrprVideoRuntimes` | excluded |  |
| 5292 | `resetPrprVideoRuntimes` | excluded |  |
| 5310 | `drawPrprVideos` | excluded |  |
| 5368 | `canPrprVideosPlay` | excluded |  |
| 5374 | `syncPrprVideo` | excluded |  |
| 5407 | `seekPrprVideo` | excluded |  |
| 5411 | `seekPrprVideo` | excluded |  |
| 5426 | `updatePrprVideoFrame` | excluded |  |
| 5436 | `drawExternalVideoCentered` | excluded |  |
| 5484 | `drawExternalTextureCentered` | excluded |  |
| 5538 | `applyPrprEffectChain` | verified |  |
| 5558 | `applyPrprEffectPass` | verified |  |
| 5622 | `applyOfficialBloomPass` | verified |  |
| 5723 | `drawBloomStage` | verified |  |
| 5743 | `setUniform1f` | verified |  |
| 5748 | `setUniform2f` | verified |  |
| 5753 | `setUniform3f` | verified |  |
| 5758 | `setUniform1i` | verified |  |
| 5763 | `evalPrprVarScalar` | verified |  |
| 5776 | `ensureOfficialBloomPrograms` | verified |  |
| 5786 | `ensureVignetteNoiseProgram` | verified |  |
| 5791 | `buildPrprProgram` | verified |  |
| 5815 | `obtainBloomFbo` | verified |  |
| 5824 | `releaseBloomFbos` | verified |  |
| 5833 | `bindEffectSampler` | verified |  |
| 5845 | `bindVignetteNoiseIfNeeded` | verified |  |
| 5933 | `prprNoiseClockSec` | verified |  |
| 5940 | `applyPrprEffectChainFull` | verified |  |
| 5966 | `applyPrprEffectChainStageViewport` | verified |  |
| 6005 | `applyPrprEffectPassFull` | verified |  |
| 6032 | `applyPrprEffectPassViewport` | verified |  |
| 6076 | `applyPrprEffectPassCommon` | verified |  |
| 6140 | `evaluatePrprVarValue` | verified |  |
| 6147 | `clone` | verified |  |
| 6160 | `evalEventList` | verified |  |
| 6189 | `applyUniform` | verified | `src\render\gl\ShaderProgram.cpp:227` `uniform` |
| 6207 | `resetSmoothBiasHistory` | verified |  |
| 6218 | `invalidateSmoothClock` | verified |  |
| 6223 | `primeSmoothClock` | verified |  |
| 6235 | `cacheSmoothedPlayhead` | verified |  |
| 6241 | `addSmoothBias` | verified |  |
| 6257 | `getSmoothedPlayheadSeconds` | verified |  |
| 6368 | `clear` | verified | `src\audio\AudioEngine.cpp:66` `clear` (+5) |
| 6375 | `ensureRenderLineInfoCapacity` | verified |  |
| 6385 | `getRenderLineInfo` | verified |  |
| 6392 | `renderSceneDirect` | verified |  |
| 6563 | `drawAmetrineVignette` | verified |  |
| 6597 | `canUseNativeRenderCommands` | excluded |  |
| 6607 | `canUseNativeTimeline` | excluded |  |
| 6617 | `prepareNativeTimelineLineStates` | excluded |  |
| 6669 | `applyNativeTimelineLineStates` | excluded |  |
| 6727 | `resolveLineText` | verified |  |
| 6735 | `prepareNativeLineState` | excluded |  |
| 6746 | `currentChartGlobalBeat` | verified |  |
| 6751 | `drawNativeHoldNotes` | excluded |  |
| 6837 | `drawNativeNoteHeads` | excluded |  |
| 6905 | `drawDebugOverlay` | verified | `src\app\GameScreen.cpp:276` `drawDebugOverlay` |
| 7032 | `normalizeAlpha01` | verified |  |
| 7042 | `isHitJudgement` | verified |  |
| 7046 | `applyYControlScale` | verified | `src\core\NoteGeometry.cpp:37` `applyYControlScale` (+1) |
| 7051 | `rpeControlHeightFromBasePx` | verified | `src\core\NoteGeometry.cpp:41` `rpeControlHeightFromBasePx` |
| 7069 | `getOrCreateDebugText` | verified |  |
| 7082 | `calcInclineValue` | verified | `src\core\NoteGeometry.cpp:53` `calcInclineValue` (+1) |
| 7103 | `combineInclineSinr` | verified | `src\core\NoteGeometry.cpp:61` `combineInclineSinr` (+1) |
| 7112 | `yOffsetToTransYPx` | verified | `src\core\NoteGeometry.cpp:70` `yOffsetToTransYPx` |
| 7120 | `ensureLineDrawOrder` | verified |  |
| 7149 | `getLineZOrder` | verified |  |
| 7156 | `drawFakeJudgeLine` | verified |  |
| 7217 | `isStoryboardJudgeLine` | verified |  |
| 7225 | `isLineAttachedToPrprVideo` | verified |  |
| 7235 | `drawJudgeLineVisualWithOutroLineCollapse` | verified |  |
| 7266 | `drawJudgeLineVisual` | verified |  |
| 7472 | `getOrLoadFileTexture` | verified |  |
| 7494 | `getOrCreateTextTexture` | verified |  |
| 7567 | `getTextPaint` | verified |  |
| 7600 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 7604 | `deleteGlTexture` | verified |  |
| 7619 | `invalidateAllCachedTextures` | verified |  |
| 7658 | `invalidateGl` | verified |  |
| 7662 | `invalidateHudDynamicText` | verified |  |
| 7674 | `ensureTextureValid` | verified |  |
| 7777 | `clearHudTextTextures` | verified |  |
| 7818 | `clearHudDynamicText` | verified |  |
| 7835 | `ellipsizeToWidth` | verified |  |
| 7839 | `ellipsizeToWidth` | verified |  |
| 7874 | `createHudTextTexture` | verified |  |
| 7878 | `createHudTextTexture` | verified |  |
| 7882 | `createHudTextTexture` | verified |  |
| 7886 | `createHudTextTexture` | verified |  |
| 7890 | `createHudTextTexture` | verified |  |
| 7983 | `isAmetrineComboBlocked` | verified |  |
| 7990 | `ametrineHudVignetteFactor` | verified |  |
| 8003 | `buildBlockedComboPattern` | verified |  |
| 8071 | `drawBlockedComboPattern` | verified |  |
| 8148 | `blockedComboNoiseSample` | verified |  |
| 8173 | `blockedComboNoise` | verified |  |
| 8181 | `measureHudTextWidth` | verified |  |
| 8204 | `drawHudText` | verified |  |
| 8227 | `updateHudDynamicText` | verified |  |
| 8326 | `drawHudDynamicText` | verified |  |
| 8343 | `updateHudTextTexturesIfNeeded` | verified |  |
| 8503 | `updateHudDisplayValues` | verified |  |
| 8526 | `approachHudDisplayValue` | verified |  |
| 8537 | `clampScoreInt` | verified |  |
| 8543 | `drawHudTexts` | verified |  |
| 8639 | `officialComboLabelOutroAlpha` | verified |  |
| 8656 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |
| 8660 | `officialHudUnitPx` | verified |  |
| 8664 | `officialHudHalfUiW` | fixed |  |
| 8673 | `officialHudX` | verified |  |
| 8677 | `officialHudY` | verified |  |
| 8681 | `officialHudSize` | verified |  |
| 8685 | `officialHudBottomTextYOffsetPx` | verified |  |
| 8689 | `formatScore7` | verified |  |
| 8701 | `formatPercentHundredths` | verified |  |
| 8706 | `getAttachUiState` | verified |  |
| 8714 | `applyUiTransformAndDraw` | verified |  |
| 8726 | `applyUiTransformAndDrawUv` | verified |  |
| 8869 | `drawGameHudWithAnimations` | verified |  |
| 8906 | `sampleCurve` | verified | `src\render\scene\HudPass.cpp:77` `sampleCurve` (+1) |
| 8916 | `officialLevelStartEase` | verified |  |
| 8923 | `officialLevelOverRootScaleY` | verified |  |
| 8929 | `drawGameHudWithAnimationsClipped` | verified |  |
| 8942 | `drawGameHud` | verified |  |
| 8974 | `drawHitOffsetIndicator` | verified | `src\render\scene\HudPass.cpp:598` `drawHitOffsetIndicator` |
| 9053 | `drawHitOffsetLineIndicator` | verified |  |
| 9101 | `hitOffsetLineBottomSafety` | verified |  |
| 9105 | `hitOffsetBottomSafety` | verified |  |
| 9117 | `drawHitOffsetArc` | verified |  |
| 9142 | `drawHitOffsetRadialLine` | verified |  |
| 9159 | `drawHitOffsetLine` | verified |  |
| 9175 | `drawHitOffsetArrowLinear` | verified |  |
| 9190 | `drawHitOffsetArrow` | verified |  |
| 9205 | `hitOffsetArrowWidth` | verified |  |
| 9209 | `hitOffsetArrowHeight` | verified |  |
| 9218 | `drawHitOffsetText` | verified |  |
| 9245 | `hitOffsetColor` | verified |  |
| 9256 | `hitOffsetPointX` | verified |  |
| 9261 | `hitOffsetPointY` | verified |  |
| 9266 | `hitOffsetLineX` | verified |  |
| 9271 | `drawPauseTouchRing` | verified |  |
| 9304 | `drawFpsCounter` | verified | `src\render\scene\HudPass.cpp:750` `drawFpsCounter` |
| 9329 | `drawTextureAnchored` | verified |  |
| 9338 | `drawTextureAnchored` | verified |  |
| 9366 | `drawGifFrame` | verified |  |
| 9418 | `renderSceneToFbo` | verified |  |
| 9446 | `blurPass` | verified | `src\render\fx\PauseBackdropPass.cpp:90` `blurPass` |
| 9468 | `pauseScalableBlurPass` | verified |  |
| 9499 | `createBackgroundBlur` | verified |  |
| 9570 | `renderBackgroundToFbo` | verified |  |
| 9603 | `drawTimerLine` | verified |  |
| 9639 | `updateGameplay` | fixed |  |
| 9676 | `syncPausedJudgementState` | fixed |  |
| 9687 | `replayRecordNowChartTimeSec` | verified |  |
| 9692 | `activateHoldAfterHead` | verified |  |
| 9712 | `trackActiveHold` | verified |  |
| 9720 | `useTrackedHoldOptimization` | verified |  |
| 9728 | `updateTrackedHoldNotes` | verified |  |
| 9771 | `getHoldBodyEffectIntervalSec` | verified |  |
| 9777 | `spawnDueHoldBodyHitEffects` | fixed |  |
| 9837 | `updateAutoplay` | fixed | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 9876 | `recordNewHoldPresses` | verified |  |
| 9893 | `updateReplayPlayback` | verified | `include\rechigros\game\GameplaySession.hpp:73` `replayPlayback` (+2) |
| 9955 | `updateJudgeManualPhi` | fixed |  |
| 10496 | `getPhigrosTouchPos` | verified |  |
| 10553 | `getJudgeOffset` | verified |  |
| 10570 | `effectiveJudgeArea` | verified | `src\core\GameplaySession.cpp:138` `effectiveJudgeArea` |
| 10588 | `sanitizeJudgeArea` | verified | `src\core\NoteGeometry.cpp:74` `sanitizeJudgeArea` |
| 10593 | `computeNoteControlHeight` | verified |  |
| 10609 | `getJudgeDistance` | verified | `src\core\GameplaySession.cpp:172` `judgeDistance` |
| 10634 | `toLineLocalX` | verified |  |
| 10642 | `toLineLocalY` | verified |  |
| 10651 | `computeNoteLocalX` | verified |  |
| 10684 | `spawnHitEffect` | verified |  |
| 10700 | `spawnHoldHeadHitEffect` | verified |  |
| 10729 | `spawnHoldBodyHitEffect` | verified |  |
| 10733 | `hasHitEffectTint` | verified |  |
| 10737 | `hitEffectTintR` | verified |  |
| 10741 | `hitEffectTintG` | verified |  |
| 10745 | `hitEffectTintB` | verified |  |
| 10749 | `getLineRotDeg` | verified |  |
| 10763 | `computeNoteHeadPositionOnLine` | verified |  |
| 10798 | `spawnBadEffect` | verified |  |
| 10853 | `obtainHitEffect` | verified |  |
| 10861 | `obtainBadEffect` | verified |  |
| 10873 | `recycleHitEffect` | verified |  |
| 10879 | `recycleBadEffect` | verified |  |
| 10885 | `recycleAllHitEffects` | verified |  |
| 10892 | `recycleAllBadEffects` | verified |  |
| 10912 | `set` | fixed |  |
| 10922 | `obtainJudgeEvent` | verified |  |
| 10929 | `recycleJudgeEvents` | verified |  |
| 10961 | `FlickTracker` | verified | `src\core\JudgementEngine.cpp:28` `FlickTracker` |
| 10972 | `move` | fixed | `src\core\JudgementEngine.cpp:34` `move` |
| 10977 | `settleAtCurrentPosition` | verified | `src\core\JudgementEngine.cpp:39` `settleAtCurrentPosition` |
| 10993 | `advanceFrame` | verified | `src\core\JudgementEngine.cpp:47` `advanceFrame` |
| 11053 | `set` | fixed |  |
| 11092 | `set` | fixed |  |
| 11115 | `addQuadToBatch` | verified |  |
| 11143 | `putBatchInstance` | verified |  |
| 11165 | `setupVertexArrayObjects` | verified |  |
| 11175 | `createQuadVao` | verified |  |
| 11197 | `createBatchVao` | verified |  |
| 11243 | `uploadQuadUvBuffer` | verified |  |
| 11251 | `uploadBatchBuffer` | verified |  |
| 11262 | `flushBatch` | verified |  |
| 11266 | `flushBatch` | verified |  |
| 11301 | `flushHitFxBatch` | verified |  |
| 11330 | `noteWidthRatio` | verified | `src\render\GameRenderer.cpp:62` `noteWidthRatio` |
| 11349 | `hitFxDragIntegral` | verified |  |
| 11356 | `hitFxParticleTravel` | verified |  |
| 11373 | `drawClickEffects` | verified |  |
| 11543 | `getHistoricalLineState` | verified |  |
| 11554 | `shouldSkipReplayClickEffect` | verified |  |
| 11569 | `removeExpiredPrefix` | verified |  |
| 11578 | `drawBadEffects` | verified |  |
| 11619 | `drawHitEffects` | verified | `src\app\CalibrationScreen.cpp:345` `drawHitEffects` (+1) |
| 11716 | `drawTextureCover` | verified |  |
| 11763 | `drawOfficialBackgroundClip` | verified |  |
| 11801 | `drawOfficialSideBackgrounds` | verified |  |
| 11810 | `drawDeferredSidePanelsForDsChain` | verified |  |
| 11823 | `drawOfficialSideColumnsOverlay` | verified |  |
| 11853 | `drawSideVerticalGaps` | verified |  |
| 11864 | `drawOfficialSideBackgroundPanel` | verified |  |
| 11880 | `drawSideDimPanel` | verified |  |
| 11885 | `hasHorizontalSideBlanks` | verified |  |
| 11889 | `isStageFullscreen` | verified |  |
| 11899 | `enableStageScissor` | verified |  |
| 11931 | `disableStageScissor` | verified |  |
| 11935 | `withScreenScissor` | verified |  |
| 11977 | `drawButton` | verified |  |
| 11981 | `computePracticeLayout` | verified | `src\app\GameScreen.cpp:770` `computePracticeLayout` |
| 12041 | `computePracticeUiScale` | verified |  |
| 12048 | `pdp` | verified |  |
| 12053 | `btnRowTop` | verified |  |
| 12057 | `practiceText` | verified |  |
| 12069 | `drawPracticeTextCentered` | verified |  |
| 12073 | `drawPracticeTextCentered` | verified |  |
| 12084 | `drawPracticeTextLeft` | verified |  |
| 12094 | `drawPracticeTextRight` | verified |  |
| 12114 | `ensurePracticeUiTextures` | verified |  |
| 12125 | `createRoundedMaskTexture` | verified |  |
| 12136 | `createDiscTexture` | verified |  |
| 12146 | `drawRoundedRect` | verified | `src\app\GameScreen.cpp:341` `drawRoundedRect` |
| 12185 | `drawDisc` | verified |  |
| 12194 | `drawCapsule` | verified |  |
| 12202 | `practiceFormatTime` | verified | `src\app\GameScreen.cpp:698` `practiceFormatTime` |
| 12208 | `practiceFormatTimePrecise` | verified | `src\app\GameScreen.cpp:706` `practiceFormatTimePrecise` |
| 12216 | `drawPracticeMenu` | verified |  |
| 12321 | `drawCard` | verified | `src\app\GameScreen.cpp:347` `drawCard` |
| 12329 | `drawJudgeMenu` | verified |  |
| 12385 | `drawJudgeSliderRow` | verified |  |
| 12404 | `drawPracticeButton` | verified | `src\app\GameScreen.cpp:928` `drawPracticeButton` |
| 12419 | `practiceHandlesTouchDown` | verified |  |
| 12473 | `practiceHandlesTouchMove` | verified |  |
| 12510 | `updatePracticeFineScrub` | verified | `src\app\GameScreen.cpp:881` `updatePracticeFineScrub` |
| 12535 | `applyPracticeFineScrubFromTouch` | verified |  |
| 12543 | `applyPracticeSpeedFromTouch` | verified | `src\app\GameScreen.cpp:904` `applyPracticeSpeedFromTouch` |
| 12551 | `judgePanelVisible` | verified |  |
| 12556 | `applyPracticeJudgeFromTouch` | verified |  |
| 12570 | `applyPracticeScrubFromTouch` | verified | `src\app\GameScreen.cpp:898` `applyPracticeScrubFromTouch` |
| 12578 | `setPracticeRepeatPoint` | verified | `src\app\GameScreen.cpp:838` `setPracticeRepeatPoint` |
| 12601 | `togglePracticeRepeat` | verified | `src\app\GameScreen.cpp:862` `togglePracticeRepeat` |
| 12611 | `practiceJump` | verified |  |
| 12616 | `drawPauseButtons` | verified |  |
| 12623 | `drawButton` | verified |  |
| 12630 | `drawFullscreenTextureBackground` | verified |  |
| 12642 | `drawFullscreenTextureFbo` | verified |  |
| 12649 | `drawSolidRect` | verified |  |
| 12654 | `drawLine` | verified |  |
| 12667 | `drawTextureTopLeft` | verified |  |
| 12678 | `drawTextureCentered` | verified |  |
| 12725 | `isNoteRectInArea` | verified | `src\render\scene\NoteBatch.cpp:110` `isNoteRectInArea` |
| 12755 | `drawFullQuadWithCurrentProgram` | verified |  |
| 12796 | `rebuildTextureMetrics` | verified |  |
| 12811 | `getNoteWidthScale` | verified |  |
| 12817 | `getHoldUv` | verified |  |
| 12824 | `buildHoldUv` | verified | `src\render\scene\NoteBatch.cpp:204` `buildHoldUv` |
| 12882 | `getNoteHeadTexture` | verified |  |
| 12895 | `getNoteHeadTextureSameType` | verified |  |
| 12903 | `rebuildNoteHeadTextureDrawList` | verified |  |
| 12923 | `rebuildNativeRenderBuckets` | excluded |  |
| 12964 | `buildNativeBucket` | excluded |  |
| 13009 | `matchesNativeBucket` | excluded |  |
| 13018 | `releaseNativeRenderBuckets` | excluded |  |
| 13036 | `findNoteHeadTextureBucket` | verified |  |
| 13086 | `invalidateGl` | verified |  |
| 13098 | `isValidForContext` | verified |  |
| 13102 | `getGifFrameTexId` | verified |  |
| 13113 | `getGifFrameIndex` | verified |  |
| 13137 | `set` | fixed |  |
| 13138 | `width` | fixed | `include\rechigros\app\UiLayout.hpp:43` `width` (+5) |
| 13139 | `height` | verified | `include\rechigros\app\UiLayout.hpp:44` `height` (+5) |
| 13140 | `centerX` | verified | `include\rechigros\app\UiLayout.hpp:45` `centerX` (+1) |
| 13141 | `centerY` | verified | `include\rechigros\app\UiLayout.hpp:46` `centerY` (+1) |
| 13142 | `contains` | verified | `src\app\CalibrationScreen.cpp:293` `contains` |
| 13170 | `PrprVideoRuntime` | excluded |  |
| 13183 | `ready` | fixed | `include\rechigros\app\TriGridBackdrop.hpp:24` `ready` (+11) |
| 13187 | `runOnPlayerThread` | excluded |  |
| 13195 | `start` | fixed |  |
| 13212 | `pause` | fixed | `src\audio\AudioEngine.cpp:643` `pause` |
| 13225 | `markPlaybackBlocked` | excluded |  |
| 13231 | `onPlaybackReleased` | excluded |  |
| 13238 | `seekTo` | verified | `src\audio\AudioEngine.cpp:431` `seekTo` |
| 13250 | `setPlaybackSpeed` | verified | `src\audio\AudioEngine.cpp:701` `setPlaybackSpeed` |
| 13262 | `updateVideoSize` | excluded |  |
| 13282 | `maybeSamplePlayerPosition` | excluded |  |
| 13291 | `samplePlayerPosition` | excluded |  |
| 13301 | `hasFreshPositionSample` | excluded |  |
| 13307 | `release` | fixed | `src\app\TriGridBackdrop.cpp:260` `release` (+11) |
| 13346 | `FboTex` | verified |  |
| 13354 | `releaseFbos` | verified |  |
| 13386 | `ensurePrprFbos` | verified |  |
| 13402 | `deleteFbo` | verified |  |
| 13425 | `PrprShaderProgram` | verified |  |
| 13465 | `applyDefaults` | verified |  |
| 13486 | `getDefaultFloat` | verified |  |
| 13499 | `UniformInfo` | verified |  |
| 13507 | `initPrprShaderPrograms` | verified |  |
| 13560 | `warmUpPrprShaderPrograms` | verified |  |
| 13645 | `getPrprProgram` | verified |  |
| 13654 | `resolvePrprShaderSource` | fixed |  |
| 13684 | `normalizePrprShaderKey` | verified |  |
| 13700 | `isPrprShaderFileName` | verified |  |
| 13706 | `prprPresetFileForKey` | verified |  |
| 13754 | `readAssetText` | verified |  |
| 13768 | `parsePrprUniformDefaults` | verified |  |
| 13808 | `preprocessPrprFragment` | verified |  |
| 13864 | `createWhiteTexture` | verified |  |
| 13886 | `createEmptyTexture` | verified |  |
| 13901 | `setLinearTextureParams` | verified |  |
| 13908 | `setLinearTextureParams` | verified |  |
| 13912 | `updateTextureWrap` | verified |  |
| 13921 | `uploadBitmapToBoundTexture` | verified |  |
| 13936 | `discardBoundFboColor` | verified |  |
| 13940 | `createFboForTexture` | verified |  |
| 13955 | `isFboComplete` | verified |  |
| 13966 | `loadTextureFromAssetsSafe` | verified |  |
| 13970 | `loadOfficialShaderTexture` | verified |  |
| 13995 | `flipBitmapVertical` | verified |  |
| 14011 | `loadStackedAtlasSlicesVFlip` | verified |  |
| 14044 | `loadTextureFromAssetsSafe` | verified |  |
| 14057 | `forceBitmapWhite` | verified |  |
| 14078 | `loadTextureFromFileSafe` | verified |  |
| 14084 | `loadTextureFromFileSafe` | verified |  |
| 14098 | `decodeBitmapDownsampled` | verified |  |
| 14116 | `loadGifTexture` | verified |  |
| 14137 | `uploadGifFramesFromPreloaded` | verified |  |
| 14190 | `uploadBitmapAsTexture` | verified |  |
| 14194 | `uploadBitmapAsTexture` | verified |  |
| 14218 | `loadResPackFromAssets` | verified |  |
| 14230 | `resolveFloat` | verified | `src\render\skin\SkinConfig.cpp:406` `resolveFloat` |
| 14240 | `resolveBool` | verified | `src\render\skin\SkinConfig.cpp:413` `resolveBool` |
| 14245 | `parseSkinColor` | verified |  |
| 14276 | `cropHoldBodyTexture` | verified |  |
| 14317 | `loadSkinTexture` | verified |  |
| 14329 | `createQuadPos` | verified |  |
| 14345 | `createNdcQuadPos` | verified |  |
| 14360 | `createQuadUv` | verified |  |
| 14377 | `updateQuadUv` | verified |  |
| 14401 | `updateQuadUvNdc` | verified |  |
| 14423 | `buildProgram` | verified | `src\app\TriGridBackdrop.cpp:124` `buildProgram` |
| 14448 | `compileShader` | verified |  |
| 14464 | `prepareShaderSource` | verified | `src\render\gl\ShaderProgram.cpp:123` `prepareShaderSource` |
| 14475 | `hasVersionDirective` | verified |  |
| 14485 | `upgradeLegacyVertexShaderPublic` | verified |  |
| 14490 | `upgradeLegacyVertexShader` | verified |  |
| 14497 | `upgradeLegacyFragmentShader` | verified |  |
| 14508 | `chooseLegacyFragmentOutputName` | verified |  |
| 14519 | `containsIdentifier` | verified |  |
| 14525 | `addGlslEs310FragmentHeader` | verified |  |
| 14572 | `shaderTypeName` | verified |  |
| 14832 | `updateOfficialBlocks` | verified |  |
| 14867 | `syncOfficialBlocksToTime` | verified |  |
| 14877 | `blockCompositeUsable` | verified |  |
| 14909 | `ensureBlockComposite` | verified |  |
| 15110 | `releaseBlockComposite` | verified |  |
| 15171 | `uploadBlockMaterialUniforms` | verified |  |
| 15236 | `updateTouchSlots` | verified |  |
| 15300 | `uploadBlockUniforms` | verified |  |
| 15325 | `putMaskVertex` | verified |  |
| 15333 | `emitMaskQuad` | verified |  |
| 15358 | `emitHoverQuad` | verified |  |
| 15372 | `renderBlockMasks` | verified |  |
| 15543 | `blockGlowRingWeight` | verified |  |
| 15555 | `runBlockPipelineInto` | verified |  |
| 15568 | `runBlockPrepasses` | verified |  |
| 15674 | `drawSolidRectRot` | verified |  |
| 15680 | `renderOfficialBlockComposite` | verified |  |
| 15715 | `drawBlockCompositeQuad` | verified |  |
| 15839 | `isTouchInsideBlock` | verified |  |
| 15846 | `processBlockedTouchesOfficial` | verified |  |
| 15872 | `updateLowPassFilterStateOfficial` | verified |  |
| 15882 | `setBlockAudioFilter` | verified |  |
| 15921 | `blockHash` | verified | `src\render\fx\BlockOverlay.cpp:52` `blockHash` |
| 15927 | `drawOfficialBlock` | verified |  |
| 16045 | `drawOfficialBlocks` | verified |  |
| 16089 | `updateOfficialChallenge` | verified |  |
| 16116 | `isOfficialChallengeFailed` | verified |  |
| 16121 | `getOfficialEffectName` | verified |  |

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
| 112 | `globalSec2beat` | verified | `src\core\JudgeLine.cpp:22` `globalSec2beat` |
| 118 | `beat2sec` | fixed | `src\core\JudgeLine.cpp:23` `beat2sec` |
| 123 | `globalBeat2sec` | verified | `src\core\JudgeLine.cpp:24` `globalBeat2sec` |
| 167 | `NoteControlPoint` | verified |  |
| 170 | `NoteControlPoint` | verified |  |
| 174 | `NoteControlPoint` | verified |  |
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
| 625 | `normalize` | excluded | `src\core\JudgeLine.cpp:32` `normalize` |
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
| 187 | `alive` | verified |  |
| 189 | `lifeAlpha` | verified |  |
| 200 | `sizeAt` | verified | `src\core\MessageParticleField.cpp:131` `sizeAt` |
| 207 | `bandAlpha` | verified | `src\core\MessageParticleField.cpp:47` `bandAlpha` |
| 225 | `dotCount` | verified |  |
| 227 | `dotAt` | verified |  |
| 228 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |

## `com\wuying\phigros\game\NativeRenderCommands.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 64 | `NativeRenderCommands` | excluded |  |
| 66 | `isAvailable` | verified | `src\platform\FileDialog.cpp:131` `available` (+3) |
| 174 | `LineState` | excluded |  |
| 324 | `LineTimeline` | verified |  |
| 674 | `initNativeStore` | excluded |  |
| 679 | `releaseNativeStore` | excluded |  |
| 686 | `hasWhiteDefault` | verified |  |
| 693 | `countNonNullLayers` | verified |  |
| 702 | `countSpeedLayers` | verified |  |
| 711 | `hasSpeedLayers` | verified |  |
| 715 | `countNoteControls` | excluded |  |
| 719 | `countLineEvents` | excluded |  |
| 728 | `countMoveEvents` | excluded |  |
| 737 | `countSpeedEvents` | excluded |  |
| 746 | `countSpeedLayerEvents` | excluded |  |
| 755 | `countColorEvents` | excluded |  |
| 764 | `countSpeedBezierData` | excluded |  |
| 775 | `countSpeedLayerBezierData` | excluded |  |
| 784 | `countColorBezierData` | excluded |  |
| 795 | `countGifEvents` | excluded |  |
| 804 | `countLineBezierData` | excluded |  |
| 813 | `countMoveBezierData` | excluded |  |
| 822 | `bezierDataCount` | excluded |  |
| 828 | `bezierDataCount` | excluded |  |
| 849 | `LineEventWriter` | excluded |  |
| 863 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 870 | `add` | fixed |  |
| 882 | `writeBezier` | excluded |  |
| 917 | `MoveEventWriter` | excluded |  |
| 933 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 940 | `add` | fixed |  |
| 954 | `writeBezier` | excluded |  |
| 994 | `SpeedEventWriter` | excluded |  |
| 1015 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1038 | `writeBezier` | excluded |  |
| 1075 | `ColorEventWriter` | excluded |  |
| 1093 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1138 | `GifEventWriter` | excluded |  |
| 1148 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1170 | `NoteControlWriter` | excluded |  |
| 1176 | `addAll` | excluded | `src\render\fx\BlockOverlay.cpp:137` `updateAll` |
| 1228 | `NoteBucket` | excluded |  |
| 1267 | `setNote` | excluded | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 1299 | `syncDynamicStates` | verified |  |
| 1316 | `resetRuntimeState` | verified |  |
| 1331 | `syncDynamicState` | verified |  |
| 1336 | `updateLoopStarts` | verified |  |
| 1362 | `initNativeStore` | excluded |  |
| 1367 | `releaseNativeStore` | excluded |  |

## `com\wuying\phigros\game\OfficialBlock.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 101 | `hasTransformEvents` | verified | `include\rechigros\game\OfficialBlock.hpp:62` `hasTransformEvents` |
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
| 425 | `easedProgress` | verified | `src\core\OfficialBlock.cpp:22` `easedProgress` |
| 444 | `findEventIndex` | verified | `src\core\OfficialBlock.cpp:28` `findEventIndex` |
| 460 | `corners` | verified | `src\core\OfficialBlock.cpp:293` `corners` |

## `com\wuying\phigros\game\OfficialChartParser.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `OfficialChartParser` | verified |  |
| 46 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 62 | `parseRoot` | verified | `include\rechigros\app\Replay.hpp:97` `root` (+2) |
| 100 | `call` | fixed |  |
| 151 | `parseBlockAreas` | verified | `src\chart\OfficialChartParser.cpp:125` `parseBlockAreas` |
| 245 | `parseJudgeLine` | verified | `src\chart\RpeChartParser.cpp:806` `parseJudgeLine` |
| 303 | `parseNotes` | verified | `src\chart\OfficialChartParser.cpp:44` `parseNotes` (+1) |
| 344 | `parseLineEvents1D` | verified |  |
| 372 | `parseMoveComponentEvents` | verified |  |
| 420 | `parseSpeedEventsOfficial` | verified |  |
| 448 | `unpackV1MoveEvents` | verified |  |
| 479 | `ensureCoverage` | verified | `src\chart\OfficialChartParser.cpp:28` `ensureCoverage` (+1) |
| 502 | `defaultSpeedEvents` | verified |  |
| 546 | `parseEventTime` | verified | `src\chart\RpeChartParser.cpp:105` `eventTime` |
| 556 | `getAsDouble` | fixed |  |
| 565 | `getAsInt` | fixed |  |
| 574 | `getAsBool` | fixed |  |
| 588 | `getFirst` | verified |  |
| 597 | `asObj` | verified |  |
| 602 | `asArray` | verified |  |
| 612 | `copyLineEvent` | verified |  |

## `com\wuying\phigros\game\OfficialEase.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 25 | `OfficialEase` | verified |  |
| 47 | `toRpeEasing` | verified | `src\core\OfficialEase.cpp:62` `toRpeEasing` |
| 84 | `evaluate` | fixed | `src\core\OfficialEase.cpp:80` `evaluate` (+2) |
| 89 | `evaluate` | fixed | `src\core\OfficialEase.cpp:80` `evaluate` (+2) |
| 117 | `bezier01` | verified | `src\core\OfficialEase.cpp:29` `bezier01` |
| 142 | `sampleX` | verified |  |
| 146 | `sampleY` | verified |  |
| 150 | `derivativeX` | verified | `src\core\OfficialEase.cpp:28` `derivativeX` |

## `com\wuying\phigros\game\OfficialLevelEffects.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 100 | `OfficialLevelEffects` | verified |  |
| 108 | `isAvailable` | verified | `src\platform\FileDialog.cpp:131` `available` (+3) |
| 117 | `loadRoot` | verified |  |
| 127 | `loadByChart` | fixed |  |
| 159 | `loadByName` | excluded |  |
| 166 | `readEffectTables` | fixed |  |
| 249 | `slashCount` | fixed |  |
| 259 | `slashAt` | fixed |  |
| 287 | `slashLightSetting` | verified |  |
| 299 | `evaluateTrack` | verified | `src\core\OfficialLevelEffects.cpp:99` `evaluateTrack` |
| 331 | `startValue` | verified |  |
| 333 | `endValue` | fixed |  |
| 334 | `track` | fixed | `src\core\OfficialLevelEffects.cpp:225` `track` (+1) |
| 342 | `hasTrack` | verified | `src\core\OfficialLevelEffects.cpp:225` `track` (+1) |
| 348 | `scalar` | fixed | `src\core\OfficialLevelEffects.cpp:235` `scalar` |
| 355 | `settingParams` | verified |  |
| 364 | `effectSettings` | verified |  |
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
| 709 | `dogConsts` | verified |  |
| 725 | `slashUniforms` | verified |  |
| 737 | `slashTracks` | verified |  |
| 749 | `addSlashVars` | verified |  |
| 767 | `slashKeys` | verified |  |
| 773 | `posEvents` | verified |  |

## `com\wuying\phigros\game\PecChartParser.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `PecChartParser` | verified |  |
| 23 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 258 | `clampEase` | verified | `src\chart\PecChartParser.cpp:85` `clampEase` |
| 264 | `parseNote` | verified | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 307 | `toNotePosX` | verified |  |
| 311 | `stripComments` | verified |  |
| 317 | `parseDouble` | verified |  |
| 326 | `round4` | verified |  |
| 339 | `lb` | fixed |  |
| 352 | `SpeedPoint` | verified |  |
| 361 | `BpmPoint` | verified |  |
| 366 | `buildSpeedEvents` | fixed |  |
| 400 | `normalizeMoveEvents` | verified |  |
| 425 | `normalizeLineEvents` | verified |  |
| 447 | `applyAlphaSpecials` | verified | `src\chart\PecChartParser.cpp:183` `applyAlphaSpecials` |
| 488 | `assignVisibleTime` | verified | `src\chart\PecChartParser.cpp:136` `assignVisibleTime` |
| 545 | `convertNotesBeats` | verified |  |
| 573 | `convertMoveEventsBeats` | verified |  |
| 592 | `convertLineEventsBeats` | verified |  |
| 609 | `defaultMove` | verified |  |
| 622 | `defaultSpeed` | verified |  |
| 633 | `defaultLine` | verified |  |
| 651 | `BpmConverter` | verified | `src\chart\PecChartParser.cpp:55` `BpmConverter` (+1) |
| 670 | `baseBpm` | verified | `src\chart\PecChartParser.cpp:64` `baseBpm` (+1) |
| 677 | `bpmAtBeat` | verified | `src\chart\PecChartParser.cpp:66` `bpmAtBeat` |
| 685 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 695 | `beatToNormBeat` | verified | `src\chart\PecChartParser.cpp:71` `beatToNormBeat` |
| 702 | `findSegment` | fixed |  |

## `com\wuying\phigros\game\PlayResult.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `PlayResult` | excluded |  |
| 37 | `PlayResult` | excluded |  |
| 71 | `isFullCombo` | verified | `include\rechigros\game\PlayResult.hpp:18` `isFullCombo` |
| 75 | `isAllPerfect` | verified | `include\rechigros\game\PlayResult.hpp:20` `isAllPerfect` |

## `com\wuying\phigros\game\PrprEffect.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `PrprEffect` | fixed |  |
| 35 | `isActive` | verified | `include\rechigros\game\PracticeJudgeWindows.hpp:22` `active` |
| 41 | `PrprVar` | verified |  |
| 49 | `ConstFloat` | verified |  |
| 53 | `ConstFloat` | verified |  |
| 62 | `ConstVec` | verified |  |
| 71 | `FloatEvents` | verified |  |
| 75 | `safeEvents` | verified |  |

## `com\wuying\phigros\game\PrprVideo.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `PrprVideo` | excluded |  |
| 52 | `emptyEvents` | verified |  |
| 57 | `alphaAt` | verified | `src\render\fx\BadEffectField.cpp:8` `alphaAt` |
| 61 | `dimAt` | verified |  |
| 65 | `resetCursors` | verified | `src\core\JudgeLine.cpp:44` `resetCursors` |
| 72 | `eval` | fixed |  |
| 80 | `sanitizeScaleType` | verified | `include\rechigros\game\PrprEffect.hpp:48` `sanitizeScaleType` |
| 85 | `finiteOr` | verified |  |
| 89 | `clamp01` | verified | `src\core\Easing.cpp:142` `clamp01` (+6) |

## `com\wuying\phigros\game\ReplayData.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `ReplayData` | verified |  |
| 48 | `toPlayResult` | verified | `src\app\Replay.cpp:139` `toPlayResult` |
| 54 | `fromPlayResult` | verified |  |

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
| 413 | `parseJudgeLine` | verified | `src\chart\RpeChartParser.cpp:806` `parseJudgeLine` |
| 607 | `parse` | fixed | `src\chart\PecChartParser.cpp:248` `parse` (+4) |
| 664 | `getEventTime` | verified | `src\chart\RpeChartParser.cpp:105` `eventTime` |
| 675 | `parseOffsetSeconds` | verified |  |
| 688 | `parseRpeVersion` | verified |  |
| 701 | `parseBpmList` | verified |  |
| 717 | `parseAlphaEvents` | verified | `src\chart\RpeChartParser.cpp:226` `parseAlphaEvents` |
| 733 | `parseLineEvents` | verified | `src\chart\OfficialChartParser.cpp:33` `parseLineEvents` (+1) |
| 798 | `parseMoveEvents` | fixed | `src\chart\OfficialChartParser.cpp:36` `parseMoveEvents` (+1) |
| 936 | `parseSpeedEvents` | verified | `src\chart\RpeChartParser.cpp:621` `parseSpeedEvents` |
| 994 | `buildLegacySpeedHeightLayer` | verified | `src\chart\RpeChartParser.cpp:562` `buildLegacySpeedHeightLayer` |
| 1047 | `applyLegacySpeedHeightMode` | verified | `src\chart\RpeChartParser.cpp:614` `applyLegacySpeedHeightMode` |
| 1061 | `LegacySpeedCursor` | verified |  |
| 1065 | `valueAt` | verified |  |
| 1114 | `valueBefore` | verified |  |
| 1162 | `addSpeedEvent` | verified | `src\chart\RpeChartParser.cpp:545` `addSpeedEvent` |
| 1185 | `parseScaleIntoLine` | verified |  |
| 1233 | `parseInclineEvents` | verified |  |
| 1248 | `applyStaticSkew` | verified |  |
| 1260 | `parseStaticSkew` | verified |  |
| 1273 | `parseGifEvents` | verified |  |
| 1314 | `parseColorEvents` | verified |  |
| 1364 | `parseTextEvents` | verified |  |
| 1414 | `parseNotesIntoLine` | verified |  |
| 1465 | `parseRpeNoteAbove` | verified |  |
| 1479 | `parseRpeAboveFlag` | verified |  |
| 1516 | `parseNote` | verified | `src\chart\PecChartParser.cpp:87` `parseNote` (+1) |
| 1637 | `RpeNoteControlRaw` | verified |  |
| 1644 | `parseNoteControls` | verified |  |
| 1660 | `parseNoteControlsAny` | verified |  |
| 1672 | `parseNoteControlsAny` | verified |  |
| 1712 | `withValueFallbacks` | verified |  |
| 1723 | `mergeSpeedLayers` | verified | `src\chart\RpeChartParser.cpp:504` `mergeSpeedLayers` |
| 1777 | `SpeedLayerCursor` | verified |  |
| 1781 | `valueAtStart` | verified |  |
| 1826 | `valueBefore` | verified |  |
| 1869 | `speedEventAt` | verified |  |
| 1873 | `start` | fixed |  |
| 1874 | `end` | fixed |  |
| 1880 | `speedAt` | verified | `src\core\EventUtils.cpp:740` `speedAt` |
| 1885 | `speedAtLayersStart` | verified | `src\chart\RpeChartParser.cpp:362` `speedAtLayersStart` |
| 1894 | `speedAtLayersEnd` | verified | `src\chart\RpeChartParser.cpp:368` `speedAtLayersEnd` |
| 1901 | `addSampledMergedSpeedEvents` | verified | `src\chart\RpeChartParser.cpp:490` `addSampledMergedSpeedEvents` |
| 1916 | `addMergedSpeedEventFromVarying` | verified | `src\chart\RpeChartParser.cpp:459` `addMergedSpeedEventFromVarying` |
| 1952 | `speedEventProgress` | verified | `src\chart\RpeChartParser.cpp:390` `speedEventProgress` |
| 1962 | `subSpeedEasingWindow` | verified | `src\chart\RpeChartParser.cpp:399` `subSpeedEasingWindow` |
| 1977 | `hasSpeedBezier` | verified | `src\chart\RpeChartParser.cpp:375` `hasSpeedBezier` |
| 1983 | `evalSpeedBezier` | verified | `src\chart\RpeChartParser.cpp:380` `evalSpeedBezier` |
| 1995 | `buildSpeedSubBezierLut` | verified | `src\chart\RpeChartParser.cpp:411` `buildSpeedSubBezierLut` |
| 2015 | `addMergedSpeedEvent` | verified | `src\chart\RpeChartParser.cpp:429` `addMergedSpeedEvent` |
| 2053 | `ensureCoverageRotate` | verified |  |
| 2057 | `ensureCoverageAlpha` | verified |  |
| 2061 | `ensureCoveragePos` | verified |  |
| 2065 | `ensureCoverage1D` | verified |  |
| 2089 | `ensureCoverageMove` | verified |  |
| 2093 | `ensureCoverageMove` | verified |  |
| 2120 | `ensureCoverageSpeed` | verified |  |
| 2173 | `defaultMove` | verified |  |
| 2186 | `defaultSpeed` | verified |  |
| 2197 | `canPair` | verified |  |
| 2214 | `selectJudgeLineArray` | fixed |  |
| 2224 | `looksLikeJudgeLineArray` | verified |  |
| 2238 | `asObj` | verified |  |
| 2244 | `getAsArray` | verified |  |
| 2254 | `getAsObject` | verified |  |
| 2264 | `getFirst` | verified |  |
| 2273 | `getAsDouble` | fixed |  |
| 2285 | `getAsInt` | fixed |  |
| 2297 | `getAsBool` | fixed |  |
| 2316 | `getAsString` | verified |  |
| 2326 | `resolvePath` | verified |  |
| 2337 | `toXNorm` | verified |  |
| 2342 | `toYRaw` | verified |  |
| 2347 | `toNotePosX` | verified |  |
| 2352 | `parseBeat` | fixed | `src\chart\RpeChartParser.cpp:83` `parseBeat` |
| 2391 | `parseBezierPoints` | fixed | `src\core\EventUtils.cpp:16` `hasBezierPoints` |
| 2423 | `parseVec2` | fixed | `src\chart\RpeChartParser.cpp:235` `vec2` |
| 2443 | `parseRgb` | verified | `src\chart\RpeChartParser.cpp:672` `rgb` |
| 2483 | `clamp01f` | verified |  |
| 2493 | `BpmPoint` | verified |  |
| 2505 | `BpmConverter` | verified | `src\chart\PecChartParser.cpp:55` `BpmConverter` (+1) |
| 2524 | `baseBpm` | verified | `src\chart\PecChartParser.cpp:64` `baseBpm` (+1) |
| 2531 | `beatToSec` | fixed | `src\chart\PecChartParser.cpp:67` `beatToSec` (+2) |
| 2541 | `beatToNormBeat` | verified | `src\chart\PecChartParser.cpp:71` `beatToNormBeat` |
| 2548 | `findSegment` | fixed |  |
| 2565 | `toTimeline` | verified |  |

## `com\wuying\phigros\ui\AssetBitmaps.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 30 | `sizeOf` | verified |  |
| 42 | `AssetBitmaps` | verified |  |
| 44 | `preload` | verified |  |
| 58 | `get` | fixed | `src\render\text\TextRenderer.cpp:633` `get` |
| 81 | `cached` | excluded |  |
| 87 | `decodeAndCache` | verified |  |
| 95 | `decode` | excluded | `src\app\UiText.cpp:140` `decode` |

## `com\wuying\phigros\ui\CalibrationActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 59 | `Runnable` | excluded |  |
| 61 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 71 | `onCreate` | excluded |  |
| 95 | `buildUi` | verified |  |
| 199 | `addStepButton` | verified |  |
| 209 | `makeButton` | verified |  |
| 221 | `matchWrap` | verified |  |
| 227 | `setAudioOffsetMs` | verified |  |
| 237 | `refreshValueViews` | verified |  |
| 261 | `finishWithResult` | verified |  |
| 270 | `onBackPressed` | verified |  |
| 275 | `onPause` | verified |  |
| 282 | `onResume` | verified |  |
| 296 | `onDestroy` | excluded |  |
| 309 | `applyImmersiveMode` | excluded |  |
| 325 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 329 | `clamp` | fixed |  |
| 366 | `CalibrationView` | verified |  |
| 376 | `setAudioOffsetMs` | verified |  |
| 381 | `setPanelWidthPx` | verified |  |
| 385 | `resetSmoothClock` | verified |  |
| 392 | `onDraw` | excluded |  |
| 447 | `drawTap` | verified |  |
| 460 | `drawHitEffects` | verified | `src\app\CalibrationScreen.cpp:345` `drawHitEffects` (+1) |
| 510 | `drawTouchMarkers` | verified | `src\app\CalibrationScreen.cpp:409` `drawTouchMarkers` |
| 531 | `onTouchEvent` | verified |  |
| 550 | `currentNoteY` | verified |  |
| 563 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 567 | `currentLoopTime` | verified |  |
| 574 | `currentMarkerProgress` | verified |  |
| 578 | `rememberRenderedTapPosition` | verified |  |
| 587 | `judgeLineHalfWidth` | verified |  |
| 591 | `judgeLineY` | verified |  |
| 595 | `noteDrawProgressForLoopTime` | verified |  |
| 605 | `noteMarkerProgressForLoopTime` | verified |  |
| 612 | `tapYForProgress` | verified |  |
| 616 | `touchMarkerLineWidth` | verified |  |
| 620 | `clamp` | fixed |  |
| 626 | `perfectColor` | verified |  |
| 630 | `indicatorColor` | verified |  |
| 635 | `rgb` | fixed | `src\chart\RpeChartParser.cpp:672` `rgb` |
| 642 | `loadAssetBitmap` | verified |  |
| 651 | `splitHitFxFrames` | verified |  |
| 669 | `loadHitFxGrid` | verified |  |
| 686 | `TouchMarker` | verified |  |
| 704 | `reset` | fixed | `include\rechigros\audio\Mixer.hpp:147` `reset` (+9) |
| 713 | `getSeconds` | verified |  |
| 760 | `resetBiasHistory` | verified |  |
| 766 | `addBias` | verified |  |
| 792 | `CalibrationHitEffect` | verified |  |

## `com\wuying\phigros\ui\ChartSelectActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 244 | `onCreate` | excluded |  |
| 267 | `onSaveInstanceState` | excluded |  |
| 288 | `onNewIntent` | verified |  |
| 295 | `onResume` | verified |  |
| 306 | `refreshLocalChartsOnResume` | verified |  |
| 321 | `onPause` | verified |  |
| 330 | `onDestroy` | excluded |  |
| 338 | `onWindowFocusChanged` | verified |  |
| 344 | `onBackPressed` | verified |  |
| 364 | `setupRoot` | excluded |  |
| 394 | `restoreUiState` | excluded |  |
| 423 | `renderRestoredScreen` | excluded |  |
| 437 | `findLocalRecordById` | verified |  |
| 446 | `applyImmersiveMode` | excluded |  |
| 467 | `renderHome` | verified |  |
| 571 | `applyHomeLayout` | verified |  |
| 653 | `openGithub` | verified |  |
| 661 | `menuButtonLp` | verified |  |
| 667 | `menuButton` | verified |  |
| 676 | `renderChartList` | verified |  |
| 721 | `OnChildAttachStateChangeListener` | excluded |  |
| 722 | `onChildViewAttachedToWindow` | excluded |  |
| 732 | `onChildViewDetachedFromWindow` | excluded |  |
| 799 | `baseScreen` | excluded |  |
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
| 955 | `notifyVisibleChartItems` | excluded |  |
| 966 | `updateVisibleLocalCards` | excluded |  |
| 976 | `updateVisibleLocalCard` | excluded |  |
| 984 | `applyVisibleLocalCard` | verified |  |
| 994 | `updateSelectionStateView` | verified |  |
| 1007 | `addOnlinePageControls` | excluded |  |
| 1034 | `addSideIcon` | verified |  |
| 1048 | `addDetailTopIcon` | verified |  |
| 1062 | `renderSettings` | verified |  |
| 1119 | `OnPreDrawListener` | excluded |  |
| 1122 | `onPreDraw` | excluded |  |
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
| 1311 | `settingsCard` | verified |  |
| 1323 | `syncSettingsToggles` | verified |  |
| 1332 | `addSwitch` | verified |  |
| 1349 | `addSliderRow` | verified |  |
| 1377 | `addEditRow` | verified |  |
| 1396 | `SimpleWatcher` | excluded |  |
| 1397 | `afterTextChanged` | excluded |  |
| 1403 | `addActionRow` | verified |  |
| 1420 | `addDualActions` | verified |  |
| 1435 | `addReplayRow` | verified | `src\app\SettingsScreen.cpp:558` `replayRow` |
| 1459 | `rowBase` | verified |  |
| 1466 | `smallButton` | verified |  |
| 1483 | `applyDetailBackground` | verified |  |
| 1514 | `clearDetailBackground` | verified |  |
| 1519 | `renderDetail` | verified |  |
| 1635 | `updateDetailTextSafety` | verified |  |
| 1648 | `DetailCanvasHost` | excluded |  |
| 1660 | `textInset` | verified |  |
| 1664 | `updateTextSafety` | verified |  |
| 1696 | `onMeasure` | excluded |  |
| 1713 | `onLayout` | verified |  |
| 1729 | `SettingsBackdrop` | verified |  |
| 1730 | `onDraw` | excluded |  |
| 1742 | `addDetailText` | verified |  |
| 1751 | `showSearchDialog` | verified |  |
| 1781 | `showPageJumpDialog` | verified |  |
| 1801 | `showDetailInfoDialog` | verified |  |
| 1816 | `showSkinSelectDialog` | verified |  |
| 1829 | `deleteSelectedSkin` | verified |  |
| 1844 | `selectedSkinName` | verified |  |
| 1851 | `hitOffsetLabel` | verified | `src\app\SettingsScreen.cpp:370` `hitOffsetLabel` |
| 1858 | `cycleHitOffsetMode` | verified |  |
| 1864 | `loadOnlineIndex` | excluded |  |
| 1899 | `confirmDownload` | excluded |  |
| 1908 | `downloadChart` | excluded |  |
| 1939 | `ensureOnlineCover` | excluded |  |
| 1979 | `filterLocal` | verified |  |
| 1991 | `filterOnline` | excluded |  |
| 2004 | `toggleLocalSelection` | verified |  |
| 2018 | `updateLocalSelectionState` | verified |  |
| 2024 | `toggleSelectAllLocal` | verified |  |
| 2037 | `confirmDeleteSelected` | verified |  |
| 2057 | `startSelectedRecord` | verified |  |
| 2061 | `startSelectedRecord` | verified |  |
| 2071 | `playReplay` | verified | `src\app\ResultScreen.cpp:378` `playReplay` |
| 2098 | `startPlay` | verified |  |
| 2102 | `startPlay` | verified |  |
| 2115 | `putPlaySettings` | verified |  |
| 2157 | `handleIncomingIntent` | verified |  |
| 2203 | `describeFile` | verified |  |
| 2211 | `initLaunchers` | verified |  |
| 2302 | `tryUpdateSongInfo` | verified |  |
| 2317 | `launchPickZip` | verified |  |
| 2321 | `launchPickMusic` | verified |  |
| 2325 | `launchPickChart` | verified |  |
| 2329 | `launchPickBg` | verified |  |
| 2333 | `launchPickSkin` | verified |  |
| 2337 | `launchPickReplayZip` | verified |  |
| 2341 | `selectLocalChart` | verified |  |
| 2352 | `getSelectedChartId` | verified |  |
| 2357 | `clearSelectedChartIfDeleted` | verified |  |
| 2369 | `onOnlineChartImported` | excluded |  |
| 2374 | `getSelectedSkinIndex` | verified |  |
| 2378 | `setSelectedSkinIndex` | verified |  |
| 2382 | `getMusicFile` | verified |  |
| 2384 | `getChartFile` | verified |  |
| 2385 | `getBgFile` | verified |  |
| 2386 | `getSongName` | verified |  |
| 2387 | `getDifficulty` | verified |  |
| 2388 | `getChartOffsetMs` | verified |  |
| 2389 | `setLocalChartDetailVisible` | verified |  |
| 2392 | `updateStartButtonState` | verified |  |
| 2395 | `setSongName` | verified |  |
| 2400 | `setDifficulty` | verified |  |
| 2405 | `setChartOffsetMs` | verified |  |
| 2410 | `persistSelectedChartInfo` | verified |  |
| 2419 | `getAspectRatio` | verified |  |
| 2421 | `setAspectRatio` | verified |  |
| 2422 | `getKeyScale` | verified |  |
| 2423 | `setKeyScale` | verified |  |
| 2424 | `getScrollSpeed` | verified |  |
| 2425 | `setScrollSpeed` | verified |  |
| 2426 | `isMirrorX` | verified | `include\rechigros\render\math\Mat4.hpp:58` `mirrorX` |
| 2427 | `setMirrorX` | verified | `include\rechigros\render\math\Mat4.hpp:58` `mirrorX` |
| 2428 | `isAutoplay` | verified | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 2429 | `setAutoplay` | verified | `include\rechigros\game\GameplaySession.hpp:47` `autoplay` (+2) |
| 2438 | `isChallengeMode` | verified | `include\rechigros\game\JudgementEngine.hpp:43` `challengeMode` |
| 2439 | `setChallengeMode` | verified | `include\rechigros\game\JudgementEngine.hpp:43` `challengeMode` |
| 2440 | `getBackgroundDim` | verified |  |
| 2441 | `setBackgroundDim` | verified |  |
| 2442 | `setJudgeLineWidth` | verified |  |
| 2443 | `isLowResMode` | verified |  |
| 2444 | `setLowResMode` | verified |  |
| 2445 | `isAntialias` | verified |  |
| 2446 | `setAntialias` | excluded |  |
| 2447 | `isShowFps` | verified |  |
| 2448 | `setShowFps` | verified |  |
| 2449 | `isRealtimeAcc` | verified |  |
| 2450 | `setRealtimeAcc` | verified |  |
| 2451 | `isScoreJumpEffect` | verified |  |
| 2452 | `setScoreJumpEffect` | verified |  |
| 2453 | `isDisableShader` | verified |  |
| 2454 | `setDisableShader` | verified |  |
| 2455 | `isStoryMode` | verified |  |
| 2456 | `setStoryMode` | verified |  |
| 2457 | `isShowDebugInfo` | verified |  |
| 2458 | `setShowDebugInfo` | verified |  |
| 2459 | `isChartReveal` | verified |  |
| 2460 | `setChartReveal` | verified |  |
| 2469 | `isMultiPressHighlight` | verified |  |
| 2470 | `setMultiPressHighlight` | verified |  |
| 2471 | `isApfcIndicator` | verified |  |
| 2472 | `setApfcIndicator` | verified |  |
| 2473 | `isJudgementDetailDistribution` | verified | `include\rechigros\game\JudgementEngine.hpp:40` `setJudgementDetailDistribution` |
| 2474 | `setJudgementDetailDistribution` | verified | `include\rechigros\game\JudgementEngine.hpp:40` `setJudgementDetailDistribution` |
| 2475 | `isHitOffsetIndicator` | verified | `src\core\HitOffsetIndicator.cpp:9` `HitOffsetIndicator` |
| 2476 | `setHitOffsetIndicator` | verified | `src\core\HitOffsetIndicator.cpp:9` `HitOffsetIndicator` |
| 2477 | `getHitOffsetIndicatorMode` | verified |  |
| 2478 | `setHitOffsetIndicatorMode` | verified |  |
| 2484 | `getMusicVolumePct` | verified |  |
| 2485 | `setMusicVolumePct` | verified |  |
| 2486 | `getSfxVolumePct` | verified |  |
| 2487 | `setSfxVolumePct` | verified |  |
| 2488 | `getMusicSpeed` | verified | `src\core\GameplaySession.cpp:66` `setMusicSpeed` |
| 2489 | `setMusicSpeed` | verified | `src\core\GameplaySession.cpp:66` `setMusicSpeed` |
| 2490 | `getAudioOffsetMs` | verified |  |
| 2491 | `setAudioOffsetMs` | verified |  |
| 2492 | `isReplayEnabled` | verified |  |
| 2493 | `setReplayEnabled` | verified |  |
| 2494 | `loadPersistedSettings` | verified |  |
| 2534 | `getPrefs` | verified |  |
| 2539 | `isApfcIndicatorEnabled` | verified |  |
| 2545 | `editPrefs` | verified |  |
| 2549 | `isReadyToStart` | verified |  |
| 2555 | `iconButton` | verified | `src\app\UiStyle.cpp:192` `iconButton` |
| 2565 | `glassPanel` | verified | `src\app\UiLayout.cpp:182` `glassPanel` |
| 2575 | `rectPanel` | verified | `src\app\UiLayout.cpp:191` `rectPanel` |
| 2581 | `text` | fixed |  |
| 2585 | `text` | fixed |  |
| 2598 | `autoSingleLineText` | verified |  |
| 2604 | `uiTypeface` | verified |  |
| 2616 | `assetImage` | verified |  |
| 2622 | `loadAssetBitmap` | verified |  |
| 2631 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 2635 | `tryTakePersistableRead` | excluded |  |
| 2642 | `hasLowerSuffix` | verified |  |
| 2646 | `errorMessage` | excluded |  |
| 2672 | `clampPctToMul` | verified |  |
| 2676 | `sanitizeAspectRatio` | verified | `src\app\Settings.cpp:47` `sanitizeAspectRatio` |
| 2682 | `clampFinite` | verified |  |
| 2689 | `lerp` | fixed | `src\core\EventUtils.cpp:235` `lerp` |
| 2693 | `easeOutCubic` | verified | `src\app\SettingsScreen.cpp:303` `easeOutCubic` |
| 2698 | `clampInt` | verified |  |
| 2704 | `parseFloatSafe` | verified |  |
| 2713 | `parseIntSafe` | verified |  |
| 2722 | `formatFloat` | verified |  |
| 2726 | `fallback` | fixed |  |
| 2730 | `nonEmpty` | verified | `src\app\LibraryScreen.cpp:159` `nonEmpty` |
| 2734 | `shouldMarquee` | verified | `src\app\LibraryScreen.cpp:157` `shouldMarquee` |
| 2738 | `restartMarquee` | verified |  |
| 2745 | `stableId` | verified |  |
| 2754 | `shortDifficulty` | verified |  |
| 2760 | `gradeName` | verified | `src\app\ChartLibrary.cpp:410` `gradeName` |
| 2770 | `safeFileName` | excluded |  |
| 2775 | `extensionOrJpg` | excluded |  |
| 2839 | `beforeTextChanged` | excluded |  |
| 2840 | `onTextChanged` | excluded |  |
| 2855 | `ChartTabBar` | verified |  |
| 2859 | `ChartTabBar` | verified |  |
| 2877 | `setState` | excluded | `src\app\LibraryScreen.cpp:214` `state` |
| 2884 | `setOnTabSelected` | verified |  |
| 2888 | `onDraw` | excluded |  |
| 2927 | `performClick` | verified |  |
| 2933 | `onTouchEvent` | verified |  |
| 2944 | `drawSkewRect` | verified |  |
| 2957 | `tabProgress` | verified |  |
| 2964 | `selectedAmount` | verified |  |
| 2971 | `centerFor` | verified |  |
| 2975 | `widthFor` | verified |  |
| 2986 | `ChartCardAdapter` | verified |  |
| 2992 | `ChartCardAdapter` | verified |  |
| 3003 | `getItemId` | verified |  |
| 3013 | `findLocalPosition` | verified |  |
| 3021 | `onCreateViewHolder` | excluded |  |
| 3090 | `onBindViewHolder` | excluded |  |
| 3095 | `onBindViewHolder` | excluded |  |
| 3100 | `bindHolder` | verified |  |
| 3149 | `getItemCount` | excluded |  |
| 3154 | `applyLocalState` | verified |  |
| 3173 | `Vh` | verified |  |
| 3188 | `ChartCardFrame` | verified |  |
| 3191 | `onMeasure` | excluded |  |

## `com\wuying\phigros\ui\CoverImageLoader.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `sizeOf` | verified |  |
| 28 | `CoverImageLoader` | verified |  |
| 30 | `load` | fixed | `src\app\CoverCache.cpp:189` `load` (+3) |
| 64 | `preload` | verified |  |
| 80 | `cacheKey` | excluded |  |
| 85 | `decodeSampled` | verified |  |

## `com\wuying\phigros\ui\GameDialog.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 62 | `GameDialog` | excluded |  |
| 83 | `Builder` | excluded |  |
| 85 | `setTitle` | verified |  |
| 87 | `setTitle` | verified |  |
| 88 | `setMessage` | verified |  |
| 89 | `setMessage` | verified |  |
| 90 | `setView` | excluded | `src\platform\Gif.cpp:30` `view` |
| 91 | `setItems` | verified |  |
| 96 | `setPositiveButton` | verified |  |
| 99 | `setPositiveButton` | verified |  |
| 102 | `setNegativeButton` | verified |  |
| 105 | `setNegativeButton` | verified |  |
| 108 | `setNeutralButton` | verified |  |
| 111 | `setNeutralButton` | verified |  |
| 114 | `setCancelable` | verified |  |
| 115 | `setOnDismissListener` | verified |  |
| 118 | `create` | excluded |  |
| 124 | `show` | verified |  |
| 131 | `onCreate` | excluded |  |
| 141 | `configureWindow` | verified |  |
| 177 | `show` | verified |  |
| 187 | `dismiss` | fixed |  |
| 205 | `AnimatorListenerAdapter` | excluded |  |
| 206 | `onAnimationEnd` | excluded |  |
| 213 | `finishDismiss` | excluded |  |
| 222 | `runEnter` | excluded |  |
| 234 | `AnimatorListenerAdapter` | excluded |  |
| 235 | `onAnimationEnd` | excluded |  |
| 241 | `applyProgress` | verified | `src\app\UiLayout.cpp:451` `progress` |
| 255 | `onStop` | excluded |  |
| 261 | `buildContent` | verified |  |
| 378 | `weightedWrap` | excluded |  |
| 386 | `styleCustomView` | verified |  |
| 401 | `text` | fixed |  |
| 411 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 420 | `ButtonSpec` | verified |  |
| 442 | `DialogPanel` | excluded |  |
| 458 | `addButton` | excluded |  |
| 464 | `panelWidth` | excluded |  |
| 469 | `onMeasure` | excluded |  |
| 503 | `onLayout` | verified |  |
| 522 | `rebuildPaths` | excluded |  |
| 546 | `applySlant` | excluded |  |
| 574 | `onDraw` | excluded |  |
| 584 | `onTouchEvent` | verified |  |
| 595 | `ButtonCell` | excluded |  |
| 618 | `onDraw` | excluded |  |
| 632 | `dispatchSetPressed` | verified |  |
| 638 | `setPressed` | verified |  |
| 652 | `PressHighlight` | verified |  |
| 654 | `isStateful` | excluded |  |
| 656 | `onStateChange` | excluded |  |
| 665 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 682 | `setAlpha` | verified |  |
| 684 | `setColorFilter` | verified |  |
| 685 | `getOpacity` | verified |  |
| 693 | `ItemRow` | excluded |  |
| 695 | `setShift` | excluded |  |
| 708 | `onDraw` | excluded |  |
| 723 | `SlantBox` | excluded |  |
| 728 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 744 | `setAlpha` | verified |  |
| 746 | `setColorFilter` | verified |  |
| 747 | `getOpacity` | verified |  |

## `com\wuying\phigros\ui\GameTextOverlay.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `GameTextOverlay` | verified |  |
| 28 | `configure` | verified |  |
| 37 | `setColors` | verified |  |
| 42 | `captureExternalColor` | verified |  |
| 45 | `draw` | fixed | `src\render\fx\BadEffectField.cpp:41` `draw` (+10) |
| 79 | `setAlpha` | verified |  |
| 80 | `setColorFilter` | verified |  |
| 81 | `getOpacity` | verified |  |
| 82 | `isStateful` | excluded |  |
| 83 | `onStateChange` | excluded |  |

## `com\wuying\phigros\ui\GameTextRenderer.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 26 | `invalidate` | excluded | `include\rechigros\app\ChartLibrary.hpp:70` `invalidate` (+1) |
| 28 | `prepare` | excluded | `src\app\TriGridBackdrop.cpp:217` `prepare` (+8) |
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
| 27 | `GameTextView` | excluded |  |
| 29 | `GameTextView` | excluded |  |
| 30 | `GameTextView` | excluded |  |
| 48 | `setDesignTextSize` | verified |  |
| 56 | `setFitWidth` | fixed |  |
| 60 | `setTextSize` | verified |  |
| 61 | `setTextSize` | verified |  |
| 67 | `getTextSize` | verified |  |
| 70 | `requestLayout` | excluded |  |
| 74 | `onTextChanged` | excluded |  |
| 82 | `hasFixedSize` | verified |  |
| 86 | `onConfigurationChanged` | excluded |  |
| 90 | `displayText` | verified |  |
| 99 | `alignment` | verified |  |
| 106 | `prepare` | excluded | `src\app\TriGridBackdrop.cpp:217` `prepare` (+8) |
| 115 | `onMeasure` | excluded |  |
| 136 | `textTop` | verified |  |
| 142 | `getBaseline` | verified |  |
| 145 | `onDraw` | excluded |  |
| 175 | `animationsEnabled` | excluded |  |
| 180 | `drawCompoundDrawables` | excluded |  |
| 192 | `setSelected` | excluded |  |

## `com\wuying\phigros\ui\JudgementHistogramView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 41 | `JudgementHistogramView` | excluded |  |
| 46 | `JudgementHistogramView` | excluded |  |
| 51 | `init` | fixed |  |
| 57 | `setHistogram` | verified |  |
| 68 | `setTitle` | verified |  |
| 73 | `setContentInsets` | verified |  |
| 83 | `setHistogramTypeface` | verified |  |
| 88 | `setSourceBitmap` | verified |  |
| 94 | `onSizeChanged` | excluded |  |
| 105 | `onDetachedFromWindow` | excluded |  |
| 111 | `onDraw` | excluded |  |
| 127 | `drawBlurredBackdrop` | verified | `src\app\ResultScreen.cpp:322` `drawBlurredBackdrop` |
| 142 | `drawPanel` | verified | `src\app\CalibrationScreen.cpp:511` `drawPanel` (+1) |
| 173 | `drawBars` | verified |  |
| 213 | `drawPeakTag` | verified |  |
| 257 | `drawAxisLabels` | verified |  |
| 272 | `effectiveMaxBarHeight` | verified |  |
| 277 | `ensureBlurredBitmap` | verified |  |
| 302 | `recycleBlurredBitmap` | verified |  |
| 311 | `boxBlur` | verified |  |
| 371 | `formatSignedMs` | verified |  |
| 376 | `formatAxisMs` | verified |  |
| 381 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 385 | `withAlpha` | verified |  |
| 389 | `clamp` | fixed |  |
| 395 | `pack` | fixed |  |

## `com\wuying\phigros\ui\LocalChartAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 33 | `LocalChartAdapter` | verified |  |
| 37 | `setItems` | verified |  |
| 47 | `setManageMode` | verified |  |
| 54 | `isManageMode` | verified |  |
| 58 | `selectedIds` | verified |  |
| 63 | `onCreateViewHolder` | excluded |  |
| 70 | `onBindViewHolder` | excluded |  |
| 97 | `getItemCount` | excluded |  |
| 109 | `Vh` | verified |  |

## `com\wuying\phigros\ui\OnlineChartAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 27 | `Item` | fixed |  |
| 43 | `OnlineChartAdapter` | excluded |  |
| 47 | `setItems` | verified |  |
| 53 | `addItems` | excluded |  |
| 59 | `setQuery` | excluded |  |
| 64 | `hasQuery` | excluded |  |
| 68 | `visibleCount` | excluded |  |
| 72 | `totalCount` | excluded |  |
| 76 | `notifyItemUpdated` | excluded |  |
| 85 | `onCreateViewHolder` | excluded |  |
| 92 | `onBindViewHolder` | excluded |  |
| 103 | `getItemCount` | excluded |  |
| 108 | `filter` | fixed | `src\render\gl\Texture.cpp:69` `setFilter` |
| 122 | `buildMeta` | excluded |  |
| 139 | `Vh` | verified |  |

## `com\wuying\phigros\ui\PlayActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 176 | `extraBool` | excluded |  |
| 191 | `onCreate` | excluded |  |
| 256 | `OnBackPressedCallback` | verified |  |
| 258 | `handleOnBackPressed` | verified |  |
| 280 | `setLoadingUi` | verified |  |
| 334 | `loadAndStart` | verified |  |
| 469 | `forceGc` | excluded |  |
| 480 | `hintSurfaceFrameRateIfPossible` | excluded |  |
| 501 | `Callback` | excluded |  |
| 503 | `surfaceCreated` | excluded |  |
| 513 | `surfaceChanged` | excluded |  |
| 514 | `surfaceDestroyed` | excluded |  |
| 518 | `buildOverlayUiOn` | excluded |  |
| 591 | `startUiTicker` | excluded |  |
| 594 | `Runnable` | excluded |  |
| 595 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 625 | `stopUiTicker` | excluded |  |
| 632 | `applyOverlayStageLayout` | verified |  |
| 687 | `officialHudUnitPx` | verified |  |
| 691 | `officialHudHalfUiW` | fixed |  |
| 696 | `officialHudX` | verified |  |
| 700 | `officialHudY` | verified |  |
| 704 | `officialHudSize` | verified |  |
| 708 | `officialHudBottomTextYOffsetPx` | verified |  |
| 712 | `onBackPressed` | verified |  |
| 725 | `tryLoadSfxRawWav` | verified |  |
| 736 | `tryLoadSfxFromSkinDir` | verified |  |
| 744 | `tryLoadSfxFromSkinFile` | verified |  |
| 763 | `onPause` | verified |  |
| 775 | `onWindowFocusChanged` | verified |  |
| 788 | `onResume` | verified |  |
| 798 | `onDestroy` | excluded |  |
| 816 | `onExit` | verified |  |
| 830 | `onFinished` | fixed |  |
| 940 | `onStageRectChanged` | verified |  |
| 949 | `onResourcesReady` | verified |  |
| 975 | `applyImmersiveMode` | excluded |  |
| 996 | `applyHighRefreshRate` | excluded |  |

## `com\wuying\phigros\ui\ReplayManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 49 | `ReplayInfo` | excluded |  |
| 59 | `ReplayManager` | excluded |  |
| 61 | `getPrefs` | verified |  |
| 67 | `isReplayEnabled` | verified |  |
| 71 | `setReplayEnabled` | verified |  |
| 75 | `startRecording` | verified |  |
| 81 | `finishRecording` | verified |  |
| 100 | `clearCachedReplay` | verified |  |
| 105 | `getCachedReplayFile` | verified |  |
| 109 | `getReplaysRoot` | verified |  |
| 115 | `getReplayDir` | verified |  |
| 119 | `getChartFile` | verified |  |
| 131 | `getMusicFile` | verified |  |
| 143 | `getBgFile` | verified |  |
| 155 | `isReplayAlreadySaved` | verified |  |
| 171 | `getReplayData` | verified |  |
| 181 | `getReplayInfo` | verified |  |
| 292 | `getReplayList` | verified |  |
| 328 | `setFavorite` | verified |  |
| 338 | `deleteReplay` | verified |  |
| 497 | `isLikelyReplayPackage` | verified |  |
| 595 | `deleteRecursive` | verified |  |
| 626 | `isInsideDir` | verified |  |
| 636 | `relativizePath` | verified |  |
| 647 | `findFile` | verified |  |
| 661 | `findChartFileInDir` | verified |  |
| 677 | `findAudioFileInDir` | verified |  |
| 694 | `findImageFileInDir` | verified |  |

## `com\wuying\phigros\ui\ResultActivity.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 81 | `iconIndex` | verified |  |
| 92 | `onCreate` | excluded |  |
| 304 | `stylePanels` | verified |  |
| 323 | `illustrationScrim` | verified |  |
| 327 | `ShaderFactory` | verified |  |
| 328 | `resize` | excluded | `src\render\GameRenderer.cpp:1096` `resize` (+2) |
| 337 | `styleCornerButton` | verified |  |
| 350 | `styleChipButton` | verified |  |
| 358 | `bindPressFeedback` | verified |  |
| 373 | `setupModeTags` | verified |  |
| 405 | `formatMultiplier` | verified | `src\app\ResultScreen.cpp:285` `formatMultiplier` |
| 409 | `addModeTag` | verified |  |
| 436 | `ModeTagFlow` | verified |  |
| 441 | `onMeasure` | excluded |  |
| 461 | `onLayout` | verified |  |
| 483 | `layoutRow` | verified |  |
| 494 | `setupResponsiveCanvas` | verified |  |
| 551 | `saveReplay` | verified | `src\app\ResultScreen.cpp:356` `saveReplay` |
| 588 | `applyPhigrosFont` | verified |  |
| 593 | `setupJudgementHistogramOverlay` | verified |  |
| 631 | `toggleJudgementHistogram` | verified |  |
| 639 | `showJudgementHistogram` | verified |  |
| 651 | `hideJudgementHistogram` | verified |  |
| 662 | `loadGradeImage` | verified |  |
| 670 | `loadButtonImage` | verified |  |
| 676 | `loadImageFromAssets` | verified |  |
| 691 | `setupFullscreen` | excluded |  |
| 718 | `blurBackground` | verified |  |
| 748 | `gaussianBlur` | verified |  |
| 808 | `clamp` | fixed |  |
| 810 | `pack` | fixed |  |
| 811 | `allEase` | verified | `src\app\ResultScreen.cpp:63` `allEase` |
| 820 | `delayedSquare` | verified | `src\app\ResultScreen.cpp:74` `delayedSquare` |
| 827 | `levelSizeEase` | verified | `src\app\ResultScreen.cpp:81` `levelSizeEase` |
| 832 | `buttonEase` | verified | `src\app\ResultScreen.cpp:86` `buttonEase` |
| 845 | `AlphaTarget` | verified |  |
| 846 | `apply` | fixed | `src\core\Easing.cpp:248` `apply` (+2) |
| 848 | `childAlphaTargets` | verified |  |
| 860 | `startEnterAnimation` | verified |  |
| 922 | `onDestroy` | excluded |  |
| 928 | `onWindowFocusChanged` | verified |  |
| 934 | `onBackPressed` | verified |  |

## `com\wuying\phigros\ui\SelectAudioFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 37 | `onCreate` | excluded |  |
| 50 | `onCreateView` | excluded |  |
| 56 | `onViewCreated` | excluded |  |
| 83 | `SimpleWatcher` | excluded |  |
| 84 | `afterTextChanged` | excluded |  |
| 111 | `SimpleWatcher` | excluded |  |
| 112 | `afterTextChanged` | excluded |  |
| 138 | `SimpleWatcher` | excluded |  |
| 139 | `afterTextChanged` | excluded |  |
| 167 | `SimpleWatcher` | excluded |  |
| 168 | `afterTextChanged` | excluded |  |
| 190 | `onResume` | verified |  |
| 196 | `refreshFromActivity` | verified |  |
| 214 | `parseIntSafe` | verified |  |
| 224 | `parseFloatSafe` | verified |  |
| 234 | `clamp` | fixed |  |
| 238 | `clampInt` | verified |  |
| 242 | `formatFloat` | verified |  |
| 246 | `host` | excluded |  |
| 252 | `beforeTextChanged` | excluded |  |
| 253 | `onTextChanged` | excluded |  |

## `com\wuying\phigros\ui\SelectChartFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 34 | `onCreateView` | excluded |  |
| 40 | `onViewCreated` | excluded |  |
| 64 | `SimpleWatcher` | excluded |  |
| 65 | `afterTextChanged` | excluded |  |
| 73 | `SimpleWatcher` | excluded |  |
| 74 | `afterTextChanged` | excluded |  |
| 82 | `SimpleWatcher` | excluded |  |
| 83 | `afterTextChanged` | excluded |  |
| 100 | `onResume` | verified |  |
| 106 | `refreshFromActivity` | verified |  |
| 153 | `host` | excluded |  |
| 157 | `appendLoadedItem` | verified |  |
| 164 | `beforeTextChanged` | excluded |  |
| 165 | `onTextChanged` | excluded |  |

## `com\wuying\phigros\ui\SelectLocalChartsFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 57 | `onCreateView` | excluded |  |
| 63 | `onViewCreated` | excluded |  |
| 85 | `Listener` | excluded |  |
| 87 | `onOpen` | verified |  |
| 91 | `onSelectionChanged` | verified |  |
| 100 | `OnScrollListener` | excluded |  |
| 101 | `onScrolled` | excluded |  |
| 126 | `onResume` | verified |  |
| 132 | `onDestroy` | excluded |  |
| 138 | `refreshFromActivity` | verified |  |
| 163 | `openRecord` | verified |  |
| 177 | `handleBackPressed` | verified |  |
| 189 | `showRecordDetail` | verified |  |
| 198 | `populateDetail` | verified |  |
| 221 | `showList` | verified |  |
| 225 | `showList` | verified |  |
| 234 | `setManageMode` | verified |  |
| 240 | `updateManageActions` | verified |  |
| 257 | `updateStateText` | verified |  |
| 269 | `setHeaderCollapsed` | excluded |  |
| 292 | `confirmDeleteSelected` | verified |  |
| 309 | `exportSelected` | verified |  |
| 329 | `attachTextWatchers` | excluded |  |
| 332 | `SimpleWatcher` | excluded |  |
| 333 | `afterTextChanged` | excluded |  |
| 341 | `SimpleWatcher` | excluded |  |
| 342 | `afterTextChanged` | excluded |  |
| 349 | `SimpleWatcher` | excluded |  |
| 350 | `afterTextChanged` | excluded |  |
| 363 | `setTextIfDifferent` | excluded |  |
| 370 | `runOnUiThread` | excluded |  |
| 377 | `host` | excluded |  |
| 383 | `beforeTextChanged` | excluded |  |
| 384 | `onTextChanged` | excluded |  |

## `com\wuying\phigros\ui\SelectOnlineChartsFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 70 | `onCreateView` | excluded |  |
| 76 | `onViewCreated` | excluded |  |
| 85 | `Listener` | excluded |  |
| 87 | `onDownload` | excluded |  |
| 91 | `onItemVisible` | excluded |  |
| 100 | `OnScrollListener` | excluded |  |
| 101 | `onScrolled` | excluded |  |
| 119 | `TextWatcher` | excluded |  |
| 120 | `beforeTextChanged` | excluded |  |
| 121 | `onTextChanged` | excluded |  |
| 122 | `afterTextChanged` | excluded |  |
| 131 | `onResume` | verified |  |
| 139 | `onDestroy` | excluded |  |
| 145 | `reloadOnline` | excluded |  |
| 158 | `resetOnlineState` | excluded |  |
| 167 | `fetchOnlineIndex` | excluded |  |
| 205 | `applyOnlineIndex` | excluded |  |
| 215 | `loadNextPage` | excluded |  |
| 238 | `fetchVisibleItemCover` | excluded |  |
| 253 | `confirmDownload` | excluded |  |
| 262 | `downloadChart` | excluded |  |
| 307 | `ensureCoverCached` | excluded |  |
| 325 | `maybeLoadMoreForSearch` | excluded |  |
| 332 | `updateProgress` | excluded | `src\app\UiLayout.cpp:451` `progress` |
| 338 | `setStateMessage` | excluded |  |
| 344 | `updateStateCount` | excluded |  |
| 350 | `setHeaderCollapsed` | excluded |  |
| 381 | `runOnUiThread` | excluded |  |
| 388 | `extensionOrJpg` | excluded |  |
| 401 | `safeFileName` | excluded |  |
| 407 | `errorMessage` | excluded |  |
| 438 | `host` | excluded |  |

## `com\wuying\phigros\ui\SelectPagerAdapter.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 8 | `SelectPagerAdapter` | verified |  |
| 12 | `createFragment` | verified |  |
| 22 | `getItemCount` | excluded |  |

## `com\wuying\phigros\ui\SelectReplayFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 60 | `onCreateView` | excluded |  |
| 66 | `onViewCreated` | excluded |  |
| 103 | `onResume` | verified |  |
| 109 | `enterSelectionMode` | verified |  |
| 113 | `enterSelectionMode` | verified |  |
| 123 | `exitSelectionMode` | verified |  |
| 130 | `handleExportClick` | verified |  |
| 146 | `handleDeleteClick` | verified |  |
| 166 | `refreshFromActivity` | verified |  |
| 173 | `refreshList` | verified |  |
| 196 | `updateActionButtons` | verified |  |
| 219 | `buildReplayItem` | verified |  |
| 343 | `toggleReplaySelection` | verified |  |
| 352 | `loadAssetBitmap` | verified |  |
| 360 | `playReplay` | verified | `src\app\ResultScreen.cpp:378` `playReplay` |
| 434 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 438 | `host` | excluded |  |

## `com\wuying\phigros\ui\SelectVideoFragment.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 71 | `onCreateView` | excluded |  |
| 77 | `onViewCreated` | excluded |  |
| 146 | `SimpleWatcher` | excluded |  |
| 147 | `afterTextChanged` | excluded |  |
| 171 | `SimpleWatcher` | excluded |  |
| 172 | `afterTextChanged` | excluded |  |
| 198 | `SimpleWatcher` | excluded |  |
| 199 | `afterTextChanged` | excluded |  |
| 251 | `onResume` | verified |  |
| 257 | `refreshSkinDropdown` | verified |  |
| 292 | `refreshFromActivity` | verified |  |
| 350 | `bindHitOffsetIndicatorModeDropdown` | verified |  |
| 363 | `syncAutoplayRevealSwitches` | verified |  |
| 370 | `applyPresetSelection` | verified |  |
| 392 | `updatePresetLabelForValue` | verified |  |
| 402 | `buildPresetLabels` | verified |  |
| 411 | `buildHitOffsetModeLabels` | verified |  |
| 419 | `getHitOffsetModeLabel` | verified |  |
| 425 | `getPresetLabel` | verified |  |
| 431 | `findPresetIndex` | verified |  |
| 438 | `getScreenAspect` | verified |  |
| 446 | `parseFloatSafe` | verified |  |
| 456 | `clamp` | fixed |  |
| 460 | `formatFloat` | verified |  |
| 464 | `host` | excluded |  |
| 470 | `beforeTextChanged` | excluded |  |
| 471 | `onTextChanged` | excluded |  |
| 476 | `Filter` | fixed | `src\render\gl\Texture.cpp:69` `setFilter` |
| 477 | `performFiltering` | excluded |  |
| 484 | `publishResults` | excluded |  |
| 493 | `NoFilterStringAdapter` | excluded |  |
| 498 | `getFilter` | verified | `src\render\gl\Texture.cpp:69` `setFilter` |

## `com\wuying\phigros\ui\SettingsPreviewView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 15 | `SettingsPreviewView` | excluded |  |
| 20 | `update` | fixed | `src\chart\Md5.cpp:84` `update` (+6) |
| 27 | `onDraw` | excluded |  |

## `com\wuying\phigros\ui\SettingsSliderView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 28 | `SettingsSliderView` | excluded |  |
| 39 | `setOnValueChanged` | verified |  |
| 41 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 43 | `scale` | fixed |  |
| 44 | `capWidth` | verified |  |
| 45 | `thumbWidth` | verified |  |
| 46 | `endInset` | verified |  |
| 47 | `thumbGap` | verified |  |
| 49 | `trackLeft` | verified |  |
| 53 | `trackRight` | verified |  |
| 54 | `left` | fixed | `include\rechigros\render\math\Mat4.hpp:85` `left` |
| 58 | `right` | fixed | `include\rechigros\render\math\Mat4.hpp:88` `right` |
| 62 | `parallelogram` | verified |  |
| 74 | `onDraw` | excluded |  |
| 96 | `updateValue` | verified | `src\chart\OfficialChartParser.cpp:20` `value` |
| 106 | `updateFromX` | verified |  |
| 111 | `onTouchEvent` | verified |  |
| 158 | `performClick` | verified |  |
| 160 | `onKeyDown` | excluded |  |
| 172 | `onInitializeAccessibilityNodeInfo` | excluded |  |
| 182 | `performAccessibilityAction` | excluded |  |

## `com\wuying\phigros\ui\SettingsToggleView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `SettingsToggleView` | excluded |  |
| 31 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |
| 33 | `block` | fixed |  |
| 45 | `onDraw` | excluded |  |
| 79 | `setChecked` | verified |  |
| 105 | `onDetachedFromWindow` | excluded |  |
| 112 | `onInitializeAccessibilityNodeInfo` | excluded |  |

## `com\wuying\phigros\ui\SkewFrameLayout.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 42 | `SkewFrameLayout` | excluded |  |
| 47 | `SkewFrameLayout` | excluded |  |
| 52 | `SkewFrameLayout` | excluded |  |
| 57 | `init` | fixed |  |
| 70 | `setPanelColors` | verified |  |
| 76 | `setSkewDegrees` | verified |  |
| 82 | `setStrokeWidthDp` | verified |  |
| 89 | `setClipContentToSkew` | excluded |  |
| 94 | `setStraightLeftEdge` | verified |  |
| 100 | `setStraightRightEdge` | verified |  |
| 106 | `setTopWider` | verified |  |
| 112 | `setAccentEdge` | verified |  |
| 120 | `onSizeChanged` | excluded |  |
| 126 | `rebuildPath` | excluded |  |
| 144 | `onDraw` | excluded |  |
| 151 | `dispatchDraw` | excluded |  |
| 181 | `setAntialiasedClip` | excluded |  |
| 192 | `drawStroke` | verified |  |
| 198 | `drawAccent` | verified | `src\app\UiLayout.cpp:43` `drawAccent` |
| 234 | `dispatchTouchEvent` | verified |  |
| 243 | `containsPoint` | verified |  |
| 254 | `dp` | fixed | `include\rechigros\app\UiLayout.hpp:31` `dp` (+2) |

## `com\wuying\phigros\ui\SkinManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 37 | `SkinEntry` | verified |  |
| 42 | `isBuiltin` | verified |  |
| 47 | `SkinManager` | verified |  |
| 49 | `getPrefs` | verified |  |
| 53 | `getSkinsRoot` | verified |  |
| 57 | `getSkinDir` | verified |  |
| 62 | `getSkinList` | verified |  |
| 82 | `getSelectedSkinIndex` | verified |  |
| 86 | `setSelectedSkinIndex` | verified |  |
| 137 | `isLikelySkinPackage` | excluded |  |
| 173 | `deleteSkin` | verified |  |
| 197 | `loadSkinConfig` | verified | `src\render\skin\SkinConfig.cpp:159` `loadSkinConfig` |
| 215 | `getSkinConfig` | verified |  |
| 222 | `parseYmlConfig` | verified |  |
| 235 | `parseYmlContent` | verified |  |
| 272 | `stripYmlQuotes` | verified |  |
| 279 | `parseIntArray` | verified | `src\render\skin\SkinConfig.cpp:79` `parseIntArray` |
| 289 | `parseBool` | verified | `src\app\Settings.cpp:41` `parseBool` |
| 293 | `isSkinAssetName` | verified |  |
| 323 | `containsSkinConfigKeys` | verified |  |
| 338 | `deleteRecursive` | verified |  |

## `com\wuying\phigros\ui\TriGridBackgroundView.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 23 | `TriGridBackgroundView` | excluded |  |
| 27 | `TriGridBackgroundView` | excluded |  |
| 32 | `init` | fixed |  |
| 45 | `Runnable` | excluded |  |
| 46 | `run` | fixed | `src\app\Application.cpp:427` `run` |
| 52 | `updateRendering` | excluded |  |
| 65 | `onAttachedToWindow` | excluded |  |
| 69 | `onDetachedFromWindow` | excluded |  |
| 72 | `onWindowVisibilityChanged` | excluded |  |
| 75 | `onVisibilityChanged` | excluded |  |
| 78 | `onPause` | verified |  |
| 79 | `onResume` | verified |  |
| 80 | `setBackgroundBitmap` | verified |  |
| 206 | `setPendingBackground` | verified |  |
| 213 | `TriGridRenderer` | verified |  |
| 221 | `onSurfaceCreated` | excluded |  |
| 246 | `onSurfaceChanged` | excluded |  |
| 253 | `onDrawFrame` | verified |  |
| 286 | `drawQuad` | verified | `src\app\TriGridBackdrop.cpp:281` `drawQuad` |
| 298 | `swapPendingBackground` | verified |  |
| 326 | `loadTexture` | verified | `src\app\TriGridBackdrop.cpp:172` `loadTexture` |
| 355 | `isPowerOfTwo` | verified | `src\app\TriGridBackdrop.cpp:114` `isPowerOfTwo` |
| 359 | `createProgram` | verified |  |
| 383 | `compileShader` | verified |  |

## `com\wuying\phigros\ui\UiTypography.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 22 | `UiTypography` | excluded |  |
| 24 | `px` | fixed | `src\app\LibraryScreen.cpp:76` `px` |
| 28 | `preloadAsync` | verified |  |
| 45 | `gameTypeface` | verified |  |
| 68 | `loadGameTypeface` | verified |  |
| 76 | `applyTypeface` | verified |  |
| 90 | `useIndependentRenderer` | excluded |  |
| 104 | `bindSingleLine` | excluded |  |
| 140 | `TextWatcher` | excluded |  |
| 141 | `beforeTextChanged` | excluded |  |
| 142 | `onTextChanged` | excluded |  |
| 143 | `afterTextChanged` | excluded |  |
| 147 | `bindAllSingleLine` | excluded |  |
| 167 | `fitSingleLine` | fixed |  |
| 172 | `fitSingleLine` | fixed |  |

## `com\wuying\phigros\ui\UiWarmup.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 36 | `UiWarmup` | verified |  |
| 38 | `start` | fixed |  |

## `com\wuying\phigros\util\ChartLibraryManager.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 48 | `ChartLibraryManager` | verified |  |
| 50 | `invalidateCache` | verified |  |
| 74 | `ChartRecord` | verified |  |
| 109 | `ImportResult` | verified |  |
| 115 | `getChartsRoot` | verified |  |
| 125 | `getLocalCharts` | verified |  |
| 145 | `scanLocalCharts` | verified |  |
| 163 | `loadRecord` | excluded | `src\app\ChartLibrary.cpp:476` `loadRecord` |
| 350 | `deleteCharts` | verified |  |
| 392 | `recordBestResult` | verified | `src\app\ChartLibrary.cpp:705` `recordBestResult` |
| 413 | `recordPlayTime` | verified | `src\app\ChartLibrary.cpp:716` `recordPlayTime` |
| 421 | `getBestScore` | verified |  |
| 426 | `getBestAcc` | verified |  |
| 432 | `getLastPlayedAt` | verified |  |
| 437 | `findLocalChartIdForPath` | verified |  |
| 455 | `isComplete` | verified | `src\app\ChartLibrary.cpp:397` `complete` |
| 461 | `hasRequiredFiles` | verified |  |
| 468 | `mapToDest` | verified |  |
| 475 | `findInfoFile` | verified | `src\app\ChartLibrary.cpp:140` `findInfoFile` |
| 493 | `resolveExisting` | verified |  |
| 503 | `findChartFile` | verified | `src\app\ChartLibrary.cpp:156` `findChartFile` |
| 512 | `findAudioFile` | verified | `src\app\ChartLibrary.cpp:164` `findAudioFile` |
| 523 | `findByInfoName` | verified | `src\app\ChartLibrary.cpp:180` `findByInfoName` |
| 539 | `isChartCandidate` | verified |  |
| 559 | `isAudioCandidate` | verified |  |
| 565 | `isImageCandidate` | verified |  |
| 571 | `readHead` | verified | `src\app\ChartLibrary.cpp:20` `readHead` (+1) |
| 583 | `findImageFile` | verified | `src\app\ChartLibrary.cpp:172` `findImageFile` |
| 594 | `chooseShortestPath` | verified | `src\app\ChartLibrary.cpp:60` `chooseShortestPath` |
| 605 | `compareLocalCharts` | verified |  |
| 623 | `getRecordModifiedAt` | verified | `src\app\ChartLibrary.cpp:204` `recordModifiedAt` |
| 636 | `lastModified` | verified | `src\app\ChartLibrary.cpp:197` `lastModified` |
| 640 | `collectFiles` | verified |  |
| 706 | `deleteRecursive` | verified |  |
| 743 | `relativize` | verified | `src\app\ChartLibrary.cpp:29` `relativize` |
| 758 | `getScorePrefs` | verified |  |
| 763 | `getAccPrefs` | verified |  |
| 768 | `getPlayHistoryPrefs` | verified |  |
| 773 | `safeFileName` | excluded |  |

## `com\wuying\phigros\util\FileUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 18 | `FileUtils` | excluded |  |
| 20 | `queryDisplayName` | excluded |  |
| 38 | `stripExtension` | verified | `src\chart\ChartLoader.cpp:379` `stripExtension` |

## `com\wuying\phigros\util\InfoFileUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 25 | `InfoFileUtils` | excluded |  |
| 69 | `readInfoFile` | excluded |  |
| 91 | `readInfoFromChartFile` | verified |  |
| 119 | `buildDifficultyLabel` | verified |  |
| 139 | `normalizeDifficultyText` | excluded |  |
| 148 | `inferDifficultyPrefix` | fixed |  |
| 292 | `fromKv` | verified |  |
| 341 | `getObj` | verified |  |
| 351 | `getStr` | verified |  |
| 368 | `safe` | excluded |  |
| 373 | `parseIntOrNull` | verified |  |
| 383 | `getInt` | verified | `src\app\Settings.cpp:31` `parseInt` |
| 398 | `getBool` | verified | `src\app\Settings.cpp:41` `parseBool` |
| 418 | `firstNonEmpty` | verified | `src\app\InfoFile.cpp:46` `firstNonEmpty` |
| 427 | `stripQuotes` | verified | `src\app\InfoFile.cpp:26` `stripQuotes` (+2) |
| 440 | `trimDifficultyWhitespace` | fixed | `src\app\InfoFile.cpp:167` `trimDifficultyWhitespace` |
| 449 | `isDifficultyEdgeWhitespace` | verified | `src\app\InfoFile.cpp:130` `isDifficultyEdgeWhitespace` |
| 453 | `collapseNormalSpaceRuns` | verified | `src\app\InfoFile.cpp:111` `collapseNormalSpaceRuns` |
| 475 | `parseCsvLine` | verified | `src\app\InfoFile.cpp:231` `parseCsvLine` (+1) |

## `com\wuying\phigros\util\InfoJsonUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 13 | `InfoJsonUtils` | verified |  |
| 23 | `tryReadInfoJson` | verified |  |
| 49 | `firstString` | verified |  |

## `com\wuying\phigros\util\LanzouClient.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 57 | `pageUrl` | excluded |  |
| 64 | `titleOrFileName` | excluded |  |
| 69 | `levelOrFallback` | excluded |  |
| 80 | `FolderSession` | excluded |  |
| 93 | `Page` | excluded |  |
| 103 | `ResolvedDownload` | excluded |  |
| 109 | `LanzouClient` | excluded |  |
| 368 | `rememberCookies` | excluded |  |
| 379 | `addCookies` | excluded |  |
| 397 | `cookieHeader` | excluded |  |
| 426 | `parseJsVars` | excluded |  |
| 437 | `resolveJsValue` | excluded |  |
| 450 | `extractDescription` | excluded |  |
| 471 | `extractDownloadPageUrl` | excluded |  |
| 485 | `resolveDirectFromTp` | excluded |  |
| 494 | `htmlBlockToText` | excluded |  |
| 501 | `stripTags` | excluded |  |
| 507 | `htmlDecode` | excluded |  |
| 517 | `solveAcwScV2` | excluded |  |
| 554 | `firstGroup` | excluded |  |
| 600 | `valueOrEmpty` | excluded |  |
| 605 | `valueOrDefault` | excluded |  |

## `com\wuying\phigros\util\NativeAudioDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 10 | `NativeAudioDecoder` | excluded |  |
| 22 | `decodeFile` | verified | `src\audio\Decoder.cpp:62` `decodeFile` |

## `com\wuying\phigros\util\OnlineIndexClient.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 32 | `OnlineIndexClient` | excluded |  |
| 63 | `loadCachedOrBundled` | excluded |  |
| 169 | `saveCache` | excluded |  |
| 179 | `readCache` | excluded |  |
| 190 | `readBundledFallback` | excluded |  |
| 211 | `formatBytes` | verified |  |
| 224 | `numericChartId` | excluded |  |
| 232 | `firstNonEmpty` | verified | `src\app\InfoFile.cpp:46` `firstNonEmpty` |

## `com\wuying\phigros\util\PcmDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 15 | `PcmDecoder` | excluded |  |
| 23 | `DecodedAudio` | verified |  |
| 30 | `getNumFrames` | verified |  |
| 175 | `selectAudioTrack` | verified |  |
| 194 | `FloatArrayBuilder` | verified |  |
| 199 | `appendPcm` | verified |  |
| 222 | `ensureCapacity` | verified |  |
| 235 | `size` | fixed | `include\rechigros\game\BpmTimeline.hpp:8` `size` (+5) |
| 237 | `buffer` | excluded |  |

## `com\wuying\phigros\util\WavDecoder.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 21 | `WavDecoder` | verified |  |
| 204 | `u16` | fixed | `src\platform\Archive.cpp:30` `readU16` |

## `com\wuying\phigros\util\ZipPackUtils.java`

| Line | Reference declaration | Status | Port symbol |
| --- | --- | --- | --- |
| 36 | `ZipPackUtils` | excluded |  |
| 312 | `normalizeZipEntryName` | verified |  |
| 439 | `ZipEntryInfo` | verified |  |
| 447 | `isDirectory` | verified |  |
| 464 | `BoundedRafInputStream` | excluded |  |
| 468 | `BoundedRafInputStream` | excluded |  |
| 505 | `close` | excluded |  |
| 510 | `readDummyByte` | excluded |  |
| 517 | `deleteRecursive` | verified |  |
| 530 | `isInfoFile` | verified |  |
| 538 | `resolveInfoPath` | verified |  |
| 548 | `isAudioFile` | verified |  |
| 557 | `isImageFile` | verified |  |
| 564 | `readHead` | verified | `src\app\ChartLibrary.cpp:20` `readHead` (+1) |
| 575 | `looksLikeOfficialChartJson` | verified |  |
| 581 | `looksLikeRpeChartJson` | verified |  |
| 587 | `looksLikeAnyChartJson` | verified |  |
| 598 | `looksLikePhiEditText` | verified |  |
| 607 | `isLikelyChart` | verified |  |
| 615 | `findChartFileInExtracted` | verified |  |
| 635 | `findAudioFileInExtracted` | verified |  |
| 649 | `findImageFileInExtracted` | verified |  |
| 663 | `findByInfoNameInExtracted` | verified |  |
| 689 | `readInfoFileWithEncodingFallback` | excluded |  |
| 736 | `readInfoFileWithCharset` | verified |  |
| 757 | `readJsonWithCharset` | verified |  |
| 780 | `readYamlLikeWithCharset` | verified |  |
| 801 | `readKeyValueTxtWithCharset` | verified |  |
| 827 | `readCsvWithCharset` | verified |  |
| 852 | `infoFromKvMap` | verified |  |
| 870 | `getFirstJsonStr` | verified |  |
| 886 | `firstNonEmptyStr` | verified |  |
| 895 | `readAllWithCharset` | verified |  |
| 912 | `stripYamlVal` | verified |  |
| 925 | `parseCsvLineSimple` | verified |  |
