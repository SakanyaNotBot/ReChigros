#include "rechigros/render/GameRenderer.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/MessageParticleField.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/game/NoteGeometry.hpp"
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
#include "rechigros/render/fx/RpeNoisePass.hpp"
#include "fx/RpeNoiseMaterial.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/HudPass.hpp"
#include "rechigros/render/scene/LinePass.hpp"
#include "rechigros/render/scene/ChartDebugOverlay.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/scene/RevealCamera.hpp"
#include "rechigros/render/scene/RpeCameraProjection.hpp"
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

constexpr float kNoteWidthBaseRatio = 0.1234375F;

// Displays narrower than 16:9 shrink the notes; wider displays are untouched.
float noteAspectFactor(float stageWidth, float stageHeight) {
  constexpr float kReferenceAspect = 16.0F / 9.0F;
  const float aspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : kReferenceAspect;
  return aspect < kReferenceAspect ? aspect / kReferenceAspect : 1.0F;
}

// Note width as a fraction of stage width.
float noteWidthRatio(float stageWidth, float stageHeight) {
  return kNoteWidthBaseRatio * noteAspectFactor(stageWidth, stageHeight);
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
void drawStoryboardText(batch::SpriteBatch& batch, text::TextRenderer& text,
    const scene::StageLayout& layout, const scene::LineFrame& frame, const scene::LineTint& tint,
    float alpha, const gl::Texture& primaryTexture) {
  if (frame.text.empty() || !(alpha > 0.0F)) return;
  std::vector<std::string> sourceLines;
  std::size_t begin = 0;
  for (;;) {
    const auto end = frame.text.find('\n', begin);
    auto line = frame.text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    sourceLines.push_back(std::move(line));
    if (end == std::string::npos) break;
    begin = end + 1;
  }

  struct TextLine {
    text::TextTexture* texture{};
  };
  std::vector<TextLine> lines;
  lines.reserve(sourceLines.size());
  const float fontSize = layout.stage.width * (52.0F / 1350.0F);
  if (frame.scaleX == 0.0F || frame.scaleY == 0.0F) return;
  const float contentScale = std::max({1.0F, std::abs(frame.scaleX), std::abs(frame.scaleY)});
  // Rasterise close to the on-screen size, but bucket the size and cap it: a storyboard line
  // can animate its scale from 1x to 40x in a fraction of a second, and a fresh 2000px strip
  // for every intermediate value fills the cache and reallocates GL textures every frame.
  // A small number of 64px buckets covers that range; the residual GPU magnification stays
  // mild for ordinary storyboard text and only grows for text that is already far off-screen.
  constexpr float kMaxStoryboardRasterSize = 320.0F;
  constexpr float kStoryboardRasterStep = 64.0F;
  const float desiredSize = std::min(fontSize * contentScale, kMaxStoryboardRasterSize);
  const float rasterFontSize = std::clamp(
      std::ceil(std::max(16.0F, desiredSize) / kStoryboardRasterStep) * kStoryboardRasterStep,
      16.0F, kMaxStoryboardRasterSize);
  const float rasterScale = rasterFontSize / fontSize;
  const float scaleX = frame.scaleX / rasterScale;
  const float scaleY = frame.scaleY / rasterScale;
  float ascent = 0.0F;
  float descent = 0.0F;
  float textWidth = 0.0F;
  for (const auto& source : sourceLines) {
    auto* texture = text.get(source, rasterFontSize);
    lines.push_back({texture});
    if (texture == nullptr) continue;
    ascent = std::max(ascent, texture->ascentPx > 0.0F ? texture->ascentPx : rasterFontSize * 0.8F);
    descent = std::max(descent, texture->descentPx > 0.0F ? texture->descentPx : rasterFontSize * 0.2F);
    textWidth = std::max(textWidth, texture->advanceWidth);
  }
  if (lines.empty() || textWidth <= 0.0F) return;
  const float lineHeight = std::max(1.0F, ascent + descent);
  const float textHeight = lineHeight * static_cast<float>(lines.size());
  const float radians = static_cast<float>(frame.rotateDeg) * 3.14159265358979323846F / 180.0F;
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto* glyphs = lines[index].texture;
    if (glyphs == nullptr || glyphs->width <= 0 || glyphs->height <= 0) continue;
    const float left = -textWidth * frame.anchorX;
    const float top = -textHeight * (1.0F - frame.anchorY);
    const float width = std::abs(static_cast<float>(glyphs->width) * scaleX);
    const float height = std::abs(static_cast<float>(glyphs->height) * scaleY);
    const float localX = (left + glyphs->inkLeft - 1.0F) * scaleX + width * 0.5F;
    const float localY = (top + ascent + static_cast<float>(index) * lineHeight +
        glyphs->inkTop - 1.0F) * scaleY + height * 0.5F;
    const float centerX = frame.x + localX * cosine - localY * sine;
    const float centerY = frame.y + localX * sine + localY * cosine;
    const float u0 = scaleX < 0.0F ? 1.0F : 0.0F;
    const float u1 = scaleX < 0.0F ? 0.0F : 1.0F;
    const float v0 = scaleY < 0.0F ? 1.0F : 0.0F;
    const float v1 = scaleY < 0.0F ? 0.0F : 1.0F;
    const bool queued = batch.addQuadTextured(centerX, centerY, width, height,
        frame.rotateDeg, tint.red, tint.green, tint.blue, alpha * tint.alpha, u0, v0, u1, v1,
        glyphs->texture);
    if (!queued) {
      // Every extra sampler slot is occupied. Submit the batch and retry this glyph in a
      // fresh one; plain lines and earlier text keep their relative order.
      batch.flush(layout.stageProjection(), primaryTexture, nullptr);
      batch.addQuadTextured(centerX, centerY, width, height, frame.rotateDeg, tint.red,
          tint.green, tint.blue, alpha * tint.alpha, u0, v0, u1, v1, glyphs->texture);
    }
  }
}

} // namespace

struct GameRenderer::Impl {
  ~Impl() { releaseResources(); }

  void releaseResources() noexcept {
    // Stop decode workers before the cache and its graphics objects go away.
    textures.awaitPrefetchAssets();
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
    rpeNoise.release();
    sceneFbo.reset();
    blockResultFbo.reset();
    pingFbo.reset();
    pongFbo.reset();
    aaFbo.reset();
    halfResFbo.reset();
    batch.release();
    text.shutdown();
    textReady = false;
    textures.clear();
    materialsLoaded = false;
    useChartBackground = false;
    backgroundPath.clear();
    loadedChart = nullptr;
    background = nullptr;
    defaultBackground = nullptr;
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
  // Two-stage startup: the artwork decode starts in begin() and the uploads happen in
  // finish(), so a loading screen can animate in between.
  bool beginDone{};
  bool textReady{};
  std::chrono::steady_clock::time_point initStart{};
  double initTextMs{};

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
  // RPE noise touch staging, reused every frame even while no noise layer is drawn.
  std::vector<fx::RpeNoiseTouch> rpeNoiseTouches;
  // Replaces the finished frame inside a red zone; the inline silhouettes are skipped
  // while it is active so the two paths never draw the same zone twice.
  fx::BlockCompositePass blockComposite;
  fx::BadEffectField badEffects;
  // Notes that already produced their bad effect, so each note produces one copy.
  std::vector<unsigned char> badSpawned;
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
  bool materialsLoaded{};
  bool useChartBackground{};
  std::filesystem::path backgroundPath;
  bool prefetchStarted{false};
  std::filesystem::path prefetchSkinKey_;
  double blockUpdateLastSec{};
  fx::DsGridPass dsGrid;
  fx::KaleidoBackdropPass kaleido;
  fx::RpeNoisePass rpeNoise;
  gl::Texture messageCubeTexture;
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
  gl::Texture* loadSkinAwareTextureDeferred(const char* fileName, const char* assetFallback);
  // Same fallback order, but decodes the image so it can be cropped before upload.
  asset::Image loadSkinAwareImage(
      const char* fileName, const char* assetFallback, std::string& cacheKey);
  gl::Texture* cropHoldBody(
      const char* fileName, const char* assetFallback, int tailSlice, int headSlice);
  // Re-slices the hold sheet from the current material settings. The pack and a skin may
  // change the slice sizes, so this runs again whenever those settings change.
  void refreshHoldSheet();
  void prepareForChart(game::Chart* chart);
  void prefetchLineTextures(const game::Chart& chart);
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
  void drawBadEffects(const scene::StageLayout& layout, const RenderFrameInput& input,
      game::Chart* chart);
  void drawHitEffects(const scene::StageLayout& layout, const RenderFrameInput& input, game::Chart* chart);
  // Paints the backdrop ring before chart objects, leaving their scaled overflow visible.
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
  textures.cancelDeferredUploads();
  if (!useChartBackground) {
    defaultBackground = textures.loadAsset("res/background.jpg");
    if (defaultBackground != nullptr) background = defaultBackground;
  }
  applyResPackSettings();
  refreshHoldSheet();
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
  hitFx = loadSkinAwareTextureDeferred("hit_fx.png", "res/hit_fx.png");
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

  for (int highlight = 0; highlight < 2; ++highlight) {
    if (holdTextures[highlight] != nullptr) {
      holdUv[highlight] = scene::buildHoldUv(holdTextures[highlight]->height(),
          holdTailSlice[highlight], holdHeadSlice[highlight]);
    }
  }
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
  materialsLoaded = true;
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
      asset::Image image = textures.imageForFile(skinPath);
      if (image.valid()) {
        cacheKey = skinPath.generic_string();
        return image;
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

gl::Texture* GameRenderer::Impl::loadSkinAwareTextureDeferred(
    const char* fileName, const char* assetFallback) {
  if (!skinDirectory.empty()) {
    const auto skinPath = skin::resolveSkinFile(skinDirectory, fileName);
    if (!skinPath.empty()) {
      if (gl::Texture* texture = textures.loadFileDeferred(skinPath)) return texture;
    }
  }
  if (assetFallback == nullptr) return nullptr;
  return textures.loadAssetDeferred(assetFallback);
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
  body.premultiplied = sheet.premultiplied;
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
  hitSkin.perfectFromSkin = skinMaterials.perfectFromSkin;
  hitSkin.goodFromSkin = skinMaterials.goodFromSkin;

  hitEffects.setAtlas(hitAtlas);
  hitEffects.setStyle(hitStyle);
  hitEffects.setSkin(hitSkin);
}

void GameRenderer::Impl::prefetchLineTextures(const game::Chart& chart) {
  std::vector<std::filesystem::path> lineTextures;
  lineTextures.reserve(chart.judgeLineList.size());
  for (const auto& line : chart.judgeLineList) {
    if (line == nullptr || line->texture.empty() || line->textureIsGif) continue;
    lineTextures.push_back(platform::pathFromUtf8(line->texture));
  }
  std::sort(lineTextures.begin(), lineTextures.end());
  lineTextures.erase(std::unique(lineTextures.begin(), lineTextures.end()), lineTextures.end());
  if (!lineTextures.empty()) textures.beginPrefetchFiles(lineTextures);
}

void GameRenderer::Impl::prepareForChart(game::Chart* chart) {
  if (chart == nullptr) return;
  if (chart == loadedChart) return;
  loadedChart = chart;
  badEffects.clear();
  badSpawned.assign(chart->allNotesSorted.size(), 0);
  hitEffects.clear();
  clickEffects.reset(*chart);
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
  if (!chart->rpeNoiseAreas.empty()) {
    rpeNoise.prepare(chart->packDir, chart->rpeNoiseLayers, assets, blockDisplaceMap,
        blockSparkMap, blockNoiseMap);
    rpeNoise.resize(width, height);
  } else {
    rpeNoise.release();
  }
  // Storyboard judge lines can cycle through dozens of textures (Bad Apple is one per
  // frame). Decoding each one on the frame it first becomes visible shows up as a hitch in
  // the play field, so hand the whole set to the background decoder before the run starts.
  prefetchLineTextures(*chart);
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
  // black clear, then the stage dim overlay covers it. The caller paints the reveal ring next.
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

  // Outro: plain lines hold their full length until the collapse window, then contract
  // towards the stage centre before the overlay takes over.
  float outroScale = 1.0F;
  if (input.transition.inOutro) {
    if (!scene::outroLinesVisible(true, input.transition.outroElapsedSec)) {
      batch.discard();
      return;
    }
    if (input.transition.outroElapsedSec >= scene::OUTRO_LINE_COLLAPSE_START_SEC) {
      // Inside the window the length is the collapse factor itself.
      outroScale = std::max(0.001F,
          scene::outroLineCollapseScale(true, input.transition.outroElapsedSec));
    }
  }

  const float lineWidthScale = std::clamp(input.judgeLineWidth, 0.5F, 2.0F);
  for (const int lineIndex : lineDrawOrder) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(chart->judgeLineList.size())) continue;
    const auto& line = *chart->judgeLineList[static_cast<std::size_t>(lineIndex)];
    if (line.attachUiElementId != 0) continue;
    // Cover lines paint the stage backdrop (storyboard scenery, black plates and the like).
    // The exit collapse belongs to the note canvas, so these lines leave with the backdrop
    // instead of being squeezed with the judge-line canvas.
    if (line.isCover && input.transition.inOutro &&
        input.transition.outroElapsedSec >= scene::OUTRO_LINE_COLLAPSE_START_SEC) {
      continue;
    }
    const auto& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
    if (!frame.valid || (frame.hideLine && !input.revealHiddenObjects)) continue;
    const bool backdropLine = line.isCover || !line.texture.empty() || frame.hasTextEvents;
    // Backdrop lines leave with the stage scenery: they fade over the first second of the
    // exit, then the note canvas collapses without carrying the artwork or black plates.
    if (backdropLine && input.transition.inOutro &&
        input.transition.outroElapsedSec >= scene::OUTRO_LINE_COLLAPSE_START_SEC) {
      continue;
    }
    const float backdropAlpha = backdropLine
        ? scene::outroBackdropAlpha(input.transition.inOutro, input.transition.outroElapsedSec)
        : 1.0F;
    const float alpha = scene::revealLineAlpha(frame.alphaRaw, frame.hideLine,
        input.revealHiddenObjects, input.revealHiddenFloorAlpha) * backdropAlpha;
    if (alpha <= 0.0F) continue;

    const float halfLength = official
        ? layout.stage.height * scene::LINE_LENGTH_OFFICIAL * std::abs(frame.scaleX)
        : layout.stage.width * scene::LINE_LENGTH_EDITOR * std::abs(frame.scaleX);
    // The judge-line-width setting scales only plain judge lines, like the reference;
    // storyboard textures and text lines keep their own size.
    const float lineWidth = official
        ? layout.stage.height * scene::LINE_WIDTH_OFFICIAL * std::abs(frame.scaleY) * lineWidthScale
        : layout.stage.height * scene::LINE_WIDTH_EDITOR * std::abs(frame.scaleY) * lineWidthScale;

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
    const bool hideStoryboard = !scene::storyboardVisibleDuringOutro(input.transition.inOutro,
        input.transition.outroElapsedSec);
    if (!hideStoryboard && !frame.hasTextEvents && !line.texture.empty()) {
      // A GIF line texture advances frames with the visual clock, or pins a frame when a
      // GIF event supplies a position.
      asset::AnimatedTexture* animated = (line.textureIsGif || asset::TextureCache::isGifPath(line.texture))
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
          width = std::abs(texScaleX) * baseScaleX;
          height = std::abs(texScaleX) * baseScaleX;
        } else {
          width = static_cast<float>(texture->width()) * baseScaleX * std::abs(texScaleX);
          height = static_cast<float>(texture->height()) * baseScaleX * std::abs(texScaleY);
        }
        if (width > 0.0F && height > 0.0F) {
          const float u0 = texScaleX < 0.0F ? 1.0F : 0.0F;
          const float u1 = texScaleX < 0.0F ? 0.0F : 1.0F;
          const float v0 = texScaleY < 0.0F ? 1.0F : 0.0F;
          const float v1 = texScaleY < 0.0F ? 0.0F : 1.0F;
          const bool queued = batch.addQuadAnchoredTextured(frame.x, frame.y, width, height,
              frame.rotateDeg, tint.red, tint.green, tint.blue, alpha, frame.anchorX,
              frame.anchorY, u0, v0, u1, v1, *texture);
          if (!queued) {
            // All extra sampler slots are taken. Submit the ordered batch and keep this
            // image in the next one.
            batch.flush(projection, textures.white(), nullptr);
            batch.addQuadAnchoredTextured(frame.x, frame.y, width, height, frame.rotateDeg,
                tint.red, tint.green, tint.blue, alpha, frame.anchorX, frame.anchorY, u0, v0,
                u1, v1, *texture);
          }
          continue;
        }
      }
    } else if (!hideStoryboard && !frame.text.empty()) {
      drawStoryboardText(batch, text, layout, frame, tint, alpha, textures.white());
      continue;
    }
    if (hideStoryboard && (!line.texture.empty() || frame.hasTextEvents)) continue;
    // A line that owns text events draws nothing once its text is empty.
    if (frame.hasTextEvents) continue;

    scene::drawPlainLine(batch, frame.x, frame.y, halfLength * outroScale, lineWidth,
        frame.rotateDeg, tint, alpha);
  }
  batch.flush(projection, textures.white(), nullptr);
}

void GameRenderer::Impl::drawBadEffects(const scene::StageLayout& layout,
    const RenderFrameInput& input, game::Chart* chart) {
  if (chart == nullptr || lineFrames.size() != chart->judgeLineList.size()) return;

  scene::NotePassParams params;
  params.stageWidth = layout.stage.width;
  params.stageHeight = layout.stage.height;
  const float keyScale = input.keyScale > 0.0F ? input.keyScale : 1.0F;
  // The native ClickBad keeps local scale 0.18 while CreateNote scales the note itself by
  // noteScale * min(aspect/(16/9), 1); on narrow stages the copy is therefore relatively
  // larger than the note. Its base is the 16:9 note width, not the shrunken one.
  params.noteWidthBase = layout.stage.width * kNoteWidthBaseRatio * keyScale;
  params.noteWidthScale = noteWidthScale;

  // Fading copies of notes that were just judged bad. A note is spawned once, the first
  // frame it is seen judged, so a note never produces two copies.
  const auto processBad = [&](const game::Note* note) {
    if (note == nullptr
        || note->judgeResult != static_cast<int>(game::JudgementEngine::Judgement::Bad)) {
      return;
    }
    if (note->sortedIndex < 0
        || static_cast<std::size_t>(note->sortedIndex) >= badSpawned.size()) {
      return;
    }
    auto& spawned = badSpawned[static_cast<std::size_t>(note->sortedIndex)];
    if (spawned != 0) return;
    spawned = 1;
    const game::Note& judged = *note;
    const int lineIndex = judged.master != nullptr ? judged.master->index : -1;
    if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= lineFrames.size()) return;
    const auto& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
    if (!frame.valid || judged.master == nullptr) return;
    const int type = std::clamp(judged.type, 0, 4);
    // The official noteBad prefab keeps the normal note sheet even when the note itself
    // uses the multi-press highlight variant.
    int variant = 0;
    gl::Texture* texture = noteHeads[type][0];
    if (texture == nullptr) {
      variant = 1;
      texture = noteHeads[type][1];
    }
    if (texture == nullptr) return;
    const float widthScale = params.widthScaleFor(type, variant == 1);
    const float size = (!std::isfinite(judged.size) || judged.size == 0.0F) ? 1.0F : judged.size;
    const float xScale =
        (!std::isfinite(judged.xScale) || judged.xScale == 0.0F) ? 1.0F : judged.xScale;
    const float baseWidth = params.noteWidthBase * size * widthScale;
    const float noteWidth = baseWidth * xScale;
    const float noteHeight =
        (baseWidth / static_cast<float>(texture->width())) * static_cast<float>(texture->height());
    // ClickBad is authored at local scale 0.18 while the note prefab is 0.22, and the
    // native judge parents the copy without copying the note's scale.
    constexpr float kBadEffectScale = 0.18F / 0.22F;
    const double judgedAt = std::isfinite(judged.judgeTimeSec) ? judged.judgeTimeSec : judged.sect;
    const float stageAspect = params.stageHeight > 1.0e-6F
        ? params.stageWidth / params.stageHeight : 16.0F / 9.0F;
    const auto state = judged.master->fillState(judgedAt, stageAspect);
    const auto pose = game::resolveNoteHeadEffectPose(judged, *judged.master, state, judgedAt,
        input.scrollSpeed, layout.stage.x, layout.stage.y, params.stageWidth, params.stageHeight);
    if (!pose.valid) return;
    badEffects.spawn(input.visualTimeSec, pose.x, pose.y, noteWidth * kBadEffectScale,
        noteHeight * kBadEffectScale, pose.rotationDeg, 0.0F, 0.0F, 1.0F, 1.0F, texture);
  };
  if (input.judgementEventsAuthoritative) {
    std::vector<std::size_t> indices;
    for (const auto& event : input.judgementEvents) {
      if (event.judgement == static_cast<int>(game::JudgementEngine::Judgement::Bad)
          && event.noteIndex >= 0
          && static_cast<std::size_t>(event.noteIndex) < chart->allNotesSorted.size()) {
        indices.push_back(static_cast<std::size_t>(event.noteIndex));
      }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (const std::size_t index : indices) processBad(chart->allNotesSorted[index]);
  } else {
    for (const game::Note* note : chart->allNotesSorted) processBad(note);
  }
  if (badEffects.size() == 0) return;
  batch.begin();
  badEffects.draw(batch, layout.stageProjection(), input.visualTimeSec, textures.white(),
      layout.mirror);
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
  const float cullScale = input.revealCameraActive ? input.revealScale : 1.0F;
  const math::Rect worldBounds = scene::revealViewportWorldBounds(
      layout, input.revealCameraActive, input.revealScale);
  const float marginW = layout.stage.width / cullScale / 12.0F;
  const float marginH = layout.stage.height / cullScale / 12.0F;
  params.cullBounds = {worldBounds.x - marginW, worldBounds.y - marginH,
      worldBounds.width + marginW * 2.0F, worldBounds.height + marginH * 2.0F};

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
}

void GameRenderer::Impl::drawHitEffects(const scene::StageLayout& layout,
    const RenderFrameInput& input, game::Chart* chart) {
  if (chart == nullptr || hitFx == nullptr || !textures.deferredUploadsComplete()) return;
  const float noteWidth =
      layout.stage.width * noteWidthRatio(layout.stage.width, layout.stage.height) * input.keyScale;
  const bool autoplayBursts = input.practice.autoPlay && !input.replayPlayback;
  hitEffects.update(*chart, input.chartTimeSec, input.visualTimeSec, layout.stage.x,
      layout.stage.y, layout.stage.width, layout.stage.height, noteWidth,
      input.practice.enabled ? input.practice.speed : 1.0F, autoplayBursts,
      input.judgementEventsAuthoritative ? &input.judgementEvents : nullptr);
  // Hold bursts are timed by the gameplay session and arrive as events. A head burst is
  // placed where the head was when it was hit (above the line for an early hit, on the line
  // once the head has arrived); body bursts sit on the line contact point.
  const float musicSpeed = input.practice.enabled ? input.practice.speed : 1.0F;
  for (const auto& burst : input.holdHeadBursts) {
    const game::Note* note = burst.noteIndex >= 0 &&
            static_cast<std::size_t>(burst.noteIndex) < chart->allNotesSorted.size()
        ? chart->allNotesSorted[static_cast<std::size_t>(burst.noteIndex)]
        : nullptr;
    if (note == nullptr || note->master == nullptr) continue;
    const int lineIndex = note->master->index;
    if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= lineFrames.size()) continue;
    const float stageAspect =
        layout.stage.height > 1.0e-6F ? layout.stage.width / layout.stage.height : 16.0F / 9.0F;
    const auto state = note->master->fillState(burst.timeSec, stageAspect);
    const auto pose = game::resolveHoldHeadEffectPose(*note, *note->master, state, burst.timeSec,
        input.scrollSpeed, layout.stage.x, layout.stage.y, layout.stage.width, layout.stage.height);
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
  // The ring is a backdrop layer: chart objects remain visible when their scaled geometry
  // extends beyond the inner stage.
  const gl::Texture* blurTexture =
      background != nullptr ? backgroundBlur.ensure(width, height, background) : nullptr;
  const gl::Texture* artworkTexture = blurTexture != nullptr ? blurTexture : background;
  const auto projection = outer.projection();
  const auto& full = outer.stage;
  const auto& shown = inner.stage;
  const math::Rect panels[4] = {
      {full.x, full.y, full.width, std::max(0.0F, shown.y - full.y)},
      {full.x, shown.bottom(), full.width, std::max(0.0F, full.bottom() - shown.bottom())},
      {full.x, shown.y, std::max(0.0F, shown.x - full.x), shown.height},
      {shown.right(), shown.y, std::max(0.0F, full.right() - shown.right()), shown.height}};
  float drawWidth = outer.viewWidth;
  float drawHeight = full.height;
  float drawX = 0.0F;
  float drawY = full.y;
  if (artworkTexture != nullptr) {
    const float textureAspect = static_cast<float>(artworkTexture->width()) /
        std::max(1.0F, static_cast<float>(artworkTexture->height()));
    const float destinationAspect = outer.viewWidth / std::max(1.0F, full.height);
    if (textureAspect > destinationAspect) {
      drawWidth = full.height * textureAspect;
    } else {
      drawHeight = outer.viewWidth / textureAspect;
    }
    drawX = (outer.viewWidth - drawWidth) * 0.5F;
    drawY = full.y + (full.height - drawHeight) * 0.5F;
  }
  for (const math::Rect& panel : panels) {
    if (panel.empty()) continue;
    if (artworkTexture != nullptr) {
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
  values.revealCameraActive = input.revealCameraActive;
  values.revealScale = input.revealScale;
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
      transform.rotateDeg = frame.rotateDeg;
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
  const bool clipHud = scene::hudNeedsStageClip(input.revealCameraActive, layout.fullscreen);
  if (clipHud) {
    batch.setScissor(true, layout.stage.x, layout.stage.y,
        layout.stage.right(), layout.stage.bottom());
  }
  scene::drawHud(batch, text, layout, values, colors, hudTextures, transforms, hitOffset,
      hudAlpha, layout.projection());
  // The pass advanced the readout; keep the state for the next frame.
  hudDisplayScore = values.displayScore;
  hudDisplayAccuracy = values.displayAccuracy;
  hudDisplayLastUpdateSec = values.displayLastUpdateSec;
  hudDisplayReset = false;

  // A failed challenge replaces the combo readout with the distorted block field, drawn at
  // the same anchor the number would occupy.
  if (chart != nullptr && chart->levelEffects != nullptr
      && chart->levelEffects->isChallengeFailed() && values.combo >= 3) {
    const auto reveal = scene::hudRevealTransform(
        layout, input.revealCameraActive, input.revealScale);
    const float unit = reveal.size(scene::hudUnit(layout));
    blockedComboNumber.prepare(assets, std::to_string(values.combo), unit * 70.0F);
    blockedComboNumber.draw(batch, layout.projection(), textures.white(),
        reveal.x(scene::hudX(layout, 0.0F)),
        reveal.y(scene::hudY(layout, scene::kComboNumberUiY)),
        hudAlpha * 0.92F, input.visualTimeSec, static_cast<float>(width),
        static_cast<float>(height));
    // The label clears with its own shorter fade, so it leaves slightly ahead of the number.
    const float labelAlpha =
        hudAlpha * scene::comboLabelOutroAlpha(input.hud.outroProgress);
    blockedComboLabel.prepare(assets, values.comboLabel, unit * 24.0F);
    blockedComboLabel.draw(batch, layout.projection(), textures.white(),
        reveal.x(scene::hudX(layout, 0.0F)),
        reveal.y(scene::hudY(layout, scene::kComboLabelUiY)), labelAlpha,
        input.visualTimeSec, static_cast<float>(width), static_cast<float>(height));
  }
  if (clipHud) batch.setScissor(false, 0.0F, 0.0F, 0.0F, 0.0F);
}

GameRenderer::GameRenderer() : impl_(std::make_unique<Impl>()) {}

GameRenderer::~GameRenderer() = default;

bool GameRenderer::initialize(const std::filesystem::path& assetRoot) {
  return initializeBegin(assetRoot) && initializeFinish();
}

bool GameRenderer::initializeBegin(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory, const std::filesystem::path& backgroundImage) {
  if (impl_->initialized || impl_->beginDone) return true;
  impl_->initStart = std::chrono::steady_clock::now();
  impl_->assets = assetRoot;
  impl_->textures.setAssetRoot(assetRoot);
  impl_->skinDirectory = skinDirectory;
  impl_->useChartBackground = !backgroundImage.empty();
  prefetchGameplayTextures(skinDirectory);
  prefetchBackgroundImage(backgroundImage);
  impl_->beginDone = true;
  return true;
}

bool GameRenderer::initializeFinish(bool initializeText) {
  if (!impl_->beginDone) return false;
  if (!impl_->initialized) {
    impl_->textures.awaitPrefetchAssets();
    impl_->loadGameplayTextures();
    (void)impl_->blockComposite.prepare(impl_->assets, impl_->blockDisplaceMap,
        impl_->blockSparkMap, impl_->blockNoiseMap);
    const double texturesMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - impl_->initStart).count();
    bindDefaultBlend();
    impl_->initialized = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render] renderer initialized (textures) %.1fms", texturesMs);
  }
  if (initializeText && !impl_->textReady) {
    const auto started = std::chrono::steady_clock::now();
    if (!impl_->text.initialize(impl_->assets)) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
          "[render] renderer initialization failed: text layer unavailable (asset root %s)",
          impl_->assets.string().c_str());
      return false;
    }
    impl_->textReady = true;
    impl_->initTextMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render] text initialized %.1fms", impl_->initTextMs);
  }
  return true;
}

bool GameRenderer::initializeBegun() const noexcept { return impl_->beginDone; }

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
  impl_->rpeNoise.resize(impl_->width, impl_->height);
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
    // The opaque backdrop is drawn first, so it cannot cover the particle layer.
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
    // White squares drift in from above and below; the field advances on the chart clock
    // and stays idle until the level's particle start time.
    const float startTime = effect.scalar("particleStartTime", -1.0F);
    if (startTime < 0.0F || chartTime >= startTime) {
      if (!messageCubeTexture.valid()) {
        const auto bytes = platform::AssetManager::readBinary(assets, "official_textures/MsgCube.png");
        auto image = asset::decodeImageMemory(bytes.data(), bytes.size());
        for (std::size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
          const auto alpha = static_cast<std::uint8_t>(
              game::MessageParticleField::textureAlpha(
                  image.pixels[index], image.pixels[index + 1]));
          image.pixels[index] = alpha;
          image.pixels[index + 1] = alpha;
          image.pixels[index + 2] = alpha;
          image.pixels[index + 3] = alpha;
        }
        messageCubeTexture.uploadRgba(image.width, image.height, image.pixels.data());
      }
      const float bandWidth = effect.track("bandWidth", chartTime);
      const float delta = messageParticleLastSec < 0.0
          ? 0.0F
          : static_cast<float>(input.chartTimeSec - messageParticleLastSec);
      messageParticleLastSec = input.chartTimeSec;
      if (delta > 0.0F && delta <= 0.5F) messageParticles.update(delta, bandWidth);

      const float stageHeight = layout.stage.height;
      const float stageWidth = layout.stage.width;
      batch.begin();
      for (int index = 0; index < messageParticles.count(); ++index) {
        const auto& particle = messageParticles.at(index);
        if (particle.age >= game::MessageParticleField::LIFE_SEC) continue;
        const float alpha = game::MessageParticleField::bandAlpha(particle, bandWidth);
        if (!(alpha > 0.004F)) continue;
        const float size = messageParticles.sizeAt(particle) * stageHeight;
        if (!(size > 0.5F)) continue;
        // World x spans the full play-field width; uv y maps onto the stage height.
        const float x = layout.stage.x + stageWidth * 0.5F +
            particle.worldX / game::MessageParticleField::UNITS * stageHeight;
        const float y = layout.stage.y + particle.y * stageHeight;
        batch.addQuad(x, y, size, size, 0.0F, 1.0F, 1.0F, 1.0F, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
      }
      const auto& texture = messageCubeTexture.valid() ? messageCubeTexture : textures.white();
      batch.flush(layout.projection(), texture, nullptr);
    } else {
      messageParticleLastSec = -1.0;
      messageParticles.reset();
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
  chain.setStageSize(stageRect.width, stageRect.height);
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
  // The RPE noise timeline is advanced on every frame, not only when a layer is drawn.
  // A finger released during a gap otherwise stays latched in the timeline and its hover
  // pops back when the next layer opens, even though the finger is already up.
  rpeNoiseTouches.clear();
  if (chart != nullptr && !chart->rpeNoiseAreas.empty()) {
    rpeNoiseTouches.reserve(static_cast<std::size_t>(input.noiseTouchCount));
    for (int index = 0; index < input.noiseTouchCount; ++index) {
      const auto& touch = input.noiseTouches[index];
      rpeNoiseTouches.push_back({touch.id, touch.x, touch.y});
    }
    if (rpeNoise.ready()) {
      rpeNoise.updateTouches(rpeNoiseTouches, input.chartTimeSec, chart->rpeNoiseLayers);
    }
  }
  // Demo mode reflects the play field about the stage centre. The reflection lives in the
  // stage projection, so notes, judge lines and hit effects all move with it while the HUD
  // (projected with `projection()`) keeps its unmirrored layout.
  layout.mirror = input.mirrorX;

  RenderFrameInput revealInput = input;
  const scene::RevealCameraState reveal = scene::resolveRevealCamera(input.chartReveal,
      input.transition.inIntro, input.transition.inOutro, input.chartTimeSec, input.playheadSec,
      input.hud.totalDurationSec);
  revealInput.revealCameraActive = reveal.active;
  revealInput.revealScale = reveal.scale;
  revealInput.revealHiddenObjects = reveal.hiddenObjects;
  revealInput.revealHiddenFloorAlpha = reveal.hiddenFloorAlpha;
  revealInput.hud.revealCameraActive = reveal.active;
  revealInput.hud.revealScale = reveal.scale;

  const auto cameraFrame = chart != nullptr
      ? chart->rpeCamera.sample(input.chartTimeSec) : game::RpeCameraFrame{};
  const auto camera = scene::resolveRpeCamera(cameraFrame, layout);
  layout.cameraIncludesUi = camera.active && camera.followChart;
  layout.cameraMatrix = camera.matrix;

  scene::StageLayout sceneLayout = layout;
  sceneLayout.fieldScale = reveal.active ? reveal.scale : 1.0F;
  sceneLayout.cameraActive = camera.active;
  sceneLayout.cameraMatrix = camera.matrix;
  scene::StageLayout maskLayout = layout;
  maskLayout.stage = scene::revealStageRect(layout, reveal.active, reveal.scale);

  // The screen-space composite resamples the finished frame inside a red zone, so it
  // needs that frame as a texture while it writes into the caller's target. A frame with
  // a zone on screen takes the offscreen route, with or without a storyboard chain; so
  // does a frame with a swallowed finger still held, because the touch glow is emitted by
  // the same composite and has to survive gaps between zones.
  const bool anyZoneVisible = chart != nullptr && !chart->blockAreaList.empty()
      && !input.transition.inIntro && input.chartTimeSec >= 0.0
      && chart->hasLiveBlock(input.chartTimeSec);
  // A swallowed finger keeps its hover slot until it lifts, even when no zone is live.
  // The composite has to keep running for those frames because the touch glow is added
  // outside the zone body; gating it on live zones alone made the special touch disappear
  // during gaps and reappear when the next zone opened.
  const bool anyBlockHover = input.touches.count > 0;
  const bool useBlockComposite = (anyZoneVisible || anyBlockHover) && blockComposite.ready() &&
      blockComposite.allocated() && sceneFbo.valid();

  // A chart with storyboard effects renders into an offscreen target first so the
  // shader chain can process the whole play field. Zone composite precedes those effects;
  // the low-resolution fallback and direct scene follow, with antialias around the paths
  // that resolve through FXAA.
  const bool anyRpeNoiseVisible = applyEffects && chart != nullptr &&
      !chart->rpeNoiseAreas.empty() && input.chartTimeSec >= 0.0 &&
      std::any_of(chart->rpeNoiseAreas.begin(), chart->rpeNoiseAreas.end(),
          [&](const game::RpeNoiseArea& area) {
            return input.chartTimeSec >= area.appear && input.chartTimeSec < area.disappear;
          });
  const bool useRpeNoise = anyRpeNoiseVisible && rpeNoise.ready() && sceneFbo.valid();
  const bool useEffects = applyEffects && chart != nullptr && sceneFbo.valid() &&
      ((!input.disableChartShader && !chart->prprEffects.empty()) || useRpeNoise);
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
  const bool needBars = !reveal.active && !layout.fullscreen;
  const bool deferSideColumns = needBars && dsGridCanvasVisible(revealInput, chart);
  drawBackgroundPass(layout, artworkAlpha, dimAlpha, sideArtworkAlpha, sideDimAlpha, opening,
      deferSideColumns);
  gl::logErrors("scene.background");
  if (reveal.active && reveal.scale < 0.999F) {
    drawRevealPanels(maskLayout, layout, sideDimAlpha);
    gl::logErrors("scene.revealPanels");
  }

  // A level effect may draw its own objects between the backdrop and the play field.
  drawLevelEffectObjects(chart, revealInput, sceneLayout);
  gl::logErrors("scene.levelObjects");
  if (deferSideColumns) {
    // The same blurred copy the backdrop uses, so the band keeps matching the play field.
    drawSideColumns(layout, backgroundBlur.ensure(width, height, background),
        sideArtworkAlpha, sideDimAlpha);
    gl::logErrors("scene.sideColumns");
  }

  if (chart != nullptr) {
    lineFrames.clear();
    const math::Rect worldBounds = camera.active ? camera.worldBounds
        : scene::revealViewportWorldBounds(layout, reveal.active, reveal.scale);
    scene::LinePassParams lineParams;
    lineParams.chartTimeSec = input.chartTimeSec;
    lineParams.officialFormat = chart->formatVersion > 0;
    lineParams.revealHiddenObjects = reveal.hiddenObjects;
    lineParams.revealFloorAlpha = reveal.hiddenFloorAlpha;
    const bool blockNotes = input.transition.inIntro || input.chartTimeSec < 0.0;
    scene::evaluateLines(*chart, sceneLayout, input.chartTimeSec, input.scrollSpeed, blockNotes,
        revealInput.revealHiddenObjects, reveal.scale, worldBounds, layout.stage.width * 2.0F,
        lineParams, lineFrames);
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
    if (useRpeNoise) {
      gl::Texture* underlay = rpeNoise.draw(sceneFbo.color(), chart->rpeNoiseAreas,
          chart->rpeNoiseLayers, input.chartTimeSec, maskLayout.stage.x, maskLayout.stage.y,
          maskLayout.stage.width, maskLayout.stage.height, chart->rpeNoiseIgnoreTripleInversion, {},
          camera.active ? &cameraFrame : nullptr, camera.viewScale, 1);
      if (underlay != nullptr && underlay->id() != sceneFbo.color().id()) {
        sceneFbo.bind();
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        fx::BlitPass::instance().draw(*underlay, 1.0F, 1.0F, 1.0F, 1.0F, false);
        bindDefaultBlend();
      }
    }
    // The native ClickBad copies live on the Background sorting layer, under the judge line
    // and under every note sprite, so they are submitted before the line pass.
    if (!blockNotes) drawBadEffects(sceneLayout, revealInput, chart);
    gl::logErrors("scene.badEffects");
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
      drawHudPass(revealInput, layout, chart, judgement);
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
    if (reveal.active) {
      const float centerX = layout.centerX();
      const float centerY = layout.centerY();
      const auto transform = [centerX, centerY, scale = reveal.scale](fx::BlockMaskQuad& quad) {
        quad.centerX = centerX + (quad.centerX - centerX) * scale;
        quad.centerY = centerY + (quad.centerY - centerY) * scale;
        quad.width *= scale;
        quad.height *= scale;
      };
      std::for_each(maskA.begin(), maskA.end(), transform);
      std::for_each(maskB.begin(), maskB.end(), transform);
    }
    blockComposite.setGeometryScale(reveal.active ? reveal.scale : 1.0F);
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
    blockComposite.setStageRect(maskLayout.stage.x, maskLayout.stage.y,
        maskLayout.stage.right(), maskLayout.stage.bottom());
    {
      // Held points reach the composite in the shader's own space: a fraction of the width,
      // and a fraction of the height measured from the bottom.
      fx::BlockCompositePass::TouchState touch;
      const float safeWidth = std::max(1.0F, static_cast<float>(width));
      const float safeHeight = std::max(1.0F, static_cast<float>(height));
      touch.count = std::clamp(input.touches.count, 0, 10);
      // The shipped shader compares `tpos.x` (a height fraction, 0..aspect) against
      // `touchPos.x * aspect`. touchPos.x is therefore a width fraction (0..1), not a height
      // fraction; dividing it by the height shifted the hover right by roughly a third of
      // the screen on a 16:9 view.
      for (int index = 0; index < touch.count; ++index) {
        touch.points[index].x = input.touches.points[index].x / safeWidth;
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
          maskLayout.stage, input.chartTimeSec);
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
          drawHudPass(revealInput, layout, chart, judgement);
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
    const bool hasGlobal = !input.disableChartShader && std::any_of(
        chart->prprEffects.begin(), chart->prprEffects.end(),
        [](const game::PrprEffect& effect) { return effect.global; });
    gl::Texture* result = !input.disableChartShader
        ? applyStageScopedChain(*chart, &sceneFbo.color(), maskLayout.stage, input.chartTimeSec)
        : &sceneFbo.color();
    bool hudShaded = false;
    const auto compositeRpeNoise = [&](gl::Texture* source) {
      if (!useRpeNoise || source == nullptr) return source;
      gl::Texture* composited = rpeNoise.draw(*source, chart->rpeNoiseAreas,
          chart->rpeNoiseLayers, input.chartTimeSec, maskLayout.stage.x, maskLayout.stage.y,
          maskLayout.stage.width, maskLayout.stage.height, chart->rpeNoiseIgnoreTripleInversion,
          rpeNoiseTouches, camera.active ? &cameraFrame : nullptr, camera.viewScale, 2);
      if (composited == nullptr || composited->id() == source->id()) return source;
      glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo.fbo());
      glViewport(0, 0, width, height);
      glDisable(GL_BLEND);
      fx::BlitPass::instance().ensure();
      fx::BlitPass::instance().draw(*composited, 1.0F, 1.0F, 1.0F, 1.0F, false);
      bindDefaultBlend();
      return &sceneFbo.color();
    };
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
      if (hudVisible) drawHudPass(revealInput, layout, chart, judgement);
      result = compositeRpeNoise(&sceneFbo.color());
      result = chain.apply(collectGlobalEffects(*chart), &sceneFbo.color(), pingFbo, pongFbo,
          input.chartTimeSec);
      hudShaded = hudVisible;
      gl::logErrors("scene.effectChainGlobal");
    } else if (useRpeNoise) {
      glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo.fbo());
      glViewport(0, 0, width, height);
      if (result != nullptr && result->id() != sceneFbo.color().id()) {
        glDisable(GL_BLEND);
        fx::BlitPass::instance().ensure();
        fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
        bindDefaultBlend();
      }
      if (hudVisible) drawHudPass(revealInput, layout, chart, judgement);
      hudShaded = hudVisible;
      result = compositeRpeNoise(&sceneFbo.color());
      gl::logErrors("scene.rpeNoise");
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
    drawHudPass(revealInput, layout, chart, judgement);
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
        RenderFrameInput dimmed = revealInput;
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

void GameRenderer::resetJudgementEffects(game::Chart* chart) {
  if (impl_ == nullptr) return;
  // Only the per-run effect state is cleared here. prepareForChart owns the chart-scoped
  // setup (storyboard chain, noise layers, line order) and skips itself when the chart is
  // already loaded, so claiming the chart here would leave a fresh run without its shaders.
  impl_->badEffects.clear();
  impl_->hitEffects.clear();
  // A chart that is not the loaded one has to go through prepareForChart again, even when it
  // happens to reuse the address of the chart it replaced. Only the pointer is compared, so
  // the released chart is never dereferenced.
  if (impl_->loadedChart != chart) impl_->loadedChart = nullptr;
  if (chart == nullptr) {
    impl_->badSpawned.clear();
    return;
  }
  impl_->badSpawned.assign(chart->allNotesSorted.size(), 0);
  impl_->clickEffects.reset(*chart);
}

void GameRenderer::prefetchChartTextures(const game::Chart& chart) {
  if (impl_ != nullptr) impl_->prefetchLineTextures(chart);
}

void GameRenderer::prefetchGameplayTextures(const std::filesystem::path& skinDirectory) {
  if (impl_ == nullptr) return;
  if (impl_->prefetchStarted && impl_->prefetchSkinKey_ == skinDirectory) return;
  impl_->prefetchStarted = true;
  impl_->prefetchSkinKey_ = skinDirectory;
  std::vector<std::string> assets = {
      "res/pause.png", "res/timerLine.png", "res/NoteRing.png", "res/arrow.png",
      "official_textures/Glitch4in1.png", "official_textures/GlitchMap.png",
      "official_textures/WireNoise.png", "official_textures/DisplaceMap.png",
      "official_textures/BlockNoise1.png", "official_textures/PointNoise.png",
      "official_textures/FD_Noise_00000.png", "official_textures/KaleidoNormal.png",
      "official_textures/KaleidoCol.png"};
  std::vector<std::filesystem::path> files;
  const auto resolve = [&](const char* name, const char* fallback) {
    if (!skinDirectory.empty()) {
      const auto path = skin::resolveSkinFile(skinDirectory, name);
      if (!path.empty()) {
        files.push_back(path);
        return;
      }
    }
    if (fallback != nullptr) assets.emplace_back(fallback);
  };
  resolve("click.png", "res/click.png");
  resolve("click_mh.png", "res/click_mh.png");
  resolve("drag.png", "res/drag.png");
  resolve("drag_mh.png", "res/drag_mh.png");
  resolve("flick.png", "res/flick.png");
  resolve("flick_mh.png", "res/flick_mh.png");
  resolve("hold.png", "res/hold.png");
  resolve("hold_mh.png", "res/hold_mh.png");
  resolve("hit_fx.png", "res/hit_fx.png");
  resolve("hold_body.png", nullptr);
  resolve("hold_body_mh.png", nullptr);
  impl_->textures.beginPrefetchAssets(assets);
  impl_->textures.beginPrefetchFiles(files);
}

void GameRenderer::prefetchBackgroundImage(const std::filesystem::path& image) {
  if (impl_ == nullptr) return;
  if (image.empty()) impl_->textures.beginPrefetchAssets({"res/background.jpg"});
  else impl_->textures.beginPrefetchFiles({image});
}

bool GameRenderer::gameplayTexturesReady() const {
  return impl_ == nullptr || !impl_->prefetchStarted || impl_->textures.prefetchFinished();
}

void GameRenderer::setSkinDirectory(const std::filesystem::path& skinDirectory) {
  if (impl_ == nullptr || (impl_->materialsLoaded && impl_->skinDirectory == skinDirectory)) return;
  impl_->skinDirectory = skinDirectory;
  impl_->loadGameplayTextures();
}

void GameRenderer::setBackgroundImage(const std::filesystem::path& image) {
  if (impl_ == nullptr || !impl_->initialized || impl_->backgroundPath == image) return;
  impl_->backgroundPath = image;
  gl::Texture* loaded = image.empty() ? nullptr : impl_->textures.loadFile(image);
  impl_->useChartBackground = loaded != nullptr;
  if (loaded != nullptr) impl_->background = loaded;
  else {
    if (impl_->defaultBackground == nullptr) {
      impl_->defaultBackground = impl_->textures.loadAsset("res/background.jpg");
    }
    if (impl_->defaultBackground != nullptr) impl_->background = impl_->defaultBackground;
  }
  impl_->backgroundBlur.invalidate();
}

void GameRenderer::render(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement) {
  if (!impl_->initialized) return;
  const auto refreshHitFx = [this]() {
    impl_->hitFx = impl_->loadSkinAwareTextureDeferred("hit_fx.png", "res/hit_fx.png");
  };
  if (!impl_->textures.deferredUploadsComplete()) refreshHitFx();
  if (chart != nullptr && !chart->allNotesSorted.empty() && !impl_->textures.deferredUploadsComplete()
      && input.chartTimeSec + 0.35 >= chart->allNotesSorted.front()->sect) {
    impl_->textures.completeDeferredUploads();
    refreshHitFx();
  }
  impl_->disableChartShaderSetting = input.disableChartShader;
  impl_->prepareForChart(chart);
  // The target is whatever the window presents from; on a desktop GL context or an EGL
  // surface that is zero, while the UIKit view owns a framebuffer object.
  impl_->drawScene(input, chart, judgement, gl::screenFramebuffer());
  if (!impl_->textures.deferredUploadsComplete()) {
    impl_->textures.advanceDeferredUploads(12.0);
    refreshHitFx();
  }
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
  impl_->textures.completeDeferredUploads();
  impl_->hitFx = impl_->loadSkinAwareTextureDeferred("hit_fx.png", "res/hit_fx.png");
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
  impl_->rpeNoise.resize(width, height);
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
  gl::bindScreenFramebuffer();

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
  impl_->textures.completeDeferredUploads();
  impl_->hitFx = impl_->loadSkinAwareTextureDeferred("hit_fx.png", "res/hit_fx.png");
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
  impl_->rpeNoise.resize(width, height);
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
      gl::bindScreenFramebuffer();
      return true;
    }
    outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
    gl::bindScreenFramebuffer();
    flipRows();
    return true;
  }
  outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
  gl::bindScreenFramebuffer();
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
  gl::bindScreenFramebuffer();
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
  gl::bindScreenFramebuffer();
  return converted;
}

bool GameRenderer::takePendingCaptureFrame(std::vector<std::uint8_t>& outPlanes) {
  if (!impl_->capturing || !impl_->captureYuv.valid()) return false;
  if (!impl_->captureYuv.hasPending()) return false;
  const bool collected = impl_->captureYuv.takePending(outPlanes);
  gl::bindScreenFramebuffer();
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
  impl_->rpeNoise.resize(impl_->width, impl_->height);
  glViewport(0, 0, impl_->width, impl_->height);
}

int GameRenderer::captureWidth() const noexcept { return impl_->captureWidth; }

int GameRenderer::captureHeight() const noexcept { return impl_->captureHeight; }

} // namespace rechigros::render
