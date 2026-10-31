#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Per-skin material overrides for the gameplay renderer. A resource pack (the built-in
// assets/res/respack.json) and an imported skin directory both use the same document
// schema: a small set of optional fields describing the atlas grids, hit-effect tuning,
// hold-body behaviour and the perfect/good note colours. Every field is optional so a skin
// can override a subset without restating the built-in values.
namespace rechigros::render::skin {

// Optional document fields. Boxed (optional) values keep "not specified here"
// distinguishable from an explicit value, which the resolution rules depend on.
struct SkinConfig {
  std::optional<std::string> name;
  std::optional<std::string> author;
  std::optional<std::string> description;
  // Hold sheet split: {tail height px, head height px}.
  std::optional<std::vector<int>> holdAtlas;
  std::optional<std::vector<int>> holdAtlasMH;
  // Hit-effect sheet grid: {columns, rows}.
  std::optional<std::vector<int>> hitFx;
  std::optional<float> hitFxScale;
  std::optional<float> hitFxDuration;
  std::optional<bool> hitFxRotate;
  std::optional<bool> hideParticles;
  std::optional<bool> hitFxTinted;
  std::optional<bool> holdKeepHead;
  std::optional<bool> holdRepeat;
  std::optional<bool> holdCompact;
  // Colour text: "0xAARRGGBB", "AARRGGBB", "RRGGBB" or a decimal integer. An 8 digit value
  // carries alpha; six digits keep the caller's default alpha.
  std::optional<std::string> colorPerfect;
  std::optional<std::string> colorGood;
};

// The built-in material pack (assets/res/respack.json). Missing or unparsable fields stay
// unset, exactly like an imported skin document.
[[nodiscard]] SkinConfig loadBuiltInPack(const std::filesystem::path& assetRoot);

// A skin directory's config: info.yml wins when present and readable (tolerant line parser),
// then skin.json. As a compatibility path for packages produced by earlier ports, a
// respack.json in the skin directory is read with the JSON binder when neither of the two
// reference documents exists. Returns "no config" when nothing binds; a config that parses
// is returned even when it sets nothing.
[[nodiscard]] std::optional<SkinConfig> loadSkinConfig(
    const std::filesystem::path& skinDirectory);

// Tolerant line parser for info.yml. Keys are case sensitive (camelCase and the documented
// snake_case spelling), values may carry trailing comments, quotes are stripped, and a bad
// value simply leaves that field unset.
[[nodiscard]] SkinConfig parseSkinConfigText(const std::string& content);
// Whole-document JSON parser for skin.json. Unknown keys are ignored; a value of the wrong
// type rejects the document, matching the reference binder. For compatibility with earlier
// packages a colour key may also be a [red, green, blue] or [red, green, blue, alpha] array
// of 0..1 floats, which is converted to the equivalent text colour.
[[nodiscard]] std::optional<SkinConfig> parseSkinConfigJson(const std::string& content);
// Display name of a config, or an empty string when the document does not set one.
[[nodiscard]] std::string skinDisplayName(const SkinConfig& config);

// One colour resolved from the reference colour-string grammar.
struct SkinColour {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
};

// Parses a colour string into channels and returns the alpha it specifies, or -1 when the
// text is missing/unparsable (the caller then keeps its current alpha). On success `colour`
// is updated; on failure it is left untouched.
[[nodiscard]] float parseSkinColourInto(const std::string& text, SkinColour& colour);
[[nodiscard]] std::optional<SkinColour> parseSkinColour(const std::string& text);

// Fully resolved material values handed to the renderer. The resolution follows the
// reference: the built-in pack provides the base layer; when a skin is selected its explicit
// values replace the pack for the scalar tuning fields, while texture-atlas fields fall back
// to the pack when the skin does not set them.
struct ResolvedMaterials {
  // {tail, head} hold sheet slices in pixels.
  std::array<int, 2> holdAtlas{{64, 64}};
  std::array<int, 2> holdAtlasMH{{64, 64}};
  // {columns, rows} hit-effect grid.
  std::array<int, 2> hitFx{{4, 4}};
  float hitFxScale{1.0F};
  float hitFxDuration{0.5F};
  bool hitFxRotate{false};
  bool hitFxTinted{true};
  bool hideParticles{false};
  bool holdCompact{false};
  bool holdKeepHead{false};
  bool holdRepeat{false};
  // Judgement sprite colours from the Phigros 4.0.1 prefabs.
  SkinColour perfect{1.0F, 0.92704993F, 0.6273585F, 0.88235295F};
  SkinColour good{180.0F / 255.0F, 225.0F / 255.0F, 1.0F, 0.92156863F};
  // True when the selected skin itself declared the colour field (the reference gates the
  // judge-line colour override on this, not on whether the text parsed).
  bool perfectFromSkin{false};
  bool goodFromSkin{false};
};

// Resolves the material values for a renderer frame. `skin` may be null (built-in look).
[[nodiscard]] ResolvedMaterials resolveMaterials(const SkinConfig& pack,
    const SkinConfig* skin);

// One texture a skin may replace. `fileName` is read from the skin directory and
// `assetFallback` is the built-in asset loaded when the skin file is absent (a null fallback
// means the skin is the only source).
struct SkinAssetOverride {
  const char* fileName;
  const char* assetFallback;
};

// The textures the reference lets a skin replace, in resolution order.
inline constexpr SkinAssetOverride kSkinTextureOverrides[] = {
    {"click.png", "res/click.png"},
    {"click_mh.png", "res/click_mh.png"},
    {"drag.png", "res/drag.png"},
    {"drag_mh.png", "res/drag_mh.png"},
    {"flick.png", "res/flick.png"},
    {"flick_mh.png", "res/flick_mh.png"},
    {"hold.png", "res/hold.png"},
    {"hold_mh.png", "res/hold_mh.png"},
    {"hit_fx.png", "res/hit_fx.png"},
    {"hold_body.png", nullptr},
    {"hold_body_mh.png", nullptr},
};

// Absolute path of `fileName` inside the skin directory when it names a readable file,
// otherwise an empty path (the caller then falls back to the built-in asset).
[[nodiscard]] std::filesystem::path resolveSkinFile(const std::filesystem::path& skinDirectory,
    const std::string& fileName);

} // namespace rechigros::render::skin
