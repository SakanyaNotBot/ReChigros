#include "rechigros/chart/RpeChartParser.hpp"
#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/chart/OfficialChartParser.hpp"
#include "rechigros/chart/PecChartParser.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "TestScratch.hpp"
#include <string_view>

namespace {
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool close(double a, double b, double epsilon = 1e-5) { return std::abs(a - b) <= epsilon; }
}

int main() {
  using namespace rechigros::game;
  const auto chart = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
  {
    "META":{"offset":125,"RPEVersion":160},
    "BPMList":[
      {"bpm":120,"startTime":[0,0,1]},
      {"bpm":240,"startTime":[1,0,1]}
    ],
    "judgeLineList":[{
      "Texture":"line.png",
      "attachUI":"level",
      "bpmfactor":1,
      "father":-1,
      "isCover":1,
      "eventLayers":[{
        "alphaEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":255,"end":255}],
        "moveXEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":675,"end":675}],
        "moveYEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":0,"end":0}],
        "rotateEvents":[],
        "speedEvents":[{"startTime":[0,0,1],"endTime":[4,0,1],"start":4.5,"end":4.5}]
      }],
      "judgeAreaControl":[
        {"x":0,"judgeArea":1,"easing":1},
        {"x":900,"judgeArea":3,"easing":1}
      ],
      "notes":[
        {"type":1,"above":1,"startTime":[0,0,1],"positionX":0,"judge_area":2.5},
        {"type":2,"above":2,"startTime":[1,0,1],"endTime":[2,0,1],"positionX":75.375,"speed":1.25}
      ],
      "extended":{
        "scaleXEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":0.04,"end":0.045}],
        "scaleYEvents":[],
        "colorEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":[255,0,0],"end":[0,255,0]}],
        "textEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":"RPE","end":"Ready"}],
        "gifEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":0.25,"end":0.75}]
      }
    }]
  })json"});

  require(close(chart.offset, 0.125), "RPE millisecond offset");
  require(chart.rpeVersion == 160, "RPE version");
  require(chart.bpmTimeline && close(chart.bpmTimeline->beatToSec(64), 0.75), "RPE BPM timeline");
  require(chart.judgeLineList.size() == 1, "RPE line count");
  auto& line = *chart.judgeLineList.front();
  require(line.attachUiElementId == 7 && chart.attachUiLineIndex[7] == 0, "RPE attachUI mapping");
  require(line.speedEventLayers.size() == 1
      && close(EventUtils::speedAt(line.speedEventLayers[0], 16), 1.0), "RPE speed normalization");
  require(close(line.calcNoteControl(450, JudgeLine::NOTE_CTRL_JUDGE_AREA, 1), 2), "RPE judge-area control");
  require(line.notesAbove.size() == 1 && line.notesBelow.size() == 1, "RPE above flag");
  require(line.notesAbove[0].type == NOTE_TAP && close(line.notesAbove[0].judgeArea, 2.5), "RPE tap aliases");
  require(line.notesBelow[0].type == NOTE_HOLD && close(line.notesBelow[0].time, 32)
      && close(line.notesBelow[0].holdTime, 16), "RPE hold BPM conversion");
  const auto state = line.fillState(0.0, 16.0F / 9.0F);
  require(close(state.xNorm, 1.0) && close(state.yNorm, 0.5), "RPE stage coordinate conversion");
  require(close(state.alpha, 1.0), "RPE alpha normalization");
  require(close(state.scaleX, 0.04) && close(state.scaleY, 1.0), "RPE line scale semantics");
  require(state.hasColorEvent && close(state.colorR, 1.0) && close(state.colorG, 0.0), "RPE color event state");
  require(state.text == "RPE" && close(state.gifPosition, 0.25), "RPE text and GIF event state");
  require(chart.allNotesSorted.size() == 2, "RPE runtime preparation");

  // ---------------- RPE parity ----------------
  {
    // Note fields: `size`/`xScale` collapse into xScale, and a non-positive value falls
    // back to 1. `size` is always forced to 1 (it is a separate render channel).
    const auto fields = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notes":[
       {"type":1,"startTime":[0,0,1],"size":2.5},
       {"type":1,"startTime":[1,0,1],"xScale":-4},
       {"type":1,"startTime":[2,0,1],"alpha":128},
       {"type":1,"startTime":[3,0,1],"alpha":-1},
       {"type":1,"startTime":[4,0,1],"yOffset":450},
       {"type":1,"startTime":[5,0,1],"visibleTime":999999},
       {"type":1,"startTime":[6,0,1],"judgeArea":0}
     ]}]})json"});
    const auto& fieldLine = *fields.judgeLineList.at(0);
    require(fieldLine.notesAbove.size() == 7, "RPE combined notes form");
    require(close(fieldLine.notesAbove[0].xScale, 2.5) && close(line.notesAbove[0].size, 1.0),
        "RPE size maps to xScale, size forced to 1");
    require(close(fieldLine.notesAbove[1].xScale, 1.0), "RPE non-positive xScale falls back to 1");
    // alpha above 1.5 is treated as byte-scaled: 128/255.
    require(close(fieldLine.notesAbove[2].alpha, 128.0 / 255.0), "RPE byte-scaled alpha");
    // Negative alpha clamps to 0.
    require(close(fieldLine.notesAbove[3].alpha, 0.0), "RPE negative alpha clamps to 0");
    // yOffset is normalised by RPE_HEIGHT (900): 450 -> 0.5.
    require(close(fieldLine.notesAbove[4].yOffset, 0.5), "RPE yOffset normalisation");
    // The 999999 sentinel means "always visible" -> -1.
    require(close(fieldLine.notesAbove[5].visibleTime, -1.0), "RPE visibleTime sentinel");
    // A non-positive judgeArea falls back to 1.
    require(close(fieldLine.notesAbove[6].judgeArea, 1.0), "RPE judgeArea fallback");

    // Note type promotion: 0..3 are promoted to 1..4, so type 0 becomes a Tap and
    // type 2 becomes a Hold (not a Flick).
    const auto types = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notesAbove":[
       {"type":0,"startTime":[0,0,1]},
       {"type":2,"startTime":[1,0,1]},
       {"type":4,"startTime":[2,0,1]}
     ]}]})json"});
    const auto& typeLine = *types.judgeLineList.at(0);
    require(typeLine.notesAbove[0].type == NOTE_TAP, "RPE type 0 promotes to tap");
    require(typeLine.notesAbove[1].type == NOTE_HOLD, "RPE type 2 promotes to hold");
    require(typeLine.notesAbove[2].type == NOTE_DRAG, "RPE type 4 stays drag");

    // Explicit notesAbove/notesBelow take precedence: a populated `notes` array is ignored
    // whenever either split key is present, even if both split arrays are empty.
    const auto precedence = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notesAbove":[],"notes":[
       {"type":1,"startTime":[0,0,1]}]}]})json"});
    require(precedence.judgeLineList.at(0)->notesAbove.empty()
        && precedence.judgeLineList.at(0)->notesBelow.empty(),
        "RPE empty notesAbove suppresses the combined notes form");

    // Hit-effect tint: an explicit tint object wins; otherwise a bare switch reuses the
    // note tint.
    const auto tints = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notesAbove":[
       {"type":1,"startTime":[0,0,1],"tint":[255,0,0],"tintHitEffects":true},
       {"type":1,"startTime":[1,0,1],"tint":[0,255,0],"hitEffectTint":[0,0,255]}
     ]}]})json"});
    const auto& tintLine = *tints.judgeLineList.at(0);
    require(tintLine.notesAbove[0].hasHitEffectTint
        && close(tintLine.notesAbove[0].hitEffectR, 1.0)
        && close(tintLine.notesAbove[0].hitEffectG, 0.0), "RPE tint switch reuses note tint");
    require(tintLine.notesAbove[1].hasHitEffectTint
        && close(tintLine.notesAbove[1].hitEffectB, 1.0)
        && close(tintLine.notesAbove[1].hitEffectR, 0.0), "RPE explicit hit tint wins");

    // Note positionX conversion: raw / (670 * 9/80). 750.375 -> 750.375/75.375 = 9.9577...
    const auto pos = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notesAbove":[{"type":1,"startTime":[0,0,1],"positionX":75.375}]}]})json"});
    require(close(pos.judgeLineList.at(0)->notesAbove[0].positionX, 75.375 / (670.0 * 9.0 / 80.0)),
        "RPE positionX conversion");

    // parseBeat rounds to 3 decimals before scaling by 32, so [1,1,3] = 1.3333 -> 1.333
    // -> 42.656 ticks rather than 42.6667.
    const auto beats = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"notesAbove":[
       {"type":1,"startTime":[1,1,3]},{"type":1,"startTime":[2,0,1]}]}]})json"});
    require(close(beats.judgeLineList.at(0)->notesAbove[0].time, 1.333 * 32.0),
        "RPE beat rounding to 3 decimals then *32");
    require(close(beats.judgeLineList.at(0)->notesAbove[0].rpeGlobalBeat, 1.333 * 32.0),
        "RPE rpeGlobalBeat retains the raw tick");
  }

  // ---------------- RPE speed-layer semantics ----------------
  {
    // RPE speed values are normalised by /4.5, so a raw start/end of 4.5 becomes a rate of
    // exactly 1.0. Worked through: normBeat([1,0,1]) with bpm 120 and bp 120 is
    // beatToSec(32)/(PGR_BEAT/120) = 0.5/0.015625 = 32 normalised beats, so a constant rate
    // of 1.0 over the segment integrates to 32 * 1.0 = 32 floor units.
    const auto one = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"eventLayers":[{"speedEvents":[
       {"startTime":[0,0,1],"endTime":[1,0,1],"start":4.5,"end":4.5}]}]}]})json"});
    require(one.judgeLineList.at(0)->speedEventLayers.size() == 1, "RPE one speed layer");
    const auto& layer = one.judgeLineList.at(0)->speedEventLayers.at(0);
    require(close(EventUtils::speedAt(layer, 16.0), 1.0), "RPE speed normalises by /4.5");
    require(close(EventUtils::getFloorPosition(32.0, layer), 32.0),
        "RPE constant rate integrates to rate * normalised duration");
    // Halfway through the segment the integral is half of that.
    require(close(EventUtils::getFloorPosition(16.0, layer), 16.0),
        "RPE constant rate midpoint");

    // Two layers summing to a rate of 1.0 each therefore cover 64 over the same span; the
    // layered overload sums per-layer integrals rather than summing rates first.
    const auto two = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"eventLayers":[
       {"speedEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":4.5,"end":4.5}]},
       {"speedEvents":[{"startTime":[0,0,1],"endTime":[1,0,1],"start":4.5,"end":4.5}]}]}]})json"});
    require(two.judgeLineList.at(0)->speedEventLayers.size() == 2, "RPE two speed layers");
    require(close(EventUtils::getFloorPosition(32.0, two.judgeLineList.at(0)->speedEvents,
            two.judgeLineList.at(0)->speedEventLayers), 64.0),
        "RPE layered floor position sums the layer integrals");

    // A ramping segment: raw 4.5 -> 9.0 is a rate ramp 1.0 -> 2.0, whose integral over the
    // segment is the mean rate times the span = 1.5 * 32 = 48.
    const auto ramp = rechigros::chart::RpeChartParser::parse(std::string_view{R"json(
    {"META":{"offset":0,"RPEVersion":160},"BPMList":[{"bpm":120,"startTime":[0,0,1]}],
     "judgeLineList":[{"bpmfactor":1,"eventLayers":[{"speedEvents":[
       {"startTime":[0,0,1],"endTime":[1,0,1],"start":4.5,"end":9,"easingType":1}]}]}]})json"});
    const auto& rampLayer = ramp.judgeLineList.at(0)->speedEventLayers.at(0);
    require(close(EventUtils::speedAt(rampLayer, 16.0), 1.5), "RPE linear speed ramp midpoint");
    require(close(EventUtils::getFloorPosition(32.0, rampLayer), 48.0),
        "RPE linear ramp integral is mean rate * span");
  }

  // ---------------- PEC parity ----------------
  {
    // offset: round4((ms - 150)/1000). 0 -> round4(-0.15) = -0.15; 400 -> round4(0.25) = 0.25.
    const auto zero = rechigros::chart::PecChartParser::parse("0\nbp 0 120\n");
    require(close(zero.offset, -0.15), "PEC offset 0ms");
    const auto late = rechigros::chart::PecChartParser::parse("400\nbp 0 120\n");
    require(close(late.offset, 0.25), "PEC offset 400ms");
    // 1 beat = 32 ticks, and with bp 0 120 the line bpm equals the base bpm, so the
    // normalised beat equals the raw tick value and sect = tick * PGR_BEAT/120.
    const auto tap = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 0 1 0\n");
    require(tap.judgeLineList.size() == 1, "PEC line count");
    require(close(tap.judgeLineList[0]->bpm, 120.0), "PEC line bpm is base bpm");
    require(!tap.judgeLineList[0]->invertRotation, "PEC disables rotation inversion");
    require(close(tap.allNotesSorted[0]->time, 32.0), "PEC note time is ticks");
    require(close(tap.allNotesSorted[0]->sect, 0.5), "PEC note sect = 32 * 1.875/120");
    require(close(tap.allNotesSorted[0]->positionX, 0.0), "PEC note positionX");
    // positionX = raw * 9/1024. 512 -> 4.5.
    const auto centred = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 512 1 0\n");
    require(close(centred.judgeLineList[0]->notesAbove[0].positionX, 4.5), "PEC positionX scale");
    // above flag: 0 puts the note below.
    const auto below = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 0 0 0\n");
    require(below.judgeLineList[0]->notesBelow.size() == 1
        && below.judgeLineList[0]->notesAbove.empty(), "PEC above flag");
    // Hold duration: n2 end 2 - start 1 = 1 beat = 32 ticks.
    const auto hold = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn2 0 1 2 0 1 0\n");
    require(close(hold.allNotesSorted[0]->holdTime, 32.0), "PEC hold ticks");
    require(close(hold.allNotesSorted[0]->secht, 0.5), "PEC hold seconds");
    // Fake notes are excluded from allNotesSorted but stay on the line.
    const auto fake = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 0 1 1\n");
    require(fake.judgeLineList[0]->notesAbove.size() == 1 && fake.allNotesSorted.empty(),
        "PEC fake note excluded from runtime");
    // '# speed' attaches to the immediately preceding note; '&' sets its xScale.
    const auto mods = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 0 1 0\n# 2\n& 1.5\n");
    require(close(mods.judgeLineList[0]->notesAbove[0].speed, 2.0), "PEC # speed modifier");
    require(close(mods.judgeLineList[0]->notesAbove[0].xScale, 1.5), "PEC & xScale modifier");
    // cv normalises by 7 and defaults an unparseable value to 1.0 before dividing, so a
    // non-numeric value yields 1/7 rather than 1. With an explicit 7 the result is exactly 1.
    // Note a cv line with fewer than 4 tokens is skipped outright, leaving default speed 1.
    const auto speedDefault = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncv 0 0 abc\n");
    require(close(EventUtils::speedAt(speedDefault.judgeLineList[0]->speedEvents, 0.0), 1.0 / 7.0),
        "PEC cv unparseable value defaults before /7");
    const auto speedShort = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncv 0 0\n");
    require(close(EventUtils::speedAt(speedShort.judgeLineList[0]->speedEvents, 0.0), 1.0),
        "PEC cv with too few tokens is skipped");
    const auto speedSeven = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncv 0 0 7\n");
    require(close(EventUtils::speedAt(speedSeven.judgeLineList[0]->speedEvents, 0.0), 1.0),
        "PEC cv 7 normalises to 1");
    // cm ending coordinates are x/2048 and y/1400.
    const auto move = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncm 0 0 1 1024 700\n");
    const auto moveState = move.judgeLineList[0]->fillState(0.5, 16.0F / 9.0F);
    require(close(moveState.xNorm, 0.5) && close(moveState.yNorm, 0.5), "PEC cm default centre");
    // cm to the far right: x = 2048/2048 = 1.0 -> xNorm 1.0 at the end of the ramp.
    const auto moveRight = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncm 0 0 1 2048 1400\n");
    const auto rightState = moveRight.judgeLineList[0]->fillState(0.5, 16.0F / 9.0F);
    require(close(rightState.xNorm, 1.0), "PEC cm x scale");
    // cp is an instant jump with linear easing and no interpolation.
    const auto jump = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncp 0 0 2048 700\n");
    require(close(jump.judgeLineList[0]->fillState(0.1, 16.0F / 9.0F).xNorm, 1.0), "PEC cp instant x");
    // cr rotates to a target angle over a range.
    const auto rotate = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncr 0 0 1 90\n");
    require(close(rotate.judgeLineList[0]->fillState(0.5, 16.0F / 9.0F).rotateDeg, 90.0),
        "PEC cr rotation");
    // ca sets alpha directly; 255 -> 1.0.
    const auto alpha = rechigros::chart::PecChartParser::parse("0\nbp 0 120\nca 0 0 255\n");
    require(close(alpha.judgeLineList[0]->fillState(0.1, 16.0F / 9.0F).alpha, 1.0), "PEC ca alpha");
    // Alpha special value -1 maps to -255 -> -1.0 normalised (hideLine via the -1 branch).
    const auto hide = rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncf 0 0 1 -1\n");
    const auto hideState = hide.judgeLineList[0]->fillState(0.5, 16.0F / 9.0F);
    require(close(hideState.alpha, -1.0) && hideState.hideLine && hideState.hideNotes,
        "PEC cf -1 alpha maps to hidden line");
    // visibleTime encoding: value = -100 - visibleBeat*10. On a single-tick span the tap at
    // tick 1 samples alpha = the end value -120 -> visibleBeat = 2 -> 2 * 60/120 = 1.0s.
    const auto visible = rechigros::chart::PecChartParser::parse(
        "0\nbp 0 120\nn1 0 1 0 1 0\ncf 0 0 1 -120\n");
    require(close(visible.allNotesSorted[0]->visibleTime, 1.0), "PEC visibleTime encoding");
    // Multi-BPM: bp 0 120 then bp 32 240 makes tick 64 land at 0.5s (32 ticks at 120bpm)
    // plus 32 ticks at 240bpm (0.25s) = 0.75s.
    const auto bpmChange = rechigros::chart::PecChartParser::parse(
        "0\nbp 0 120\nbp 1 240\nn1 0 2 0 1 0\n");
    require(close(bpmChange.allNotesSorted[0]->sect, 0.75), "PEC multi-BPM note time");
    // Line ids are sorted ascending, so lines appear in numeric order regardless of the
    // order the commands were written.
    const auto order = rechigros::chart::PecChartParser::parse(
        "0\nbp 0 120\nn1 5 1 0 1 0\nn1 2 1 0 1 0\n");
    require(order.judgeLineList.size() == 2, "PEC distinct line ids");
    require(close(order.judgeLineList[0]->notesAbove[0].time, 32.0)
        && close(order.judgeLineList[1]->notesAbove[0].time, 32.0), "PEC line ordering");
    // Comment stripping happens before parsing, and blank lines are ignored.
    const auto comments = rechigros::chart::PecChartParser::parse(
        "0 // offset\n\nbp 0 120\nn1 0 1 0 1 0 // tap\n");
    require(comments.allNotesSorted.size() == 1, "PEC comment and blank line handling");
  }

  // --- RPE speed layers -------------------------------------------------------
  {
    // Two speed layers that both cover the same span: layer A runs at 1.0, layer B at 2.0,
    // so the combined instantaneous rate is 3.0. The parser keeps the per-layer lists and
    // also builds a merged curve; both must agree on the rate.
    const std::string rpe = R"({
      "RPEVersion": 160,
      "META": { "offset": 0 },
      "BPMList": [ { "startTime": [0,0,1], "bpm": 120 } ],
      "judgeLineList": [
        {
          "bpmfactor": 1,
          "eventLayers": [
            {
              "speedEvents": [
                { "startTime": [0,0,1], "endTime": [4,0,1], "start": 1.0, "end": 1.0 },
                { "startTime": [4,0,1], "endTime": [8,0,1], "start": 3.0, "end": 3.0 }
              ],
              "moveXEvents": [ { "startTime": [0,0,1], "endTime": [8,0,1], "start": 0.5, "end": 0.5 } ],
              "moveYEvents": [ { "startTime": [0,0,1], "endTime": [8,0,1], "start": 0.3, "end": 0.3 } ],
              "rotateEvents": [ { "startTime": [0,0,1], "endTime": [8,0,1], "start": 0, "end": 0 } ],
              "alphaEvents": [ { "startTime": [0,0,1], "endTime": [8,0,1], "start": 1, "end": 1 } ]
            },
            {
              "speedEvents": [
                { "startTime": [0,0,1], "endTime": [8,0,1], "start": 2.0, "end": 2.0 }
              ]
            }
          ],
          "notes": []
        }
      ]
    })";
    const auto layered = rechigros::chart::RpeChartParser::parse(rpe, {});
    require(layered.judgeLineList.size() == 1, "RPE speed layers: one judge line");
    const auto& line = *layered.judgeLineList.front();
    require(line.speedEventLayers.size() == 2, "both speed layers are kept on the line");
    require(!line.speedEvents.empty(), "a merged speed curve is built");

    // Every merged segment must carry the sum of the active layer rates.
    const auto rateAt = [](const std::vector<rechigros::game::SpeedEvent>& events, double time) {
      double value = 0.0;
      for (const auto& event : events) {
        if (time >= event.startTime && time <= event.endTime) value = event.value;
      }
      return value;
    };
    // RPE speeds are normalised by dividing by 4.5, so a chart speed of N becomes N/4.5.
    constexpr double kSpeedNormalisation = 4.5;
    // 240 ticks per beat at 120bpm: tick 32 is one beat, so the first span covers ticks
    // 0..128 and the second 128..256.
    const double firstRate = rateAt(line.speedEvents, 64.0);
    const double secondRate = rateAt(line.speedEvents, 200.0);
    require(close(firstRate, (1.0 + 2.0) / kSpeedNormalisation),
        "merged rate in the first span is 1 + 2");
    require(close(secondRate, (3.0 + 2.0) / kSpeedNormalisation),
        "merged rate in the second span is 3 + 2");
    // The merged curve and the per-layer integration must agree on the floor position too.
    const double mergedFloor = rechigros::game::EventUtils::getFloorPosition(200.0,
        line.speedEvents, {});
    const double layeredFloor = rechigros::game::EventUtils::getFloorPosition(200.0,
        line.speedEvents, line.speedEventLayers);
    require(close(mergedFloor, layeredFloor, 1.0e-6),
        "the merged curve matches the per-layer integration");
  }

  // --- format detection -------------------------------------------------------
  {
    // Detection reads the content, not the extension: a chart saved under a misleading
    // name must still reach the right parser.
    const auto writeTemp = [](const std::string& name, const std::string& text) {
      const auto path = rechigros::test::scratchDirectory("chart") / name;
      std::ofstream out(path, std::ios::binary);
      out.write(text.data(), static_cast<std::streamsize>(text.size()));
      out.close();
      return path;
    };

    const std::string official = R"({
      "formatVersion": 3,
      "judgeLineList": [
        { "bpm": 120.0,
          "notesAbove": [ { "type": 1, "time": 32, "holdTime": 0, "positionX": 0, "speed": 1 } ],
          "notesBelow": [], "speedEvents": [],
          "judgeLineMoveEvents": [], "judgeLineRotateEvents": [],
          "judgeLineDisappearEvents": [] }
      ]
    })";
    const std::string rpe = R"({
      "RPEVersion": 160, "META": { "offset": 0 },
      "BPMList": [ { "startTime": [0,0,1], "bpm": 120 } ],
      "judgeLineList": [ { "bpmfactor": 1, "eventLayers": [], "notes": [] } ]
    })";

    // An official chart under a `.rpe` name must still parse as an official chart: the
    // RPE parser would see no event layers and lose the note.
    const auto misnamedOfficial = writeTemp("rechigros_misnamed_official.rpe", official);
    const auto fromMisnamed = rechigros::chart::ChartLoader::loadFromFile(misnamedOfficial);
    require(fromMisnamed != nullptr, "a misnamed official chart loads");
    if (fromMisnamed != nullptr) {
      require(fromMisnamed->formatVersion == 3, "the misnamed chart is recognised as official");
      require(fromMisnamed->allNotesSorted.size() == 1, "the misnamed chart keeps its note");
    }

    // An RPE chart under a `.json` name must reach the RPE parser.
    const auto misnamedRpe = writeTemp("rechigros_misnamed_rpe.json", rpe);
    const auto fromRpe = rechigros::chart::ChartLoader::loadFromFile(misnamedRpe);
    require(fromRpe != nullptr, "a misnamed level-editor chart loads");
    if (fromRpe != nullptr) {
      require(fromRpe->rpeVersion == 160, "the misnamed chart is recognised as the editor format");
    }

    // Only the four header keys matter, not any two of them together.
    const std::string judgeLineListOnly = R"({
      "JudgeLineList": [ { "bpmfactor": 1, "eventLayers": [], "notes": [] } ]
    })";
    const auto looseRpe = writeTemp("rechigros_judgeline_only.json", judgeLineListOnly);
    const auto fromLoose = rechigros::chart::ChartLoader::loadFromFile(looseRpe);
    require(fromLoose != nullptr, "a header with only JudgeLineList loads");

    // A plain-text chart still reaches the editor parser.
    const auto pec = writeTemp("rechigros_plain.txt", "0\nbp 0 120\nn1 0 1 0 1 0\n");
    const auto fromPec = rechigros::chart::ChartLoader::loadFromFile(pec);
    require(fromPec != nullptr, "a plain-text chart loads");
    if (fromPec != nullptr) {
      require(fromPec->allNotesSorted.size() == 1, "the plain-text chart keeps its note");
    }

    // --- line.csv -----------------------------------------------------------
    {
      // A CSV beside the chart overrides per-line textures and scales, matched by chart
      // name; unscoped rows apply to any chart.
      const auto packDir = rechigros::test::scratchDirectory("chart");
      std::error_code ignored;
      std::filesystem::create_directories(packDir, ignored);
      const std::string chartJson = R"({
        "formatVersion": 3,
        "offset": 0,
        "judgeLineList": [
          { "bpm": 120.0, "notesAbove": [], "notesBelow": [], "speedEvents": [],
            "judgeLineMoveEvents": [], "judgeLineRotateEvents": [],
            "judgeLineDisappearEvents": [] }
        ]
      })";
      const auto chartPath = packDir / "song.json";
      {
        std::ofstream out(chartPath, std::ios::binary);
        out << chartJson;
      }
      {
        // Quoted field exercises the CSV quoting rules.
        std::ofstream out(packDir / "line.csv", std::ios::binary);
        out << "Chart,Image,LineId,Horz,Vert\n";
        out << "\"song.json\",texture.png,0,2.5,0.75\n";
      }
      {
        std::ofstream out(packDir / "texture.png", std::ios::binary);
        out << "not a real image";
      }

      const auto withCsv = rechigros::chart::ChartLoader::loadFromFile(chartPath);
      require(withCsv != nullptr, "a chart with line.csv loads");
      if (withCsv != nullptr && !withCsv->judgeLineList.empty()) {
        const auto& line = *withCsv->judgeLineList.front();
        require(!line.texture.empty(), "line.csv gives the line a texture");
        require(line.texture.size() >= 11 &&
                line.texture.compare(line.texture.size() - 11, 11, "texture.png") == 0,
            "the texture path is resolved beside the CSV");
        require(line.useOfficialScale, "a line.csv texture uses the official scale");
        require(line.judgeLineScaleEvents.size() == 1, "line.csv writes one scale span");
        if (line.judgeLineScaleEvents.size() == 1) {
          require(close(line.judgeLineScaleEvents.front().start, 2.5),
              "the horizontal scale comes from the CSV");
          require(close(line.judgeLineScaleEvents.front().start2, 0.75),
              "the vertical scale comes from the CSV");
        }
      }

      // Without the CSV the line keeps no texture.
      std::filesystem::remove(packDir / "line.csv", ignored);
      const auto withoutCsv = rechigros::chart::ChartLoader::loadFromFile(chartPath);
      require(withoutCsv != nullptr && !withoutCsv->judgeLineList.empty(),
          "the chart still loads without a CSV");
      if (withoutCsv != nullptr && !withoutCsv->judgeLineList.empty()) {
        require(withoutCsv->judgeLineList.front()->texture.empty(),
            "no CSV means no texture override");
      }
      std::filesystem::remove_all(packDir, ignored);
    }

    std::error_code ignored;
    std::filesystem::remove(misnamedOfficial, ignored);
    std::filesystem::remove(misnamedRpe, ignored);
    std::filesystem::remove(looseRpe, ignored);
    std::filesystem::remove(pec, ignored);
  }

  // --- official block areas ---------------------------------------------------
  {
    const std::string official = R"({
      "formatVersion": 3,
      "judgeLineList": [],
      "blockAreaList": [
        { "topRightPercentage": { "x": 0.72, "y": 0.86 },
          "bottomLeftPercentage": { "x": 0.28, "y": 0.62 },
          "appearTime": 1.5, "enableTime": 2.5, "disableTime": 7.5,
          "disappearTime": 9.0, "isSubtract": true,
          "rotateEvents": [ { "anchor": { "x": 0.4, "y": 0.6 }, "time": 2.0,
              "easeType": 3, "rotation": 45.0 } ],
          "moveEvents": [ { "endPosition": { "x": 0.6, "y": 0.4 }, "time": 3.0,
              "easeTypeX": 1, "easeTypeY": 2 } ],
          "scaleEvents": [ { "anchor": { "x": 0.5, "y": 0.5 }, "time": 4.0,
              "easeTypeX": 0, "easeTypeY": 0, "scale": { "x": 1.5, "y": 0.5 } } ] }
      ]
    })";
    const auto chart = rechigros::chart::OfficialChartParser::parse(std::string_view(official));
    require(chart.blockAreaList.size() == 1, "the official chart carries one block area");
    if (!chart.blockAreaList.empty()) {
      const auto& block = chart.blockAreaList.front();
      require(close(block.topRightX, 0.72) && close(block.topRightY, 0.86),
          "the block keeps its top-right corner");
      require(close(block.bottomLeftX, 0.28) && close(block.bottomLeftY, 0.62),
          "the block keeps its bottom-left corner");
      require(close(block.appearTime, 1.5) && close(block.enableTime, 2.5),
          "the block keeps its appear and enable times");
      require(close(block.disableTime, 7.5) && close(block.disappearTime, 9.0),
          "the block keeps its disable and disappear times");
      require(block.isSubtract, "the subtract flag survives parsing");
      require(block.rotateEvents.size() == 1 && block.moveEvents.size() == 1 &&
              block.scaleEvents.size() == 1, "all three transform tracks are parsed");
      if (block.rotateEvents.size() == 1) {
        require(close(block.rotateEvents.front().anchorX, 0.4) &&
                close(block.rotateEvents.front().rotation, 45.0),
            "rotate event fields are read");
      }
      if (block.moveEvents.size() == 1) {
        require(close(block.moveEvents.front().posX, 0.6) &&
                block.moveEvents.front().easeTypeY == 2, "move event fields are read");
      }
      if (block.scaleEvents.size() == 1) {
        require(close(block.scaleEvents.front().scaleX, 1.5) &&
                close(block.scaleEvents.front().scaleY, 0.5), "scale event fields are read");
      }
      // The phase machine reads runtime state produced by update(), so drive it first.
      auto driven = block;
      constexpr float kScreenW = 1280.0F;
      constexpr float kScreenH = 720.0F;
      driven.update(1.0F, 0.0F, 0.0F, kScreenW, kScreenH, 0.016F);
      require(!driven.visible, "before appear the block is not visible");
      driven.update(2.0F, 0.0F, 0.0F, kScreenW, kScreenH, 0.016F);
      require(driven.visible && driven.phase(2.0F) == 1,
          "between appear and enable the block is ready");
      driven.update(5.0F, 0.0F, 0.0F, kScreenW, kScreenH, 0.016F);
      require(driven.visible && driven.phase(5.0F) == 2,
          "between enable and disable the block is active");
      require(driven.halfW > 0.0F && driven.halfH > 0.0F,
          "an active block resolves its screen geometry");
      driven.update(8.0F, 0.0F, 0.0F, kScreenW, kScreenH, 0.016F);
      require(driven.visible && driven.phase(8.0F) == 0,
          "after disable the block is visible but not active");
      driven.update(9.5F, 0.0F, 0.0F, kScreenW, kScreenH, 0.016F);
      require(!driven.visible, "after disappear the block is gone");
    }
    // A chart without the list must not invent blocks.
    const std::string noBlocks = R"({ "formatVersion": 3, "judgeLineList": [] })";
    require(rechigros::chart::OfficialChartParser::parse(std::string_view(noBlocks)).blockAreaList.empty(),
        "a chart without block areas has none");
  }

  std::cout << "All ReChigros chart parser tests passed\n";
  return 0;
}
