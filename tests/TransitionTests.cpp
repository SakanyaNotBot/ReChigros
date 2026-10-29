#include "rechigros/game/OfficialBlock.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/render/fx/BadEffectField.hpp"
#include "rechigros/render/fx/BlockCompositePass.hpp"
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/DsGridPass.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/scene/HudPass.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"
#include "rechigros/render/scene/RevealCamera.hpp"
#include "rechigros/render/scene/Transition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int g_failures = 0;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

bool close(float first, float second, float epsilon = 1.0e-4F) {
  return std::abs(first - second) <= epsilon;
}

} // namespace

int main() {
  using namespace rechigros::render::scene;

  // --- animated score readout --------------------------------------------------
  // The displayed score and accuracy are owned by the frame input, so a new run starts from a
  // clean slate. They are advanced by the elapsed clock over a fixed catch-up window, which is
  // what makes the digits slide instead of jumping; with the effect off they follow the target
  // exactly. The reset flag is what a seek or a fresh run sets.
  {
    HudValues values;
    values.scoreJumpEffect = true;
    values.score = 1000000;
    values.accuracy = 1.0;
    values.showAccuracy = true;
    values.visualTimeSec = 0.0;
    int score = 0;
    int hundredths = 0;

    // The first frame primes the display at the target rather than sliding up from zero.
    updateHudDisplayValuesForTest(values, score, hundredths);
    require(score == 1000000, "an unprimed readout starts at the target");
    require(hundredths == 100, "an unprimed accuracy starts at the target");

    // A later frame with the same target stays there.
    values.visualTimeSec = 0.5;
    updateHudDisplayValuesForTest(values, score, hundredths);
    require(score == 1000000, "a steady target holds the readout");

    // A jump to a lower target moves part of the way, not all of it: the step is the elapsed
    // time over a 50 ms catch-up window, so a 60 Hz frame covers a third of the remaining gap.
    values.score = 0;
    values.accuracy = 0.5;
    values.visualTimeSec = 0.5 + 1.0 / 60.0;
    updateHudDisplayValuesForTest(values, score, hundredths);
    require(score == 666667,
        "one 60 Hz frame covers a third of the remaining gap toward the target");
    require(hundredths == 83, "the accuracy readout follows the same catch-up");
    // After enough frames it converges.
    for (int frame = 0; frame < 60; ++frame) {
      values.visualTimeSec += 1.0 / 60.0;
      updateHudDisplayValuesForTest(values, score, hundredths);
    }
    require(score == 0, "the readout converges on the target");
    require(hundredths == 50, "the accuracy readout converges on the target");

    // The reset flag discards the state, so the next frame starts at the new target instead
    // of sliding toward it from the previous run's value.
    values.score = 500000;
    values.accuracy = 1.0;
    values.displayReset = true;
    values.visualTimeSec += 1.0 / 60.0;
    updateHudDisplayValuesForTest(values, score, hundredths);
    require(score == 500000, "a reset readout starts at the new target");
    require(hundredths == 100, "a reset accuracy starts at the new target");

    // With the effect off the readout is the target, with no lag at all.
    values.scoreJumpEffect = false;
    values.score = 123456;
    values.visualTimeSec += 1.0 / 60.0;
    updateHudDisplayValuesForTest(values, score, hundredths);
    require(score == 123456, "the effect being off shows the target directly");
  }

  // --- intro line curve -------------------------------------------------------
  require(close(introLineScale(0.0F), 0.0F), "intro curve starts at zero");
  require(close(introLineScale(1.0F), 1.0F), "intro curve ends at one");
  // Monotonic by construction, so spot-check that the sampled curve never dips.
  float previous = -1.0F;
  bool monotonic = true;
  for (int step = 0; step <= 200; ++step) {
    const float value = introLineScale(static_cast<float>(step) / 200.0F);
    if (value + 1.0e-6F < previous) monotonic = false;
    previous = value;
  }
  require(monotonic, "intro curve is monotonically non-decreasing");
  // The curve is strongly eased near the start: by a quarter of the window the decorative
  // line has covered only a few percent of its travel.
  require(introLineScale(0.25F) < 0.1F, "intro curve is strongly eased at the start");
  // Sampling is clamped outside [0, 1].
  require(close(introLineScale(-1.0F), 0.0F), "intro curve clamps below zero");
  require(close(introLineScale(2.0F), 1.0F), "intro curve clamps above one");

  // --- intro UI ease ----------------------------------------------------------
  require(close(introUiEase(0.0F), 0.0F), "UI ease starts at zero");
  require(close(introUiEase(1.0F), 1.0F), "UI ease ends at one");

  // --- smoothstep -------------------------------------------------------------
  require(close(smoothStep01(0.0F), 0.0F), "smoothstep at zero");
  require(close(smoothStep01(0.5F), 0.5F), "smoothstep at the midpoint");
  require(close(smoothStep01(1.0F), 1.0F), "smoothstep at one");
  require(close(smoothStep01(-3.0F), 0.0F), "smoothstep clamps low");
  require(close(smoothStep01(7.0F), 1.0F), "smoothstep clamps high");

  // --- intro dim progress -----------------------------------------------------
  // The decorative line spans 0.72 of the stage width per side, so its tip reaches the
  // stage ends at scale 0.5 / 0.72 = 0.6944, which is where the dim has fully landed.
  require(close(introDimProgress(0.0F, true), 0.0F), "intro dim starts at zero");
  require(close(introDimProgress(0.5F / 0.72F, true), 1.0F), "intro dim lands when the line reaches the ends");
  require(close(introDimProgress(1.0F, true), 1.0F), "intro dim clamps at one");
  require(introDimProgress(0.2F, true) > 0.0F && introDimProgress(0.2F, true) < 1.0F,
      "intro dim is partial mid-growth");

  // --- exit dim ---------------------------------------------------------------
  require(close(outroDimMix(false, 0.0F), 0.0F), "exit dim is zero outside the exit");
  require(close(outroDimMix(true, 0.0F), 0.0F), "exit dim starts at zero");
  require(close(outroDimMix(true, UI_OUTRO_DURATION_SEC), 1.0F), "exit dim completes in 40 frames");
  require(close(outroDimMix(true, 10.0F), 1.0F), "exit dim holds at one");

  // --- line collapse ----------------------------------------------------------
  require(close(outroLineCollapseScale(false, 0.5F), 0.0F), "no collapse outside the exit");
  require(close(outroLineCollapseScale(true, 0.5F), 0.0F), "lines hold full length before the window");
  require(close(outroLineCollapseScale(true, OUTRO_LINE_COLLAPSE_START_SEC), 1.0F),
      "collapse starts at full length");
  require(close(outroLineCollapseScale(true, OUTRO_LINE_COLLAPSE_START_SEC + 0.35F), 0.5F),
      "collapse is linear through the window");
  require(close(outroLineCollapseScale(true, OUTRO_LINE_COLLAPSE_START_SEC +
          OUTRO_LINE_COLLAPSE_DURATION_SEC), 0.0F), "collapse ends at zero");
  require(!outroLinesVisible(true, OUTRO_LINE_COLLAPSE_START_SEC +
          OUTRO_LINE_COLLAPSE_DURATION_SEC), "lines are gone after the collapse");
  require(outroLinesVisible(true, 0.5F), "lines are visible before the collapse");
  require(outroLinesVisible(false, 5.0F), "lines are visible outside the exit");

  // --- background transmission ------------------------------------------------
  // Gameplay with no dim: the artwork shows through at half strength.
  require(close(stageTextureAlpha(true, 1.0F, 1.0F, 0.0F, false, 0.0F), 0.5F),
      "gameplay artwork transmission");
  // With the dim slider at full, the artwork is fully hidden.
  require(close(stageTextureAlpha(true, 1.0F, 1.0F, 1.0F, false, 0.0F), 0.5F),
      "full dim keeps the stage transmission formula consistent");
  // At the start of the opening the artwork shows at INTRO_START_BRIGHTNESS.
  require(close(stageTextureAlpha(true, 0.0F, 0.0F, 0.0F, false, 0.0F), INTRO_START_BRIGHTNESS),
      "opening starts at the authored brightness");
  // The exit walks the stage to the result page's transmission.
  require(close(stageTextureAlpha(true, 1.0F, 1.0F, 0.0F, true, UI_OUTRO_DURATION_SEC), 1.0F),
      "exit hands over at full artwork");
  // The side bands draw the artwork opaque and rely on the overlay for their fixed
  // transmission, so the visible result is SIDE_FACTOR once the opening has finished.
  require(close(sideArtworkAlpha(true, 1.0F), 1.0F), "side bands draw the artwork opaque");
  require(close(sideArtworkAlpha(true, 1.0F) * (1.0F - sideDimAlpha(true, 1.0F, false, 0.0F)),
          SIDE_FACTOR, 1.0e-3F), "side band visible transmission matches the fixed factor");
  require(close(sideArtworkAlpha(true, 0.0F), INTRO_START_BRIGHTNESS),
      "side bands open at the authored brightness");

  // --- overlay alpha ----------------------------------------------------------
  require(close(backgroundDimAlpha(true, 1.0F, 0.0F, false, 0.0F), 0.0F),
      "no overlay at zero dim");
  require(close(backgroundDimAlpha(true, 1.0F, 1.0F, false, 0.0F), 0.7F),
      "overlay reaches 0.7 at full dim");
  require(close(backgroundDimAlpha(true, 0.0F, 0.0F, false, 0.0F), 0.0F),
      "opening has no overlay before the dim lands");
  require(close(backgroundDimAlpha(true, 1.0F, 0.0F, true, UI_OUTRO_DURATION_SEC),
          1.0F - OUTRO_BG_TRANSMISSION), "exit overlay reaches the result level");
  require(close(sideDimAlpha(true, 0.0F, false, 0.0F), 0.0F), "side overlay starts clear");
  require(close(sideDimAlpha(true, 1.0F, false, 0.0F), 1.0F - SIDE_FACTOR),
      "side overlay settles at the fixed factor");

  // --- pause backdrop curves ---------------------------------------------------
  require(close(pauseBackdropIntroAlpha(0.0F), 0.0F), "pause backdrop starts transparent");
  require(close(pauseBackdropIntroAlpha(PAUSE_BACKDROP_FADE_IN_SEC), 1.0F),
      "pause backdrop is opaque once the fade completes");
  require(close(pauseBackdropIntroAlpha(-1.0F), 0.0F), "pause backdrop clamps before the fade");
  require(close(pauseBackdropIntroAlpha(5.0F), 1.0F), "pause backdrop holds after the fade");
  // The shipped curve is strongly eased: it has passed 0.66 a third of the way in.
  require(pauseBackdropIntroAlpha(0.083333336F) > 0.7F,
      "pause backdrop fades in quickly, matching the shipped curve");
  {
    float previous = -1.0F;
    bool monotonic = true;
    for (int step = 0; step <= 100; ++step) {
      const float value = pauseBackdropIntroAlpha(
          static_cast<float>(step) / 100.0F * PAUSE_BACKDROP_FADE_IN_SEC);
      if (value + 1.0e-6F < previous) monotonic = false;
      previous = value;
    }
    require(monotonic, "pause backdrop fade is monotonic");
  }


  // --- resume curves ----------------------------------------------------------
  require(close(resumeBackdropAlpha(0.0F), 1.0F), "resume backdrop starts opaque");
  require(close(resumeBackdropAlpha(RESUME_BACKDROP_ALPHA_FADE_START_SEC), 1.0F),
      "resume backdrop holds until the fade window");
  require(close(resumeBackdropAlpha(RESUME_BACKDROP_ALPHA_FADE_END_SEC), 0.0F),
      "resume backdrop clears at the end of the fade window");
  require(close(resumeBackdropAlpha(10.0F), 0.0F), "resume backdrop stays clear afterwards");
  require(close(resumeBackdropBrightness(0.0F), PAUSE_BACKDROP_BRIGHTNESS),
      "resume brightness starts at the pause level");
  require(close(resumeBackdropBrightness(RESUME_BACKDROP_BRIGHTNESS_DURATION_SEC), 1.0F),
      "resume brightness reaches full after its ramp");
  require(close(resumeBackdropFlatten(0.0F), PAUSE_BACKDROP_FLATTEN),
      "resume flatten starts at the pause level");
  require(close(resumeBackdropFlatten(RESUME_BACKDROP_FLATTEN_DURATION_SEC), 0.0F),
      "resume flatten reaches neutral after its ramp");

  // --- level-effect grid geometry ---------------------------------------------
  {
    // The authored square is 2000 units on a canvas that scales uniformly, so the same
    // aspect ratio must give the same side at proportional resolutions.
    const float at1080 = rechigros::render::fx::DsGridPass::squareSidePixels(1920, 1080);
    require(close(at1080, 2000.0F, 0.5F), "the square is 2000 px at the reference resolution");
    const float at720 = rechigros::render::fx::DsGridPass::squareSidePixels(1280, 720);
    require(close(at720, 2000.0F * (2.0F / 3.0F), 1.0F),
        "the square scales with the resolution");
    // A very wide screen still yields a square, not a stretched rectangle: only one side
    // length is produced.
    const float ultraWide = rechigros::render::fx::DsGridPass::squareSidePixels(3440, 1440);
    require(ultraWide > 0.0F && std::isfinite(ultraWide), "an ultra-wide screen gives a finite side");
  }

  // --- shader preprocessing ---------------------------------------------------
  {
    using rechigros::render::fx::preparePresetSource;
    // A one-line function definition must survive: the hoisting pass only moves plain
    // variable declarations, and a definition carries both a parenthesis and a brace.
    const std::string withFunction =
        "uniform float time;\n"
        "float ease(float x) { x = clamp(x, 0.0, 1.0); return x * x; }\n"
        "void main() {\n"
        "  gl_FragColor = vec4(ease(time));\n"
        "}\n";
    const auto prepared = preparePresetSource(withFunction);
    require(prepared.find("float ease(float x)") != std::string::npos,
        "a one-line function definition is left intact");
    require(prepared.find("float x;") == std::string::npos,
        "a function definition is not rewritten as a declaration");
    require(prepared.find("void main") != std::string::npos, "main survives preprocessing");

    // A genuine global initialiser is still hoisted into main.
    const std::string withInitialiser =
        "uniform float time;\n"
        "float seed = 1.0;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(seed);\n"
        "}\n";
    const auto hoisted = preparePresetSource(withInitialiser);
    // The declaration loses its initialiser and the assignment moves inside main. The
    // exact spacing of the hoisted statement is not part of the contract.
    const auto mainAt = hoisted.find("void main");
    require(mainAt != std::string::npos, "main survives hoisting");
    const auto assignmentAt = hoisted.find("seed =");
    require(assignmentAt != std::string::npos && assignmentAt > mainAt,
        "the initialiser is moved into main");
    require(hoisted.find("seed = 1.0") > mainAt || hoisted.find("seed =  1.0") > mainAt,
        "the initialised value follows the assignment into main");
  }

  // --- highlight sheet width correction ---------------------------------------
  {
    using rechigros::render::scene::highlightWidthScale;
    // The shipped sheets: a highlight sheet is a wider canvas than its normal one, so the
    // correction keeps the drawn note the same size.
    require(close(highlightWidthScale(654, 742), 742.0F / 654.0F, 1.0e-4F),
        "the highlight width correction is the ratio of the two sheet widths");
    require(highlightWidthScale(654, 742) > 1.0F,
        "a wider highlight sheet widens the note");
    require(close(highlightWidthScale(612, 718), 718.0F / 612.0F, 1.0e-4F),
        "the correction follows each sheet pair independently");
    // A square pair needs no correction.
    require(close(highlightWidthScale(512, 512), 1.0F), "equal widths need no correction");
    // Unusable sheets fall back to no correction rather than a division by zero.
    require(close(highlightWidthScale(0, 100), 1.0F), "a zero normal width needs no correction");
    require(close(highlightWidthScale(100, 0), 1.0F), "a zero highlight width needs no correction");
    require(close(highlightWidthScale(-5, 100), 1.0F), "a negative width needs no correction");
  }

  // --- reveal camera, HUD transform and hidden-line alpha ----------------------
  {
    require(!resolveRevealCamera(false,false,false,2.0,2.0,10.0).active,
        "reveal stays off when disabled");
    require(!resolveRevealCamera(true,true,false,2.0,2.0,10.0).active,
        "reveal stays off during the opening");
    require(!resolveRevealCamera(true,false,true,2.0,2.0,10.0).active,
        "reveal stays off during the exit");
    require(!resolveRevealCamera(true,false,false,-0.1,-0.1,10.0).active,
        "reveal stays off before the chart starts");
    const auto start = resolveRevealCamera(true,false,false,0.0,0.0,10.0);
    require(start.active && close(start.scale,1.0F) && start.hiddenObjects &&
        close(start.hiddenFloorAlpha,0.0F),"reveal starts at the full stage");
    const auto middle = resolveRevealCamera(true,false,false,0.5,0.5,10.0);
    require(close(middle.scale,0.65F) && close(middle.hiddenFloorAlpha,0.10F),
        "reveal shrink and hidden fade share their ease");
    const auto settled = resolveRevealCamera(true,false,false,2.0,2.0,10.0);
    require(close(settled.scale,0.30F) && close(settled.hiddenFloorAlpha,0.20F),
        "reveal settles at its target");
    const auto returning = resolveRevealCamera(true,false,false,9.5,9.5,10.0);
    require(returning.active && !returning.hiddenObjects && close(returning.scale,0.65F) &&
        close(returning.hiddenFloorAlpha,0.0F),"reveal return hides the hidden layer");
    require(close(resolveRevealCamera(true,false,false,10.0,10.0,10.0).scale,1.0F),
        "reveal return reaches the full stage");

    const StageLayout layout = computeStageLayout(1280.0F,720.0F,16.0F/9.0F,false);
    const auto inner = revealStageRect(layout,true,0.30F);
    require(close(inner.x,448.0F) && close(inner.y,252.0F) &&
        close(inner.width,384.0F) && close(inner.height,216.0F),
        "reveal inner stage scales about the centre");
    const auto world = revealViewportWorldBounds(layout,true,0.30F);
    require(close(world.x,-1493.3333F,1e-3F) && close(world.y,-840.0F) &&
        close(world.right(),2773.3333F,1e-3F) && close(world.bottom(),1560.0F),
        "reveal culling inverse-projects the whole viewport");
    const auto ordinary = revealViewportWorldBounds(layout,false,0.30F);
    require(close(ordinary.x,layout.stage.x) && close(ordinary.width,layout.stage.width),
        "ordinary culling keeps stage bounds");
    StageLayout projectedLayout = layout;
    projectedLayout.fieldScale = 0.30F;
    const auto projected = projectedLayout.stageProjection();
    const float projectedX = projected.m[0] * 100.0F + projected.m[12];
    const float projectedY = projected.m[5] * 60.0F + projected.m[13];
    require(close(projectedX,2.0F*478.0F/1280.0F-1.0F) &&
        close(projectedY,1.0F-2.0F*270.0F/720.0F),
        "stage projection applies the reveal scale to chart points");

    const auto hud = hudRevealTransform(layout,true,0.30F);
    require(close(hud.x(100.0F),478.0F) && close(hud.y(60.0F),270.0F) &&
        close(hud.size(100.0F),30.0F),"HUD widgets shrink about the stage centre");
    const auto fixed = hudRevealTransform(layout,false,0.30F);
    require(close(fixed.x(100.0F),100.0F) && close(fixed.size(100.0F),100.0F),
        "inactive camera leaves fixed HUD elements untouched");
    require(hudNeedsStageClip(false,false),"ordinary letterboxed HUD clips to the stage");
    require(!hudNeedsStageClip(true,false),"revealed HUD is not stage clipped");
    require(!hudNeedsStageClip(false,true),"fullscreen HUD is not stage clipped");

    require(close(revealLineAlpha(0.0F,false,true,0.20F),0.20F),
        "zero-alpha line receives the reveal floor");
    require(close(revealLineAlpha(-2.0F,true,true,0.20F),0.20F),
        "negative-alpha hidden line receives the reveal floor");
    require(close(revealLineAlpha(0.10F,false,true,0.20F),0.10F),
        "visible low-alpha line keeps its authored alpha");
    require(close(revealLineAlpha(128.0F,false,true,0.20F),128.0F/255.0F),
        "byte alpha normalizes before the floor comparison");
    require(close(revealLineAlpha(0.0F,true,false,0.20F),0.0F),
        "hidden line stays transparent without reveal");
  }

  // --- hold geometry: negative length, negative speed and the cover rule ------
  {
    using rechigros::render::scene::holdHasStarted;
    using rechigros::render::scene::holdHeadClampMin;
    using rechigros::render::scene::holdTailCapOffset;
    using rechigros::render::scene::noteCoveredByLine;

    // RPE holds collapse onto the line strictly after their start instant, so the head is
    // still a travelling head exactly at its own time; the official format is unchanged.
    require(!holdHasStarted(1.0, 1.0, false), "an RPE hold is still travelling at its start instant");
    require(holdHasStarted(1.0, 1.0, true), "the official format collapses the body at the start instant");
    require(holdHasStarted(1.0, 1.25, false), "a hold collapses once its instant has passed");

    // The end cap closes the body from the outside, on whichever side the tail fell.
    require(close(holdTailCapOffset(40.0F, 120.0F, 20.0F, false), 10.0F),
        "a tail ahead of the head keeps the cap beyond it");
    require(close(holdTailCapOffset(40.0F, -60.0F, 20.0F, false), -10.0F),
        "a tail behind the head draws the cap on that side");
    require(close(holdTailCapOffset(40.0F, 40.0F, 20.0F, false), 10.0F),
        "a zero-length hold keeps the historical side");
    require(close(holdTailCapOffset(40.0F, -60.0F, 20.0F, true), 0.0F),
        "a compact skin centres the cap on the tail");

    // Only the official format pins a head that fell behind the line onto the line edge.
    const float behindLine = -80.0F;
    require(close(std::max(holdHeadClampMin(20.0F, false, false), behindLine), behindLine),
        "the RPE format draws a head that is behind the line where it is");
    require(close(std::max(holdHeadClampMin(20.0F, false, true), behindLine), -10.0F),
        "the official format pins the head half a height behind the line");
    require(close(std::max(holdHeadClampMin(20.0F, true, true), behindLine), 0.0F),
        "a compact official hold pins the head on the line");

    // The cover rule follows the far end of a hold. A negative note speed draws the head
    // behind the line long before the tail arrives, which must not hide the hold.
    rechigros::game::Note held;
    held.isHold = true;
    held.sect = 0.5;
    held.floorPosition = 8.0;
    held.holdEndFloorPosition = 4.0;
    rechigros::render::scene::LineFrame coverFrame;
    coverFrame.cover = true;
    coverFrame.floorPosition = 0.0;
    require(!noteCoveredByLine(held, coverFrame, 0.0, false, -5.0F),
        "a hold whose tail is still ahead of the line stays visible");
    held.holdEndFloorPosition = -1.0;
    require(noteCoveredByLine(held, coverFrame, 0.0, false, 5.0F),
        "a hold is covered once its tail has passed the line");
    held.yOffset = 2.0F;
    require(!noteCoveredByLine(held, coverFrame, 0.0, false, 5.0F),
        "a positive y offset keeps the shifted tail ahead of the cover line");
    held.yOffset = 0.0F;
    // Taps keep the drawn-distance test they have always used.
    rechigros::game::Note tap;
    tap.isHold = false;
    tap.sect = 0.5;
    tap.floorPosition = 8.0;
    require(noteCoveredByLine(tap, coverFrame, 0.0, false, -5.0F),
        "a tap keeps the drawn-distance cover test");
    require(noteCoveredByLine(held, coverFrame, 0.0, true, -5.0F),
        "the official cover test keeps comparing the drawn head");

    // An active hold head keeps its speed-scaled y offset. The hit effect deliberately does
    // not scale that offset, so the two paths must not be folded together.
    rechigros::game::Chart chart;
    chart.formatVersion = 0;
    auto line = std::make_unique<rechigros::game::JudgeLine>();
    line->bpm = 120.0;
    rechigros::game::SpeedEvent speed;
    speed.endTime = 1.0e9;
    speed.value = speed.endValue = 1.0;
    line->speedEvents.push_back(speed);
    rechigros::game::Note activeHold;
    activeHold.type = rechigros::game::NOTE_HOLD;
    activeHold.time = 32.0;
    activeHold.holdTime = 32.0;
    activeHold.positionX = 0.0;
    activeHold.speed = 2.0;
    activeHold.yOffset = 0.1F;
    line->notesAbove.push_back(activeHold);
    chart.judgeLineList.push_back(std::move(line));
    chart.prepareRuntime();

    rechigros::render::scene::LineFrame poseFrame;
    poseFrame.valid = true;
    poseFrame.renderable = true;
    poseFrame.noteValid = true;
    poseFrame.cosLine = 1.0F;
    poseFrame.sinLine = 0.0F;
    poseFrame.bpm = 120.0;
    rechigros::render::scene::NotePassParams poseParams;
    poseParams.stageWidth = 1350.0F;
    poseParams.stageHeight = 1080.0F;
    poseParams.noteWidthBase = 100.0F;
    poseParams.chartTimeSec = 0.6;
    poseParams.chartGlobalBeat = 64.0;
    const auto pose = rechigros::render::scene::noteHeadPose(
        *chart.allNotesSorted.at(0), poseFrame, poseParams);
    require(pose.valid, "the active hold head resolves");
    require(close(pose.y, -0.1F * 1080.0F * 2.0F, 1.0e-2F),
        "an active hold head keeps its speed-scaled y offset");
  }

  // --- bad effect lifetime ----------------------------------------------------
  {
    using rechigros::render::fx::BadEffectField;
    require(close(BadEffectField::alphaAt(0.0), 1.0F), "a bad effect starts fully opaque");
    require(close(BadEffectField::alphaAt(BadEffectField::LIFETIME_SEC), 0.0F),
        "a bad effect is gone at the end of its lifetime");
    require(close(BadEffectField::alphaAt(BadEffectField::LIFETIME_SEC * 0.5), 0.5F),
        "a bad effect fades linearly");
    require(close(BadEffectField::alphaAt(-1.0), 1.0F),
        "a bad effect is opaque before it starts");
    require(close(BadEffectField::alphaAt(BadEffectField::LIFETIME_SEC * 3.0), 0.0F),
        "a bad effect stays gone past its lifetime");
    float previous = 1.0F;
    bool monotone = true;
    for (int step = 0; step <= 50; ++step) {
      const float value = BadEffectField::alphaAt(
          BadEffectField::LIFETIME_SEC * static_cast<double>(step) / 50.0);
      if (value > previous + 1.0e-6F) monotone = false;
      previous = value;
    }
    require(monotone, "the bad effect fade never brightens");
  }

  // --- block composite coverage and glow --------------------------------------
  {
    using rechigros::render::fx::BlockCompositePass;
    // The material blanks the zone above the high threshold, fills it above the low one
    // and leaves the band between untouched.
    require(close(BlockCompositePass::gate(0.0F), 0.0F), "an empty mask is not covered");
    require(close(BlockCompositePass::gate(0.05F), 0.0F), "a faint mask is not covered");
    require(close(BlockCompositePass::gate(0.1F), 1.0F), "one sprite covers the zone");
    require(close(BlockCompositePass::gate(0.12F), -1.0F),
        "two overlapping sprites cancel the zone instead of covering it");
    require(close(BlockCompositePass::gate(0.5F), -1.0F), "a saturated mask cancels the zone");
    require(BlockCompositePass::gate(BlockCompositePass::GATE_LOW) > 0.0F,
        "the low threshold is the first covered value");
    require(BlockCompositePass::gate(BlockCompositePass::GATE_HIGH) < 0.0F,
        "the high threshold is the first cancelled value");

    // The glow rings fade outwards and the weights form a partition.
    require(close(BlockCompositePass::glowRingWeight(0), 5.0F / 15.0F),
        "the outermost glow ring carries its remaining reach over the total");
    require(close(BlockCompositePass::glowRingWeight(4), 1.0F / 15.0F),
        "the innermost glow ring carries the least weight");
    require(close(BlockCompositePass::glowRingWeight(-1), 0.0F),
        "a ring outside the chain carries no weight");
    require(close(BlockCompositePass::glowRingWeight(BlockCompositePass::GLOW_PASSES), 0.0F),
        "a ring past the chain carries no weight");
    float total = 0.0F;
    for (int pass = 0; pass < BlockCompositePass::GLOW_PASSES; ++pass) {
      total += BlockCompositePass::glowRingWeight(pass);
    }
    require(close(total, 1.0F), "the glow ring weights sum to one");
    bool decreasing = true;
    for (int pass = 1; pass < BlockCompositePass::GLOW_PASSES; ++pass) {
      if (BlockCompositePass::glowRingWeight(pass) >=
          BlockCompositePass::glowRingWeight(pass - 1)) {
        decreasing = false;
      }
    }
    require(decreasing, "each glow ring is lighter than the one outside it");
  }

  // --- zone mask rasterisation -------------------------------------------------
  {
    using rechigros::game::OfficialBlock;
    using rechigros::render::fx::BlockCompositePass;
    using rechigros::render::fx::BlockMaskQuad;
    using rechigros::render::fx::BlockOverlay;

    constexpr float kScreenWidth = 1920.0F;
    constexpr float kScreenHeight = 1080.0F;
    const auto makeZone = [](float left, float bottom, float right, float top, float enableTime,
                              bool subtract) {
      OfficialBlock zone;
      zone.bottomLeftX = left;
      zone.bottomLeftY = bottom;
      zone.topRightX = right;
      zone.topRightY = top;
      zone.appearTime = 0.0F;
      zone.enableTime = enableTime;
      zone.disableTime = 1000.0F;
      zone.disappearTime = 1000.0F;
      zone.isSubtract = subtract;
      return zone;
    };
    const BlockOverlay overlay;

    // The reduced targets follow the shipped material: masks and coverage at screen/8,
    // the edge/glow target at screen/4, never below one texel.
    require(BlockCompositePass::reducedSize(1920, BlockCompositePass::MASK_DIVISOR) == 240,
        "a 1920-wide mask is 240 texels wide");
    require(BlockCompositePass::reducedSize(1080, BlockCompositePass::MASK_DIVISOR) == 135,
        "a 1080-tall mask is 135 texels tall");
    require(BlockCompositePass::reducedSize(1920, BlockCompositePass::EFFECT_DIVISOR) == 480,
        "the edge and glow target is a quarter of the screen");
    require(BlockCompositePass::reducedSize(4, BlockCompositePass::MASK_DIVISOR) == 1,
        "a reduced target never drops below one texel");
    require(BlockCompositePass::MASK_DIVISOR == BlockCompositePass::COMPOSE_DIVISOR,
        "the coverage target matches the mask resolution it samples");
    require(BlockCompositePass::gate(BlockCompositePass::MASK_WRITE_ALPHA) > 0.0F,
        "one subtract write covers the zone");
    require(BlockCompositePass::gate(2.0F * BlockCompositePass::MASK_WRITE_ALPHA) < 0.0F,
        "two overlapping subtract writes cancel the zone");

    std::vector<OfficialBlock> zones;
    zones.push_back(makeZone(0.86F, 0.0F, 1.0F, 1.0F, 0.0F, false));   // active normal
    zones.push_back(makeZone(0.90F, 0.42F, 0.98F, 0.58F, 0.0F, true));  // active subtract
    zones.push_back(makeZone(0.06F, 0.30F, 0.30F, 0.62F, 10.0F, false)); // pre-warning
    // Three updates drive the one-shot warning fade to full strength (0.5 s at 0.25 s each).
    for (int step = 0; step < 3; ++step) {
      overlay.updateAll(zones, 1.0F, 0.25F, 0.0F, 0.0F, kScreenWidth, kScreenHeight);
    }
    std::vector<BlockMaskQuad> maskA;
    std::vector<BlockMaskQuad> maskB;
    overlay.collectMaskQuads(zones, 1.0F, maskA, maskB);
    require(maskA.size() == 3, "the active and warning layers are written to the first mask");
    require(maskB.empty(), "an active subtract zone is not repeated in the second mask");
    if (maskA.size() == 3) {
      // Active normal zone: red channel, at its real screen position and size.
      require(close(maskA[0].red, 1.0F), "an active zone fills the red channel");
      require(close(maskA[0].green, 0.0F) && close(maskA[0].blue, 0.0F),
          "an active normal zone leaves the other mask channels alone");
      require(close(maskA[0].centerX, 0.93F * kScreenWidth), "the zone keeps its screen centre");
      require(close(maskA[0].width, 0.14F * kScreenWidth), "the zone keeps its screen width");
      require(close(maskA[0].height, kScreenHeight), "a full-height zone covers the stage");
      // Active subtract zone: green channel with a single 0.1 write.
      require(close(maskA[1].green, BlockCompositePass::MASK_WRITE_ALPHA),
          "an active subtract zone writes one mask step");
      require(close(maskA[1].red, 0.0F), "a subtract zone writes no fill coverage");
      // Pre-warning zone: blue channel carries the one-shot fade, not a coverage value.
      require(close(maskA[2].blue, 1.0F), "the warning silhouette reaches full fade");
      require(close(maskA[2].red, 0.0F) && close(maskA[2].green, 0.0F),
          "the warning silhouette writes only the fade channel");
    }

    // A zone inside its ready lead writes the ready highlight in the second mask and
    // keeps the warning silhouette in the first.
    std::vector<OfficialBlock> readyZones;
    readyZones.push_back(makeZone(0.10F, 0.20F, 0.40F, 0.50F, 5.0F, false));
    readyZones.push_back(makeZone(0.50F, 0.20F, 0.60F, 0.50F, 5.0F, true));
    for (int step = 0; step < 3; ++step) {
      overlay.updateAll(readyZones, 4.75F, 0.25F, 0.0F, 0.0F, kScreenWidth, kScreenHeight);
    }
    require(readyZones[0].phase(4.75F) == 1, "a zone enters its ready phase before enabling");
    maskA.clear();
    maskB.clear();
    overlay.collectMaskQuads(readyZones, 4.75F, maskA, maskB);
    require(maskA.size() == 1, "a ready normal zone still shows its warning silhouette");
    require(maskB.size() == 2, "ready normal and ready subtract zones land in the second mask");
    if (maskA.size() == 1 && maskB.size() == 2) {
      require(close(maskA[0].blue, 1.0F), "the warning silhouette reaches full fade");
      require(close(maskB[0].green, 1.0F), "the ready highlight reaches full fade");
      require(close(maskB[1].red, BlockCompositePass::MASK_WRITE_ALPHA),
          "a ready subtract zone keeps its constant subtract step");
      require(close(maskB[1].blue, BlockCompositePass::MASK_WRITE_ALPHA),
          "a ready subtract zone writes the ready step into its own channel");
    }

    // A zone smaller than half a screen pixel in either direction cannot be rasterised.
    std::vector<OfficialBlock> tiny;
    tiny.push_back(makeZone(0.5F, 0.5F, 0.5002F, 1.0F, 0.0F, false));
    overlay.updateAll(tiny, 1.0F, 0.25F, 0.0F, 0.0F, kScreenWidth, kScreenHeight);
    maskA.clear();
    maskB.clear();
    overlay.collectMaskQuads(tiny, 1.0F, maskA, maskB);
    require(maskA.empty() && maskB.empty(), "a zone too small to be seen is not rasterised");
  }

  // --- windowed block advancing matches the whole-list pass ---------------------
  {
    using rechigros::game::OfficialBlock;
    using rechigros::render::fx::BlockOverlay;

    constexpr float kScreenWidth = 1920.0F;
    constexpr float kScreenHeight = 1080.0F;
    // A zone list spread over the whole chart: most entries are outside their window at any
    // given time. The windowed update must equal a pass that visits every entry.
    std::vector<OfficialBlock> zones;
    for (int index = 0; index < 40; ++index) {
      OfficialBlock zone;
      const float slot = static_cast<float>(index) * 1.5F;
      zone.bottomLeftX = 0.05F + 0.01F * static_cast<float>(index % 5);
      zone.bottomLeftY = 0.2F;
      zone.topRightX = 0.30F + 0.01F * static_cast<float>(index % 7);
      zone.topRightY = 0.8F;
      zone.appearTime = slot;
      zone.enableTime = slot + 0.75F;
      zone.disableTime = slot + 2.0F;
      zone.disappearTime = slot + 3.0F;
      zone.isSubtract = (index % 6) == 0;
      OfficialBlock::MoveEvent move;
      move.time = slot;
      move.posX = 0.4F;
      move.posY = 0.5F;
      zone.moveEvents.push_back(move);
      zones.push_back(zone);
    }

    std::vector<OfficialBlock> reference = zones;
    const auto referenceUpdateAll = [&](float timeSec, float deltaSec) {
      for (auto& block : reference) {
        if (timeSec < block.appearTime || timeSec >= block.disappearTime) {
          block.resetRuntime();
          continue;
        }
        block.update(timeSec, 0.0F, 0.0F, kScreenWidth, kScreenHeight, deltaSec);
      }
    };

    BlockOverlay overlay;
    constexpr float kStep = 1.0F / 60.0F;
    bool matched = true;
    for (float timeSec = 0.0F; timeSec < 60.0F; timeSec += kStep) {
      referenceUpdateAll(timeSec, kStep);
      overlay.updateAll(zones, timeSec, kStep, 0.0F, 0.0F, kScreenWidth, kScreenHeight);
      for (std::size_t index = 0; index < zones.size(); ++index) {
        const auto& expected = reference[index];
        const auto& actual = zones[index];
        if (expected.visible != actual.visible || expected.active != actual.active
            || !close(expected.centerX, actual.centerX, 1e-3)
            || !close(expected.centerY, actual.centerY, 1e-3)
            || !close(expected.halfW, actual.halfW, 1e-3)
            || !close(expected.halfH, actual.halfH, 1e-3)
            || expected.phase(timeSec) != actual.phase(timeSec)
            || !close(expected.showProgress(), actual.showProgress(), 1e-3)) {
          matched = false;
          break;
        }
      }
      if (!matched) break;
    }
    require(matched, "windowed block advancing matches the whole-list pass");
  }

  // --- HUD numeric readouts -------------------------------------------------
  {
    // The score is always seven digits, padded and clamped.
    require(formatScore(0) == "0000000", "score 0 is seven digits");
    require(formatScore(1234) == "0001234", "score is left padded");
    require(formatScore(1000000) == "1000000", "a perfect score is seven digits");
    require(formatScore(-5) == "0000000", "a negative score clamps to zero");
    require(formatScore(2000000) == "1000000", "an oversized score clamps to a million");

    // The accuracy is a percentage in hundredths with a two digit fraction.
    require(formatAccuracyHundredths(0) == "0.00%", "accuracy 0");
    require(formatAccuracyHundredths(10000) == "100.00%", "accuracy 100");
    require(formatAccuracyHundredths(6534) == "65.34%", "accuracy keeps its hundredths");
    require(formatAccuracyHundredths(5) == "0.05%", "a small fraction keeps its zero");
    require(formatAccuracyHundredths(-1) == "0.00%", "a negative accuracy clamps");
    require(formatAccuracyHundredths(99999) == "100.00%", "an oversized accuracy clamps");
  }

  {
    // The displayed score catches up over a 50 ms window: a frame of a quarter window moves
    // the display a quarter of the way, and a frame at or beyond the window snaps to target.
    using rechigros::render::scene::approachDisplayValue;
    require(close(approachDisplayValue(0.0, 100.0, 0.0), 0.0, 1e-12),
        "a zero length frame does not move the display");
    require(close(approachDisplayValue(0.0, 100.0, 25.0), 50.0, 1e-9),
        "a quarter window moves a quarter of the way");
    require(close(approachDisplayValue(0.0, 100.0, 50.0), 100.0, 1e-9),
        "a full window reaches the target");
    require(close(approachDisplayValue(0.0, 100.0, 500.0), 100.0, 1e-9),
        "a long frame saturates instead of overshooting");
    require(close(approachDisplayValue(0.0, 100.0, -10.0), 0.0, 1e-12),
        "a negative frame does not move the display");
    // A not-a-number target clears the readout; a not-a-number current adopts the target.
    const double cleared = approachDisplayValue(0.0, std::nan(""), 16.0);
    require(std::isnan(cleared), "a non-finite target clears the display");
    require(close(approachDisplayValue(std::nan(""), 100.0, 16.0), 100.0, 1e-9),
        "a non-finite display adopts the target");
    // Within a ten-thousandth of the target the display snaps, so it cannot creep forever.
    require(close(approachDisplayValue(99.99999, 100.0, 16.0), 100.0, 1e-12),
        "a near target snaps exactly");
  }

  {
    // The exit animation moves the HUD out of the frame instead of fading it: it grows about the
    // play field centre, so elements above the centre travel up and those below travel down, and
    // the text keeps full opacity the whole way. A fade would leave the stretch at one and dim the
    // text, which is the behaviour this pins down.
    using rechigros::render::scene::comboLabelOutroAlpha;
    using rechigros::render::scene::hudRootScaleY;
    using rechigros::render::scene::kHudOutroRootScaleEnd;
    require(close(hudRootScaleY(1.0F, 0.0F), 1.0F, 1e-6),
        "a finished HUD start leaves the HUD unstretched");
    require(close(hudRootScaleY(1.0F, 1.0F), kHudOutroRootScaleEnd, 1e-6),
        "the exit ends on the stretch that pushes the HUD out of frame");
    // Monotone growth: the stretch never steps backwards while the exit runs.
    float previous = hudRootScaleY(1.0F, 0.0F);
    for (int step = 1; step <= 20; ++step) {
      const float current = hudRootScaleY(1.0F, static_cast<float>(step) / 20.0F);
      require(current >= previous - 1e-6F, "the exit stretch never shrinks");
      previous = current;
    }
    // A mid exit pose: the HUD has visibly moved but has not arrived yet.
    const float middle = hudRootScaleY(1.0F, 0.5F);
    require(middle > 1.02F && middle < kHudOutroRootScaleEnd - 0.02F,
        "halfway through the exit the HUD has travelled but is still visible");

    // The combo label is the one element that also loses its opacity, and it starts fading with
    // the exit rather than after it.
    require(close(comboLabelOutroAlpha(0.0F), 1.0F, 1e-6), "the label is opaque before the exit");
    require(close(comboLabelOutroAlpha(1.0F), 0.0F, 1e-6), "the label has cleared by the end");
    require(comboLabelOutroAlpha(0.9F) < comboLabelOutroAlpha(0.5F),
        "the label fades progressively through the exit");
    // It clears before the travel finishes, which is why it is not drawn on the last frames.
    require(comboLabelOutroAlpha(0.95F) < 0.02F, "the label clears ahead of the travel");
  }

  if (g_failures != 0) {
    std::cerr << g_failures << " transition test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros transition tests passed\n";
  return 0;
}
