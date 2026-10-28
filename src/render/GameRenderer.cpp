#include "rechigros/render/GameRenderer.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/MessageParticleField.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/fx/Yuv420Pass.hpp"
#include "rechigros/render/gl/AsyncReadback.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/GlDebug.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/fx/BadEffectField.hpp"
#include "rechigros/render/fx/AmetrineVignette.hpp"
#include "rechigros/render/fx/BlockedComboField.hpp"
#include "rechigros/render/fx/BlockCompositePass.hpp"
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/ClickEffectField.hpp"
#include "rechigros/render/fx/DsGridPass.hpp"
#include "rechigros/render/fx/FxaaPass.hpp"
#include "rechigros/render/fx/KaleidoBackdropPass.hpp"
#include "rechigros/render/fx/HitEffect.hpp"
#include "rechigros/render/fx/PauseBackdropPass.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"
#include "rechigros/render/scene/BackgroundBlur.hpp"
#include "rechigros/render/scene/PauseHudBlur.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/HudPass.hpp"
#include "rechigros/render/scene/LinePass.hpp"
#include "rechigros/render/scene/ChartDebugOverlay.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/scene/StageLayout.hpp"
#include "rechigros/render/scene/Transition.hpp"
#include "rechigros/render/skin/SkinConfig.hpp"
#include "rechigros/render/text/TextRenderer.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
#include <vector>

#include <stb_image_write.h>

namespace rechigros::render {
namespace {
constexpr int kNoteTypeTap = 1;
constexpr int kNoteTypeDrag = 2;
constexpr int kNoteTypeHold = 3;
constexpr int kNoteTypeFlick = 4;

// Head textures paint in this order so drag sits behind taps and flicks on top.
constexpr int kHeadDrawOrder[3] = {kNoteTypeDrag, kNoteTypeTap, kNoteTypeFlick};

// Height of the hover disc a held finger paints inside a zone, as a fraction of the view
// height: a 44 px sprite at 100 units per world unit, scaled 11.5 and viewed at 5 world units
// per half height.
constexpr float kHoverSpriteHeightFraction = 44.0F / 100.0F * 11.5F / 10.0F;

// Note width as a fraction of stage width. Displays narrower than 16:9 draw
// proportionally smaller notes; wider displays are untouched.
float noteWidthRatio(float stageWidth, float stageHeight) {
  constexpr float kReferenceAspect = 16.0F / 9.0F;
  constexpr float kBaseRatio = 0.1234375F;
  const float aspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : kReferenceAspect;
  const float factor = aspect < kReferenceAspect ? aspect / kReferenceAspect : 1.0F;
  return kBaseRatio * factor;
}

void bindDefaultBlend() {
  // Premultiplied alpha: sprites composite over the target instead of darkening it.
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void clearTarget(float red, float green, float blue, float alpha) {
  glClearColor(red, green, blue, alpha);
  glClear(GL_COLOR_BUFFER_BIT);
}
} // namespace

struct GameRenderer::Impl {
  ~Impl() { releaseResources(); }

  void releaseResources() noexcept {
    chain.release();
    // The composite owns GL programs and reduced-size targets; release them before the
    // texture cache drops the samplers they point at.
    blockComposite.release();
    // The overlay passes own GL programs; release them with the rest of the resources.
    pauseBackdrop.release();
  backgroundBlur.release();
  pauseHudBlur.release();
    dsGrid.release();
    kaleido.release();
    sceneFbo.reset();
    blockResultFbo.reset();
    pingFbo.reset();
    pongFbo.reset();
    aaFbo.reset();
    halfResFbo.reset();
    batch.release();
    text.shutdown();
    textures.clear();
    loadedChart = nullptr;
    background = nullptr;
    for (auto& pair : noteHeads) {
      pair[0] = nullptr;
      pair[1] = nullptr;
    }
    holdTextures[0] = holdTextures[1] = nullptr;
    holdBodies[0] = holdBodies[1] = nullptr;
    hitFx = nullptr;
    hudArtwork.pause = hudArtwork.timerLine = hudArtwork.noteRing = nullptr;
    hudArtwork.hitOffsetArrow = nullptr;
    effectGlitchMap = effectGlitchDogMap = effectWireNoise = effectDisplaceMap = nullptr;
    blockDisplaceMap = blockSparkMap = blockNoiseMap = nullptr;
    headTextureList.clear();
  }

  std::filesystem::path assets;
  int width{1280};
  int height{720};
  bool initialized{};

  // Offscreen capture state for recording. The target and the size it replaced are held here so
  // a capture survives many frames without reallocating or disturbing the interactive size.
  gl::Framebuffer captureTarget;
  // Staging buffers for the offscreen readback: reading through them keeps the CPU off the GPU's
  // critical path, so a recording costs the same per frame as drawing it.
  gl::AsyncReadback captureReadback;
  // Converts a finished frame to the encoder's colour layout on the graphics device.
  fx::Yuv420Pass captureYuv;
  int captureWidth{};
  int captureHeight{};
  int capturePreviousWidth{};
  int capturePreviousHeight{};
  bool capturing{};

  asset::TextureCache textures;
  text::TextRenderer text;
  batch::SpriteBatch batch;
  gl::Framebuffer sceneFbo;
  gl::Framebuffer blockResultFbo;
  gl::Framebuffer pingFbo;
  gl::Framebuffer pongFbo;
  // Antialias resolve target (full resolution) and the low-resolution scene target.
  gl::Framebuffer aaFbo;
  gl::Framebuffer halfResFbo;
  fx::HitEffectField hitEffects;
  // Autoplay bursts, drawn from the chart's pre-rolled debris so the layout is stable.
  fx::ClickEffectField clickEffects;
  // Failure vignette of the Ametrine level mod; idle for every other level.
  fx::AmetrineVignette ametrineVignette;
  // Block field standing in for the combo readout once a challenge has failed.
  fx::BlockedComboField blockedComboNumber;
  fx::BlockedComboField blockedComboLabel;
  // Visual clock of the previous frame, so the Ametrine ramp advances by a real delta.
  double ametrineLastSec{-1.0};
  // Displayed score and accuracy of the catch-up readout, carried across frames and reset when
  // a run starts or seeks. NaN means "not primed yet".
  double hudDisplayScore{std::numeric_limits<double>::quiet_NaN()};
  double hudDisplayAccuracy{std::numeric_limits<double>::quiet_NaN()};
  double hudDisplayLastUpdateSec{-1.0};
  bool hudDisplayReset{true};
  fx::PauseBackdropPass pauseBackdrop;
  scene::BackgroundBlur backgroundBlur;
  scene::PauseHudBlur pauseHudBlur;
  fx::BlockOverlay blockOverlay;
  // Mask staging reused between frames: constructing and growing these per zone frame
  // would allocate on every composite frame.
  std::vector<fx::BlockMaskQuad> maskQuadsA;
  std::vector<fx::BlockMaskQuad> maskQuadsB;
  // Held-point staging for the composite's hover channel, reused between frames.
  std::vector<fx::BlockOverlay::HoverPoint> hoverPoints;
  // Replaces the finished frame inside a red zone; the inline silhouettes are skipped
  // while it is active so the two paths never draw the same zone twice.
  fx::BlockCompositePass blockComposite;
  fx::BadEffectField badEffects;
  // Notes that already produced their bad effect, so each note produces one copy.
  std::unordered_set<const game::Note*> badSpawned;
  game::MessageParticleField messageParticles;
  double messageParticleLastSec{-1.0};
  // Material settings resolved from the built-in pack and then the selected skin layer.
  fx::HitEffectAtlas hitAtlas;
  // Hold sheet slicing and body behaviour, overridable by the resource pack and a skin.
  int holdTailSlice[2]{50, 50};
  int holdHeadSlice[2]{52, 95};
  bool holdRepeat{};
  bool holdKeepHead{};
  bool holdCompact{};
  fx::HitEffectStyle hitStyle;
  fx::HitEffectSkin hitSkin;
  // Fully resolved material values from the built-in pack and the selected skin layer.
  skin::ResolvedMaterials skinMaterials;
  std::filesystem::path skinDirectory;
  double blockUpdateLastSec{};
  fx::DsGridPass dsGrid;
  fx::KaleidoBackdropPass kaleido;
  gl::Texture* kaleidoNormal{};
  gl::Texture* kaleidoColour{};
  gl::Texture* dsGridTexture{};
  gl::Framebuffer pauseBlurA;
  fx::PrprChain chain;
  // Cached chart-shader setting of the frame being rendered, read before the per-chart
  // preparation so the storyboard chain can be skipped when the setting is off.
  bool disableChartShaderSetting{};

  // Gameplay textures, resolved once per asset root.
  gl::Texture* background{};
  gl::Texture* defaultBackground{};
  gl::Texture* noteHeads[5][2]{};
  gl::Texture* holdTextures[2]{};
  gl::Texture* holdBodies[2]{};
  gl::Texture* hitFx{};
  gl::Texture* effectGlitchMap{};
  gl::Texture* effectGlitchDogMap{};
  gl::Texture* effectWireNoise{};
  gl::Texture* effectDisplaceMap{};
  // Noise maps the screen-space composite samples while it distorts a zone.
  gl::Texture* blockDisplaceMap{};
  gl::Texture* blockSparkMap{};
  gl::Texture* blockNoiseMap{};

  // HUD artwork, grouped so the HUD pass receives it as one value.
  struct HudArtwork {
    gl::Texture* pause{};
    gl::Texture* timerLine{};
    gl::Texture* noteRing{};
    gl::Texture* hitOffsetArrow{};
  } hudArtwork;

  game::Chart* loadedChart{};
  std::vector<scene::LineFrame> lineFrames;
  std::vector<int> lineDrawOrder;
  std::vector<gl::Texture*> headTextureList;
  // Width correction per note type and highlight variant, derived from the loaded sheets.
  std::array<std::array<float, 2>, 5> noteWidthScale{{{{1.0F, 1.0F}}, {{1.0F, 1.0F}},
      {{1.0F, 1.0F}}, {{1.0F, 1.0F}}, {{1.0F, 1.0F}}}};
  scene::HoldUv holdUv[2];

  void loadGameplayTextures();
  // Applies the built-in resource pack, then the selected skin layer on top.
  void applyResPackSettings();
  // Loads a texture the skin may override: the skin directory's file first, then the
  // built-in asset. Returns null when neither can be decoded.
  gl::Texture* loadSkinAwareTexture(const char* fileName, const char* assetFallback);
  // Same fallback order, but decodes the image so it can be cropped before upload.
  asset::Image loadSkinAwareImage(
      const char* fileName, const char* assetFallback, std::string& cacheKey);
  gl::Texture* cropHoldBody(
      const char* fileName, const char* assetFallback, int tailSlice, int headSlice);
  // Re-slices the hold sheet from the current material settings. The pack and a skin may
  // change the slice sizes, so this runs again whenever those settings change.
  void refreshHoldSheet();
  void prepareForChart(game::Chart* chart);
  void drawBackgroundPass(const scene::StageLayout& layout, float artworkAlpha, float dimAlpha,
      float sideArtworkAlpha, float sideDimAlpha, bool opening, bool deferSideColumns);
  // The fixed side columns of a letterboxed stage. They are a separate pass because one level
  // draws a canvas larger than the play field: those columns have to end up above it, not
  // underneath, so they are painted after the level's own objects instead of with the backdrop.
  void drawSideColumns(const scene::StageLayout& layout, const gl::Texture* artwork,
      float sideArtworkAlpha, float sideDimAlpha);
  // True while the Desultory Signals grid canvas is on screen; its canvas is wider than the
  // play field, so the fixed columns are repainted over it.
  [[nodiscard]] bool dsGridCanvasVisible(const RenderFrameInput& input,
      const game::Chart* chart) const;
  void drawLinePass(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, const scene::StageLayout& layout);
  void drawNotePasses(const scene::StageLayout& layout, const RenderFrameInput& input,
      game::Chart* chart);
  void drawHitEffects(const scene::StageLayout& layout, const RenderFrameInput& input, game::Chart* chart);
  // Chart reveal: paints the fixed blurred backdrop in the ring between the shrunken play
  // field and the normal stage rect, covering anything that overflows the reveal mask.
  void drawRevealPanels(const scene::StageLayout& inner, const scene::StageLayout& outer,
      float dimAlpha);
  // Draws the pulse disc and ring at every finger held inside an active red zone.
  void drawBlockedTouchPulses(const RenderFrameInput& input, const scene::StageLayout& layout,
      game::Chart* chart);
  void drawHudPass(const RenderFrameInput& input, const scene::StageLayout& layout,
      game::Chart* chart, const game::JudgementEngine* judgement);
  // Draws the objects a level effect adds to the scene itself, which are separate from the
  // storyboard post-processing chain.
  void drawLevelEffectObjects(game::Chart* chart, const RenderFrameInput& input,
      const scene::StageLayout& layout);
  // Runs the effects that are scoped to the play field: the chain processes the stage
  // rectangle only and the surround keeps the scene. Returns the resulting texture, or the
  // input when no such effect is active.
  gl::Texture* applyStageScopedChain(game::Chart& chart, gl::Texture* input,
      const math::Rect& stageRect, double chartTimeSec);
  // The effects that cover the whole frame, HUD included.
  std::vector<game::PrprEffect> collectGlobalEffects(const game::Chart& chart) const;
  // Draws one complete frame of the game into whatever target is bound, running the
  // storyboard chain first when the chart asks for it.
  void drawScene(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, unsigned int targetFbo, bool applyEffects = true);
  // True from the chart time the Message level lets its canvas cover the whole viewport. The
  // switch is derived from the level-effect table, so a seek backwards puts the letterboxed
  // stage back instead of leaving the expanded one up.
  [[nodiscard]] bool messageFullscreenStage(const RenderFrameInput& input,
      const game::Chart* chart) const noexcept;
  scene::LineTint lineTintFor(const RenderFrameInput& input,
      const game::JudgementEngine* judgement) const;
};

void GameRenderer::Impl::loadGameplayTextures() {
  defaultBackground = textures.loadAsset("res/background.jpg");
  background = defaultBackground;
  // A selected skin replaces the note, hold and hit-effect artwork with files of the same
  // names in its directory; everything it does not ship keeps the built-in asset.
  noteHeads[kNoteTypeTap][0] = loadSkinAwareTexture("click.png", "res/click.png");
  noteHeads[kNoteTypeTap][1] = loadSkinAwareTexture("click_mh.png", "res/click_mh.png");
  noteHeads[kNoteTypeDrag][0] = loadSkinAwareTexture("drag.png", "res/drag.png");
  noteHeads[kNoteTypeDrag][1] = loadSkinAwareTexture("drag_mh.png", "res/drag_mh.png");
  noteHeads[kNoteTypeFlick][0] = loadSkinAwareTexture("flick.png", "res/flick.png");
  noteHeads[kNoteTypeFlick][1] = loadSkinAwareTexture("flick_mh.png", "res/flick_mh.png");
  holdTextures[0] = loadSkinAwareTexture("hold.png", "res/hold.png");
  holdTextures[1] = loadSkinAwareTexture("hold_mh.png", "res/hold_mh.png");
  noteHeads[kNoteTypeHold][0] = holdTextures[0];
  noteHeads[kNoteTypeHold][1] = holdTextures[1];
  hitFx = loadSkinAwareTexture("hit_fx.png", "res/hit_fx.png");
  // HUD artwork; a missing file makes the corresponding widget fall back to a flat quad.
  hudArtwork.pause = textures.loadAsset("res/pause.png");
  hudArtwork.timerLine = textures.loadAsset("res/timerLine.png");
  hudArtwork.noteRing = textures.loadAsset("res/NoteRing.png");
  hudArtwork.hitOffsetArrow = textures.loadAsset("res/arrow.png");
  // A highlight sheet is a different canvas size from its normal counterpart, so the drawn
  // width is corrected by the ratio of the two sheets to keep the note the same size.
  for (int type = 0; type < 5; ++type) {
    noteWidthScale[static_cast<std::size_t>(type)][0] = 1.0F;
    noteWidthScale[static_cast<std::size_t>(type)][1] = 1.0F;
    const gl::Texture* normal = noteHeads[type][0];
    const gl::Texture* highlight = noteHeads[type][1];
    if (normal != nullptr && highlight != nullptr && normal->valid() && highlight->valid() &&
        normal->width() > 0) {
      noteWidthScale[static_cast<std::size_t>(type)][1] =
          scene::highlightWidthScale(normal->width(), highlight->width());
    }
  }
  pauseBackdrop.prepare(assets);
  backgroundBlur.prepare();
  pauseHudBlur.prepare();
  dsGrid.prepare(assets);
  kaleido.prepare(assets);

  // Chapter-9 effect samplers; a missing file simply disables its preset.
  effectGlitchMap = textures.loadAssetFlipped("official_textures/Glitch4in1.png");
  effectGlitchDogMap = textures.loadAssetFlipped("official_textures/GlitchMap.png");
  effectWireNoise = textures.loadAssetFlipped("official_textures/WireNoise.png");
  effectDisplaceMap = textures.loadAssetFlipped("official_textures/DisplaceMap.png");
  if (effectGlitchMap != nullptr) {
    effectGlitchMap->setFilter(false);
    effectGlitchMap->setWrapRepeat(true, true);
  }
  if (effectGlitchDogMap != nullptr) {
    effectGlitchDogMap->setFilter(false);
    effectGlitchDogMap->setWrapRepeat(true, true);
  }
  if (effectDisplaceMap != nullptr) effectDisplaceMap->setFilter(true);
  if (effectWireNoise != nullptr) effectWireNoise->setWrapRepeat(true, true);

  // Red-zone composite samplers. Background pixels inside a zone are resampled through
  // the displacement and noise maps, so they have to tile and stay point sampled: a
  // filtered displacement map would smooth the staircase edge the material depends on.
  blockDisplaceMap = textures.loadAssetFlipped("official_textures/BlockNoise1.png");
  blockSparkMap = textures.loadAssetFlipped("official_textures/PointNoise.png");
  blockNoiseMap = textures.loadAssetFlipped("official_textures/FD_Noise_00000.png");
  if (blockDisplaceMap != nullptr) {
    blockDisplaceMap->setFilter(false);
    blockDisplaceMap->setWrapMirrored(true, true);
  }
  if (blockSparkMap != nullptr) {
    blockSparkMap->setFilter(false);
    blockSparkMap->setWrapRepeat(true, true);
  }
  if (blockNoiseMap != nullptr) {
    blockNoiseMap->setFilter(false);
    blockNoiseMap->setWrapMirrored(true, true);
  }

  // Resolve the material settings before slicing the hold sheet: the pack and a skin may
  // change the slice sizes.
  applyResPackSettings();

  // The hold sheet is a vertical stack of tail, body and head. The body is not a
  // separate asset: it is the middle slice of the same sheet, cropped here.
  refreshHoldSheet();

  headTextureList.clear();
  for (const int type : kHeadDrawOrder) {
    for (const auto& pair : noteHeads[type]) {
      gl::Texture* texture = pair;
      if (texture == nullptr) continue;
      if (std::find(headTextureList.begin(), headTextureList.end(), texture) !=
          headTextureList.end()) {
        continue;
      }
      headTextureList.push_back(texture);
    }
  }
}

void GameRenderer::Impl::refreshHoldSheet() {
  for (int highlight = 0; highlight < 2; ++highlight) {
    // The atlas pair is [tail slice, head slice] in pixels; the highlighted sheet has its
    // own pair. `buildHoldUv` and the body crop must agree on the same two numbers.
    const int tailSlice = holdTailSlice[highlight];
    const int headSlice = holdHeadSlice[highlight];
    const char* sheetName = highlight == 1 ? "hold_mh.png" : "hold.png";
    const char* assetFallback = highlight == 1 ? "res/hold_mh.png" : "res/hold.png";
    const char* bodyName = highlight == 1 ? "hold_body_mh.png" : "hold_body.png";

    // A skin may ship a ready-made body strip; otherwise the body is the middle slice of
    // the same (skin or built-in) hold sheet.
    holdBodies[highlight] = nullptr;
    if (!skinDirectory.empty()) {
      const auto bodyPath = skin::resolveSkinFile(skinDirectory, bodyName);
      if (!bodyPath.empty()) {
        holdBodies[highlight] = textures.loadFile(bodyPath);
        if (holdBodies[highlight] != nullptr) holdBodies[highlight]->setWrapRepeat(false, true);
      }
    }
    if (holdBodies[highlight] == nullptr) {
      holdBodies[highlight] = cropHoldBody(sheetName, assetFallback, tailSlice, headSlice);
    }
    if (holdTextures[highlight] != nullptr) {
      holdUv[highlight] =
          scene::buildHoldUv(holdTextures[highlight]->height(), tailSlice, headSlice);
    }
  }
}

asset::Image GameRenderer::Impl::loadSkinAwareImage(
    const char* fileName, const char* assetFallback, std::string& cacheKey) {
  cacheKey.clear();
  if (!skinDirectory.empty()) {
    const auto skinPath = skin::resolveSkinFile(skinDirectory, fileName);
    if (!skinPath.empty()) {
      const auto bytes = platform::readBinaryFile(skinPath);
      if (!bytes.empty()) {
        asset::Image image = asset::decodeImageMemory(bytes.data(), bytes.size());
        if (image.valid()) {
          cacheKey = skinPath.generic_string();
          return image;
        }
      }
    }
  }
  if (assetFallback != nullptr) {
    const auto bytes = platform::AssetManager::readBinary(assets, assetFallback);
    if (!bytes.empty()) {
      asset::Image image = asset::decodeImageMemory(bytes.data(), bytes.size());
      if (image.valid()) {
        cacheKey = assetFallback;
        return image;
      }
    }
  }
  return asset::Image{};
}

gl::Texture* GameRenderer::Impl::loadSkinAwareTexture(
    const char* fileName, const char* assetFallback) {
  if (!skinDirectory.empty()) {
    const auto skinPath = skin::resolveSkinFile(skinDirectory, fileName);
    if (!skinPath.empty()) {
      // A skin file that exists but cannot be decoded falls back to the built-in asset,
      // matching the reference texture resolver.
      if (gl::Texture* texture = textures.loadFile(skinPath)) return texture;
    }
  }
  if (assetFallback == nullptr) return nullptr;
  return textures.loadAsset(assetFallback);
}

gl::Texture* GameRenderer::Impl::cropHoldBody(
    const char* fileName, const char* assetFallback, int tailSlice, int headSlice) {
  std::string cacheKey;
  const asset::Image sheet = loadSkinAwareImage(fileName, assetFallback, cacheKey);
  if (!sheet.valid()) return nullptr;

  const int bodyTop = std::max(0, tailSlice);
  const int bodyHeight = sheet.height - bodyTop - std::max(0, headSlice);
  if (bodyHeight <= 0 || sheet.width <= 0) return nullptr;

  const auto rowBytes = static_cast<std::size_t>(sheet.width) * 4;
  asset::Image body;
  body.width = sheet.width;
  body.height = bodyHeight;
  body.pixels.resize(rowBytes * static_cast<std::size_t>(bodyHeight));
  const auto* source = sheet.pixels.data() + static_cast<std::size_t>(bodyTop) * rowBytes;
  std::copy_n(source, body.pixels.size(), body.pixels.data());

  // The body is tiled vertically along the hold, so it repeats in T only.
  auto* texture = textures.loadPixels(std::string("holdbody:") + cacheKey, std::move(body));
  if (texture != nullptr) texture->setWrapRepeat(false, true);
  return texture;
}

void GameRenderer::Impl::applyResPackSettings() {
  // The built-in pack provides the base layer. A selected skin replaces the tuning fields it
  // declares and keeps the pack's atlas grids where it leaves them out, which is exactly the
  // reference merge rule for the two layers.
  const skin::SkinConfig pack = skin::loadBuiltInPack(assets);
  std::optional<skin::SkinConfig> selection;
  if (!skinDirectory.empty()) selection = skin::loadSkinConfig(skinDirectory);
  skinMaterials = skin::resolveMaterials(pack, selection ? &*selection : nullptr);

  holdTailSlice[0] = std::max(0, skinMaterials.holdAtlas[0]);
  holdHeadSlice[0] = std::max(0, skinMaterials.holdAtlas[1]);
  holdTailSlice[1] = std::max(0, skinMaterials.holdAtlasMH[0]);
  holdHeadSlice[1] = std::max(0, skinMaterials.holdAtlasMH[1]);
  holdRepeat = skinMaterials.holdRepeat;
  holdKeepHead = skinMaterials.holdKeepHead;
  holdCompact = skinMaterials.holdCompact;

  hitAtlas.columns = std::max(1, skinMaterials.hitFx[0]);
  hitAtlas.rows = std::max(1, skinMaterials.hitFx[1]);
  hitStyle.sizeScale = skinMaterials.hitFxScale;
  hitStyle.duration = skinMaterials.hitFxDuration;
  hitStyle.rotateSheet = skinMaterials.hitFxRotate;
  hitStyle.tinted = skinMaterials.hitFxTinted;
  hitStyle.showParticles = !skinMaterials.hideParticles;

  hitSkin.perfectRed = skinMaterials.perfect.red;
  hitSkin.perfectGreen = skinMaterials.perfect.green;
  hitSkin.perfectBlue = skinMaterials.perfect.blue;
  hitSkin.perfectAlpha = skinMaterials.perfect.alpha;
  hitSkin.goodRed = skinMaterials.good.red;
  hitSkin.goodGreen = skinMaterials.good.green;
  hitSkin.goodBlue = skinMaterials.good.blue;
  hitSkin.goodAlpha = skinMaterials.good.alpha;

  hitEffects.setAtlas(hitAtlas);
  hitEffects.setStyle(hitStyle);
  hitEffects.setSkin(hitSkin);
}

void GameRenderer::Impl::prepareForChart(game::Chart* chart) {
  if (chart == nullptr) return;
  if (chart == loadedChart) return;
  loadedChart = chart;
  badEffects.clear();
  badSpawned.clear();
  hitEffects.clear();
  clickEffects.reset();
  // A new chart starts the readout from zero rather than from the previous chart's value.
  hudDisplayScore = std::numeric_limits<double>::quiet_NaN();
  hudDisplayAccuracy = std::numeric_limits<double>::quiet_NaN();
  hudDisplayLastUpdateSec = -1.0;
  hudDisplayReset = true;
  // The level mod's ramp and challenge state belong to one chart; a switch starts over.
  ametrineLastSec = -1.0;
  blockUpdateLastSec = 0.0;
  if (chart->levelEffects != nullptr) chart->levelEffects->resetChallenge();
  // The chart-shader setting skips loading the storyboard chain entirely, like the
  // reference does for the shader path.
  if (!disableChartShaderSetting) {
    chain.prepare(assets, chart->prprShaderSources, chart->prprEffects);
  }
  chain.setAuxTextures(effectGlitchMap, effectGlitchDogMap, effectWireNoise, effectDisplaceMap);
  scene::buildLineDrawOrder(*chart, lineDrawOrder);
}

scene::LineTint GameRenderer::Impl::lineTintFor(const RenderFrameInput& input,
    const game::JudgementEngine* judgement) const {
  // The skin's note colour overrides the plain-line colour; with the AP/FC indicator enabled
  // the same colours replace the indicator's perfect/good tones, while a run that has lost a
  // note always shows the plain white line.
  const auto perfectTint = [this]() {
    return skinMaterials.perfectFromSkin
        ? scene::LineTint{skinMaterials.perfect.red, skinMaterials.perfect.green,
              skinMaterials.perfect.blue, skinMaterials.perfect.alpha}
        : scene::LineTint{1.0F, 1.0F, 1.0F, 1.0F};
  };
  if (!input.apfcIndicator) return perfectTint();
  const auto goodTint = [this]() {
    return skinMaterials.goodFromSkin
        ? scene::LineTint{skinMaterials.good.red, skinMaterials.good.green,
              skinMaterials.good.blue, skinMaterials.good.alpha}
        : scene::LineTint{162.0F / 255.0F, 238.0F / 255.0F, 1.0F, 1.0F};
  };
  if (judgement == nullptr) return perfectTint();
  const auto& counts = judgement->counts();
  if (counts[2] + counts[3] > 0) return scene::LineTint{1.0F, 1.0F, 1.0F, 1.0F};
  if (counts[1] > 0) return goodTint();
  return skinMaterials.perfectFromSkin
      ? perfectTint()
      : scene::LineTint{254.0F / 255.0F, 1.0F, 169.0F / 255.0F, 1.0F};
}

void GameRenderer::Impl::drawSideColumns(const scene::StageLayout& layout,
    const gl::Texture* artwork, float sideArtworkAlpha, float sideDimAlpha) {
  if (!layout.hasSideBlanks) return;
  const auto& stage = layout.stage;
  const auto projection = layout.projection();
  const float leftWidth = std::max(0.0F, stage.x);
  const float rightX = std::min(layout.viewWidth, stage.right());
  const float rightWidth = std::max(0.0F, layout.viewWidth - rightX);
  const auto drawSolid = [&](const math::Rect& rect, float alpha) {
    if (alpha <= 0.001F || rect.empty()) return;
    batch.begin();
    batch.addQuadTopLeft(rect.x, rect.y, rect.width, rect.height, 0.0F, 0.0F, 0.0F, alpha, 0.0F,
        0.0F, 1.0F, 1.0F);
    batch.flush(projection, textures.white(), nullptr);
  };
  const auto drawColumn = [&](float x, float columnWidth) {
    if (columnWidth <= 0.5F) return;
    const math::Rect band{x, stage.y, columnWidth, stage.height};
    if (artwork != nullptr && sideArtworkAlpha > 0.001F) {
      // The blurred texture already holds the full-view cover crop; the band shows the
      // corresponding slice of the same full-width background.
      const float textureAspect = static_cast<float>(artwork->width()) /
          std::max(1.0F, static_cast<float>(artwork->height()));
      const float destinationWidth = layout.viewWidth;
      const float destinationHeight = stage.height;
      const float destinationAspect = destinationWidth / std::max(1.0F, destinationHeight);
      float drawWidth = destinationWidth;
      float drawHeight = destinationHeight;
      if (textureAspect > destinationAspect) {
        drawWidth = destinationHeight * textureAspect;
      } else {
        drawHeight = destinationWidth / textureAspect;
      }
      const float drawX = (destinationWidth - drawWidth) * 0.5F;
      const float drawY = stage.y + (destinationHeight - drawHeight) * 0.5F;
      const float left = std::max(drawX, band.left());
      const float top = std::max(drawY, band.top());
      const float right = std::min(drawX + drawWidth, band.right());
      const float bottom = std::min(drawY + drawHeight, band.bottom());
      if (right > left + 0.5F && bottom > top + 0.5F) {
        const float u0 = (left - drawX) / drawWidth;
        const float u1 = (right - drawX) / drawWidth;
        const float v0 = (top - drawY) / drawHeight;
        const float v1 = (bottom - drawY) / drawHeight;
        batch.begin();
        batch.addQuadTopLeft(left, top, right - left, bottom - top, 1.0F, 1.0F, 1.0F,
            sideArtworkAlpha, u0, v0, u1, v1);
        batch.flush(projection, *artwork, nullptr);
      }
    }
    drawSolid(band, sideDimAlpha);
  };
  if (stage.y > 0.5F) {
    drawSolid(math::Rect{0.0F, 0.0F, leftWidth, stage.y}, 1.0F);
    drawSolid(math::Rect{rightX, 0.0F, rightWidth, stage.y}, 1.0F);
  }
  const float bottom = stage.bottom();
  if (bottom < layout.viewHeight - 0.5F) {
    drawSolid(math::Rect{0.0F, bottom, leftWidth, layout.viewHeight - bottom}, 1.0F);
    drawSolid(math::Rect{rightX, bottom, rightWidth, layout.viewHeight - bottom}, 1.0F);
  }
  drawColumn(0.0F, leftWidth);
  drawColumn(rightX, rightWidth);
}

bool GameRenderer::Impl::dsGridCanvasVisible(const RenderFrameInput& input,
    const game::Chart* chart) const {
  if (chart == nullptr || chart->levelEffects == nullptr) return false;
  const auto& effect = *chart->levelEffects;
  if (effect.effectName() != "DesultorySignalsLevelEffects") return false;
  return effect.track("gridOpacity", static_cast<float>(input.chartTimeSec)) > 0.001F;
}

void GameRenderer::Impl::drawBackgroundPass(const scene::StageLayout& layout, float artworkAlpha,
    float dimAlpha, float sideArtworkAlpha, float sideDimAlpha, bool opening,
    bool deferSideColumns) {
  const auto& stage = layout.stage;
  const auto projection = layout.projection();

  // The reference never shows the raw artwork during gameplay: the play field and the
  // fixed side panels both sample a downscaled, heavily blurred copy. During the opening
  // the blur may not exist yet, and the reference falls back to a flat dark field there.
  const gl::Texture* blurTexture =
      background != nullptr ? backgroundBlur.ensure(width, height, background) : nullptr;
  const gl::Texture* artworkTexture = blurTexture != nullptr
      ? blurTexture
      : (opening ? nullptr : background);

  // Draws `texture` so it covers the given destination rectangle, restricted to `clip`.
  // The sprite batch has no scissor support, so the visible part is emitted as one quad
  // whose UVs are mapped back onto the cover-cropped source rectangle.
  const auto drawCover = [&](const gl::Texture& texture, float destinationX, float destinationY,
                             float destinationWidth, float destinationHeight, float alpha,
                             const math::Rect& clip) {
    if (alpha <= 0.001F || destinationWidth <= 0.5F || destinationHeight <= 0.5F) return;
    const float textureAspect = static_cast<float>(texture.width()) /
        std::max(1.0F, static_cast<float>(texture.height()));
    const float destinationAspect = destinationWidth / std::max(1.0F, destinationHeight);
    float drawWidth = destinationWidth;
    float drawHeight = destinationHeight;
    if (textureAspect > destinationAspect) {
      drawWidth = destinationHeight * textureAspect;
    } else {
      drawHeight = destinationWidth / textureAspect;
    }
    const float drawX = destinationX + (destinationWidth - drawWidth) * 0.5F;
    const float drawY = destinationY + (destinationHeight - drawHeight) * 0.5F;
    const float left = std::max(drawX, clip.left());
    const float top = std::max(drawY, clip.top());
    const float right = std::min(drawX + drawWidth, clip.right());
    const float bottom = std::min(drawY + drawHeight, clip.bottom());
    if (right <= left + 0.5F || bottom <= top + 0.5F) return;
    const float u0 = (left - drawX) / drawWidth;
    const float u1 = (right - drawX) / drawWidth;
    const float v0 = (top - drawY) / drawHeight;
    const float v1 = (bottom - drawY) / drawHeight;
    batch.begin();
    batch.addQuadTopLeft(left, top, right - left, bottom - top, 1.0F, 1.0F, 1.0F, alpha, u0, v0,
        u1, v1);
    batch.flush(projection, texture, nullptr);
  };
  const auto drawSolid = [&](const math::Rect& rect, float alpha) {
    if (alpha <= 0.001F || rect.empty()) return;
    batch.begin();
    batch.addQuadTopLeft(rect.x, rect.y, rect.width, rect.height, 0.0F, 0.0F, 0.0F, alpha, 0.0F,
        0.0F, 1.0F, 1.0F);
    batch.flush(projection, textures.white(), nullptr);
  };

  // Fixed side columns: the reference draws the blurred artwork clipped to each band and
  // then a fixed dark overlay, independent of the background-dim setting. The areas above
  // and below the play field inside a band stay black. A level whose canvas is wider than the
  // play field repaints them after its own objects, so this pass can leave them out.
  if (!deferSideColumns) drawSideColumns(layout, artworkTexture, sideArtworkAlpha, sideDimAlpha);

  // Play field backdrop: the artwork is drawn with the intro/outro transmission over the
  // black clear, then the stage dim overlay covers it. The chart-reveal ring is painted
  // after the scene content instead (drawRevealPanels), so it can cover the overflow.
  if (artworkTexture != nullptr) {
    drawCover(*artworkTexture, 0.0F, stage.y, layout.viewWidth, stage.height, artworkAlpha,
        stage);
  }
  drawSolid(stage, dimAlpha);

}

void GameRenderer::Impl::drawLinePass(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, const scene::StageLayout& layout) {
  const auto projection = layout.stageProjection();
  const scene::LineTint indicatorTint = lineTintFor(input, judgement);
  const bool official = chart->formatVersion > 0;
  batch.begin();

  if (input.transition.inIntro) {
    // The opening draws a single decorative line that grows from the centre, then holds
    // fully expanded until gameplay starts; the chart's own lines stay hidden.
    scene::drawIntroLine(batch, layout, official, input.transition.introLineScale,
        std::clamp(input.judgeLineWidth, 0.5F, 2.0F), indicatorTint);
    batch.flush(projection, textures.white(), nullptr);
    return;
  }

  // Outro: plain lines collapse towards the stage centre before the overlay takes over.
  float outroScale = 1.0F;
  if (input.transition.inOutro) {
    outroScale = scene::outroLineCollapseScale(true, input.transition.outroElapsedSec);
    // Before the collapse window the lines keep their full length, so only a finished
    // collapse may hide them.
    if (!scene::outroLinesVisible(true, input.transition.outroElapsedSec) ||
        (outroScale <= 0.001F && input.transition.outroElapsedSec >=
             scene::OUTRO_LINE_COLLAPSE_START_SEC + scene::OUTRO_LINE_COLLAPSE_DURATION_SEC)) {
      batch.discard();
      return;
    }
    if (input.transition.outroElapsedSec >= scene::OUTRO_LINE_COLLAPSE_START_SEC) {
      // Inside the window the length is the collapse factor itself.
      outroScale = std::max(0.001F, outroScale);
    }
  }

  const float lineWidthScale = std::clamp(input.judgeLineWidth, 0.5F, 2.0F);
  for (const int lineIndex : lineDrawOrder) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(chart->judgeLineList.size())) continue;
    const auto& line = *chart->judgeLineList[static_cast<std::size_t>(lineIndex)];
    if (line.attachUiElementId != 0) continue;
    const auto& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
    // The chart-reveal camera lifts hidden lines to a low alpha floor instead of hiding
    // them, so the authored geometry becomes visible while the camera is settled.
    const bool revealHiddenLine =
        input.revealHiddenObjects && input.revealHiddenFloorAlpha > 0.001F;
    if (!frame.valid || (frame.hideLine && !revealHiddenLine)) continue;
    const float alpha = frame.hideLine
        ? input.revealHiddenFloorAlpha
        : std::clamp(frame.alphaRaw, 0.0F, 1.0F);
    if (alpha <= 0.0F) continue;

    const float halfLength = official
        ? layout.stage.height * scene::LINE_LENGTH_OFFICIAL * frame.scaleX
        : layout.stage.width * scene::LINE_LENGTH_EDITOR * frame.scaleX;
    // The judge-line-width setting scales only plain judge lines, like the reference;
    // storyboard textures and text lines keep their own size.
    const float lineWidth = official
        ? layout.stage.height * scene::LINE_WIDTH_OFFICIAL * frame.scaleY * lineWidthScale
        : layout.stage.height * scene::LINE_WIDTH_EDITOR * frame.scaleY * lineWidthScale;

    // The skin/indicator colour applies only to a plain line: a line with a colour event
    // uses the event colour, and a line that draws a texture or storyboard text keeps the
    // default white tint instead of the indicator colour.
    const bool plainLine =
        !frame.hasColorEvent && line.texture.empty() && !frame.hasTextEvents;
    scene::LineTint tint = plainLine ? indicatorTint : scene::LineTint{};
    if (frame.hasColorEvent) {
      tint.red = frame.colorR;
      tint.green = frame.colorG;
      tint.blue = frame.colorB;
    }
    // A storyboard line with a texture draws the texture instead of the plain quad;
    // a line with text events draws its resolved text. Both keep the anchor and the
    // line rotation. The reference hides these storyboard elements during the exit
    // animation, leaving only the plain lines to collapse.
    if (!input.transition.inOutro && !frame.hasTextEvents && !line.texture.empty()) {
      // A GIF line texture advances frames with the visual clock, or pins a frame when a
      // GIF event supplies a position.
      asset::AnimatedTexture* animated = asset::TextureCache::isGifPath(line.texture)
          ? textures.loadGifFile(line.texture)
          : nullptr;
      gl::Texture* texture = animated != nullptr
          ? &animated->frameAt(std::isfinite(frame.gifPosition)
                  ? animated->frameForPosition(frame.gifPosition)
                  : animated->frameForTime(input.visualTimeSec))
          : textures.loadFile(line.texture);
      if (texture != nullptr) {
        // RPE authors in a 1350x900 space, so the texture is scaled by the stage.
        const float baseScaleX = std::max(0.1F, layout.stage.width / 1350.0F);
        float texScaleX = frame.scaleX;
        float texScaleY = frame.scaleY;
        if (line.useOfficialScale && texture->height() > 0) {
          const float newSy = (1080.0F / static_cast<float>(texture->height())) *
              std::abs(texScaleY);
          texScaleX = newSy * texScaleX;
          texScaleY = newSy;
        }
        float width = 0.0F;
        float height = 0.0F;
        if (line.rpeTextureScaleInPixels) {
          width = texScaleX * baseScaleX;
          height = texScaleX * baseScaleX;
        } else {
          width = static_cast<float>(texture->width()) * baseScaleX * texScaleX;
          height = static_cast<float>(texture->height()) * baseScaleX * texScaleY;
        }
        if (width > 0.0F && height > 0.0F) {
          batch.begin();
          batch.addQuadAnchored(frame.x, frame.y, width, height, frame.rotateDeg, tint.red,
              tint.green, tint.blue, alpha, frame.anchorX, frame.anchorY, 0.0F, 0.0F, 1.0F, 1.0F);
          batch.flush(projection, *texture, nullptr);
          continue;
        }
      }
    } else if (!input.transition.inOutro && !frame.hasTextEvents && !frame.text.empty()) {
      // Text is rasterised at 50 units in the 1350-wide authoring space, then scaled to
      // the stage; a multi-line string scales so each line keeps the full size.
      const float fontBaseSize = layout.stage.width * (50.0F / 1350.0F);
      std::string normalized = frame.text;
      std::replace(normalized.begin(), normalized.end(), '\n', '\n');
      int lineCount = 1;
      for (const char character : normalized) {
        if (character == '\n') ++lineCount;
      }
      auto* glyphs = text.get(normalized, fontBaseSize);
      if (glyphs != nullptr && glyphs->height > 0) {
        const float targetHeight = fontBaseSize * static_cast<float>(lineCount);
        const float scaleToTarget = targetHeight / static_cast<float>(glyphs->height);
        const float width = static_cast<float>(glyphs->width) * scaleToTarget * frame.scaleX;
        const float height = static_cast<float>(glyphs->height) * scaleToTarget * frame.scaleY;
        if (width > 0.0F && height > 0.0F) {
          batch.begin();
          batch.addQuadAnchored(frame.x, frame.y, width, height, frame.rotateDeg, tint.red,
              tint.green, tint.blue, alpha, frame.anchorX, frame.anchorY, 0.0F, 0.0F, 1.0F, 1.0F);
          batch.flush(projection, glyphs->texture, nullptr);
          continue;
        }
      }
    }
    // A line that owns text events draws nothing once its text is empty.
    if (frame.hasTextEvents) continue;

    scene::drawPlainLine(batch, frame.x, frame.y, halfLength * outroScale, lineWidth,
        frame.rotateDeg, tint, alpha);
  }
  batch.flush(projection, textures.white(), nullptr);
}

void GameRenderer::Impl::drawNotePasses(const scene::StageLayout& layout,
    const RenderFrameInput& input, game::Chart* chart) {
  if (chart == nullptr || lineFrames.size() != chart->judgeLineList.size()) return;
  const auto projection = layout.stageProjection();

  scene::NotePassParams params;
  params.chartTimeSec = input.chartTimeSec;
  params.chartGlobalBeat = chart->bpmTimeline != nullptr
      ? chart->bpmTimeline->secToBeat(input.chartTimeSec)
      : std::numeric_limits<double>::quiet_NaN();
  params.stageWidth = layout.stage.width;
  params.stageHeight = layout.stage.height;
  // A non-positive key scale would collapse or invert the notes, so it falls back to 1.
  const float keyScale = input.keyScale > 0.0F ? input.keyScale : 1.0F;
  params.noteWidthBase =
      layout.stage.width * noteWidthRatio(layout.stage.width, layout.stage.height) * keyScale;
  params.noteWidthScale = noteWidthScale;
  params.scrollSpeed = input.scrollSpeed;
  params.officialFormat = chart->formatVersion > 0;
  params.multiPressHighlight = input.multiPressHighlight;
  // Chart reveal draws the hidden notes that the authored chart keeps out of view.
  params.revealHiddenObjects = input.revealHiddenObjects;
  params.revealHiddenFloorAlpha = input.revealHiddenFloorAlpha;
  // The mirror mode reflects the whole play field through the stage projection. Sprite
  // artwork has to be un-mirrored again or the notes would show flipped, so the note and
  // hold batches swap their horizontal UVs when the field is mirrored.
  params.flipU = layout.mirror;
  // Notes are culled against the play field expanded by a twelfth on each side.
  const float marginW = layout.stage.width / 12.0F;
  const float marginH = layout.stage.height / 12.0F;
  params.cullBounds = {layout.stage.x - marginW, layout.stage.y - marginH,
      layout.stage.width + marginW * 2.0F, layout.stage.height + marginH * 2.0F};

  // Hold bodies and heads share one pass so the body never paints over its head.
  for (int highlight = 0; highlight < 2; ++highlight) {
    gl::Texture* holdTexture = holdTextures[highlight];
    if (holdTexture == nullptr) continue;
    batch.begin();
    scene::buildHoldBatch(batch, lineFrames, lineDrawOrder, *chart, highlight == 1,
        static_cast<float>(holdTexture->width()), static_cast<float>(holdTexture->height()),
        holdUv[highlight], holdRepeat, holdKeepHead, holdCompact,
        static_cast<float>(holdBodies[highlight] != nullptr ? holdBodies[highlight]->width() : 0),
        static_cast<float>(holdBodies[highlight] != nullptr ? holdBodies[highlight]->height() : 0),
        params);
    if (!batch.empty()) batch.flush(projection, *holdTexture, holdBodies[highlight]);
  }

  // Note heads, one draw per distinct texture.
  for (gl::Texture* texture : headTextureList) {
    if (texture == nullptr) continue;
    for (const int type : kHeadDrawOrder) {
      for (int highlight = 0; highlight < 2; ++highlight) {
        if (noteHeads[type][highlight] != texture) continue;
        batch.begin();
        scene::buildHeadBatch(batch, lineFrames, lineDrawOrder, *chart, type, highlight == 1,
            static_cast<float>(texture->width()), static_cast<float>(texture->height()), params);
        if (!batch.empty()) batch.flush(projection, *texture, nullptr);
      }
    }
  }
  // Fading copies of notes that were just judged bad. A note is spawned once, the first
  // frame it is seen judged, so a note never produces two copies.
  for (const game::Note* note : chart->allNotesSorted) {
    if (note == nullptr || note->judgeResult != static_cast<int>(game::JudgementEngine::Judgement::Bad)) {
      continue;
    }
    if (badSpawned.find(note) != badSpawned.end()) continue;
    badSpawned.insert(note);
    const game::Note& judged = *note;
    // Locate the note's line by identity; the frame list is indexed like the line list.
    int lineIndex = -1;
    for (std::size_t index = 0; index < chart->judgeLineList.size(); ++index) {
      if (chart->judgeLineList[index].get() == judged.master) {
        lineIndex = static_cast<int>(index);
        break;
      }
    }
    if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= lineFrames.size()) continue;
    const auto& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
    if (!frame.valid) continue;
    gl::Texture* texture = noteHeads[judged.type][(judged.morebets == 1 && input.multiPressHighlight) ? 1 : 0];
    if (texture == nullptr) continue;
    const int type = std::clamp(judged.type, 0, 4);
    const float widthScale = params.widthScaleFor(type, judged.morebets == 1 && input.multiPressHighlight);
    const float size = (!std::isfinite(judged.size) || judged.size == 0.0F) ? 1.0F : judged.size;
    const float xScale =
        (!std::isfinite(judged.xScale) || judged.xScale == 0.0F) ? 1.0F : judged.xScale;
    const float baseWidth = params.noteWidthBase * size * widthScale;
    const float noteWidth = baseWidth * xScale;
    const float noteHeight =
        (baseWidth / static_cast<float>(texture->width())) * static_cast<float>(texture->height());
    const float offsetX = static_cast<float>(judged.positionX * game::PGR_WIDTH * params.stageWidth);
    const float noteX = frame.x + offsetX * frame.cosLine;
    const float noteY = frame.y + offsetX * frame.sinLine;
    const float rotation = frame.rotateDeg + (judged.isAbove ? 0.0F : 180.0F);
    badEffects.spawn(input.visualTimeSec, noteX, noteY, noteWidth, noteHeight, rotation,
        0.0F, 0.0F, 1.0F, 1.0F, texture);
  }
  batch.begin();
  badEffects.draw(batch, projection, input.visualTimeSec, textures.white(), layout.mirror);
}

void GameRenderer::Impl::drawHitEffects(const scene::StageLayout& layout,
    const RenderFrameInput& input, game::Chart* chart) {
  if (chart == nullptr || hitFx == nullptr) return;
  const float noteWidth =
      layout.stage.width * noteWidthRatio(layout.stage.width, layout.stage.height) * input.keyScale;
  const bool autoplayBursts = input.practice.autoPlay && !input.replayPlayback;
  hitEffects.update(*chart, input.chartTimeSec, input.visualTimeSec, layout.stage.x,
      layout.stage.y, layout.stage.width, layout.stage.height, noteWidth,
      input.practice.enabled ? input.practice.speed : 1.0F, autoplayBursts);
  // Hold bursts are timed by the gameplay session and arrive as events. A head burst is
  // placed where the head was when it was hit (above the line for an early hit, on the line
  // once the head has arrived); body bursts sit on the line contact point.
  const float musicSpeed = input.practice.enabled ? input.practice.speed : 1.0F;
  for (const auto& burst : input.holdHeadBursts) {
    const game::Note* note = burst.noteIndex >= 0 &&
            static_cast<std::size_t>(burst.noteIndex) < chart->allNotesSorted.size()
        ? chart->allNotesSorted[static_cast<std::size_t>(burst.noteIndex)]
        : nullptr;
    if (note == nullptr) continue;
    // Resolve the judged frame's line state, then the head pose on that line.
    int lineIndex = -1;
    for (std::size_t index = 0; index < chart->judgeLineList.size(); ++index) {
      if (chart->judgeLineList[index].get() == note->master) {
        lineIndex = static_cast<int>(index);
        break;
      }
    }
    if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= lineFrames.size()) continue;
    // The burst has to be placed where the head was when it was hit, not where it is by the
    // time the frame is drawn; an autoplay hit can be noticed a frame after the note's own time,
    // and using the frame's state would slide the effect past the head.
    scene::LineFrame judgedFrame = lineFrames[static_cast<std::size_t>(lineIndex)];
    if (note->master != nullptr && burst.timeSec != input.chartTimeSec) {
      const float stageAspect =
          layout.stage.height > 1.0e-6F ? layout.stage.width / layout.stage.height : 16.0F / 9.0F;
      const auto state = note->master->fillState(burst.timeSec, stageAspect);
      judgedFrame.x = layout.stage.x + state.xNorm * layout.stage.width;
      judgedFrame.y = layout.stage.y + state.yNorm * layout.stage.height;
      judgedFrame.rotateDeg = state.rotateDeg;
      const double radians = static_cast<double>(state.rotateDeg) * 3.14159265358979323846 / 180.0;
      judgedFrame.cosLine = static_cast<float>(std::cos(radians));
      judgedFrame.sinLine = static_cast<float>(std::sin(radians));
      judgedFrame.floorPosition = state.floorPosition;
      judgedFrame.bpm = note->master->bpm > 0.0 ? note->master->bpm : 120.0;
      judgedFrame.valid = true;
      judgedFrame.renderable = true;
    }
    scene::NotePassParams poseParams;
    poseParams.chartTimeSec = burst.timeSec;
    poseParams.stageWidth = layout.stage.width;
    poseParams.stageHeight = layout.stage.height;
    poseParams.scrollSpeed = input.scrollSpeed;
    poseParams.officialFormat = chart->formatVersion > 0;
    const scene::NoteHeadPose pose = scene::noteHeadPose(*note, judgedFrame, poseParams);
    if (!pose.valid) continue;
    hitEffects.spawnHoldHead(*chart, burst, input.chartTimeSec, input.visualTimeSec, pose.x,
        pose.y, pose.rotationDeg, noteWidth, musicSpeed);
  }
  for (const auto& burst : input.holdBodyBursts) {
    hitEffects.spawnHoldBody(*chart, burst, input.chartTimeSec, input.visualTimeSec,
        layout.stage.x, layout.stage.y, layout.stage.width, layout.stage.height, noteWidth);
  }
  // Autoplay draws the bursts the chart rolled during preparation; a manual run and a
  // replay draw the ones their own judgements produce. The reference keeps the two apart,
  // so a note is never burst twice.
  if (autoplayBursts) {
    clickEffects.draw(*chart, batch, layout.stageProjection(), *hitFx, textures.white(),
        input.chartTimeSec, input.visualTimeSec, layout.stage.x, layout.stage.y,
        layout.stage.width, layout.stage.height, noteWidth, hitAtlas, hitStyle, hitSkin,
        layout.mirror);
  }
  hitEffects.draw(batch, layout.stageProjection(), *hitFx, textures.white(), input.visualTimeSec,
      layout.mirror);
}

void GameRenderer::Impl::drawBlockedTouchPulses(const RenderFrameInput& input,
    const scene::StageLayout& layout, game::Chart* chart) {
  if (chart == nullptr) return;
  if (input.blockedTouchPositions.empty()) return;
  const auto& white = textures.white();
  if (!white.valid()) return;

  // The pulse decays over half a second from the moment the zone last confirmed the finger, and
  // it sits at the finger itself so the player can see which touch the zone is holding. While the
  // finger is held the list is re-stamped every frame, so the disc stays at full strength and only
  // decays in the frame the press ends.
  const float age = input.blockedTouchPulseSec >= 0.0
      ? static_cast<float>(input.chartTimeSec - input.blockedTouchPulseSec)
      : 1.0F;
  const float pulse = std::max(0.0F, 1.0F - std::max(0.0F, age) * 2.0F);
  const float alpha = 0.30F + 0.35F * pulse;

  batch.begin();
  for (const auto& zone : chart->blockAreaList) {
    if (!zone.visible || !zone.active) continue;
    // Each zone pulses only for the fingers it actually swallows, so a point inside a hole or
    // outside the zone draws nothing for it.
    for (const auto& point : input.blockedTouchPositions) {
      if (!zone.containsInset(point[0], point[1], layout.stage.height)) continue;
      // The pulse scales with the zone the finger is inside, so a wide zone pulses wider.
      const float radius = std::max(zone.halfW, zone.halfH) * (1.0F + 0.08F * pulse);
      batch.addQuad(point[0], point[1], radius * 2.0F, radius * 2.0F, 0.0F, 1.0F, 0.0F, 0.0F,
          alpha * 0.5F, 0.0F, 0.0F, 1.0F, 1.0F);
      const float ringRadius = radius * 0.62F;
      batch.addQuad(point[0], point[1], ringRadius * 2.0F, ringRadius * 2.0F, 0.0F, 1.0F, 0.35F,
          0.35F, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
    }
  }
  if (!batch.empty()) batch.flush(layout.projection(), white, nullptr);
}

void GameRenderer::Impl::drawRevealPanels(const scene::StageLayout& inner,
    const scene::StageLayout& outer, float dimAlpha) {
  // The ring is drawn after the scene content, because judge lines and notes overflow the
  // play field and must be covered outside the reveal mask. It reuses the side-column
  // treatment: the fixed blurred artwork at full strength under the fixed dark overlay.
  const gl::Texture* blurTexture =
      background != nullptr ? backgroundBlur.ensure(width, height, background) : nullptr;
  const gl::Texture* artworkTexture = blurTexture != nullptr ? blurTexture : background;
  if (artworkTexture == nullptr) return;
  const auto projection = outer.projection();
  const auto& full = outer.stage;
  const auto& shown = inner.stage;
  const math::Rect panels[4] = {
      {full.x, full.y, full.width, std::max(0.0F, shown.y - full.y)},
      {full.x, shown.bottom(), full.width, std::max(0.0F, full.bottom() - shown.bottom())},
      {full.x, shown.y, std::max(0.0F, shown.x - full.x), shown.height},
      {shown.right(), shown.y, std::max(0.0F, full.right() - shown.right()), shown.height}};
  const float textureAspect = static_cast<float>(artworkTexture->width()) /
      std::max(1.0F, static_cast<float>(artworkTexture->height()));
  const float destinationAspect = full.width / std::max(1.0F, full.height);
  float drawWidth = full.width;
  float drawHeight = full.height;
  if (textureAspect > destinationAspect) {
    drawWidth = full.height * textureAspect;
  } else {
    drawHeight = full.width / textureAspect;
  }
  const float drawX = full.x + (full.width - drawWidth) * 0.5F;
  const float drawY = full.y + (full.height - drawHeight) * 0.5F;
  for (const math::Rect& panel : panels) {
    if (panel.empty()) continue;
    const float left = std::max(drawX, panel.left());
    const float top = std::max(drawY, panel.top());
    const float right = std::min(drawX + drawWidth, panel.right());
    const float bottom = std::min(drawY + drawHeight, panel.bottom());
    if (right > left + 0.5F && bottom > top + 0.5F) {
      batch.begin();
      batch.addQuadTopLeft(left, top, right - left, bottom - top, 1.0F, 1.0F, 1.0F, 1.0F,
          (left - drawX) / drawWidth, (top - drawY) / drawHeight, (right - drawX) / drawWidth,
          (bottom - drawY) / drawHeight);
      batch.flush(projection, *artworkTexture, nullptr);
    }
    batch.begin();
    batch.addQuadTopLeft(panel.x, panel.y, panel.width, panel.height, 0.0F, 0.0F, 0.0F, dimAlpha,
        0.0F, 0.0F, 1.0F, 1.0F);
    batch.flush(projection, textures.white(), nullptr);
  }
}

void GameRenderer::Impl::drawHudPass(const RenderFrameInput& input,
    const scene::StageLayout& layout, game::Chart* chart,
    const game::JudgementEngine* judgement) {
  scene::HudValues values;
  values.score = input.hud.score;
  values.combo = input.hud.combo;
  values.maxCombo = input.hud.maxCombo;
  values.accuracy = input.hud.accuracy;
  values.playheadSec = input.hud.playheadSec;
  values.totalDurationSec = input.hud.totalDurationSec;
  values.songName = input.songName;
  values.levelName = input.levelName;
  values.comboLabel = input.hud.comboLabel;
  values.showAccuracy = input.hud.showAccuracy;
  values.scoreJumpEffect = input.hud.scoreJumpEffect;
  values.introProgress = input.hud.introProgress;
  values.outroProgress = input.hud.outroProgress;
  values.visualTimeSec = input.hud.visualTimeSec;
  values.pauseRingAgeSec = input.hud.pauseRingAgeSec;
  values.revealCameraActive = input.hud.revealCameraActive;
  values.revealScale = input.hud.revealScale;
  values.fps = input.hud.fps;
  values.showFps = input.hud.showFps;
  // The catch-up readout carries over between frames, so the state lives here rather than in
  // the pass; a new chart starts it over, which is what a fresh run shows.
  values.displayScore = hudDisplayScore;
  values.displayAccuracy = hudDisplayAccuracy;
  values.displayLastUpdateSec = hudDisplayLastUpdateSec;
  values.displayReset = hudDisplayReset;
  // A failed level challenge replaces the combo readouts with the distorted block pattern,
  // so the plain widgets are suppressed while that state is up.
  if (chart != nullptr && chart->levelEffects != nullptr) {
    values.showCombo = !chart->levelEffects->isChallengeFailed();
  }

  // Attach-UI: a chart may animate individual HUD widgets with judge lines. The line
  // evaluation for this frame already produced their state, so the HUD only needs to
  // receive the seven element transforms in canvas space.
  scene::HudTransforms transforms;
  if (chart != nullptr) {
    transforms.useFix = chart->useAttachUiFix;
    for (int element = 1; element < scene::kHudElementCount; ++element) {
      const int lineIndex = chart->attachUiLineIndex[static_cast<std::size_t>(element)];
      if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= lineFrames.size()) continue;
      const scene::LineFrame& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
      if (!frame.valid) continue;
      auto& transform = transforms.element[static_cast<std::size_t>(element)];
      transform.valid = true;
      transform.translateX = (frame.xNorm - 0.5F) * layout.stage.width;
      transform.translateY = (frame.yNorm - 0.5F) * layout.stage.height;
      transform.rotateDeg = -frame.rotateDeg;
      transform.scaleX = frame.scaleX;
      transform.scaleY = frame.scaleY;
      // Negative alphas encode line visibility modes; an attached HUD widget simply
      // stays hidden in that case.
      const float rawAlpha = frame.alphaRaw;
      transform.alpha = rawAlpha < 0.0F ? 0.0F
          : std::clamp(rawAlpha > 1.5F ? rawAlpha / 255.0F : rawAlpha, 0.0F, 1.0F);
      transform.red = std::isfinite(frame.colorR) ? frame.colorR : 1.0F;
      transform.green = std::isfinite(frame.colorG) ? frame.colorG : 1.0F;
      transform.blue = std::isfinite(frame.colorB) ? frame.colorB : 1.0F;
      if (element == static_cast<int>(scene::HudElement::ComboLabel) && frame.hasTextEvents) {
        transform.text = frame.text;
      }
    }
  }

  // Judgement offset indicator state for this frame. The engine keeps the marks and the
  // pointer easing; the HUD only mirrors them (the update call also expires old marks and
  // starts the pointer's return-to-centre, which is presentation state).
  scene::HudHitOffset hitOffset;
  hitOffset.mode = input.hud.hitOffsetIndicatorMode;
  hitOffset.suppressed = input.hud.hitOffsetSuppressed;
  if (judgement != nullptr) {
    auto& indicator = const_cast<game::HitOffsetIndicator&>(judgement->indicator());
    indicator.update(input.hud.visualTimeSec);
    hitOffset.perfectWindowSec = static_cast<float>(judgement->tapPerfectWindow());
    hitOffset.goodWindowSec = static_cast<float>(judgement->tapGoodWindow());
    hitOffset.badWindowSec = static_cast<float>(judgement->windows().bad);
    hitOffset.pointerAngleDeg = indicator.pointerAngleDeg();
    for (const auto& mark : indicator.marks()) {
      if (hitOffset.markCount >= static_cast<int>(hitOffset.marks.size())) break;
      if (!mark.active) continue;
      const float age = game::HitOffsetIndicator::markAge(input.hud.visualTimeSec, mark);
      if (age < 0.0F || age >= 1.0F) continue;
      auto& destination = hitOffset.marks[static_cast<std::size_t>(hitOffset.markCount)];
      destination.active = true;
      destination.angleDeg = mark.angleDeg;
      destination.judgement = static_cast<int>(mark.judgement);
      destination.age = age;
      ++hitOffset.markCount;
    }
  }

  scene::HudColors colors;
  scene::HudTextures hudTextures;
  hudTextures.pause = hudArtwork.pause;
  hudTextures.timerLine = hudArtwork.timerLine;
  hudTextures.noteRing = hudArtwork.noteRing;
  hudTextures.hitOffsetArrow = hudArtwork.hitOffsetArrow;
  hudTextures.white = &textures.white();
  // The Ametrine failure vignette also dims the HUD, because the reference scales the whole
  // HUD canvas with the same ramp instead of drawing the ring over it.
  float hudAlpha = input.hud.alpha;
  if (chart != nullptr && chart->levelEffects != nullptr) {
    hudAlpha *= fx::AmetrineVignette::hudDimFactor(
        chart->levelEffects->ametrineVignette());
  }
  scene::drawHud(batch, text, layout, values, colors, hudTextures, transforms, hitOffset,
      hudAlpha,
      layout.projection());
  // The pass advanced the readout; keep the state for the next frame.
  hudDisplayScore = values.displayScore;
  hudDisplayAccuracy = values.displayAccuracy;
  hudDisplayLastUpdateSec = values.displayLastUpdateSec;
  hudDisplayReset = false;

  // A failed challenge replaces the combo readout with the distorted block field, drawn at
  // the same anchor the number would occupy.
  if (chart != nullptr && chart->levelEffects != nullptr
      && chart->levelEffects->isChallengeFailed() && values.combo >= 3) {
    const float unit = scene::hudUnit(layout);
    blockedComboNumber.prepare(assets, std::to_string(values.combo), unit * 70.0F);
    blockedComboNumber.draw(batch, layout.projection(), textures.white(),
        scene::hudX(layout, 0.0F), scene::hudY(layout, scene::kComboNumberUiY),
        hudAlpha * 0.92F, input.visualTimeSec, static_cast<float>(width),
        static_cast<float>(height));
    // The label clears with its own shorter fade, so it leaves slightly ahead of the number.
    const float labelAlpha =
        hudAlpha * scene::comboLabelOutroAlpha(input.hud.outroProgress);
    blockedComboLabel.prepare(assets, values.comboLabel, unit * 24.0F);
    blockedComboLabel.draw(batch, layout.projection(), textures.white(),
        scene::hudX(layout, 0.0F), scene::hudY(layout, scene::kComboLabelUiY), labelAlpha,
        input.visualTimeSec, static_cast<float>(width), static_cast<float>(height));
  }
}

GameRenderer::GameRenderer() : impl_(std::make_unique<Impl>()) {}

GameRenderer::~GameRenderer() = default;

bool GameRenderer::initialize(const std::filesystem::path& assetRoot) {
  // This is the largest single block of the launch, and a packaged build shows it as black screen,
  // so each part reports its own cost instead of being one opaque number.
  const auto launchStart = std::chrono::steady_clock::now();
  const auto since = [&launchStart]() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - launchStart).count();
  };
  impl_->assets = assetRoot;
  impl_->textures.setAssetRoot(assetRoot);
  // Decode the startup artwork on worker threads while the text layer initialises. Decoding is the
  // half of a texture load that costs the time on a device and needs no graphics context, so it
  // runs underneath a stage that does not touch textures; the uploads below then run back to back
  // on this thread. Names are the ones loadGameplayTextures asks for.
  impl_->textures.beginPrefetchAssets({"res/background.jpg", "res/click.png", "res/click_mh.png",
      "res/drag.png", "res/drag_mh.png", "res/flick.png", "res/flick_mh.png", "res/hold.png",
      "res/hold_mh.png", "res/hit_fx.png", "res/pause.png", "res/timerLine.png",
      "res/NoteRing.png", "res/arrow.png", "official_textures/Glitch4in1.png",
      "official_textures/GlitchMap.png", "official_textures/WireNoise.png",
      "official_textures/DisplaceMap.png", "official_textures/BlockNoise1.png",
      "official_textures/BlockSpark.png"});
  if (!impl_->text.initialize(assetRoot)) {
    // A renderer that was not initialised draws nothing at all, which reads as an empty
    // scene on the device; the reason is logged instead of failing silently.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "[render] renderer initialization failed: text layer unavailable (asset root %s)",
        assetRoot.string().c_str());
    return false;
  }
  const double textMs = since();
  impl_->textures.awaitPrefetchAssets();
  impl_->loadGameplayTextures();
  const double texturesMs = since() - textMs;
  // Both shipped shaders are loaded here and stay ready for the whole session; a missing
  // asset simply leaves the composite unavailable and the inline silhouettes in charge.
  (void)impl_->blockComposite.prepare(assetRoot, impl_->blockDisplaceMap, impl_->blockSparkMap,
      impl_->blockNoiseMap);
  const double compositeMs = since() - textMs - texturesMs;
  bindDefaultBlend();
  impl_->initialized = true;
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
      "[render] renderer initialized (asset root %s) text=%.1fms textures=%.1fms shaders=%.1fms",
      assetRoot.string().c_str(), textMs, texturesMs, compositeMs);
  return true;
}

void GameRenderer::shutdown() noexcept {
  impl_->releaseResources();
  impl_->initialized = false;
}

void GameRenderer::resize(int width, int height) noexcept {
  // A capture owns the render size for its whole run. Taking the window size here would resize
  // every offscreen target out from under the capture, and the scene would be laid out for the
  // window while the frame is read back at the capture size -- the letterbox bands then appear
  // inside the video.
  if (impl_->capturing) {
    impl_->capturePreviousWidth = std::max(1, width);
    impl_->capturePreviousHeight = std::max(1, height);
    return;
  }
  impl_->width = std::max(1, width);
  impl_->height = std::max(1, height);
  glViewport(0, 0, impl_->width, impl_->height);
  impl_->sceneFbo.resize(impl_->width, impl_->height);
  impl_->blockResultFbo.resize(impl_->width, impl_->height);
  impl_->pingFbo.resize(impl_->width, impl_->height);
  impl_->pongFbo.resize(impl_->width, impl_->height);
  impl_->aaFbo.resize(impl_->width, impl_->height);
  impl_->halfResFbo.resize(std::max(1, impl_->width / 2), std::max(1, impl_->height / 2));
  impl_->blockComposite.resize(impl_->width, impl_->height);
  impl_->backgroundBlur.invalidate();
  // Allocate the pause backdrop's pyramid and its soft HUD target alongside the other frame
  // targets. Deferring them to the first pause makes that frame stall while the driver
  // creates and clears the textures.
  impl_->pauseBackdrop.warmUp(impl_->width, impl_->height);
  impl_->pauseHudBlur.warmUp(impl_->width, impl_->height);
  impl_->pauseBlurA.resize(impl_->width, impl_->height);
}

void GameRenderer::Impl::drawLevelEffectObjects(game::Chart* chart,
    const RenderFrameInput& input, const scene::StageLayout& layout) {
  if (chart == nullptr || chart->levelEffects == nullptr) return;
  auto& effect = *chart->levelEffects;
  const auto chartTime = static_cast<float>(input.chartTimeSec);
  const std::string& name = effect.effectName();

  // The Ametrine ramp advances on the wall clock, so a held frame does not step it and a
  // seek does not jump it. The elapsed time is clamped like every other visual clock.
  {
    const double now = input.visualTimeSec;
    float deltaSec = 0.0F;
    if (ametrineLastSec >= 0.0 && now >= ametrineLastSec) {
      deltaSec = std::clamp(static_cast<float>(now - ametrineLastSec), 0.0F, 0.25F);
    }
    ametrineLastSec = now;
    effect.updateAmetrineVignette(chartTime, deltaSec);
  }

  // Story mode gates every level effect except the Message level, which always plays its
  // effects.
  if (!input.storyMode && name != "MessageLevelEffects") return;

  // A level can black the backdrop out itself; the object sits over the background but
  // under the play field, so the notes stay fully lit.
  if (name == "HateLevelEffects") {
    const float alpha = effect.track("bgAlpha", chartTime);
    if (alpha > 0.002F) {
      // The quad covers the whole screen, side bars included.
      const auto projection = math::Mat4::orthoTopLeft(
          static_cast<float>(width), static_cast<float>(height));
      batch.begin();
      batch.addQuadTopLeft(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height),
          0.0F, 0.0F, 0.0F, std::min(1.0F, alpha), 0.0F, 0.0F, 1.0F, 1.0F);
      batch.flush(projection, textures.white(), nullptr);
    }
    return;
  }

  if (name == "MessageLevelEffects") {
    // White squares drift in from above and below; the field advances on the chart clock
    // and stays idle until the level's particle start time.
    const float startTime = effect.scalar("particleStartTime", -1.0F);
    if (startTime < 0.0F || chartTime >= startTime) {
      const float bandWidth = effect.track("bandWidth", chartTime);
      const float delta = messageParticleLastSec < 0.0
          ? 0.0F
          : std::clamp(static_cast<float>(input.chartTimeSec - messageParticleLastSec), 0.0F,
                0.1F);
      messageParticleLastSec = input.chartTimeSec;
      messageParticles.update(delta, bandWidth);

      const float stageHeight = layout.stage.height;
      const float stageWidth = layout.stage.width;
      batch.begin();
      for (int index = 0; index < messageParticles.count(); ++index) {
        const auto& particle = messageParticles.at(index);
        if (particle.age >= game::MessageParticleField::LIFE_SEC) continue;
        const float alpha = game::MessageParticleField::bandAlpha(particle, bandWidth);
        if (!(alpha > 0.002F)) continue;
        const float size = messageParticles.sizeAt(particle) * stageHeight;
        if (!(size > 0.5F)) continue;
        // World x spans the full play-field width; uv y maps onto the stage height.
        const float x = layout.stage.x + stageWidth * 0.5F +
            particle.worldX / game::MessageParticleField::UNITS * stageHeight;
        const float y = layout.stage.y + particle.y * stageHeight;
        batch.addQuad(x, y, size, size, 0.0F, 1.0F, 1.0F, 1.0F, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
      }
      batch.flush(layout.projection(), textures.white(), nullptr);
    } else {
      messageParticleLastSec = -1.0;
      messageParticles.reset();
    }
    // The kaleidoscope layer covers the stage rectangle and is opaque black apart from
    // the pattern it mixes in.
    const float bgAlpha = effect.track("bgAlpha", chartTime);
    if (bgAlpha > 0.001F && kaleido.ready()) {
      if (kaleidoNormal == nullptr || kaleidoColour == nullptr) {
        kaleidoNormal = textures.loadAssetFlipped("official_textures/KaleidoNormal.png");
        kaleidoColour = textures.loadAssetFlipped("official_textures/KaleidoCol.png");
        // The pattern's polar coordinates span past the texture edge, so both maps mirror.
        if (kaleidoNormal != nullptr) kaleidoNormal->setWrapRepeat(true, true);
        if (kaleidoColour != nullptr) kaleidoColour->setWrapRepeat(true, true);
      }
      if (kaleidoNormal != nullptr && kaleidoColour != nullptr) {
        const auto viewportX = static_cast<int>(std::lround(layout.stage.x));
        const auto viewportY = static_cast<int>(
            std::lround(static_cast<float>(height) - (layout.stage.y + layout.stage.height)));
        const int viewportWidth = std::max(1, static_cast<int>(std::lround(layout.stage.width)));
        const int viewportHeight = std::max(1, static_cast<int>(std::lround(layout.stage.height)));
        kaleido.draw(*kaleidoNormal, *kaleidoColour, chartTime,
            effect.track("kaleidoMix", chartTime), std::min(1.0F, bgAlpha), viewportX,
            viewportY, viewportWidth, viewportHeight);
      }
    }
    return;
  }

  if (name != "DesultorySignalsLevelEffects" || !dsGrid.ready()) return;
  const float opacity = effect.track("gridOpacity", chartTime);
  if (!(opacity > 0.001F)) return;

  if (dsGridTexture == nullptr) {
    dsGridTexture = textures.loadAssetFlipped("official_textures/DsGrid.png");
    if (dsGridTexture != nullptr) dsGridTexture->setWrapRepeat(true, true);
  }
  if (dsGridTexture == nullptr) return;

  // The pulse freezes at the level's freeze moment.
  const float freezeTime = effect.scalar("freezeTime", 3.4e38F);
  const bool frozen = chartTime >= freezeTime;
  dsGrid.draw(*dsGridTexture, chartTime, opacity, frozen, width, height);
}

gl::Texture* GameRenderer::Impl::applyStageScopedChain(game::Chart& chart, gl::Texture* input,
    const math::Rect& stageRect, double chartTimeSec) {
  std::vector<game::PrprEffect> scoped;
  for (const auto& effect : chart.prprEffects) {
    if (!effect.global) scoped.push_back(effect);
  }
  if (scoped.empty() || input == nullptr) return input;
  // The chain scissor takes the play field in screen pixels with a bottom-left origin.
  fx::StageRect rect;
  rect.left = stageRect.x;
  rect.top = stageRect.y;
  rect.width = stageRect.width;
  rect.height = stageRect.height;
  return chain.applyStageViewport(scoped, input, pingFbo, pongFbo, rect, chartTimeSec);
}

std::vector<game::PrprEffect> GameRenderer::Impl::collectGlobalEffects(
    const game::Chart& chart) const {
  std::vector<game::PrprEffect> globals;
  for (const auto& effect : chart.prprEffects) {
    if (effect.global) globals.push_back(effect);
  }
  return globals;
}

bool GameRenderer::Impl::messageFullscreenStage(const RenderFrameInput& input,
    const game::Chart* chart) const noexcept {
  if (chart == nullptr || chart->levelEffects == nullptr) return false;
  const auto& effect = *chart->levelEffects;
  if (effect.effectName() != "MessageLevelEffects") return false;
  // A negative hide time means the level never expands; otherwise it expands from that time
  // on, which is also where its backdrop stops being visible under the opaque mask.
  const float hideTime = effect.scalar("rawImageHideTime", -1.0F);
  if (hideTime < 0.0F) return true;
  return static_cast<float>(input.chartTimeSec) >= hideTime;
}

void GameRenderer::Impl::drawScene(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, unsigned int targetFbo, bool applyEffects) {
  // The play field keeps the reference aspect rule: a positive target aspect is honoured,
  // otherwise the official 16:9 stage is fitted into the viewport and clamped so it is
  // never wider than the window. The chart-reveal camera always uses the official stage.
  // The Message level is the one exception: once its canvas covers the viewport the stage
  // stops being letterboxed, so the side bands and their blurred artwork go away.
  const bool fullscreenStage = messageFullscreenStage(input, chart);
  const float targetAspect = input.chartReveal
      ? 16.0F / 9.0F
      : (input.aspectRatio > 0.0F ? input.aspectRatio : 16.0F / 9.0F);
  scene::StageLayout layout = scene::computeStageLayout(
      static_cast<float>(width), static_cast<float>(height), targetAspect, fullscreenStage);
  // Demo mode reflects the play field about the stage centre. The reflection lives in the
  // stage projection, so notes, judge lines and hit effects all move with it while the HUD
  // (projected with `projection()`) keeps its unmirrored layout.
  layout.mirror = input.mirrorX;

  // Chart-reveal camera: the play field shrinks towards the stage centre at the start of
  // the music and again before the exit, and hidden chart objects become visible while the
  // camera is settled (reference CHART_REVEAL_* curves).
  RenderFrameInput revealInput = input;
  if (input.chartReveal && !input.transition.inIntro && !input.transition.inOutro &&
      input.chartTimeSec >= 0.0) {
    const float total = static_cast<float>(input.hud.totalDurationSec);
    const float playhead = static_cast<float>(input.playheadSec);
    constexpr float kRevealTransitionSec = 1.0F;
    constexpr float kRevealTargetScale = 0.30F;
    constexpr float kRevealHiddenAlpha = 0.20F;
    const auto smooth = [](float value) {
      value = std::clamp(value, 0.0F, 1.0F);
      return value * value * (3.0F - 2.0F * value);
    };
    const bool preOutro = total > 0.0F && playhead >= std::max(0.0F, total - kRevealTransitionSec);
    revealInput.revealCameraActive = true;
    if (preOutro) {
      const float start = std::max(0.0F, total - kRevealTransitionSec);
      const float elapsed = (playhead - start) / kRevealTransitionSec;
      revealInput.revealScale = kRevealTargetScale +
          (1.0F - kRevealTargetScale) * smooth(elapsed);
      revealInput.revealHiddenObjects = false;
    } else {
      const float elapsed = playhead / kRevealTransitionSec;
      const float settled = smooth(elapsed);
      revealInput.revealScale = 1.0F + (kRevealTargetScale - 1.0F) * settled;
      revealInput.revealHiddenObjects = true;
      revealInput.revealHiddenFloorAlpha = kRevealHiddenAlpha * settled;
    }
    revealInput.revealScale = std::clamp(revealInput.revealScale, kRevealTargetScale, 1.0F);
  }
  // The revealed play field uses a shrunk stage rect; the HUD keeps the normal layout.
  scene::StageLayout sceneLayout = layout;
  if (revealInput.revealCameraActive && revealInput.revealScale < 0.999F) {
    const float shownWidth = layout.stage.width * revealInput.revealScale;
    const float shownHeight = layout.stage.height * revealInput.revealScale;
    sceneLayout.stage = math::Rect{layout.stage.x + (layout.stage.width - shownWidth) * 0.5F,
        layout.stage.y + (layout.stage.height - shownHeight) * 0.5F, shownWidth, shownHeight};
  }

  // The screen-space composite resamples the finished frame inside a red zone, so it
  // needs that frame as a texture while it writes into the caller's target. Any frame
  // with a zone on screen therefore takes the offscreen route, with or without a
  // storyboard chain. Zones are culled on the cached live-window query: the pass is a
  // full-screen resample and must not run on frames that show none.
  const bool anyZoneVisible = chart != nullptr && !chart->blockAreaList.empty()
      && !input.transition.inIntro && input.chartTimeSec >= 0.0
      && chart->hasLiveBlock(input.chartTimeSec);
  const bool useBlockComposite = anyZoneVisible && blockComposite.ready() &&
      blockComposite.allocated() && sceneFbo.valid();

  // A chart with storyboard effects renders into an offscreen target first so the
  // shader chain can process the whole play field. Zone composite precedes those effects;
  // the low-resolution fallback and direct scene follow, with antialias around the paths
  // that resolve through FXAA.
  const bool useEffects = applyEffects && !input.disableChartShader && chart != nullptr &&
      !chart->prprEffects.empty() && sceneFbo.valid();
  const bool useLowRes = input.lowResolutionMode && !useEffects && !useBlockComposite &&
      halfResFbo.valid();
  // The pause overlay is resolved through the antialias pass while it is up.
  const bool pauseWantsAa = input.pause.mode != PauseMode::None;
  const bool useAa = input.antialias && (pauseWantsAa || useEffects || useLowRes)
      && aaFbo.valid();
  const unsigned int sceneTarget = useAa ? aaFbo.fbo() : targetFbo;
  if (useEffects || useBlockComposite) {
    sceneFbo.bind();
    glViewport(0, 0, width, height);
  } else if (useLowRes) {
    halfResFbo.bind();
    glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
  } else {
    // The scene draws into the caller's target, so its binding and viewport are set here
    // rather than assumed: a previous frame may have left another target bound.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
    glViewport(0, 0, width, height);
  }

  clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
  bindDefaultBlend();
  // Each pass is followed by an optional error check: with diagnostics off this is a
  // no-op, and with them on the log names the first pass that failed on the device.
  gl::logErrors("scene.begin");

  // The opening rides the decorative line's growth from INTRO_START_BRIGHTNESS down to
  // the gameplay transmission; the exit walks everything to the result page's level.
  // The play field is only shown once the opening has begun, so the intro alpha ramp is
  // always live: line scale zero means the very first frame of the opening.
  const bool introStarted = true;
  const float introMix = input.transition.inIntro
      ? scene::introDimProgress(input.transition.introLineScale,
            chart != nullptr && chart->formatVersion > 0)
      : 1.0F;
  const float artworkAlpha = scene::stageTextureAlpha(introStarted, introMix, introMix,
      input.backgroundDim, input.transition.inOutro, input.transition.outroElapsedSec);
  const float dimAlpha = scene::backgroundDimAlpha(introStarted, introMix, input.backgroundDim,
      input.transition.inOutro, input.transition.outroElapsedSec);
  const float sideArtworkAlpha = scene::sideArtworkAlpha(introStarted, introMix);
  const float sideDimAlpha = scene::sideDimAlpha(introStarted, introMix, input.transition.inOutro,
      input.transition.outroElapsedSec);
  const bool opening = !introStarted || input.transition.introLineScale < 0.999F;
  // A level whose canvas covers more than the play field has to have the fixed side columns
  // painted on top of it, so the backdrop leaves them for after the level's own objects.
  const bool deferSideColumns = sceneLayout.hasSideBlanks && dsGridCanvasVisible(revealInput, chart);
  drawBackgroundPass(sceneLayout, artworkAlpha, dimAlpha, sideArtworkAlpha, sideDimAlpha, opening,
      deferSideColumns);
  gl::logErrors("scene.background");

  // A level effect may draw its own objects between the backdrop and the play field.
  drawLevelEffectObjects(chart, revealInput, sceneLayout);
  gl::logErrors("scene.levelObjects");
  if (deferSideColumns) {
    // The same blurred copy the backdrop uses, so the band keeps matching the play field.
    drawSideColumns(sceneLayout, backgroundBlur.ensure(width, height, background),
        sideArtworkAlpha, sideDimAlpha);
    gl::logErrors("scene.sideColumns");
  }

  if (chart != nullptr) {
    lineFrames.clear();
    math::Rect worldBounds{layout.stage.x, layout.stage.y, layout.stage.width,
        layout.stage.height};
    scene::LinePassParams lineParams;
    lineParams.chartTimeSec = input.chartTimeSec;
    lineParams.officialFormat = chart->formatVersion > 0;
    const bool blockNotes = input.transition.inIntro || input.chartTimeSec < 0.0;
    scene::evaluateLines(*chart, sceneLayout, input.chartTimeSec, input.scrollSpeed, blockNotes, revealInput.revealHiddenObjects,
        1.0F, worldBounds, layout.stage.width * 2.0F, lineParams, lineFrames);
    if (lineDrawOrder.size() != chart->judgeLineList.size()) {
      scene::buildLineDrawOrder(*chart, lineDrawOrder);
    }
    // Judge lines and notes are authored in stage space and may extend past the play field:
    // a long line runs off the edge by design. The play field is what they are confined to,
    // so the submission is clipped to the stage rect. The scissor is armed only for the
    // batches that draw those objects, and cleared before the passes that follow, so the
    // level-effect chain still covers the whole frame. The reveal treatment lifts it because
    // it deliberately shows the field against the surrounding backdrop.
    const bool confineToStage = !revealInput.revealCameraActive;
    if (confineToStage) {
      batch.setScissor(true, layout.stage.x, layout.stage.y,
          layout.stage.x + layout.stage.width, layout.stage.y + layout.stage.height);
    }
    drawLinePass(revealInput, chart, judgement, sceneLayout);
    gl::logErrors("scene.lines");
    if (!blockNotes) drawNotePasses(sceneLayout, revealInput, chart);
    gl::logErrors("scene.notes");
    // The chart-debug overlay marks where each judge line and note actually is. It runs on the
    // same evaluated frames the passes above used, so a marker and the object it describes
    // always agree.
    if (input.showChartDebug && !blockNotes) {
      scene::ChartDebugOverlay::Style debugStyle;
      scene::ChartDebugOverlay::draw(batch, sceneLayout, *chart, lineFrames, lineDrawOrder,
          debugStyle, textures.white(), sceneLayout.stageProjection());
      gl::logErrors("scene.chartDebug");
    }
    // Red zones sit over the play field but under the hit effects.
    if (!chart->blockAreaList.empty() && !input.transition.inIntro &&
        input.chartTimeSec >= 0.0) {
      // The one-shot fade advances on the visual clock: live play measures real time, while an
      // offscreen run measures the same elapsed time by frame number.
      const double nowSec = input.visualTimeSec;
      const float deltaSec = blockUpdateLastSec == 0.0
          ? 0.0F
          : std::clamp(static_cast<float>(nowSec - blockUpdateLastSec), 0.0F, 0.1F);
      blockUpdateLastSec = nowSec;
      if (useBlockComposite) {
        // The composite draws the zones from mask geometry, so only the runtime state is
        // advanced here; the masks are rasterised once the frame is finished.
        blockOverlay.updateAll(chart->blockAreaList, static_cast<float>(input.chartTimeSec),
            deltaSec, layout.stage.x, layout.stage.y, layout.stage.width, layout.stage.height);
      } else {
        batch.begin();
        blockOverlay.drawAll(chart->blockAreaList, static_cast<float>(input.chartTimeSec), deltaSec,
            layout.stage.x, layout.stage.y, layout.stage.width, layout.stage.height, batch,
            layout.stageProjection(), textures.white());
        // The touch feedback belongs to the same fallback as the silhouettes it sits on. The
        // composite path lights the zone through its own shader, from the finger positions it
        // is handed, so drawing the pulse there as well doubles the light -- and the pulse is
        // sized from the zone, which for a tall zone is a screen-sized quad rather than a glow
        // around the finger.
        drawBlockedTouchPulses(input, layout, chart);
      }
      gl::logErrors("scene.blocks");
    }
    if (!input.transition.inOutro && input.showHitParticles) {
      drawHitEffects(sceneLayout, revealInput, chart);
    }
    gl::logErrors("scene.hitEffects");
    if (confineToStage) batch.setScissor(false, 0.0F, 0.0F, 0.0F, 0.0F);
  }

  // The chart-reveal ring covers whatever the scene drew outside the shrunken play field.
  if (revealInput.revealCameraActive && revealInput.revealScale < 0.999F) {
    drawRevealPanels(sceneLayout, layout, sideDimAlpha);
    gl::logErrors("scene.revealPanels");
  }

  // The Ametrine failure vignette closes in over the finished play field, before the HUD
  // and before the storyboard chain, so the level's own post passes still process it.
  // Every other level leaves the ramp at zero, which costs one comparison per frame.
  float ametrineDarkness = 0.0F;
  if (chart != nullptr && chart->levelEffects != nullptr) {
    ametrineDarkness = chart->levelEffects->ametrineVignette();
    if (ametrineDarkness > 0.001F && !input.transition.inIntro) {
      ametrineVignette.draw(ametrineDarkness);
      gl::logErrors("scene.ametrineVignette");
    }
  }

  // Tracks whether the HUD already became part of the frame the composite resamples, so
  // it is not drawn a second time on top of the torn picture.
  bool hudInsideScene = false;
  // A level effect may take the HUD away entirely (Hate, Desultory Signals and Message do).
  // The exit animation still shows it, because the HUD's own outro has to play out.
  const bool hudVisible = input.hud.visible && chart != nullptr &&
      !(chart->levelEffects != nullptr && chart->levelEffects->hideHud() &&
          !input.transition.inOutro);
  if (useBlockComposite) {
    const bool hasGlobal = useEffects && std::any_of(chart->prprEffects.begin(),
        chart->prprEffects.end(), [](const game::PrprEffect& effect) { return effect.global; });
    // Without a shader chain the HUD is part of the frame that the composite resamples.
    // With one, it is added only for the global passes, after the zone and stage effects.
    if (!useEffects && hudVisible) {
      drawHudPass(input, layout, chart, judgement);
      hudInsideScene = true;
      gl::logErrors("scene.hud");
    }
    // Zone masks, coverage/edge/glow pre-passes, then replace the frame inside the
    // zones with the displaced resample. The composite restores the target and viewport
    // it found, so no binding fix-up is needed afterwards. The staging vectors are reused
    // between frames so a zone frame does not allocate.
    auto& maskA = maskQuadsA;
    auto& maskB = maskQuadsB;
    maskA.clear();
    maskB.clear();
    blockOverlay.collectMaskQuads(chart->blockAreaList, static_cast<float>(input.chartTimeSec),
        maskA, maskB);
    // Held points write the composite's hover channel: the touched spot is rasterised into the
    // second mask's alpha with the shipped sprite, and the composite reads it back as a soft
    // union. Without this quads list the hover term can never be non-zero, however many fingers
    // the shader is told about.
    {
      hoverPoints.clear();
      const float hoverSize = kHoverSpriteHeightFraction *
          std::max(1.0F, static_cast<float>(height));
      for (int index = 0; index < std::clamp(input.touches.count, 0, 10); ++index) {
        const auto& point = input.touches.points[index];
        if (!(point.scale > 0.0F)) continue;
        fx::BlockOverlay::HoverPoint hover;
        hover.centerX = point.x;
        hover.centerY = point.y;
        hover.radius = hoverSize * point.scale * 0.5F;
        hoverPoints.push_back(hover);
      }
      if (!hoverPoints.empty()) fx::BlockOverlay::appendHoverQuads(hoverPoints, maskB);
    }
    blockComposite.rasteriseMasks(maskA, maskB);
    const unsigned int blockTarget = useEffects ? blockResultFbo.fbo() : sceneTarget;
    glBindFramebuffer(GL_FRAMEBUFFER, blockTarget);
    glViewport(0, 0, width, height);
    blockComposite.setStageRect(sceneLayout.stage.x, sceneLayout.stage.y,
        sceneLayout.stage.right(), sceneLayout.stage.bottom());
    {
      // Held points reach the composite in the shader's own space: a fraction of the width,
      // and a fraction of the height measured from the bottom.
      fx::BlockCompositePass::TouchState touch;
      const float safeWidth = std::max(1.0F, static_cast<float>(width));
      const float safeHeight = std::max(1.0F, static_cast<float>(height));
      touch.count = std::clamp(input.touches.count, 0, 10);
      // The shader compares against `uv * screenSize / screenSize.y`, so its space is a height
      // of one with x running to the aspect ratio. Sending plain 0..1 fractions would put the
      // point at the wrong place on any stage that is not square.
      for (int index = 0; index < touch.count; ++index) {
        touch.points[index].x = input.touches.points[index].x / safeHeight;
        touch.points[index].y = (safeHeight - input.touches.points[index].y) / safeHeight;
      }
      // The shine runs on wall time, so a paused chart does not freeze it.
      touch.realTimeSec = input.visualTimeSec;
      blockComposite.draw(sceneFbo.color(), static_cast<float>(input.chartTimeSec), touch);
    }
    bindDefaultBlend();
    gl::logErrors("scene.blockComposite");
    if (useEffects) {
      // The composite owns a separate target: sampling the scene and writing it back into the
      // same texture would be a framebuffer feedback loop. Shader stages consume that result.
      gl::Texture* result = applyStageScopedChain(*chart, &blockResultFbo.color(),
          sceneLayout.stage, input.chartTimeSec);
      if (hasGlobal) {
        sceneFbo.bind();
        glViewport(0, 0, width, height);
        if (result != nullptr && result->id() != sceneFbo.color().id()) {
          glDisable(GL_BLEND);
          fx::BlitPass::instance().ensure();
          fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
          bindDefaultBlend();
        }
        if (hudVisible) {
          drawHudPass(input, layout, chart, judgement);
          hudInsideScene = true;
        }
        result = chain.apply(collectGlobalEffects(*chart), &sceneFbo.color(), pingFbo, pongFbo,
            input.chartTimeSec);
        gl::logErrors("scene.effectChainGlobal");
      }
      glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
      glViewport(0, 0, width, height);
      clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
      if (result != nullptr) {
        glDisable(GL_BLEND);
        fx::BlitPass::instance().ensure();
        fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
        bindDefaultBlend();
      }
      gl::logErrors("scene.effectChain");
    }
  } else if (useEffects) {
    // The chain runs in two segments, in this order:
    //   1. stage-scoped effects over the play field only, so the surround keeps the scene,
    //   2. global effects over the whole frame, with the HUD composited in first because a
    //      global pass is meant to affect the HUD too.
    // A chart with no global effect therefore leaves the HUD unshaded.
    const bool hasGlobal = std::any_of(chart->prprEffects.begin(), chart->prprEffects.end(),
        [](const game::PrprEffect& effect) { return effect.global; });
    gl::Texture* result = applyStageScopedChain(*chart, &sceneFbo.color(),
        sceneLayout.stage, input.chartTimeSec);
    bool hudShaded = false;
    if (hasGlobal) {
      // The HUD belongs to the frame a global pass processes; draw it into the scene before
      // handing the frame to the chain.
      glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo.fbo());
      glViewport(0, 0, width, height);
      if (result != nullptr && result->id() != sceneFbo.color().id()) {
        glDisable(GL_BLEND);
        fx::BlitPass::instance().ensure();
        fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
        bindDefaultBlend();
      }
      if (hudVisible) drawHudPass(input, layout, chart, judgement);
      result = chain.apply(collectGlobalEffects(*chart), &sceneFbo.color(), pingFbo, pongFbo,
          input.chartTimeSec);
      hudShaded = hudVisible;
      gl::logErrors("scene.effectChainGlobal");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
    glViewport(0, 0, width, height);
    clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
    if (result != nullptr) {
      glDisable(GL_BLEND);
      fx::BlitPass::instance().ensure();
      fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
      bindDefaultBlend();
    }
    hudInsideScene = hudShaded;
    gl::logErrors("scene.effectChain");
  }

  // A global pass receives the HUD with the scene. Otherwise it is drawn after stage-scoped
  // effects; without a shader chain the zone composite already resampled it. The low-resolution
  // fallback renders the HUD together with the scene before upscaling the finished frame.
  if (hudVisible && !hudInsideScene) {
    drawHudPass(input, layout, chart, judgement);
  }
  gl::logErrors("scene.hud");
  if (useLowRes) {
    glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
    glViewport(0, 0, width, height);
    clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
    glDisable(GL_BLEND);
    fx::BlitPass::instance().ensure();
    fx::BlitPass::instance().draw(halfResFbo.color(), 1.0F, 1.0F, 1.0F, 1.0F, false);
    bindDefaultBlend();
    gl::logErrors("scene.lowResUpscale");
  }

  // Pause and resume draw the frozen play field behind the menu. The scene is re-rendered
  // into the offscreen target so the backdrop can be blurred, then the graded result is
  // composited over the presented frame.
  if (input.pause.mode != PauseMode::None && chart != nullptr && pauseBackdrop.ready() &&
      sceneFbo.valid()) {
    float alpha = 0.0F;
    float brightness = scene::PAUSE_BACKDROP_BRIGHTNESS;
    float flatten = scene::PAUSE_BACKDROP_FLATTEN;
    if (input.pause.mode == PauseMode::Menu) {
      alpha = scene::pauseBackdropIntroAlpha(input.pause.elapsedSec);
    } else if (input.pause.mode == PauseMode::Countdown) {
      // The resume countdown retreats the paused backdrop along its own curves.
      alpha = scene::resumeBackdropAlpha(input.pause.elapsedSec);
      brightness = scene::resumeBackdropBrightness(input.pause.elapsedSec);
      flatten = scene::resumeBackdropFlatten(input.pause.elapsedSec);
    } else {
      alpha = scene::pauseBackdropIntroAlpha(input.pause.elapsedSec);
    }
    if (alpha > 0.001F) {
      // Re-render the frozen play field into the offscreen capture target. The HUD is
      // excluded from that frame and composited separately as one soft blurred layer,
      // so the pause picture never shows a crisp HUD over the graded backdrop.
      if (pauseBlurA.width() != width || pauseBlurA.height() != height) {
        pauseBlurA.resize(width, height);
      }
      RenderFrameInput frozen = input;
      frozen.pause.mode = PauseMode::None;
      // The graded backdrop must not carry a HUD copy: the soft HUD is composited on
      // top instead, so the pause picture contains exactly one HUD layer.
      frozen.hud.visible = false;
      pauseBlurA.bind();
      glViewport(0, 0, width, height);
      drawScene(frozen, chart, judgement, pauseBlurA.fbo(), false);
      glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
      glViewport(0, 0, width, height);
      // The presented frame receives the graded result; the scalable-blur pyramid lives
      // inside the pass so the paused scene goes through the reference blur chain.
      pauseBackdrop.draw(pauseBlurA.color(), sceneTarget, alpha,
          scene::PAUSE_BACKDROP_VIBRANCY, brightness, flatten);
      bindDefaultBlend();
      // The HUD stays visible through the countdown too: the reference draws it from the
      // same HUD pass for every pause mode, so dropping it here made the whole interface
      // disappear between pressing Resume and the countdown finishing. It is still soft,
      // matching the graded backdrop rather than a sharp overlay.
      if (input.pause.mode != PauseMode::None && hudVisible && pauseHudBlur.begin(width, height)) {
        const float hudAspect = input.chartReveal
            ? 16.0F / 9.0F
            : (input.aspectRatio > 0.0F ? input.aspectRatio : 16.0F / 9.0F);
        const scene::StageLayout hudLayout = scene::computeStageLayout(
            static_cast<float>(pauseHudBlur.width()),
            static_cast<float>(pauseHudBlur.height()), hudAspect, false);
        RenderFrameInput dimmed = input;
        dimmed.hud.alpha = input.hud.alpha * scene::PAUSE_HUD_DIM;
        // begin() bound the reduced HUD target, so the HUD pass flushes into it directly.
        bindDefaultBlend();
        drawHudPass(dimmed, hudLayout, chart, judgement);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget);
        glViewport(0, 0, width, height);
        // The soft copy fades in together with the graded backdrop so the entry does not
        // show two HUD states at once.
        pauseHudBlur.composite(scene::PAUSE_HUD_SOFT_BRIGHTNESS * alpha);
        bindDefaultBlend();
      }
      gl::logErrors("scene.pauseBackdrop");
    }
  }

  // The antialias path renders the whole gameplay frame (including the pause backdrop)
  // into the full-resolution target, then resolves it with FXAA into the presented target.
  if (useAa) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
    fx::FxaaPass::instance().draw(aaFbo.color(), width, height);
    bindDefaultBlend();
    gl::logErrors("scene.antialias");
  }
}

void GameRenderer::resetHudDisplay() {
  impl_->hudDisplayScore = std::numeric_limits<double>::quiet_NaN();
  impl_->hudDisplayAccuracy = std::numeric_limits<double>::quiet_NaN();
  impl_->hudDisplayLastUpdateSec = -1.0;
  impl_->hudDisplayReset = true;
}

void GameRenderer::setSkinDirectory(const std::filesystem::path& skinDirectory) {
  if (impl_ == nullptr) return;
  impl_->skinDirectory = skinDirectory;
  // Re-resolve the material settings and reload the gameplay textures: the selected skin
  // may replace any texture, and loading from the defaults also clears the previous skin's
  // values, so an empty path restores the built-in look.
  impl_->loadGameplayTextures();
}

void GameRenderer::setBackgroundImage(const std::filesystem::path& image) {
  if (impl_ == nullptr || !impl_->initialized) return;
  gl::Texture* loaded =
      image.empty() ? nullptr : impl_->textures.loadFile(image);
  impl_->background = loaded != nullptr ? loaded : impl_->defaultBackground;
  // The backdrop blur is built from the artwork, so it has to be rebuilt for the new image.
  impl_->backgroundBlur.invalidate();
}

void GameRenderer::render(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement) {
  if (!impl_->initialized) return;
  impl_->disableChartShaderSetting = input.disableChartShader;
  impl_->prepareForChart(chart);
  // Target 0 is the default framebuffer the window presents from.
  impl_->drawScene(input, chart, judgement, 0);
}
void GameRenderer::render(double chartTimeSeconds, game::Chart* chart,
    const game::JudgementEngine* judgement) {
  RenderFrameInput input;
  input.chartTimeSec = chartTimeSeconds;
  input.playheadSec = chartTimeSeconds;
  input.visualTimeSec = chartTimeSeconds;
  render(input, chart, judgement);
}

bool GameRenderer::renderOffscreen(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, const std::filesystem::path& outputPng, int frames,
    int width, int height) {
  if (!impl_->initialized || width <= 0 || height <= 0) return false;
  gl::Framebuffer target;
  if (!target.resize(width, height)) return false;

  const int previousWidth = impl_->width;
  const int previousHeight = impl_->height;
  impl_->width = width;
  impl_->height = height;
  // The effect chain needs the offscreen targets, which the interactive path allocates
  // in resize(); this path sets the size directly and must allocate them itself.
  impl_->sceneFbo.resize(width, height);
  impl_->blockResultFbo.resize(width, height);
  impl_->pingFbo.resize(width, height);
  impl_->pongFbo.resize(width, height);
  impl_->aaFbo.resize(width, height);
  impl_->halfResFbo.resize(std::max(1, width / 2), std::max(1, height / 2));
  impl_->blockComposite.resize(width, height);
  impl_->disableChartShaderSetting = input.disableChartShader;
  impl_->chain.setFrozenNoiseClock(true);
  impl_->prepareForChart(chart);

  const scene::StageLayout layout =
      scene::computeStageLayout(static_cast<float>(width), static_cast<float>(height), input.aspectRatio > 0.0F ? input.aspectRatio : 16.0F / 9.0F,
          false);
  // The opening rides the decorative line's growth from INTRO_START_BRIGHTNESS down to
  // the gameplay transmission; the exit walks everything to the result page's level.
  // The play field is only shown once the opening has begun, so the intro alpha ramp is
  // always live: line scale zero means the very first frame of the opening.
  const bool introStarted = true;
  const float introMix = input.transition.inIntro
      ? scene::introDimProgress(input.transition.introLineScale, chart != nullptr &&
            chart->formatVersion > 0)
      : 1.0F;
  const float artworkAlpha = scene::stageTextureAlpha(introStarted, introMix, introMix,
      input.backgroundDim, input.transition.inOutro, input.transition.outroElapsedSec);
  const float dimAlpha = scene::backgroundDimAlpha(introStarted, introMix, input.backgroundDim,
      input.transition.inOutro, input.transition.outroElapsedSec);

  for (int frame = 0; frame < std::max(1, frames); ++frame) {
    target.bind();
    glViewport(0, 0, width, height);
    impl_->drawScene(input, chart, judgement, target.fbo());
  }

  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Render targets are stored bottom-up, so the rows are flipped before writing.
  const auto rowBytes = static_cast<std::size_t>(width) * 4;
  std::vector<std::uint8_t> flipped(pixels.size());
  for (int row = 0; row < height; ++row) {
    std::copy_n(pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes, rowBytes,
        flipped.data() + static_cast<std::size_t>(row) * rowBytes);
  }

  impl_->width = previousWidth;
  impl_->height = previousHeight;
  impl_->chain.setFrozenNoiseClock(false);

  const auto path = outputPng.string();
  return stbi_write_png(path.c_str(), width, height, 4, flipped.data(),
             static_cast<int>(rowBytes)) != 0;
}

bool GameRenderer::beginCapture(const RenderFrameInput& input, game::Chart* chart, int width,
    int height) {
  if (!impl_->initialized || width <= 0 || height <= 0) return false;
  endCapture();
  if (!impl_->captureTarget.resize(width, height)) return false;
  // The staging buffers are a latency optimisation only: a driver without pixel buffer support
  // still produces the same frames through the direct read.
  (void)impl_->captureReadback.resize(width, height);
  // The planar conversion is a faster path, not a requirement: without it the recording still runs
  // through the coloured readback.
  (void)impl_->captureYuv.ensure(width, height);
  impl_->captureWidth = width;
  impl_->captureHeight = height;
  impl_->capturePreviousWidth = impl_->width;
  impl_->capturePreviousHeight = impl_->height;
  impl_->capturing = true;
  impl_->chain.setFrozenNoiseClock(true);
  applyCaptureSize(input, chart);
  return true;
}

void GameRenderer::applyCaptureSize(const RenderFrameInput& input, game::Chart* chart) {
  const int width = impl_->captureWidth;
  const int height = impl_->captureHeight;
  impl_->width = width;
  impl_->height = height;
  // The effect chain reads these targets and the interactive path allocates them in resize();
  // this path sets the size directly and has to allocate them itself.
  impl_->sceneFbo.resize(width, height);
  impl_->blockResultFbo.resize(width, height);
  impl_->pingFbo.resize(width, height);
  impl_->pongFbo.resize(width, height);
  impl_->aaFbo.resize(width, height);
  impl_->halfResFbo.resize(std::max(1, width / 2), std::max(1, height / 2));
  impl_->blockComposite.resize(width, height);
  impl_->disableChartShaderSetting = input.disableChartShader;
  impl_->prepareForChart(chart);
}

bool GameRenderer::captureFrame(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPixels) {
  if (!impl_->capturing || !impl_->captureTarget.valid()) return false;
  const int width = impl_->captureWidth;
  const int height = impl_->captureHeight;
  // Every frame restates the capture size: anything that touched the renderer's size between
  // frames would otherwise lay the scene out for a different viewport than the one read back.
  impl_->width = width;
  impl_->height = height;
  impl_->captureTarget.bind();
  glViewport(0, 0, width, height);
  impl_->drawScene(input, chart, judgement, impl_->captureTarget.fbo());
  glBindFramebuffer(GL_FRAMEBUFFER, impl_->captureTarget.fbo());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  // Readback rows arrive bottom-up and every consumer wants the first row to be the top one.
  const auto flipRows = [&]() {
    const auto rowBytes = static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> row(rowBytes);
    for (int top = 0, bottom = height - 1; top < bottom; ++top, --bottom) {
      auto* upper = outPixels.data() + static_cast<std::size_t>(top) * rowBytes;
      auto* lower = outPixels.data() + static_cast<std::size_t>(bottom) * rowBytes;
      std::copy_n(upper, rowBytes, row.data());
      std::copy_n(lower, rowBytes, upper);
      std::copy_n(row.data(), rowBytes, lower);
    }
  };
  if (impl_->captureReadback.valid()) {
    // The current frame is queued for transfer first, then the previous one is collected. On the
    // first call nothing is pending yet, so that frame is read directly -- a single synchronous
    // read for the whole recording, instead of one per frame.
    impl_->captureReadback.request(width, height);
    if (impl_->captureReadback.take(outPixels)) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return true;
    }
    outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    flipRows();
    return true;
  }
  outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  flipRows();
  return true;
}


bool GameRenderer::captureFrameYuv(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPlanes) {
  if (!impl_->capturing || !impl_->captureTarget.valid()) return false;
  if (!impl_->captureYuv.valid()) return false;
  const int width = impl_->captureWidth;
  const int height = impl_->captureHeight;
  impl_->width = width;
  impl_->height = height;
  impl_->captureTarget.bind();
  glViewport(0, 0, width, height);
  impl_->drawScene(input, chart, judgement, impl_->captureTarget.fbo());
  // The scene stays in its render target and is converted straight out of it, so the frame is
  // never read back at full colour depth.
  const bool converted = impl_->captureYuv.convert(impl_->captureTarget.color(), outPlanes);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return converted;
}

bool GameRenderer::captureFrameYuvAsync(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPlanes,
    bool& frameReady) {
  frameReady = false;
  if (!impl_->capturing || !impl_->captureTarget.valid()) return false;
  if (!impl_->captureYuv.valid()) return false;
  const int width = impl_->captureWidth;
  const int height = impl_->captureHeight;
  impl_->width = width;
  impl_->height = height;
  impl_->captureTarget.bind();
  glViewport(0, 0, width, height);
  impl_->drawScene(input, chart, judgement, impl_->captureTarget.fbo());
  const bool converted =
      impl_->captureYuv.convertAsync(impl_->captureTarget.color(), outPlanes, frameReady);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return converted;
}

bool GameRenderer::takePendingCaptureFrame(std::vector<std::uint8_t>& outPlanes) {
  if (!impl_->capturing || !impl_->captureYuv.valid()) return false;
  if (!impl_->captureYuv.hasPending()) return false;
  const bool collected = impl_->captureYuv.takePending(outPlanes);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return collected;
}

bool GameRenderer::hasPendingCaptureFrame() const noexcept {
  return impl_->capturing && impl_->captureYuv.valid() && impl_->captureYuv.hasPending();
}

void GameRenderer::endCapture() noexcept {
  if (!impl_->capturing) return;
  impl_->capturing = false;
  impl_->chain.setFrozenNoiseClock(false);
  impl_->captureTarget.reset();
  impl_->captureYuv.release();
  impl_->width = impl_->capturePreviousWidth;
  impl_->height = impl_->capturePreviousHeight;
  impl_->sceneFbo.resize(impl_->width, impl_->height);
  impl_->blockResultFbo.resize(impl_->width, impl_->height);
  impl_->pingFbo.resize(impl_->width, impl_->height);
  impl_->pongFbo.resize(impl_->width, impl_->height);
  impl_->aaFbo.resize(impl_->width, impl_->height);
  impl_->halfResFbo.resize(std::max(1, impl_->width / 2), std::max(1, impl_->height / 2));
  impl_->blockComposite.resize(impl_->width, impl_->height);
  glViewport(0, 0, impl_->width, impl_->height);
}

int GameRenderer::captureWidth() const noexcept { return impl_->captureWidth; }

int GameRenderer::captureHeight() const noexcept { return impl_->captureHeight; }

} // namespace rechigros::render
