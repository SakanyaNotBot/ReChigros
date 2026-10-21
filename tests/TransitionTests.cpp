#include "rechigros/render/fx/BadEffectField.hpp"
#include "rechigros/render/fx/DsGridPass.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"
#include "rechigros/render/scene/Transition.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

  if (g_failures != 0) {
    std::cerr << g_failures << " transition test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros transition tests passed\n";
  return 0;
}
