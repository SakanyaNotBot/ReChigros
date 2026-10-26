#include "rechigros/game/MessageParticleField.hpp"
#include "rechigros/game/OfficialLevelEffects.hpp"

#include <cmath>
#include <filesystem>
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

// A stepping key holds its own value; an eased key spans startValue to endValue.
rechigros::game::LevelEffectKey steppingKey(float time, float value, int ease = 0) {
  rechigros::game::LevelEffectKey key;
  key.time = time;
  key.value = value;
  key.stepping = true;
  key.startValue = value;
  key.endValue = value;
  key.easeType = ease;
  return key;
}

rechigros::game::LevelEffectKey easingKey(float time, float start, float end, int ease) {
  rechigros::game::LevelEffectKey key;
  key.time = time;
  key.value = start;
  key.stepping = false;
  key.startValue = start;
  key.endValue = end;
  key.easeType = ease;
  return key;
}

} // namespace

int main() {
  using namespace rechigros::game;

  // --- empty track ------------------------------------------------------------
  require(close(OfficialLevelEffects::evaluateTrack({}, 5.0F), 0.0F),
      "an empty track evaluates to zero");

  // --- single key -------------------------------------------------------------
  {
    const std::vector<LevelEffectKey> keys{steppingKey(3.0F, 0.75F)};
    require(close(OfficialLevelEffects::evaluateTrack(keys, 0.0F), 0.75F),
        "a single key holds before its time");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 3.0F), 0.75F),
        "a single key holds at its time");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 9.0F), 0.75F),
        "a single key holds after its time");
  }

  // --- eased span -------------------------------------------------------------
  {
    // Linear ease from 0 at t=0 to 1 at t=10.
    const std::vector<LevelEffectKey> keys{easingKey(0.0F, 0.0F, 1.0F, 0), steppingKey(10.0F, 1.0F)};
    require(close(OfficialLevelEffects::evaluateTrack(keys, 0.0F), 0.0F),
        "linear span starts at its start value");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 5.0F), 0.5F),
        "linear span interpolates at the midpoint");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 10.0F), 1.0F),
        "linear span ends at the next key's value");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 20.0F), 1.0F),
        "a finished span holds the last key");
    // Before the first key the track holds that key's entry value; an eased key's entry
    // value is its own `v`, not the span's end.
    require(close(OfficialLevelEffects::evaluateTrack(keys, -5.0F), 0.0F),
        "before the first key the track holds its entry value");
  }

  // --- ease table -------------------------------------------------------------
  {
    // OutSine (ease 2) rises faster than linear, so the midpoint is above half.
    const std::vector<LevelEffectKey> keys{easingKey(0.0F, 0.0F, 1.0F, 2), steppingKey(1.0F, 1.0F)};
    const float midpoint = OfficialLevelEffects::evaluateTrack(keys, 0.5F);
    require(midpoint > 0.5F, "OutSine passes the midpoint early");
    require(midpoint < 1.0F, "OutSine has not finished at the midpoint");

    // InSine (ease 1) rises slower than linear.
    const std::vector<LevelEffectKey> slow{easingKey(0.0F, 0.0F, 1.0F, 1), steppingKey(1.0F, 1.0F)};
    require(OfficialLevelEffects::evaluateTrack(slow, 0.5F) < 0.5F,
        "InSine passes the midpoint late");

    // Zero (ease 13) never leaves its start value; One (ease 14) jumps immediately.
    const std::vector<LevelEffectKey> zero{easingKey(0.0F, 0.0F, 1.0F, 13), steppingKey(1.0F, 1.0F)};
    require(close(OfficialLevelEffects::evaluateTrack(zero, 0.5F), 0.0F),
        "the Zero ease never leaves its start value");
    const std::vector<LevelEffectKey> one{easingKey(0.0F, 0.0F, 1.0F, 14), steppingKey(1.0F, 1.0F)};
    require(close(OfficialLevelEffects::evaluateTrack(one, 0.5F), 1.0F),
        "the One ease jumps to its end value");
  }

  // --- multi-key walk ---------------------------------------------------------
  {
    // Values 0, 1 and 0 with linear easing, so both spans pass through their midpoint.
    std::vector<LevelEffectKey> keys;
    keys.push_back(easingKey(0.0F, 0.0F, 1.0F, 0));
    keys.push_back(easingKey(10.0F, 1.0F, 0.0F, 0));
    keys.push_back(easingKey(20.0F, 0.0F, 0.0F, 0));
    require(close(OfficialLevelEffects::evaluateTrack(keys, 5.0F), 0.5F), "first span midpoint");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 15.0F), 0.5F), "second span midpoint");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 25.0F), 0.0F), "past the last key");
  }

  // --- zero-length span -------------------------------------------------------
  {
    // Two keys at the same time must not divide by zero; the span resolves to its end.
    const std::vector<LevelEffectKey> keys{easingKey(4.0F, 0.0F, 1.0F, 0), steppingKey(4.0F, 1.0F)};
    const float value = OfficialLevelEffects::evaluateTrack(keys, 4.0F);
    require(std::isfinite(value), "a zero-length span stays finite");
  }

  // --- stepping keys carry explicit start and end values ----------------------
  {
    // A stepping key interpolates from its own start value to the next key's end value,
    // which is how a hold is expressed: the payload keeps `sv` equal to `value`.
    rechigros::game::LevelEffectKey hold;
    hold.time = 1.0F;
    hold.value = 2.0F;
    hold.stepping = true;
    hold.startValue = 2.0F;
    hold.endValue = 2.0F;
    hold.easeType = 0;

    rechigros::game::LevelEffectKey next;
    next.time = 3.0F;
    next.value = 4.0F;
    next.stepping = true;
    next.startValue = 4.0F;
    next.endValue = 4.0F;
    next.easeType = 0;

    const std::vector<LevelEffectKey> keys{hold, next};
    require(close(OfficialLevelEffects::evaluateTrack(keys, 1.0F), 2.0F), "hold starts at sv");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 2.0F), 3.0F), "hold interpolates to the next ev");
    require(close(OfficialLevelEffects::evaluateTrack(keys, 3.0F), 4.0F), "hold ends at the next ev");
  }

  // --- table loading ----------------------------------------------------------
  {
    // The shipped table is optional; loading a missing root must not crash and must
    // report the absence.
    const auto missing = OfficialLevelEffects::load("definitely-not-a-real-asset-root");
    require(missing == nullptr, "a missing level-effect table loads as null");
  }

  // --- the table on a platform whose assets are not files ---------------------
  // A packaged platform keeps its assets inside the application package, where a file stream
  // cannot reach them. The table would then load as null and every chart would silently lose
  // its effect -- on that platform only, never on a desktop tree. The application installs a
  // reader for exactly this case, so the loader is checked with the filesystem deliberately
  // unable to serve the table.
  {
    const std::string tableText = R"({
      "version": "2",
      "songs": { "message": { "effect": "MessageLevelEffects", "hideHud": true } },
      "effects": { "MessageLevelEffects": { "tracks": {}, "settings": {}, "scalars": {} } }
    })";
    const std::filesystem::path absentRoot = "definitely-not-a-real-asset-root";

    // No reader: the filesystem is the only source, so the table is absent.
    OfficialLevelEffects::setAssetReader(nullptr);
    require(OfficialLevelEffects::load(absentRoot) == nullptr,
        "without a reader and without a file the table loads as null");

    // With a reader that serves the same bytes, the table loads even though no file exists.
    std::string served;
    OfficialLevelEffects::setAssetReader(
        [&tableText, &served](const std::filesystem::path& root, std::string_view relative) {
          served = std::string(relative);
          // The loader must ask for the table's own relative path, not a whole-tree scan.
          if (relative != "official_effects/chapter9_phase2.json") return std::vector<std::uint8_t>{};
          (void)root;
          return std::vector<std::uint8_t>(tableText.begin(), tableText.end());
        });
    const auto throughReader = OfficialLevelEffects::load(absentRoot);
    require(throughReader != nullptr, "an installed reader supplies the table without a file");
    require(served == "official_effects/chapter9_phase2.json",
        "the loader asks the reader for the table's relative path");
    const auto selected = OfficialLevelEffects::forChart(*throughReader,
        "8d5072369f4e120775a469e7541dd9e8", "Message", true);
    require(selected != nullptr, "a table read through the asset reader selects its effect");

    // A reader that returns nothing must not mask a file that does exist later in the run.
    OfficialLevelEffects::setAssetReader(
        [](const std::filesystem::path&, std::string_view) { return std::vector<std::uint8_t>{}; });
    require(OfficialLevelEffects::load(absentRoot) == nullptr,
        "a reader with no bytes leaves the table absent");

    // Leave the process as it was found; later cases rely on the file path.
    OfficialLevelEffects::setAssetReader(nullptr);
  }

  // --- the shipped table ------------------------------------------------------
  {
    // The test runs from the build tree, so walk up to the source asset root.
    std::filesystem::path root;
    for (std::filesystem::path candidate = std::filesystem::current_path();
         !candidate.empty(); candidate = candidate.parent_path()) {
      if (std::filesystem::exists(candidate / "assets" / "official_effects" /
              "chapter9_phase2.json")) {
        root = candidate / "assets";
        break;
      }
      if (!candidate.has_parent_path() || candidate.parent_path() == candidate) break;
    }
    if (root.empty()) {
      std::cout << "  (shipped table not found from the working directory; skipping)\n";
    } else {
      const auto table = OfficialLevelEffects::load(root);
      require(table != nullptr, "the shipped level-effect table loads");

      // Selection is by normalised digest: only whitelisted charts carry an effect, and a
      // song name never substitutes for the digest.
      const auto desultory = OfficialLevelEffects::forChart(*table,
          "68d8b27ffc34007d6a01a4aa98faff1e", "Desultory Signals");
      require(desultory != nullptr, "a whitelisted digest selects an effect");
      require(desultory->effectName() == "DesultorySignalsLevelEffects",
          "the selected effect is the Desultory Signals one");
      const auto passes = desultory->buildPrprEffects();
      require(!passes.empty(), "the selected effect builds at least one pass");

      // Every built pass stays mounted for the whole chart.
      for (const auto& pass : passes) {
        require(pass.startTime == 0.0, "a level-effect pass starts at the chart start");
        require(std::isinf(pass.endTime), "a level-effect pass never expires");
        require(pass.global, "a level-effect pass is global");
        require(!pass.variables.empty(), "a level-effect pass carries variables");
      }

      // Bloom is part of the chain and its brightness track is animated.
      bool sawBloom = false;
      for (const auto& pass : passes) {
        if (pass.shader != "official_bloom") continue;
        sawBloom = true;
        const auto it = pass.variables.find("bloomBrightness");
        require(it != pass.variables.end(), "the bloom pass binds its brightness");
        require(it->second.animated(), "the bloom brightness is an animated track");
      }
      require(sawBloom, "the Desultory Signals chain contains bloom");

      // An unlisted digest selects nothing, even with a name that matches a table song.
      require(OfficialLevelEffects::forChart(*table, "00000000000000000000000000000000",
                  "Desultory Signals") == nullptr,
          "an unlisted digest selects nothing even with a known song name");
      require(OfficialLevelEffects::forChart(*table, std::string{}, "Desultory Signals") ==
              nullptr, "an empty digest selects nothing");

      // With story mode off every chart except Message plays as a plain chart.
      require(OfficialLevelEffects::forChart(*table, "68d8b27ffc34007d6a01a4aa98faff1e",
                  "Desultory Signals", false) == nullptr,
          "story mode off suppresses a non-Message level effect");
    }
  }

  // --- message particle field -------------------------------------------------
  {
    using rechigros::game::MessageParticleField;

    // The size curve runs from nothing to full size and stays inside that range.
    require(close(MessageParticleField::sizeCurve(0.0F), 0.0F), "the size curve starts at zero");
    require(close(MessageParticleField::sizeCurve(1.0F), 1.0F), "the size curve ends at one");
    {
      bool insideRange = true;
      bool monotonic = true;
      float previous = -1.0F;
      for (int step = 0; step <= 100; ++step) {
        const float value = MessageParticleField::sizeCurve(static_cast<float>(step) / 100.0F);
        if (value < 0.0F || value > 1.0F) insideRange = false;
        if (value + 1.0e-5F < previous) monotonic = false;
        previous = value;
      }
      require(insideRange, "the size curve stays inside its range");
      require(monotonic, "the size curve never shrinks");
    }

    // Emission: two emitters at ten per second, so one second yields about twenty.
    MessageParticleField field;
    field.reset();
    require(field.count() == 0, "a fresh field is empty");
    for (int frame = 0; frame < 60; ++frame) field.update(1.0F / 60.0F, 1.0F);
    // Two emitters at ten per second over one second, allowing for the carry rounding.
    require(field.count() >= 18 && field.count() <= 22,
        "one second of emission yields about twenty particles");

    // Particles spawn outside the centre and move towards it.
    bool topMovesDown = true;
    bool bottomMovesUp = true;
    int topCount = 0;
    int bottomCount = 0;
    for (int index = 0; index < field.count(); ++index) {
      const auto& particle = field.at(index);
      if (particle.top) {
        ++topCount;
        if (!(particle.velocityY > 0.0F)) topMovesDown = false;
      } else {
        ++bottomCount;
        if (!(particle.velocityY < 0.0F)) bottomMovesUp = false;
      }
    }
    require(topCount > 0 && bottomCount > 0, "both emitters produce particles");
    require(topMovesDown, "top particles move down towards the centre");
    require(bottomMovesUp, "bottom particles move up towards the centre");

    // The constant force accelerates them: the speed grows over time.
    MessageParticleField accelerating;
    accelerating.reset();
    for (int frame = 0; frame < 12; ++frame) accelerating.update(1.0F / 60.0F, 1.0F);
    if (accelerating.count() > 0) {
      const float before = accelerating.at(0).velocityY;
      for (int frame = 0; frame < 60; ++frame) accelerating.update(1.0F / 60.0F, 1.0F);
      const float after = accelerating.at(0).velocityY;
      require(std::abs(after) > std::abs(before), "the constant force speeds particles up");
    }

    // A zero delta must not advance anything.
    MessageParticleField frozen;
    frozen.reset();
    frozen.update(0.0F, 1.0F);
    require(frozen.count() == 0, "a zero delta spawns nothing");

    // The band clip hides particles that sit inside the band and shows the rest.
    MessageParticleField::Particle above;
    above.top = true;
    above.y = 0.1F;
    MessageParticleField::Particle inside;
    inside.top = true;
    inside.y = 0.5F;
    require(MessageParticleField::bandAlpha(above, 0.3F) > 0.9F,
        "a particle well outside the band is fully visible");
    require(MessageParticleField::bandAlpha(inside, 0.3F) < 0.1F,
        "a particle inside the band is hidden");
  }

  if (g_failures != 0) {
    std::cerr << g_failures << " level-effect test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros level-effect tests passed\n";
  return 0;
}
