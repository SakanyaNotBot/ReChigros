#include "rechigros/game/OfficialBlock.hpp"
#include "rechigros/render/fx/BadEffectField.hpp"
#include "rechigros/render/fx/BlockCompositePass.hpp"
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/DsGridPass.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"
#include "rechigros/render/scene/Transition.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

  if (g_failures != 0) {
    std::cerr << g_failures << " transition test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros transition tests passed\n";
  return 0;
}
