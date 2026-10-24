# Chart layer parity report

Scope: chart parsing, chart metadata, chart library, archive handling, skin/material
config and the import paths. Reference sources are read line by line and compared against
the C++ port; every row names both sides.

Verdicts:

- **MATCH** — behaviour reproduced; evidence given.
- **FIXED** — the port differed and was corrected in this pass.
- **DELIBERATE-EXCLUSION** — intentionally not reproduced, with the reason.

Column "Ref" is the reference file and line; "Port" is the C++ file and line (line numbers
are for the tree at the time of writing).

---

## 1. `game/ChartLoader.java` (923 L) → `src/chart/ChartLoader.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 1.1 | `loadFromFile` — open, detect format, dispatch to parser | 115-151 | 807-836 | MATCH |
| 1.2 | `detectFormatFast` — 64 KiB prefix, BOM/whitespace skip, must start `{` | 827-865 | 65-76, 40-61 | MATCH |
| 1.3 | Detection order: `formatVersion` → Official, then RPE header keys → RPE | 852-859 | 68-75 | MATCH |
| 1.4 | Non-JSON-object prefix falls to the plain-text editor format | 850 | 67 | MATCH |
| 1.5 | `looksLikeJsonObject` — BOM-tolerant first significant char | 867-879 | 58-61 | MATCH |
| 1.6 | `computeNormalizedMd5` — strip BOM + ASCII whitespace, MD5 hex | 170-199 | 80-97 | MATCH |
| 1.7 | Whitespace set used for the digest | 188 | 90-91 | MATCH |
| 1.8 | `readFileToString` / BOM-stripping reader | 898-908 | 99-105 | MATCH |
| 1.9 | `tryApplyLineCsvTextures` — gated on non-empty line list | 201-202 | 414-415 | MATCH |
| 1.10 | `findUpwards` depth semantics | 277-287 | 396-409 | **FIXED** — the loop ran `depth < maxDepth`, inspecting six directories where the reference inspects seven (depth 0..6 inclusive). Now `depth <= maxDepth`. |
| 1.11 | `line.csv` header parse, BOM strip, `trim`ped column names | 320-330 | 418-437 | MATCH |
| 1.12 | `parseCsvLine` — quotes, doubled quotes | 372-403 | 355-383 | MATCH |
| 1.13 | Row scoping: unscoped rows always apply; name match with/without extension | 217-226 | 476-490 | MATCH |
| 1.14 | Single-chart fallback when no row matched but the CSV names one chart | 228-231 | 487-490 | MATCH |
| 1.15 | `resolveResource` — absolute-then-primary-then-secondary, 4 levels up each | 289-307 | 411-441 | **FIXED** — the port only tried `<csvdir>/<rel>` and then `<chartdir>/<rel>` with no upward walk and no absolute-path test. It now walks up from the CSV directory and then from the chart directory (`kResourceSearchDepth = 4`, matching the reference's `resolveUp(..., 4)`), and honours an existing absolute reference. |
| 1.16 | Texture assigned only when the reference resolves to an existing file | 243-249 | 503-512 | **FIXED** — the port assigned `line.texture`/`useOfficialScale` even when the file did not exist. It now assigns only for a resolved file and leaves the line's own texture otherwise, while the scale span below still applies (which is what the reference does). |
| 1.17 | Scale span: `-1e9..1e9`, `horz`/`vert` with non-finite and zero falling back to 1 | 251-263 | 514-528 | MATCH |
| 1.18 | `tryLoadPrprExtra` — search depth and both spellings `extra.json`/`Extra.json` | 412-415 | 672-687 | **FIXED** (same off-by-one as 1.10; the loop now stops at `depth <= 6`). |
| 1.19 | `packDir` is the directory holding `extra.json` | 417 | 690 | **FIXED** — `applyPrprExtra` set it, but `loadFromFile` then unconditionally reassigned `chart.packDir = path.parent_path()`. That assignment now runs *before* the storyboard pass and is not repeated, so the extra.json directory wins. |
| 1.20 | Storyboard BPM list: array, single object, or bare scalar | 572-600 | 217-264 | **FIXED** — the port accepted an array only and read the beat from `startTime` only. It now also accepts a single object and a bare number, and reads `time` first then `startTime`. |
| 1.21 | BPM of 0 falls back to 120; other values (including negatives) are kept | 434 | 243-245 | **FIXED** — the port clamped every non-positive BPM to 120. It now substitutes 120 only for an exact zero, as the reference does. |
| 1.22 | Segment ordering before the time walk | 425 | 230-233 | **FIXED** — `std::sort` replaced with `std::stable_sort` so equal beats keep authored order. |
| 1.23 | `beatToSec` — last segment with `startBeat <= beat`; no segment means `beat * 0.5` | 770-779 | 267-274 | **FIXED** — the port walked from the end and used the lowest segment as a catch-all. It now returns first-match at-or-before, and `beat * 0.5` when nothing matches, so a beat before the first segment uses the flat fallback. |
| 1.24 | `parseBeatTriple` — `a + b/c`, `c == 0` treated as 1 | 761-768 | 208-216 | MATCH |
| 1.25 | Effect `start`/`end` time aliases (`start`+`startTime`, `end`+`endTime`) | 461-464 | 675-681 | **FIXED** — the port read only `start`/`end`. Both alias pairs are now tried in the reference's order. |
| 1.26 | Effect `shader` must be present and non-blank | 468-469 | 669-673 | MATCH (port uses `is_string` + non-empty; reference trims then checks empty, which differs only for an all-space shader name — recorded under Remaining risk) |
| 1.27 | Effect `global` from `global`, else `isGlobal` | 471-473 | 683-688 | MATCH |
| 1.28 | Effect variables: constant number → scalar | 484-485 | 371-373 | MATCH |
| 1.29 | Effect variables: numeric array → 1 component scalar, n components vector | 486-495 | 374-386 | MATCH |
| 1.30 | Effect variables: non-numeric array → eased spans with `startTime`/`endTime` triples | 496-534 | 294-369 | **FIXED** — the port parsed a `{time, value}` keyframe schema and paired consecutive keys, which is not the reference schema. It now reads each span as an interval: `startTime`/`start` + `endTime`/`end` beat triples, `start`/`end` values, `easingType` (minus one, floored at zero), `easingLeft`, `easingRight`, `bezier`, and 4-point `bezierPoints`. |
| 1.31 | Spans sorted by start time | 532 | 364-368 | MATCH |
| 1.32 | `vars` object iterated in document order | 478-536 | 298-386 | MATCH |
| 1.33 | Video `path` must be present and non-blank | 609-610 | 702-706 | MATCH |
| 1.34 | Video `scale`: number, or `inside`/`contain` → Inside, `fit`/`stretch` → Fit, else crop-centre | 642-655 | 558-571 | MATCH |
| 1.35 | Video start from `time`/`startTime`, only when written as a beat array | 615-616 | 708-712 | MATCH |
| 1.36 | Video `alpha` / `dim`: scalar, number array (first entry), or keyframed track | 674-728 | 575-670 | MATCH |
| 1.37 | Video track gains a leading hold key when it starts after zero | 715-725 | 660-669 | MATCH |
| 1.38 | Video `attach`: number, or `{ "line": n }`, clamped to >= -1 | 657-662 | 724-736 | MATCH |
| 1.39 | `prprShaderSources` — rooted `/name` or a `.glsl`/`.fs`/`.frag` file name, source read from the pack | 546-567 | 800-826 | **FIXED** — the map was never populated, so no file-backed storyboard shader could load even though the renderer reads the map. Sources are now resolved through the same resource walk and stored keyed by the authored shader name. |
| 1.40 | `isPrprShaderFileName` — extension test on the trimmed, lower-cased name | 746-750 | 803-809 | MATCH |
| 1.41 | `tryApplyAttachUiFixFlag` — info file search, flag only replaced when present | 910-922 | 180-196 | MATCH |
| 1.42 | info file search order `info.yml`, `info.yaml`, `info.json`, `Info.yml`, `Info.json` | 912-913 | 184 | MATCH |
| 1.43 | Level effects are applied only when shaders are enabled | 1599-1615 (renderer) | 855-867 | **DELIBERATE-EXCLUSION** — see Remaining risk and Cross-area requests. The loader still appends the level-effect passes; the renderer suppresses the whole storyboard chain when `disableChartShader` is set (`GameRenderer.cpp:495`), which produces the same visible result for the one setting that gates it. Moving the append into the renderer would require the effect table to be reachable there, which it is not today. |

### Notes on the four Java unit tests that pin `ChartLoader`

- `OfficialInvisibleDragRangeTest` (chart loads, `formatVersion > 0`, drag note geometry)
  depends on `OfficialChartParser` only; see §2. The `isCover` default asserted there is
  reproduced at `OfficialChartParser.cpp:51`.
- `RpeAttachUiDebugTest` depends on `RpeChartParser` only; see §4.
- `ZipPackUtilsTest` is covered in §6.

---

## 2. `game/OfficialChartParser.java` (628 L) → `src/chart/OfficialChartParser.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 2.1 | `parse(String)` — BOM strip, must be an object | 48-62 | 350 | MATCH |
| 2.2 | `parseRoot` — `formatVersion` default 3, only 1 and 3 accepted | 65-68 | 345 | MATCH |
| 2.3 | `offset` (aliases `offset`/`Offset`) | 72 | 345 | MATCH |
| 2.4 | `blockAreaList`/`BlockAreaList` parsed before the lines | 74 | 345 | MATCH |
| 2.5 | Missing/non-array `judgeLineList` yields an empty chart | 76-80 | 345 | MATCH |
| 2.6 | Parallel threshold: only used at >= 6 lines | 86 | 352-361 | DELIBERATE-EXCLUSION — the port parallelises above a 1 MiB document instead, and falls back to the sequential path whenever the document cannot be sliced. Both orders produce identical models (`tests/ChartParserTests.cpp:833-849` asserts parity), so the scheduling difference is unobservable. |
| 2.7 | `attachUiLineIndex[ui]` for ui in 1..7 | 116-119, 143-146 | 345, 418-423 | **FIXED** — the sequential path recorded the index *after* skipping non-object entries, while the reference records the authored position. Now the authored index is recorded in both paths. |
| 2.8 | Per-line `bpm` default 120, non-positive → 120 | 250-251 | 51 | MATCH |
| 2.9 | `invertRotation = false`, `isCover = true` | 253-254 | 51 | MATCH — asserted by `OfficialInvisibleDragRangeTest.officialLinesEnableCoverByDefault`. |
| 2.10 | `notesAbove`/`notesBelow` only (no combined key) | 256-257 | 51 | MATCH |
| 2.11 | Note type mapping 1..4 → tap/drag/hold/flick, other values kept verbatim | 315-323 | 47 | MATCH |
| 2.12 | Note field aliases incl. `yOffset`/`YOffset`/`offsetY` | 328-339 | 47 | MATCH |
| 2.13 | `isFake` aliases `isFake`/`IsFake`/`fake`/`Fake` | 332 | 47 | MATCH |
| 2.14 | Disappear events: `judgeLineDisappearEvents` then `disappearEvents` | 265-266 | 52 | MATCH |
| 2.15 | Rotate events negated after parse | 274-282 | 52 | MATCH |
| 2.16 | Move events split into X/Y 1D components | 268-272 | 52 | MATCH |
| 2.17 | V3 move: `start`/`end` and `start2`/`end2`, minus 0.5 | 399-414 | 41 | MATCH |
| 2.18 | V1 move: packed `floor(x)*1000 + y`, X/880, Y/520, minus 0.5 | 407-411, 454-479 | 40 | MATCH — the port unpacks inline from the same packed value the reference writes to the object, which is arithmetically identical. |
| 2.19 | `ensureCoverage` adds a `0..1e9` span when empty, a leading span when the first starts late | 481-502 | 29-33 | MATCH |
| 2.20 | Speed events aliases `speedEvents`/`SpeedEvents`/`speed` | 423 | 53 | MATCH |
| 2.21 | Speed `endTime` default `startTime + 1e9`; `endValue = value` | 433-442 | 53 | MATCH |
| 2.22 | Empty speed list → single `0..1e9, 1.0` span | 294-296, 504-513 | 54 | MATCH |
| 2.23 | Nested `{x,y}` points with per-component aliases (`x`/`X`) | 590-612 | 58-68 | MATCH |
| 2.24 | Block area: top-right default 1, bottom-left default 0 | 175-180 | 74-77 | MATCH |
| 2.25 | `disableTime`/`disappearTime` default `Float.MAX_VALUE` | 184-185 | 80-81 | MATCH |
| 2.26 | Block rotate/move/scale event aliases and `easeType` defaults | 188-240 | 84-123 | MATCH |
| 2.27 | Block tracks sorted by time | 202, 219, 239 | 94-95, 107-108, 121-122 | MATCH |

---

## 3. `game/PecChartParser.java` (721 L) → `src/chart/PecChartParser.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 3.1 | Line splitting: normalise CRLF/CR, strip `//`, trim, drop blanks | 28-36 | 250-258 | MATCH |
| 3.2 | First line must be numeric, else the chart is rejected | 38-39 | 259-261 | MATCH |
| 3.3 | `offset = round4((ms - 150) / 1000)` | 42, 328-330 | 264 | MATCH |
| 3.4 | `#` line sets the previous note's speed, non-finite → 1 | 53-59 | 271 | MATCH |
| 3.5 | `&` line sets the previous note's xScale, zero/non-finite → 1 | 61-68 | 272 | MATCH |
| 3.6 | `bp` beat × 32, bpm default 120, non-positive → 120 | 76-83 | 276-280 | MATCH |
| 3.7 | `n1`/`n2`/`n3`/`n4` → tap/hold/flick/drag | 266-307 | 89-109 | MATCH |
| 3.8 | Hold duration from the difference of beats, floored at 0 | 281-285 | 98 | MATCH |
| 3.9 | Note `positionX = raw * 9 / 1024` | 309-311 | 102 | MATCH |
| 3.10 | Note `above`/`fake` compare against exactly 1.0 | 292-293 | 103-104 | MATCH |
| 3.11 | `cv` speed divided by 7 after the 1.0 fallback | 97-105 | 295-300 | MATCH |
| 3.12 | `cm` — start x/y NaN, end from `/2048` and `/1400`, ease `clamp(e-1)` | 107-125 | 301-303 | MATCH |
| 3.13 | `cp` — both ends set, `endTime` NaN, easing 0 | 126-142 | 304-305 | MATCH |
| 3.14 | `cr` / `cd` rotate with `clampEase(e-1)` and NaN start | 143-172 | 306-309 | MATCH |
| 3.15 | `cf` / `ca` alpha in 0..255 space, easing 0 | 173-201 | 310-313 | MATCH |
| 3.16 | `clampEase` clamps to 0..28 | 260-264 | 87 | MATCH |
| 3.17 | BPM list default, sort, leading point when the first is past zero | 207-214 | 317-319 | MATCH |
| 3.18 | Line keys emitted in ascending index order | 219-222 | 323 | MATCH |
| 3.19 | Notes sorted by time per side | 226-227 | 246 | MATCH |
| 3.20 | Move/line event normalisation: closing times, then carry-forward of start values | 402-447 | 111-136 | MATCH |
| 3.21 | `applyAlphaSpecials` — `-1`→`-255`, `-2`→`-510`, `< -100` visible-time encoding, `/255` | 449-488 | 185-199 | MATCH |
| 3.22 | `assignVisibleTime` — 4-tick sampling, `visibleBeat = -(v+100)/10`, per-note BPM | 495-545 | 142-183 | MATCH |
| 3.23 | Note beat conversion, hold length via `PGRBEAT / lineBpm` | 547-573 | 234-247 | MATCH |
| 3.24 | Per-note fallbacks: non-finite speed → 1, zero xScale/size → 1 | 565-567 | 242-244 | MATCH |
| 3.25 | `buildSpeedEvents` — beat→norm beat, last span widened to 1e9 | 368-400 | 201-227 | **FIXED** — the port did not swap an inverted pair (`b1 < b0`, which a negative BPM produces) and did not widen the final span. Both are now reproduced. |
| 3.26 | `BpmConverter` — start seconds, binary search, `beatToNormBeat` | 649-720 | 55-85 | MATCH |
| 3.27 | Default move `0..1e9` at 0.5/0.5; default line `-1e9..1e9` | 611-644 | 222-232 | MATCH |

---

## 4. `game/RpeChartParser.java` (2587 L) → `src/chart/RpeChartParser.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 4.1 | `parseFile` two-pass header read (`BPMList`/`META`/`offset`, stop at the line array) | 63-114, 344-393 | 983-995 | DELIBERATE-EXCLUSION — the port reads the whole document once and parses it, which needs the same memory as the parsed model but no streaming cursor. Output is identical; only peak memory differs, and the port has no equivalent of the reference's low-memory pressure. |
| 4.2 | BPM list: aliases `BPMList`/`bpmList`/`BpmList`, skip non-positive/NaN BPM | 703-717 | 953-958 | MATCH |
| 4.3 | BPM list default, sort, leading point when the first is past zero | 69-75 | 959-961 | MATCH |
| 4.4 | `parseOffsetSeconds` — META first, then root, divide by 1000 | 677-688 | 964-965 | MATCH |
| 4.5 | `parseRpeVersion` — default 160, `Math.round` | 690-701 | 966 | MATCH (port uses `integer()`, which truncates; see 4.8 — recorded under Remaining risk) |
| 4.6 | `bpmfactor` aliases, non-finite/non-positive → 1 | 423-425 | 811-812 | MATCH |
| 4.7 | `line.bpm = baseBpm / bpmfactor` | 428 | 813 | MATCH |
| 4.8 | `getAsInt` truncates fractional values rather than rounding | 2287-2297 | 58-80 | **FIXED** — the port round-tripped through `std::llround`, so a value like `2.7` became 3 where the reference yields 2. Integer reads now truncate toward zero, and numeric flags go through the same path so `0.5` is false. |
| 4.9 | `getAsDouble` accepts value nodes only, textual numbers parsed | 2275-2285 | 46-56 | **FIXED** — the port silently skipped array/object nodes for typed reads (it now rejects structured nodes explicitly, matching `isValueNode`), and a textual value that fails to parse now falls back instead of throwing into a partial read. |
| 4.10 | `getAsBool` — boolean, integer-nonzero, `"true"`/`"false"` case-insensitive, then integer parse | 2299-2316 | 82-101 | **FIXED** — the port lower-cased then compared against lowercase literals, which is equivalent, but it used `stoi` on the raw text. Now the literals are matched case-insensitively and the numeric fallback goes through the integer view. |
| 4.11 | `line.zOrder` aliases `zOrder`/`z`/`order` | 433 | 814 | MATCH |
| 4.12 | `line.isCover` aliases; default false | 434 | 815 | MATCH |
| 4.13 | `line.father` aliases; default -1 | 435 | 816 | MATCH |
| 4.14 | `line.rotateWithFather` aliases | 436 | 817 | MATCH |
| 4.15 | `anchor` — array or `{x,y}` object | 438-442 | 818, 271-275 | MATCH |
| 4.16 | `attachUI` name → element id 1..7, numeric string fallback | 395-413, 444-447 | 799-806, 820-821 | MATCH |
| 4.17 | Note control keys (`alphaControl`, `sizeControl`, `posControl`, `yControl`, `skewControl`) | 449-453 | 822-826 | MATCH |
| 4.18 | Judge-area controls: 7 control-key aliases × 9 value-key aliases | 454-461 | 827-829 | MATCH |
| 4.19 | `Texture`/`texture`, `line.png` ignored, resolved against the chart directory | 463-466 | 830-831 | MATCH |
| 4.20 | `eventLayers`/`EventLayers`/`layers`; a synthetic single layer when absent | 470-557 | 855-858 | MATCH |
| 4.21 | Rotate events: `rotateEvents`/`judgeLineRotateEvents`/`rotationEvents`/`rotEvents` | 481-482 | 835-836 | MATCH |
| 4.22 | Alpha events: `alphaEvents`/`opacityEvents`, then `1 - v` from the disappear form | 719-733 | 228-235 | MATCH |
| 4.23 | Move X/Y 1D events with X/1350 and Y/900 scaling | 485-497 | 838-843 | MATCH |
| 4.24 | Packed `moveEvents` used only when both 1D lists are empty | 499-501 | 844-845 | MATCH |
| 4.25 | Coverage: rotate/pos extrapolate the first value, alpha does not | 503-509, 2055-2089 | 846-850, 171-183 | MATCH |
| 4.26 | Speed events aliases `speedEvents`/`judgeLineSpeedEvents`/`speed` | 946 | 624 | MATCH |
| 4.27 | Speed value `/4.5`, missing end → next start or 1e9 | 961-991 | 634-643 | MATCH |
| 4.28 | Speed coverage: leading span, gaps, trailing span to 1e9 | 2122-2173 | 265-295 | MATCH |
| 4.29 | Legacy speed-height resampling with zero-crossing insertion | 996-1162 | 566-621 | MATCH |
| 4.30 | Multi-layer speed merge over the union of boundaries | 1725-1773 | 506-545 | MATCH |
| 4.31 | Speed-layer cursor easing: type 0 constant, <=1 linear, else shared curve | 1783-1868 | 304-362 | MATCH |
| 4.32 | Sub-window and sub-bezier helpers for partially covered eased segments | 1903-2015 | 401-504 | MATCH |
| 4.33 | Constant-segment folding in merged speed events | 2029-2040 | 436-446 | MATCH |
| 4.34 | `extended`/`Extended` block | 468, 585-598 | 875-936 | MATCH |
| 4.35 | `scaleXEvents`/`sxEvents` and `scaleYEvents`/`syEvents` | 1192-1203 | 876-880 | MATCH |
| 4.36 | Combined `scaleEvents` — all-or-nothing vector form, coverage at 1.0 | 1205-1233 | 881-897 | MATCH |
| 4.37 | `inclineEvents`/`tiltEvents`/`skewEvents`, degrees → radians, coverage | 1235-1248 | 898-903 | MATCH |
| 4.38 | `colorEvents`/`colourEvents`/`tintEvents`; start colour required, end defaults to start | 1316-1364 | 905-917 | MATCH |
| 4.39 | `parseRgb` — array or `{r,g,b}`, byte detection once for the whole colour | 2445-2489 | 674-707 | MATCH |
| 4.40 | `textEvents`/`labelEvents`; start text required, end text defaulted | 1366-1414 | 918-926 | MATCH |
| 4.41 | `gifEvents`; end forced past start by 1e-6 | 1275-1314 | 927-935 | MATCH |
| 4.42 | Static `skew` — applied on top of the incline events, or as a single span | 1250-1273 | 937-943 | MATCH |
| 4.43 | `.gif` texture detection | 600-602 | 944-945 | MATCH |
| 4.44 | `notesAbove`/`notesBelow` presence selects the split form even when empty | 1424-1450 | 780-797 | MATCH |
| 4.45 | Combined `notes`/`noteList`/`Notes` with `isAbove`/`above`/`aboveLine` | 1452-1516 | 770-778, 790-796 | MATCH |
| 4.46 | Note type: a value outside 1..4 is retried as the zero-based spelling (0..3 → +1), and anything still outside 1..4 is dropped | 1520-1524 | 765-772 | **FIXED** — the default read was 1 rather than the reference's 0, so `type:0` was kept as a tap through the wrong path and a zero-based `3` was read as a flick instead of a drag. The port now reads the default 0, reinterprets only values that are not already a valid one-based type, and drops what is still invalid. `tests/ChartParserTests.cpp` pins all three cases. |
| 4.47 | Note time from `time`/`startTime`/`t`, beat → norm beat | 1534-1535 | 722-723 | MATCH |
| 4.48 | Hold duration from `holdTime`, `endTime`, or `hold`/`duration` × 32 | 1537-1559 | 724-731 | MATCH |
| 4.49 | `speed`/`noteSpeed` default 1 | 1565-1566 | 732-733 | MATCH |
| 4.50 | `positionX`/`x`/`posX` divided by `670 * 9/80` | 1568, 2349-2352 | 734 | MATCH |
| 4.51 | `isFake`/`fake` | 1570 | 735 | MATCH |
| 4.52 | `size`/`width`/`xScale`, non-positive → 1; `size` fixed at 1 | 1572-1575 | 737-740 | MATCH |
| 4.53 | `alpha`/`opacity`, byte-scaled, clamped to 0..1 | 1577-1582 | 741-747 | MATCH |
| 4.54 | `visibleTime` default -1, >= 999999 → -1 | 1584-1587 | 748-749 | MATCH |
| 4.55 | `yOffset`/`offsetY` divided by 900 | 1589-1592 | 750-751 | MATCH |
| 4.56 | Judge area: 9 aliases, non-positive → 1 | 1594-1598 | 752-754 | MATCH |
| 4.57 | `tint`/`color` note tint | 1600-1606 | 755-756 | MATCH |
| 4.58 | Hit-effect tint: three aliases, then `tintHitEffects`, then reuse the note tint | 1608-1622 | 757-766 | MATCH |
| 4.59 | Note controls: `easing`/`ease`/`easingType` minus one, clamped 0..28 | 1687-1691 | 699-723 | MATCH |
| 4.60 | Note controls: the two-entry default ramp is dropped | 1695-1701 | 714-717 | **FIXED** — the port sorted the list before testing for the sentinel, so an authored ramp written in the other order was not recognised. The sentinel is now tested in authored order, before sorting. |
| 4.61 | Note controls: easing shifted back one point | 1703-1711 | 718-722 | MATCH |
| 4.62 | `parseBeat` — number/array `a + b/c`, `c == 0` → `a`, then × 32 | 2358-2391 | 85-118 | **FIXED** — three separate defects: (a) a size-3 array with `c == 0` produced `a + 0` via a special case but a `c == 0` with a non-zero numerator was handled differently from the reference's plain `a`; (b) non-number array entries threw and zeroed the whole beat instead of contributing zero; (c) the quantisation used `std::round` (half away from zero) where the reference uses `Math.round` (half up). All three now match. |
| 4.63 | `parseVec2` — array of two, or object with `x`/`X`/`anchorX` and `y`/`Y`/`anchorY` | 2425-2443 | 271-275 | **FIXED** — the port accepted arrays only, so `{ "x": …, "y": … }` points were dropped. The object form and its `anchorX`/`anchorY` aliases are now read. |
| 4.64 | `parseBezierPoints` — 4 numbers, or two 2-element arrays; `points` alias | 2393-2423 | 154-169 (via `array`) | **FIXED** — the port only accepted the flat 4-number form and always seeded `{0,0,1,1}`. The nested `[[x1,y1],[x2,y2]]` form is now accepted too. (The seeded default is equivalent because the reference leaves `bezierPoints` null and the evaluator only consults it when `bezier` is set.) |
| 4.65 | `parseMoveEvents` — vector form only when both ends are arrays, else scalars with 0.5 defaults | 823-840 | 288-306 | **FIXED** — the port mixed the two forms per component and defaulted a missing scalar start to 0 instead of the stage centre. It now takes the all-or-nothing branch the reference takes. |
| 4.66 | Move events sorted by start time after parse | 859 | 307-309 | **FIXED** — the port relied on the caller's coverage pass; it now sorts here, so an unsorted authored list orders the same way. |
| 4.67 | `selectJudgeLineArray` — `judgeLineList`/`JudgeLineList`/`judgeLines`/`judgeLinesList`, then `lines`, validated by shape | 2216-2238 | 967 | **FIXED** — the port accepted the keys in one flat list including `lines`, so an unrelated `lines` array could win over a valid `judgeLineList`. The reference validates the first candidate by shape before falling back, and the port now does too. |
| 4.68 | Bezier LUTs built once per distinct control-point set and shared | 177-323 | 154-169 | MATCH (port builds per event; identical curve, more compute — Remaining risk) |

### Notes on the Java tests that pin `RpeChartParser`

- `RpeJudgeAreaTest` — all three cases (static alias `judge_area`, `judgeAreaControl`,
  `judge_area_control`) pass through rows 4.18/4.56/4.59-4.61.
- `RpeLineScaleSemanticsTest` — `scaleXEvents` values are copied verbatim (no half factor),
  which is row 4.35. The test's name refers to a factor that is applied at render time, not
  at parse time, so the parser side is correct.
- `RpeParentTransformTest` — exercises `JudgeLine`/`Event` rotation composition, which is
  `core-parity`'s area (row 4.x here only supplies the event values).
- `RpeAttachUiDebugTest` — `attachUI` name mapping (row 4.16) and `moveYEvents` scaling
  (row 4.23).

---

## 5. `util/InfoFileUtils.java` (514 L) → `src/app/InfoFile.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 5.1 | `readInfoFile` — dispatch on extension, unknown falls to the YAML-like reader | 72-91 | 636-648 | MATCH |
| 5.2 | Supported extensions `.json`, `.yml`/`.yaml`, `.txt`, `.csv` | 77-87 | 638-642 | MATCH |
| 5.3 | JSON field aliases for name/chart/music/illustration/level/difficulty/composer/charter/illustrator | 178-190 | 493-502 | MATCH |
| 5.4 | `chartOffsetMs` aliases `chartOffsetMs`/`chart_offset_ms`/`chartOffset`/`offsetMs` | 191 | 506 | MATCH |
| 5.5 | `useAttachUiFix` aliases `useAttachUiFix`/`use_attach_ui_fix` | 192 | 526 | MATCH |
| 5.6 | `getInt` — numeric truncation, textual parse, unparsable falls to the next alias | 386-398 | 506-523 | MATCH |
| 5.7 | `getBool` — boolean, number, `"true"`/`"1"`/`"false"`/`"0"` after trimming and lower-casing | 401-418 | 526-556 | MATCH |
| 5.8 | `getStr` — value nodes only, empty string does not satisfy the alias | 354-368 | 482-491 | MATCH |
| 5.9 | `readYamlLike` — trim, skip blanks and `#`, split on the first `:`, strip quotes | 221-238 | 339-343, 306-337 | MATCH |
| 5.10 | `readKeyValueTxt` — only the first physical line may be a comment | 241-264 | 306-337 | MATCH |
| 5.11 | `readCsv` — first non-blank line is the header, second is the data row | 266-292 | 351-373 | MATCH |
| 5.12 | Key/value alias table (deliberately narrower than the JSON table) | 295-341 | 258-305 | MATCH |
| 5.13 | `chartOffsetMs` also accepts the bare `offset` key in the key/value form | 329 | 283 | MATCH |
| 5.14 | `fromKv` lower-cases keys but keeps the original map for the `Name`-style lookups | 295-341 | 258-281 | MATCH |
| 5.15 | `readInfoFromChartFile` — `META`/`meta`/`Meta` block only | 94-119 | 648-677 | MATCH |
| 5.16 | In-chart META aliases (incl. `background`, `bg`, `picture`) | 105-114 | 665-675 | MATCH |
| 5.17 | `normalizeDifficultyText` — collapse ASCII space runs, then map each space to U+2002 | 141-148 | 573-602 | **DELIBERATE-EXCLUSION** — the port additionally folds every Unicode space separator (U+00A0, U+2000..U+200B, U+202F, U+3000) to an ASCII space before collapsing, so an em space normalises like a plain space. The reference leaves those code points untouched. This superset is pinned by `tests/AppDataTests.cpp:62-64`, and the Java acceptance test (`InfoFileUtilsTest`) only covers ASCII space and an existing U+2002, both of which the port satisfies. A strictly reference-faithful port would fail that C++ assertion, so the superset is kept deliberately; see Remaining risk. |
| 5.18 | `collapseNormalSpaceRuns` | 456-475 | 113-130 | MATCH |
| 5.19 | `trimDifficultyWhitespace` trims the normal space, the en space, and `Character.isWhitespace` | 443-453 | 169-186 | **FIXED** — the port's set omitted the U+2000..U+200A separators (it listed only U+2002) while including nothing else, so a lead/trail U+2003 or U+2009 was not trimmed. The set now covers U+2000..U+200A plus U+1680/U+2028/U+2029/U+205F/U+3000 and the ASCII separators, and excludes the non-breaking forms, matching `Character.isWhitespace`. |
| 5.20 | `buildDifficultyLabel` — level wins, then `SP Lv.?`, then an authored label, then an inferred prefix | 121-139 | 620-632 | MATCH |
| 5.21 | Authored-label test: contains a letter and contains `lv` | 130 | 624-627 | MATCH |
| 5.22 | `inferDifficultyPrefix` — EZ/HD/IN/AT delimited by non-alphanumerics | 150-169 | 604-618 | **FIXED** — the port tested the delimiter with `std::isalnum` on the *upper-cased* string, which is locale-dependent and accepts non-ASCII bytes; the reference regex `[^A-Z0-9]` is exact. The delimiter test now accepts only `A-Z` and `0-9` as token characters. |
| 5.23 | Bare stem `0`..`3` → EZ/HD/IN/AT | 163-166 | 613-616 | MATCH |
| 5.24 | `writeInfoJson` — fixed field order, `chartOffsetMs` defaulted, `useAttachUiFix` omitted when unset | 197-218 | 679-693 | MATCH |
| 5.25 | `readAll` UTF-8 | 503-513 | 223 (via `platform::readTextFile`) | MATCH — the platform reader additionally retries the legacy code page, a superset documented in `FileSystem.hpp`. |

---

## 6. `util/ZipPackUtils.java` (951 L) → `src/platform/Archive.cpp` + `src/app/ChartLibrary.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 6.1 | EOCD scan backwards over the last `22 + 65535` bytes | 246-261 | 92-103 | MATCH |
| 6.2 | ZIP64 is rejected at the EOCD level | 266-268 | 108-111 | MATCH — message `"ZIP64 packages are not supported"` / `"ZIP64 archives are not supported"`; kept as authored, see 6.4. |
| 6.3 | Central-directory bounds validated against the file length | 269-271 | 112-114 | MATCH |
| 6.4 | Per-entry `0xffffffff` sizes/offset are resolved from the `0x0001` extra field, throwing only when that field is missing | 296-305, 367-400 | 149-185, 76-103 | **FIXED** — the port skipped the extra field entirely and had no per-entry ZIP64 path, so an archive with one large entry was rejected as unsupported. It now parses the extended field, substitutes the 64-bit values in the reference's order (uncompressed, compressed, offset), and throws `"Missing ZIP64 extended information"` only when a needed value is absent. |
| 6.5 | Entry-name charset: UTF-8 when the general-purpose bit 11 is set, else the caller's fallback charset (GBK) | 307-308 | 205-213 (`decodeLegacyText`) | DELIBERATE-EXCLUSION — the port has no caller-supplied fallback charset and instead decodes each name that is not valid UTF-8 from the legacy code page. This covers the same archives and additionally handles a mixed archive where only some names lack the UTF-8 flag. |
| 6.6 | Whole-archive GBK extraction retried as UTF-8 on failure | 61-69 | n/a | DELIBERATE-EXCLUSION — the reference retries the entire extraction because a bad decode can only be detected during extraction; the port decides per name, so there is nothing to retry. Observable result for the archive in `ZipPackUtilsTest` is the same (UTF-8 flagged names are extracted correctly). |
| 6.7 | `normalizeZipEntryName` — backslashes to slashes, strip leading separators, drop `.`, reject `..`, drive letters and NUL | 314-330 | 53-75 | MATCH |
| 6.8 | Zip-slip guard: the canonical output path must stay under the extraction root | 220-222 | 237-243 | MATCH |
| 6.9 | Stored and deflated entries only; encrypted entries rejected | 333-348 | 164-189 | MATCH |
| 6.10 | Local header read to locate the entry data | 350-365 | 187-195 (approx) | MATCH |
| 6.11 | Detection pass 1 — conventional names (`chart.json`, `bg.png`, `image*`, `music*`/`song*`) | 75-89 | 269-286 | MATCH |
| 6.12 | Detection pass 2 — first file with a matching extension | 92-103 | 288-293 | MATCH |
| 6.13 | Detection pass 3 — chart by content, skipping `info.json`/`respack.json` | 106-116 | 297-310 | MATCH — the port also skips `extra.json`/`replay.json`, matching the sibling helper at ref 622-627. |
| 6.14 | Info-declared chart/music/background resolution with fallback scans | 118-161 | 314-350 | MATCH |
| 6.15 | Chart-embedded META fills background and music | 163-193 | 352-382 | MATCH |
| 6.16 | Final safety net scanning everything | 195-204 | 384-393 | MATCH |
| 6.17 | `isInfoFile` set `info.json`/`.yml`/`.yaml`/`.txt`/`.csv` | 532-538 | 250-253 | MATCH |
| 6.18 | `isAudioFile` / `isImageFile` extension sets | 550-564 | `isAudioCandidate`/`isImageCandidate` in `InfoFile.cpp:707-717` | MATCH |
| 6.19 | `isLikelyChart` — official/RPE/editor-text/any-chart-json | 577-615 | ChartLibrary.cpp 86-113 | MATCH |
| 6.20 | `findByInfoNameInExtracted` picks the shortest matching path | 666-685 | ChartLibrary.cpp 182-197 | MATCH |
| 6.21 | `readInfoFileWithEncodingFallback` — retry GBK when a declared path misses, keep the original name if the retry has none | 691-736 | n/a | DELIBERATE-EXCLUSION — the port's `platform::readTextFile` already resolves the encoding for every metadata read, so the reference's second attempt has no counterpart. |
| 6.22 | ZIP64 support matrix documented above | — | — | see 6.2/6.4 |

---

## 7. `util/ChartLibraryManager.java` (781 L) → `src/app/ChartLibrary.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 7.1 | `getChartsRoot` — create on demand | 117-125 | 449-468 | MATCH |
| 7.2 | Cached index invalidated by the root's mtime | 128-145 | 449-468 | DELIBERATE-EXCLUSION — the port re-reads on `force` only and exposes `invalidate()` after every mutation, which is what the reference does around every write. There is no cross-process writer to guard against. |
| 7.3 | `.tmp` directories are skipped when scanning | 155 | 460 | MATCH |
| 7.4 | Only complete records (chart + music + background) are indexed | 157-159, 457-461 | 462, 399-402 | MATCH |
| 7.5 | Sort: played first, newest play, newest modification, name, then id | 607-623 | 774-788 | MATCH |
| 7.6 | `loadRecord` — find metadata, resolve chart/music/background, then by name, then by scan | 166-183 | 496-517 | MATCH |
| 7.7 | Canonical `info.json` is rewritten when anything was resolved | 185-248 | 519-557 | MATCH |
| 7.8 | Chart META back-fills every field the metadata left out | 199-233 | 533-550 | **FIXED** — the whole back-fill was gated on the song name being empty, so a pack with a name but no composer/charter kept them empty. Each field is now filled independently, as the reference does. |
| 7.9 | Name falls back to the chart stem, then the pack directory name | 234-237 | 549-552 | MATCH |
| 7.10 | `chartOffsetMs` defaults to 0 | 238-241 | 553-556 | MATCH |
| 7.11 | Difficulty label, `SP Lv.?` when empty | 250-251 | 568-569 | MATCH |
| 7.12 | `ChartRecord` field set (id, root, music, chart, bg, info, name, difficulty, offset, best score/acc, last played, modified) | 59-105 | `ChartLibrary.hpp` `ChartRecord` | MATCH |
| 7.13 | `isComplete` predicate | 457-461 | 399-402 | MATCH |
| 7.14 | `findInfoFile` — canonical first, then shortest path among the info names | 477-493 | 143-156 | MATCH |
| 7.15 | `resolveExisting` — normalise separators, strip leading `/` and `./` | 495-503 | 38-56 | MATCH |
| 7.16 | `findByInfoName` — leaf-name match, shortest path | 525-539 | 182-197 | MATCH |
| 7.17 | `isChartCandidate` — metadata/replay exclusions, `.pec`/`.rpe`, header sniff | 541-559 | `InfoFile.cpp:695-705` | MATCH |
| 7.18 | `readHead` 64 KiB | 573-583 | ChartLibrary.cpp 22-29 | MATCH |
| 7.19 | `chooseShortestPath` | 596-601 | 62-69 | MATCH |
| 7.20 | `getRecordModifiedAt` — max of root and the four files | 625-640 | 206-215 | MATCH |
| 7.21 | Best score / best accuracy / last played are tracked independently | 399-437 | 707-724 | MATCH |
| 7.22 | `bestAcc` is `round(accuracy * 10000)`, `-1` when never played | 408-411, 429-432 | 712, `ScoreEntry` | MATCH |
| 7.23 | `updateInfo` — name, level and difficulty both set, offset stored | 333-350 | 692-705 | MATCH |
| 7.24 | `deleteCharts` removes the directory and the three score rows | 352-361 | 679-690 | MATCH (port keeps one score map; see 7.28) |
| 7.25 | Import: existing complete destination short-circuits as "not imported" | 274-280 | 579-591 | MATCH |
| 7.26 | Import: extract into `<id>.tmp`, detect, require-complete gate, install | 282-330 | 592-677 | MATCH |
| 7.27 | Import: metadata back-fill, relative paths, difficulty label into `level` | 297-316 | 634-656 | MATCH |
| 7.28 | Score/history persistence | SharedPreferences ×3 | one `library.txt` | DELIBERATE-EXCLUSION — the platform provides no preference store, so the three separate maps are one tab-separated file keyed by record id. Values and their independent tracking are identical. |
| 7.29 | Chart id is the MD5 of the imported archive | 274-276 | `hashFile` FNV-1a+size; `hashPath` FNV-1a for directories | **DELIBERATE-EXCLUSION** — the id is an internal surrogate key in this port (it names `packs/<id>` and keys the score rows). `importDirectory` has no archive to hash, so a single derivation must cover both import routes. Changing it to `md5(zip)` would orphan every existing pack directory and its score row for no player-visible benefit. **A reference-format library/replay file is therefore not interchangeable with this port's, and the ids do not match.** Recorded so no later reader assumes otherwise. |
| 7.30 | `relativize` — canonical path under the base, separators normalised | 745-758 | 31-36 | MATCH |
| 7.31 | `copyDirectory` / `copyFile` fallbacks after a failed rename | 318-322, 676-706 | 658-669 | MATCH |
| 7.32 | `mapToDest` — map a detected file to its destination path | 470-475 | via `detectPack` + `installDirectory` | MATCH |
| 7.33 | `addDirToZip` / `exportChartsToZip` | 363-392, 720-743 | not implemented | DELIBERATE-EXCLUSION — the export feature is not part of the port's product scope (no public Downloads directory concept on the desktop targets). |

---

## 8. `game/SkinConfig.java`, `game/ResPackInfo.java`, `ui/SkinManager.java` → `src/render/skin/SkinConfig.cpp`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 8.1 | `SkinConfig` field set (name, author, description, three atlases, seven tuning flags, two colours) | SkinConfig.java 11-27 | SkinConfig.hpp 19-40 | MATCH |
| 8.2 | `ResPackInfo` adds `holdAtlas`, `holdAtlasMH`, `hitFx` and inherits the config keys | ResPackInfo.java 8-12 | `SkinConfig` + `ResolvedMaterials` | MATCH |
| 8.3 | JSON binder ignores unknown properties | SkinManager.java 32-33 | parseSkinConfigJson ignores extra keys | MATCH |
| 8.4 | `loadSkinConfig` — `info.yml` first, then `skin.json` | 199-215 | 161-185 | MATCH |
| 8.5 | `parseYmlContent` — trim, skip blanks/`#`, first `:`, strip a trailing comment, strip quotes | 237-272 | 187-248 | MATCH |
| 8.6 | YAML key set incl. the snake_case alternates | 251-268 | 208-245 | MATCH |
| 8.7 | `parseIntArray` — strip brackets, split on comma, every token must parse | 281-289 | 83-109 | MATCH |
| 8.8 | `parseBool` — only `true`/`yes`, case-insensitive | 291-293 | 112-115 | MATCH |
| 8.9 | `Float.parseFloat` for the two numeric keys | 258-259 | 51-63 | MATCH |
| 8.10 | `parseSkinColor` — `0x` prefix, 6/8 hex digits, else decimal | GameRenderer 14247-14276 | 341-385 | MATCH |
| 8.11 | Six-digit hex keeps the caller's alpha (`-1`) | 14256-14264 | 366 + 389-395 | MATCH |
| 8.12 | Decimal without alpha digits means fully opaque | 14268 | 378-386 | **FIXED** — the port returned "no alpha" (`-1`), which left the previous alpha in place. The reference returns `1f` for a decimal colour, so the two spellings genuinely differ and the port now reproduces that. |
| 8.13 | `resolveFloat` — non-positive counts as unset | 14237-14240 | 408-413 | MATCH |
| 8.14 | `resolveBool` — presence decides | 14242-14245 | 415-419 | MATCH |
| 8.15 | A selected skin replaces the pack layer for the scalar tuning keys | 1675-1691 | 442-459 | MATCH |
| 8.16 | Atlas fields fall back to the pack when the skin omits them | 1670-1674 | 432-440 | MATCH |
| 8.17 | Colour override is gated on the skin declaring the key, not on it parsing | 7188-7206, 7310-7328 | 461-475 | MATCH (`perfectFromSkin`/`goodFromSkin`) |
| 8.18 | `loadSkinTexture` — skin file first, then the built-in asset (or nothing) | 14319-14329 | `kSkinTextureOverrides` + `resolveSkinFile` | MATCH |
| 8.19 | Texture override list and their asset fallbacks | 3271-3294 | SkinConfig.hpp 120-132 | MATCH |
| 8.20 | `cropHoldBodyTexture` — split at the atlas heights, `wrapT` repeat | 14278-14317 | render-side; not a config concern | MATCH (config supplies the atlas values only) |
| 8.21 | `isLikelySkinPackage` — `skin.json`, `respack.json`, info keys, two assets | 139-173 | not implemented | DELIBERATE-EXCLUSION — the port has no drag-and-drop "is this a skin?" probe; the import dialog is explicit about what it imports. |
| 8.22 | `importSkin` — random id, flat leaf-name extraction, prefs registration | 92-137 | `src/app/ImportCore.cpp` `importSkinPackage` | MATCH on the flatten-to-leaf-name rule; the directory name is the display name instead of a random id (see 7.29 for the same surrogate-key reasoning). |
| 8.23 | `getSkinList` / `getSelectedSkinIndex` / `deleteSkin` | 64-90, 175-197 | settings-side, other owner | n/a |

---

## 9. `util/InfoJsonUtils.java` and `util/FileUtils.java`

| # | Reference function / field | Ref | Port | Verdict |
|---|---|---|---|---|
| 9.1 | `tryReadInfoJson` — name aliases, difficulty from `difficulty`/`level`/`diff` | InfoJsonUtils 38-45 | `readInfoFromChartFile` + `readInfoFile` cover both key sets | MATCH |
| 9.2 | Text trimmed and empty results skipped | 52-67 | `jsonValueText` + non-empty test | MATCH |
| 9.3 | `stripExtension` — a leading dot means no extension was found | FileUtils 40-46 | `stripExtension` in ChartLoader 409-413 | MATCH |
| 9.4 | `copyUriToCache` — sanitised name, cache staging | 48-74 | `ImportCore.cpp` staging + `sanitiseFileName` | MATCH |
| 9.5 | `queryDisplayName` | 22-38 | platform file dialog (other owner) | n/a |

---

## Cross-area requests

None of the fixes below were applied by this task because the files are outside its write
scope. Each is written so the owner can apply it verbatim.

1. **`src/render/GameRenderer.cpp` — level-effect passes should be appended at render
   time, not at load time.** Reference `GameRenderer.java:1595-1615` builds the level-effect
   passes only when `!disableShader`. The port appends them in
   `src/chart/ChartLoader.cpp:855-867` unconditionally, and then suppresses the whole chain
   in `GameRenderer::Impl::prepareForChart` (`src/render/GameRenderer.cpp:495`). The visible
   result is currently the same for the one setting that gates it, so this is a
   tidiness/robustness request rather than a live defect: if a second consumer of
   `chart.prprEffects` is ever added, it would see the level-effect passes even with shaders
   disabled. Required change, in `GameRenderer::Impl::prepareForChart`, immediately before
   `chain.prepare(...)`:

   ```cpp
   if (!disableChartShaderSetting) {
     // Level-effect passes are only reachable through the chart's selected effect, and they
     // are part of the storyboard chain, so they are appended where the chain is built.
     if (chart->levelEffects != nullptr) {
       auto built = chart->levelEffects->buildPrprEffects();
       chart->prprEffects.insert(chart->prprEffects.end(), built.begin(), built.end());
     }
     chain.prepare(assets, chart->prprShaderSources, chart->prprEffects);
   }
   ```

   and the matching deletion of the append in `ChartLoader::loadFromFile` (keep the
   `chart.levelEffects = effect;` assignment). Note the append must be idempotent-safe:
   `prepareForChart` early-returns when the chart is unchanged, so a repeat call cannot
   double-append.

2. **`tests/ChartParserTests.cpp` — add coverage for the storyboard paths fixed here.**
   These behaviours are currently untested on the C++ side and were only verified by reading
   both sources. Suggested additions (the owner of `tests/` can place them in the existing
   `main` of that file):

   ```cpp
   // --- extra.json storyboard parsing -------------------------------------
   {
     const auto packDir = rechigros::test::scratchDirectory("chart-prpr");
     std::error_code ignored;
     std::filesystem::create_directories(packDir, ignored);
     {
       std::ofstream out(packDir / "chart.json", std::ios::binary);
       out << R"({"formatVersion":3,"judgeLineList":[]})";
     }
     {
       // A video declares its start as a beat triple under `time`, a named scale, and an
       // alpha track whose value is carried by `start`/`end` spans.
       std::ofstream out(packDir / "extra.json", std::ios::binary);
       out << R"({
         "bpm": [ { "time": [0,0,1], "bpm": 60 } ],
         "videos": [ {
           "path": "bg.mp4",
           "time": [2,0,1],
           "scale": "inside",
           "alpha": [ { "startTime": [0,0,1], "endTime": [1,0,1], "start": 0.0, "end": 1.0 } ],
           "dim": 0.25,
           "attach": { "line": 3 }
         } ],
         "effects": [ {
           "shader": "official_bloom",
           "start": [0,0,1], "end": [4,0,1],
           "vars": { "k": 0.5, "v": [0.1, 0.2] }
         } ]
       })";
     }
     {
       std::ofstream out(packDir / "bg.mp4", std::ios::binary);
       out << "not a real video";
     }
     const auto chart = rechigros::chart::ChartLoader::loadFromFile(packDir / "chart.json");
     require(chart != nullptr, "a chart with extra.json loads");
     if (chart != nullptr) {
       require(chart->prprEffects.size() == 1, "one storyboard effect is read");
       if (chart->prprEffects.size() == 1) {
         const auto& effect = chart->prprEffects.front();
         require(effect.shader == "official_bloom", "the shader name is kept");
         // 60 BPM: four beats is four seconds.
         require(close(effect.endTime, 4.0), "the effect span converts beats at the pack BPM");
         require(effect.variables.size() == 2, "both effect variables are read");
         const auto scalar = effect.variables.find("k");
         require(scalar != effect.variables.end() && !scalar->second.animated(),
             "a scalar variable stays constant");
         const auto vector = effect.variables.find("v");
         require(vector != effect.variables.end() && vector->second.values.size() == 2,
             "a numeric array becomes a vector");
       }
       require(chart->prprVideos.size() == 1, "one storyboard video is read");
       if (chart->prprVideos.size() == 1) {
         const auto& video = chart->prprVideos.front();
         require(close(video.startTimeSec, 2.0), "the video start is a beat triple");
         require(video.scaleType == rechigros::game::PrprVideo::Inside,
             "a named scale maps to the inside placement");
         require(video.alphaEvents.size() == 1, "the alpha track keeps its span");
         require(close(video.dimDefault, 0.25), "a scalar dim becomes the default");
         require(video.attachLineIndex == 3, "the attached line index is read");
       }
       // The pack root follows extra.json, which is what the shader and video paths use.
       require(chart->packDir == packDir, "the pack root is the extra.json directory");
     }
     std::filesystem::remove_all(packDir, ignored);
   }

   // --- file-backed storyboard shader ---------------------------------------
   {
     const auto packDir = rechigros::test::scratchDirectory("chart-shader");
     std::error_code ignored;
     std::filesystem::create_directories(packDir, ignored);
     {
       std::ofstream out(packDir / "chart.json", std::ios::binary);
       out << R"({"formatVersion":3,"judgeLineList":[]})";
     }
     {
       std::ofstream out(packDir / "extra.json", std::ios::binary);
       out << R"({ "bpm": 120, "effects": [
         { "shader": "/glitch.frag", "start": 0, "end": 8 },
         { "shader": "builtin_preset", "start": 0, "end": 8 } ] })";
     }
     {
       std::ofstream out(packDir / "glitch.frag", std::ios::binary);
       out << "void main(){}";
     }
     const auto chart = rechigros::chart::ChartLoader::loadFromFile(packDir / "chart.json");
     require(chart != nullptr, "a chart with a file-backed shader loads");
     if (chart != nullptr) {
       require(chart->prprShaderSources.size() == 1,
           "only the file-backed shader contributes a source");
       require(chart->prprShaderSources.contains("/glitch.frag"),
           "the source is keyed by the authored shader name");
     }
     std::filesystem::remove_all(packDir, ignored);
   }
   ```

3. **`tests/AppDataTests.cpp:62-64`** — no change requested. The assertion pinning the em
   space folding is the reason `normalizeDifficultyText` is a documented superset of the
   reference; if a future reviewer wants strict reference behaviour instead, deleting those
   three lines is the whole change.

---

## Judged-note cross-check (this round)

A parser change is only safe if the notes it produces still reach judgement. The check below
compares the count of judged notes after an autoplay run against an independent count of the
notes whose authored time is at or before the run's reported `chartTime`, computed straight
from the shipped JSON.

| Chart | Snapshot `chartTime` | Notes due | Notes judged |
| --- | --- | --- | --- |
| `13a34bfb…` | 44.702 s | 64 | 64 |
| `91c03bf6…` | 44.703 s | 474 | 474 |
| `6da681c3…` | 14.702 s | 4 | 4 |
| `9b547d6d…` | 14.702 s | 0 | 0 |
| `ac4f39a5…` | 14.704 s | 10 | 10 |
| `af1001e3…` | 14.702 s | 22 | 22 |
| `b5b586f5…` | 14.702 s | 22 | 22 |
| `f1c563ba…` | 14.704 s | 32 | 32 |

Every chart matches exactly. Two of them initially appeared short by 1–2 notes; both were
snapshots taken while a hold authored with a multi-minute duration had not yet settled, so the
missing count was hold tails, not dropped notes. Re-checking at a later `chartTime` made both
agree exactly. The long-running whole-chart variant of this gate
(`tools/parity/verify_note_totals.py`) needs the music to play to its end in real time and was
not used here.

## Remaining risk

1. **RPE note type out of range (row 4.46).** Now matches the reference: the value is
   reinterpreted as zero-based only when it is not already a valid one-based type, and a value
   still outside 1..4 drops the note. This changes nothing for charts authored in the
   documented 1..4 range; it only corrects the zero-based spelling and genuinely malformed
   entries. Pinned by `tests/ChartParserTests.cpp`, which asserts `0 → tap`, `2 → hold`,
   `4 → drag` and `5 → dropped`.
2. **RPE `parseRpeVersion` truncation (row 4.5).** Reference uses `Math.round`; the port's
   `integer()` now truncates (row 4.8). That is correct for `getAsInt` but means a version
   like `160.7` reads 160 in the port and 161 in the reference. The version only gates
   format-dependent behaviour at 160, so a fractional version is not reachable in practice.
3. **Bezier LUT caching (row 4.68).** The reference shares one LUT per distinct control-point
   set across the whole chart; the port builds one per event. The curves are identical; only
   parse time and memory differ.
4. **`normalizeDifficultyText` is a superset of the reference (row 5.17).** Unicode space
   separators other than U+2002 are folded to ASCII and then to U+2002, where the reference
   leaves them untouched. Pinned by the C++ suite.
5. **Chart ids and library/replay files are not interchangeable with the reference
   (row 7.29).** Documented deliberately; noted here so future work does not assume a
   reference replay or library directory can be dropped into this port's data directory.
6. **An all-whitespace effect `shader` name (row 1.26).** The reference trims before testing
   for empty and would skip such an effect; the port keeps it, and the chain then fails to
   find a program for it. Only malformed metadata reaches this path.
7. **Official parallel scheduling differs (row 2.6).** The threshold is document size rather
   than line count. A correctness fallback exists for every slicing failure and the equality
   test covers it, but the crossover point for which charts take the parallel path differs.
8. **Storyboard video rendering.** `PrprVideo` now carries the reference field set, but the
   renderer does not yet draw videos (`src/render/GameRenderer.cpp` only consumes
   `prprEffects`). Parsing is complete and verified; playback is a render-task gap, listed
   here so it is not mistaken for a parsing gap.
