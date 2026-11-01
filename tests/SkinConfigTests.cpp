// Tests for the resource-pack / skin material configuration. The loader and resolver are
// free of GL and interface dependencies, so every reference field, alias, default and merge
// rule is covered headlessly.

#include "rechigros/render/skin/SkinConfig.hpp"
#include "rechigros/render/asset/ImageCache.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "TestScratch.hpp"

namespace {

int g_failures = 0;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

void requireNear(float actual, float expected, const std::string& message) {
  require(std::abs(actual - expected) < 1.0e-4F,
      message + " (got " + std::to_string(actual) + ", want " + std::to_string(expected) + ")");
}

void requireEqual(const std::string& actual, const std::string& expected,
    const std::string& message) {
  require(actual == expected, message + " (got '" + actual + "', want '" + expected + "')");
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
}

void testTextParser() {
  using namespace rechigros::render::skin;
  const std::string yaml =
      "# skin metadata\n"
      "name: Test Skin\n"
      "author: \"An Author\"\n"
      "description: 'A description'  # trailing comment\n"
      "hit_fx: [5, 6]\n"
      "hold_atlas: [50, 52]\n"
      "holdAtlasMH: [ 50 , 95 ]\n"
      "hit_fx_scale: 1.5\n"
      "hitFxDuration: 0.75\n"
      "hit_fx_rotate: yes\n"
      "hide_particles: true\n"
      "hitFxTinted: false\n"
      "hold_keep_head: YES\n"
      "holdRepeat: 1\n"
      "hold_compact: true\n"
      "color_perfect: 0x80FF0000\n"
      "colorGood: 16711680\n"
      "unknownKey: ignored\n"
      "HITFX: [9, 9]\n";
  const SkinConfig config = parseSkinConfigText(yaml);
  requireEqual(config.name.value_or(""), "Test Skin", "yml name");
  requireEqual(config.author.value_or(""), "An Author", "yml quoted author");
  requireEqual(config.description.value_or(""), "A description", "yml inline comment is stripped");
  require(config.hitFx.has_value() && config.hitFx->size() == 2 && (*config.hitFx)[0] == 5
          && (*config.hitFx)[1] == 6,
      "yml snake_case hit_fx array");
  require(config.holdAtlas.has_value() && (*config.holdAtlas)[1] == 52,
      "yml integer array elements");
  require(config.holdAtlasMH.has_value() && (*config.holdAtlasMH)[1] == 95,
      "yml camelCase atlas array");
  requireNear(config.hitFxScale.value_or(0.0F), 1.5F, "yml snake_case hit_fx_scale");
  requireNear(config.hitFxDuration.value_or(0.0F), 0.75F, "yml camelCase duration");
  require(config.hitFxRotate.value_or(false), "yml 'yes' is true");
  require(config.hideParticles.value_or(false), "yml snake_case boolean");
  require(config.hitFxTinted.has_value() && !*config.hitFxTinted, "yml explicit false is kept");
  require(config.holdKeepHead.value_or(false), "yml boolean is case insensitive");
  require(config.holdRepeat.has_value() && !*config.holdRepeat,
      "yml '1' is not a true spelling in the reference skin reader");
  require(config.holdCompact.value_or(false), "yml snake_case hold_compact");
  requireEqual(config.colorPerfect.value_or(""), "0x80FF0000", "yml colour text is kept verbatim");
  requireEqual(config.colorGood.value_or(""), "16711680", "yml decimal colour text");
  // Keys are case sensitive in the reference reader, and unknown keys are ignored.
  require(config.hitFx.has_value() && (*config.hitFx)[0] == 5, "yml uppercase key is ignored");

  // A malformed value only drops that field; the remaining fields still parse.
  const SkinConfig partial = parseSkinConfigText("name: Keep\nhitFxScale: not-a-number\n"
                                                 "hitFx: [1,]\nauthor: Also Keep\n");
  requireEqual(partial.name.value_or(""), "Keep", "a bad number does not drop other fields");
  requireEqual(partial.author.value_or(""), "Also Keep", "parsing continues after a bad value");
  require(!partial.hitFxScale.has_value(), "a malformed float leaves the field unset");
  require(!partial.hitFx.has_value(), "a malformed array leaves the field unset");
  // The line parser uses a strict integer token: a fractional element rejects the field,
  // while the JSON binder (a different reader) truncates it.
  require(!parseSkinConfigText("holdAtlas: [50, 52.5]\n").holdAtlas.has_value(),
      "a fractional yml atlas element leaves the field unset");
}

void testJsonBinding() {
  using namespace rechigros::render::skin;
  const std::string json =
      R"({"name":"Json Skin","author":"A","description":"D","holdAtlas":[50,52.5],
"holdAtlasMH":[50,95],"hitFx":[5,6],"hitFxScale":2,"hitFxDuration":0.25,
"hitFxRotate":true,"hideParticles":true,"hitFxTinted":false,"holdKeepHead":true,
"holdRepeat":true,"holdCompact":true,"colorPerfect":"0x80FF0000","colorGood":"16711680",
"unknownField":123})";
  const auto bound = parseSkinConfigJson(json);
  require(bound.has_value(), "a json skin document binds");
  if (bound) {
    requireEqual(bound->name.value_or(""), "Json Skin", "json name");
    require(bound->holdAtlas.has_value() && (*bound->holdAtlas)[1] == 52,
        "json fractional atlas element truncates");
    requireNear(bound->hitFxScale.value_or(0.0F), 2.0F, "json hitFxScale");
    require(bound->hitFxRotate.value_or(false), "json bool");
    require(bound->hitFxTinted.has_value() && !*bound->hitFxTinted, "json explicit false");
    requireEqual(bound->colorPerfect.value_or(""), "0x80FF0000", "json colour string");
  }
  // Wrongly typed known fields reject the whole document, like the reference binder.
  require(!parseSkinConfigJson(R"({"hitFxScale":"1.5"})").has_value(),
      "a string where a number belongs rejects the document");
  require(!parseSkinConfigJson(R"({"hitFx":1})").has_value(),
      "a number where an array belongs rejects the document");
  require(!parseSkinConfigJson("not json").has_value(), "a malformed document is rejected");
  require(parseSkinConfigJson(R"({"name":"Only","unknown":{"nested":true}})").has_value(),
      "unknown keys are ignored");
  require(parseSkinConfigJson(R"({"name":"Only"})").has_value(),
      "a name only document binds");
  // A legacy float colour array binds and is converted to the text grammar.
  const auto legacyColour = parseSkinConfigJson(R"({"colorPerfect":[0.1,1.0,0.2,1.0]})");
  require(legacyColour.has_value(), "a legacy float colour array binds");
  if (legacyColour) {
    requireEqual(legacyColour->colorPerfect.value_or(""), "0xFF1AFF33",
        "a legacy colour array converts to #AARRGGBB text");
  }
  const auto legacyShort = parseSkinConfigJson(R"({"colorGood":[0.1,0.2,1.0]})");
  require(legacyShort.has_value(), "a three component legacy colour binds");
  if (legacyShort) {
    requireEqual(legacyShort->colorGood.value_or(""), "0xFF1A33FF",
        "a three component legacy colour converts with an opaque alpha");
  }
  require(!parseSkinConfigJson(R"({"colorPerfect":[1,2]})").has_value(),
      "a two component colour array is rejected");
  require(!parseSkinConfigJson(R"({"colorGood":[1,"x",3]})").has_value(),
      "a colour array with a non-numeric component is rejected");
}

void testLoadOrder() {
  using namespace rechigros::render::skin;
  const auto root = rechigros::test::scratchDirectory("skin-load-order");

  // Neither file: no config at all.
  const auto empty = root / "empty";
  std::filesystem::create_directories(empty);
  require(!loadSkinConfig(empty).has_value(), "a directory without a config has no skin layer");

  // info.yml wins over skin.json.
  const auto both = root / "both";
  writeText(both / "info.yml", "name: From Yml\nhitFxScale: 1.25\n");
  writeText(both / "skin.json", R"({"name":"From Json","hitFxScale":9})");
  const auto loadedBoth = loadSkinConfig(both);
  require(loadedBoth.has_value(), "a skin with both documents loads");
  if (loadedBoth) {
    requireEqual(loadedBoth->name.value_or(""), "From Yml", "info.yml takes precedence");
    requireNear(loadedBoth->hitFxScale.value_or(0.0F), 1.25F, "info.yml values are used");
  }

  // An info.yml that sets nothing still counts as the skin's config.
  const auto blank = root / "blank";
  writeText(blank / "info.yml", "# nothing here\n");
  const auto blankConfig = loadSkinConfig(blank);
  require(blankConfig.has_value(), "an empty info.yml is still a present config");
  require(!blankConfig->name.has_value(), "an empty info.yml sets no fields");

  // skin.json only is used when no info.yml exists; a malformed document yields no config.
  const auto jsonOnly = root / "json-only";
  writeText(jsonOnly / "skin.json", R"({"name":"J"})");
  const auto jsonConfig = loadSkinConfig(jsonOnly);
  require(jsonConfig.has_value() && jsonConfig->name.value_or("") == "J",
      "skin.json is used when info.yml is absent");
  const auto broken = root / "broken";
  writeText(broken / "skin.json", R"({"hitFxScale":"oops"})");
  require(!loadSkinConfig(broken).has_value(), "a rejected skin.json yields no config");

  // Compatibility: a skin directory written by the earlier importer only has respack.json.
  const auto legacy = root / "legacy";
  writeText(legacy / "respack.json",
      R"({"hitFxScale":3.0,"hitFxDuration":0.9,"hitFxTinted":false,
"colorPerfect":[0.1,1.0,0.2,1.0],"colorGood":[0.1,0.2,1.0,1.0]})");
  const auto legacyConfig = loadSkinConfig(legacy);
  require(legacyConfig.has_value(), "respack.json in a skin directory still loads");
  if (legacyConfig) {
    requireNear(legacyConfig->hitFxScale.value_or(0.0F), 3.0F, "legacy respack tuning binds");
    require(legacyConfig->colorPerfect.has_value(),
        "a legacy float colour array is converted to the text form");
    const auto legacyMaterials = resolveMaterials(SkinConfig{}, &*legacyConfig);
    requireNear(legacyMaterials.perfect.red, 26.0F / 255.0F, "legacy colour red channel");
    requireNear(legacyMaterials.perfect.green, 1.0F, "legacy colour green channel");
    requireNear(legacyMaterials.perfect.alpha, 1.0F, "legacy colour alpha channel");
    require(legacyMaterials.goodFromSkin, "legacy good colour marks the skin override");
  }
}

void testBuiltInPack() {
  using namespace rechigros::render::skin;
  const auto root = rechigros::test::scratchDirectory("skin-built-in-pack");
  writeText(root / "res" / "respack.json",
      R"({"name":"pack","holdAtlas":[50,52],"holdAtlasMH":[50,95],"hitFx":[5,6],
"hitFxScale":1.1,"hideParticles":true})");
  const SkinConfig pack = loadBuiltInPack(root);
  requireEqual(pack.name.value_or(""), "pack", "the built-in pack name binds");
  require(pack.hitFx.has_value() && (*pack.hitFx)[0] == 5, "the built-in hit-effect grid binds");
  requireNear(pack.hitFxScale.value_or(0.0F), 1.1F, "the built-in tuning binds");
  // A missing or broken pack document yields an empty layer, not a failure.
  require(!loadBuiltInPack(root / "missing").hitFx.has_value(),
      "a missing pack document yields an empty layer");
  writeText(root / "res" / "respack.json", "{broken");
  require(!loadBuiltInPack(root).hitFx.has_value(), "a broken pack document yields an empty layer");
}

void testResolveMaterials() {
  using namespace rechigros::render::skin;
  SkinConfig pack;
  pack.holdAtlas = std::vector<int>{50, 52};
  pack.holdAtlasMH = std::vector<int>{50, 95};
  pack.hitFx = std::vector<int>{5, 6};
  pack.hitFxScale = 2.0F;
  pack.hitFxDuration = 0.25F;
  pack.hitFxRotate = true;
  pack.hitFxTinted = false;
  pack.hideParticles = true;
  pack.holdCompact = true;
  pack.colorPerfect = "0xFFFF0000";

  // No skin: the pack drives everything.
  const ResolvedMaterials builtIn = resolveMaterials(pack, nullptr);
  require(builtIn.holdAtlas[0] == 50 && builtIn.holdAtlas[1] == 52, "pack hold atlas");
  require(builtIn.holdAtlasMH[1] == 95, "pack highlight hold atlas");
  require(builtIn.hitFx[0] == 5 && builtIn.hitFx[1] == 6, "pack hit-effect grid");
  requireNear(builtIn.hitFxScale, 2.0F, "pack hitFxScale applies");
  requireNear(builtIn.hitFxDuration, 0.25F, "pack hitFxDuration applies");
  require(builtIn.hitFxRotate, "pack rotate applies");
  require(!builtIn.hitFxTinted, "pack tinted applies");
  require(builtIn.hideParticles, "pack hideParticles applies");
  require(builtIn.holdCompact, "pack hold compact applies");
  requireNear(builtIn.perfect.red, 1.0F, "pack colour red channel");
  requireNear(builtIn.perfect.green, 0.0F, "pack colour green channel");
  requireNear(builtIn.perfect.blue, 0.0F, "pack colour blue channel");
  require(!builtIn.perfectFromSkin, "the pack colour is not marked as a skin override");

  // A selected skin replaces the scalar layer: unset keys use the defaults, not the pack.
  SkinConfig skin;
  skin.name = "Layer";
  skin.holdAtlas = std::vector<int>{60, 70};
  skin.hitFxDuration = 0.5F;
  skin.hitFxTinted = true;
  const ResolvedMaterials layered = resolveMaterials(pack, &skin);
  require(layered.holdAtlas[0] == 60 && layered.holdAtlas[1] == 70,
      "the skin overrides the hold atlas");
  require(layered.holdAtlasMH[0] == 50 && layered.holdAtlasMH[1] == 95,
      "an atlas grid the skin leaves out keeps the pack value");
  require(layered.hitFx[0] == 5 && layered.hitFx[1] == 6,
      "the hit-effect grid the skin leaves out keeps the pack value");
  requireNear(layered.hitFxScale, 1.0F, "an unset skin tuning float uses the default");
  requireNear(layered.hitFxDuration, 0.5F, "the skin's own tuning float replaces the pack");
  require(!layered.hitFxRotate, "an unset skin bool uses the default");
  require(layered.hitFxTinted, "the skin's explicit bool replaces the pack");
  require(!layered.hideParticles, "skin layer replaces the pack hideParticles value");
  require(!layered.holdCompact, "skin layer replaces the pack hold compact value");
  require(!layered.perfectFromSkin, "a colour the skin omits is not a skin override");
  requireNear(layered.perfect.red, 1.0F, "an omitted skin colour keeps the default colour");
  requireNear(layered.perfect.alpha, 225.0F / 255.0F,
      "an omitted skin colour keeps the official sprite alpha");

  // Non-positive tuning values behave as "not specified".
  SkinConfig zeroed;
  zeroed.hitFxScale = 0.0F;
  zeroed.hitFxDuration = -2.0F;
  const ResolvedMaterials zeroMaterials = resolveMaterials(SkinConfig{}, &zeroed);
  requireNear(zeroMaterials.hitFxScale, 1.0F, "a zero scale is treated as unspecified");
  requireNear(zeroMaterials.hitFxDuration, 0.5F, "a negative duration is treated as unspecified");
  require(zeroMaterials.holdAtlas[0] == 64 && zeroMaterials.hitFx[0] == 4,
      "missing documents fall back to the library defaults");

  // Colours: eight digit hex carries alpha, six digit hex keeps the default alpha.
  SkinConfig coloured;
  coloured.colorPerfect = "0x80FF0000";
  coloured.colorGood = "00FF00";
  const ResolvedMaterials colourMaterials = resolveMaterials(SkinConfig{}, &coloured);
  require(colourMaterials.perfectFromSkin && colourMaterials.goodFromSkin,
      "skin colour fields are marked as overrides");
  requireNear(colourMaterials.perfect.red, 1.0F, "8 digit hex red");
  requireNear(colourMaterials.perfect.green, 0.0F, "8 digit hex green");
  requireNear(colourMaterials.perfect.alpha, 128.0F / 255.0F, "8 digit hex alpha");
  requireNear(colourMaterials.good.green, 1.0F, "6 digit hex green");
  requireNear(colourMaterials.good.alpha, 235.0F / 255.0F,
      "6 digit hex keeps the official sprite alpha");
}

void testColourParsing() {
  using namespace rechigros::render::skin;
  const auto hexAlpha = parseSkinColour("0x80112233");
  require(hexAlpha.has_value(), "8 digit hex colour parses");
  if (hexAlpha) {
    requireNear(hexAlpha->red, 0x11 / 255.0F, "hex red");
    requireNear(hexAlpha->green, 0x22 / 255.0F, "hex green");
    requireNear(hexAlpha->blue, 0x33 / 255.0F, "hex blue");
    requireNear(hexAlpha->alpha, 0x80 / 255.0F, "hex alpha");
  }
  // A bare decimal spelling of white must resolve to white. Reading it as hexadecimal instead
  // (which the reference grammar does, because it tests the hex form first) turns the
  // documented spelling into a dark, nearly transparent colour: 0x16777215 has an alpha byte of
  // 0x01 and channels 0x77/0x72/0x15. The parser therefore only forces the hex reading when the
  // text carries an explicit 0x prefix, which keeps both spellings unambiguous.
  const auto decimalWhite = parseSkinColour("16777215");
  require(decimalWhite.has_value(), "a bare decimal colour parses");
  if (decimalWhite) {
    requireNear(decimalWhite->red, 1.0F, "decimal white is red");
    requireNear(decimalWhite->green, 1.0F, "decimal white is green");
    requireNear(decimalWhite->blue, 1.0F, "decimal white is blue");
    requireNear(decimalWhite->alpha, 1.0F, "decimal white is opaque");
  }
  // The prefixed spelling keeps the hex reading, and six hex digits keep the caller's alpha.
  const auto prefixedHex = parseSkinColour("0xFFFFFF");
  require(prefixedHex.has_value() && std::abs(prefixedHex->red - 1.0F) < 1e-4F,
      "a prefixed six digit hex colour is white");
  // A token carrying letters can only be hexadecimal, so the prefix stays optional there.
  const auto letterHex = parseSkinColour("FF1E5A");
  require(letterHex.has_value() && std::abs(letterHex->green - 0x1E / 255.0F) < 1e-4F,
      "a hex colour with letters parses without a prefix");
  // Shorter numeric strings reach the decimal branch.
  const auto decimal = parseSkinColour("255");
  require(decimal.has_value() && std::abs(decimal->blue - 1.0F) < 1e-4F,
      "a short decimal colour parses as a packed RGB value");
  require(!parseSkinColour("").has_value(), "an empty colour does not parse");
  require(!parseSkinColour("not-a-colour").has_value(), "garbage does not parse");

  SkinColour untouched;
  untouched.red = 0.5F;
  require(parseSkinColourInto("bad", untouched) < 0.0F && std::abs(untouched.red - 0.5F) < 1e-4F,
      "a failed parse leaves the colour untouched");
}

void testAssetOverrides() {
  using namespace rechigros::render::skin;
  const auto root = rechigros::test::scratchDirectory("skin-assets");
  std::filesystem::create_directories(root);
  writeText(root / "click.png", "PNG");
  writeText(root / "hold_body.png", "PNG");
  std::filesystem::create_directories(root / "hit_fx.png"); // a directory is not an asset

  require(!resolveSkinFile(root, "click.png").empty(), "an existing skin asset resolves");
  require(!resolveSkinFile(root, "hold_body.png").empty(),
      "a skin-only body texture resolves");
  require(resolveSkinFile(root, "drag.png").empty(), "a missing skin asset does not resolve");
  require(resolveSkinFile(root, "hit_fx.png").empty(), "a directory is not a skin asset");
  require(resolveSkinFile({}, "click.png").empty(), "no skin directory resolves nothing");

  // The override table mirrors the reference asset set and resolution order.
  const char* const expected[] = {"click.png", "click_mh.png", "drag.png", "drag_mh.png",
      "flick.png", "flick_mh.png", "hold.png", "hold_mh.png", "hit_fx.png", "hold_body.png",
      "hold_body_mh.png"};
  require(std::size(kSkinTextureOverrides) == std::size(expected),
      "the skin texture override table has the reference size");
  for (std::size_t index = 0; index < std::size(expected)
       && index < std::size(kSkinTextureOverrides); ++index) {
    requireEqual(kSkinTextureOverrides[index].fileName, expected[index],
        "skin texture override order");
  }
  require(kSkinTextureOverrides[9].assetFallback == nullptr,
      "hold_body.png has no built-in fallback");
}

void testChannelPacking() {
  using rechigros::render::asset::Image;
  Image image;
  image.width = 2048;
  image.height = 2048;
  image.premultiplied = true;
  image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);
  for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
    const auto gray = static_cast<std::uint8_t>((index / 4) % 256);
    image.pixels[index] = gray;
    image.pixels[index + 1] = gray;
    image.pixels[index + 2] = gray;
    image.pixels[index + 3] = static_cast<std::uint8_t>((index / 4 * 7) % 256);
  }
  const auto original = image.pixels;
  rechigros::render::asset::optimizeChannels(image);
  require(image.dualChannel, "gray and independent alpha use two-channel storage");
  require(image.pixels.size() == original.size() / 2, "two-channel storage halves the byte count");
  rechigros::render::asset::expandChannels(image);
  require(!image.dualChannel, "two-channel pixels expand to RGBA");
  require(image.pixels == original, "channel packing is byte-exact");

  image.pixels[2] = static_cast<std::uint8_t>(image.pixels[2] ^ 1U);
  rechigros::render::asset::optimizeChannels(image);
  require(!image.dualChannel, "non-gray pixels keep full RGBA storage");
}

} // namespace

int main() {
  testTextParser();
  testJsonBinding();
  testLoadOrder();
  testBuiltInPack();
  testResolveMaterials();
  testColourParsing();
  testAssetOverrides();
  testChannelPacking();

  if (g_failures != 0) {
    std::cerr << g_failures << " skin config test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros skin config tests passed\n";
  return 0;
}
