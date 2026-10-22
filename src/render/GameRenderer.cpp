#include "rechigros/render/GameRenderer.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/MessageParticleField.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/GlDebug.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/fx/BadEffectField.hpp"
#include "rechigros/render/fx/BlockCompositePass.hpp"
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/DsGridPass.hpp"
#include "rechigros/render/fx/KaleidoBackdropPass.hpp"
#include "rechigros/render/fx/HitEffect.hpp"
#include "rechigros/render/fx/PauseBackdropPass.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/HudPass.hpp"
#include "rechigros/render/scene/LinePass.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"
#include "rechigros/render/scene/StageLayout.hpp"
#include "rechigros/render/scene/Transition.hpp"
#include "rechigros/render/text/TextRenderer.hpp"
#include "rechigros/platform/AssetManager.hpp"

#include <nlohmann/json.hpp>

#include <SDL_log.h>

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
    dsGrid.release();
    kaleido.release();
    sceneFbo.reset();
    pingFbo.reset();
    pongFbo.reset();
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
    hudArtwork.pause = hudArtwork.timerLine = nullptr;
    effectGlitchMap = effectGlitchDogMap = effectWireNoise = effectDisplaceMap = nullptr;
    blockDisplaceMap = blockSparkMap = blockNoiseMap = nullptr;
    headTextureList.clear();
  }

  std::filesystem::path assets;
  int width{1280};
  int height{720};
  bool initialized{};

  asset::TextureCache textures;
  text::TextRenderer text;
  batch::SpriteBatch batch;
  gl::Framebuffer sceneFbo;
  gl::Framebuffer pingFbo;
  gl::Framebuffer pongFbo;
  fx::HitEffectField hitEffects;
  fx::PauseBackdropPass pauseBackdrop;
  fx::BlockOverlay blockOverlay;
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
  std::filesystem::path skinDirectory;
  double blockUpdateLastSec{};
  fx::DsGridPass dsGrid;
  fx::KaleidoBackdropPass kaleido;
  gl::Texture* kaleidoNormal{};
  gl::Texture* kaleidoColour{};
  gl::Texture* dsGridTexture{};
  gl::Framebuffer pauseBlurA;
  gl::Framebuffer pauseBlurB;
  fx::PrprChain chain;

  // Gameplay textures, resolved once per asset root.
  gl::Texture* background{};
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
  // Layers one JSON material table over the current settings. Only present fields change.
  void applyResPackLayer(const nlohmann::json& root);
  gl::Texture* cropHoldBody(const char* sheetPath, int tailSlice, int headSlice);
  // Re-slices the hold sheet from the current material settings. The pack and a skin may
  // change the slice sizes, so this runs again whenever those settings change.
  void refreshHoldSheet();
  void prepareForChart(game::Chart* chart);
  void drawBackgroundPass(const scene::StageLayout& layout, float artworkAlpha, float dimAlpha);
  void drawLinePass(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, const scene::StageLayout& layout);
  void drawNotePasses(const scene::StageLayout& layout, const RenderFrameInput& input,
      game::Chart* chart);
  void drawHitEffects(const scene::StageLayout& layout, const RenderFrameInput& input, game::Chart* chart);
  void drawHudPass(const RenderFrameInput& input, const scene::StageLayout& layout);
  // Draws the objects a level effect adds to the scene itself, which are separate from the
  // storyboard post-processing chain.
  void drawLevelEffectObjects(game::Chart* chart, const RenderFrameInput& input,
      const scene::StageLayout& layout);
  // Draws one complete frame of the game into whatever target is bound, running the
  // storyboard chain first when the chart asks for it.
  void drawScene(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, unsigned int targetFbo, bool applyEffects = true);
  scene::LineTint lineTintFor(const RenderFrameInput& input,
      const game::JudgementEngine* judgement) const;
};

void GameRenderer::Impl::loadGameplayTextures() {
  background = textures.loadAsset("res/background.jpg");
  noteHeads[kNoteTypeTap][0] = textures.loadAsset("res/click.png");
  noteHeads[kNoteTypeTap][1] = textures.loadAsset("res/click_mh.png");
  noteHeads[kNoteTypeDrag][0] = textures.loadAsset("res/drag.png");
  noteHeads[kNoteTypeDrag][1] = textures.loadAsset("res/drag_mh.png");
  noteHeads[kNoteTypeFlick][0] = textures.loadAsset("res/flick.png");
  noteHeads[kNoteTypeFlick][1] = textures.loadAsset("res/flick_mh.png");
  holdTextures[0] = textures.loadAsset("res/hold.png");
  holdTextures[1] = textures.loadAsset("res/hold_mh.png");
  noteHeads[kNoteTypeHold][0] = holdTextures[0];
  noteHeads[kNoteTypeHold][1] = holdTextures[1];
  hitFx = textures.loadAsset("res/hit_fx.png");
  // HUD artwork; a missing file makes the corresponding widget fall back to a flat quad.
  hudArtwork.pause = textures.loadAsset("res/pause.png");
  hudArtwork.timerLine = textures.loadAsset("res/timerLine.png");
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
  dsGrid.prepare(assets);
  kaleido.prepare(assets);

  // Chapter-9 effect samplers; a missing file simply disables its preset.
  effectGlitchMap = textures.loadAsset("official_textures/Glitch4in1.png");
  effectGlitchDogMap = textures.loadAsset("official_textures/GlitchMap.png");
  effectWireNoise = textures.loadAsset("official_textures/WireNoise.png");
  effectDisplaceMap = textures.loadAsset("official_textures/DisplaceMap.png");
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
  blockDisplaceMap = textures.loadAsset("official_textures/BlockNoise1.png");
  blockSparkMap = textures.loadAsset("official_textures/PointNoise.png");
  blockNoiseMap = textures.loadAsset("official_textures/FD_Noise_00000.png");
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
    const char* sheetPath = highlight == 1 ? "res/hold_mh.png" : "res/hold.png";
    holdBodies[highlight] = cropHoldBody(sheetPath, tailSlice, headSlice);
    if (holdTextures[highlight] != nullptr) {
      holdUv[highlight] =
          scene::buildHoldUv(holdTextures[highlight]->height(), tailSlice, headSlice);
    }
  }
}

gl::Texture* GameRenderer::Impl::cropHoldBody(const char* sheetPath, int tailSlice,
    int headSlice) {
  const auto bytes = platform::AssetManager::readBinary(assets, sheetPath);
  if (bytes.empty()) return nullptr;
  asset::Image sheet = asset::decodeImageMemory(bytes.data(), bytes.size());
  if (!sheet.valid()) return nullptr;

  const int bodyTop = std::max(0, tailSlice);
  const int bodyHeight = sheet.height - bodyTop - std::max(0, headSlice);
  if (bodyHeight <= 0 || sheet.width <= 0) return nullptr;

  const auto rowBytes = static_cast<std::size_t>(sheet.width) * 4;
  asset::Image body;
  body.width = sheet.width;
  body.height = bodyHeight;
  body.pixels.resize(rowBytes * static_cast<std::size_t>(bodyHeight));
  const auto* source = sheet.pixels.data() +
      static_cast<std::size_t>(bodyTop) * rowBytes;
  std::copy_n(source, body.pixels.size(), body.pixels.data());

  // The body is tiled vertically along the hold, so it repeats in T only.
  auto* texture = textures.loadPixels(std::string("holdbody:") + sheetPath, std::move(body));
  if (texture != nullptr) texture->setWrapRepeat(false, true);
  return texture;
}

void GameRenderer::Impl::applyResPackSettings() {
  // `res/respack.json` carries the atlas grids and the note colour overrides. Every
  // field is optional, so a missing or malformed file leaves the defaults in place.
  const auto bytes = platform::AssetManager::readBinary(assets, "res/respack.json");
  if (!bytes.empty()) {
    const auto root = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (!root.is_discarded() && root.is_object()) applyResPackLayer(root);
  }
  // A selected skin may override any subset of the same fields.
  if (!skinDirectory.empty()) {
    for (const char* name : {"respack.json", "skin.json"}) {
      const auto path = skinDirectory / name;
      std::ifstream input(path, std::ios::binary);
      if (!input) continue;
      std::ostringstream buffer;
      buffer << input.rdbuf();
      const auto text = buffer.str();
      if (text.empty()) continue;
      const auto root = nlohmann::json::parse(text, nullptr, false);
      if (root.is_discarded() || !root.is_object()) continue;
      applyResPackLayer(root);
      break;
    }
  }
}

void GameRenderer::Impl::applyResPackLayer(const nlohmann::json& root) {
  // Every field is optional: only the ones present in this layer change, so a skin can
  // override one value and inherit the rest from the built-in pack.
  if (const auto it = root.find("hitFx"); it != root.end() && it->is_array() && it->size() >= 2) {
    hitAtlas.columns = std::max(1, it->at(0).get<int>());
    hitAtlas.rows = std::max(1, it->at(1).get<int>());
  }
  if (const auto it = root.find("hitFxScale"); it != root.end() && it->is_number()) {
    hitStyle.sizeScale = it->get<float>();
  }
  if (const auto it = root.find("hitFxDuration"); it != root.end() && it->is_number()) {
    hitStyle.duration = it->get<float>();
  }
  if (const auto it = root.find("hitFxRotate"); it != root.end() && it->is_boolean()) {
    hitStyle.rotateSheet = it->get<bool>();
  }
  if (const auto it = root.find("hitFxTinted"); it != root.end() && it->is_boolean()) {
    hitStyle.tinted = it->get<bool>();
  }
  // The pack can turn the debris particles off entirely, under either spelling.
  if (const auto it = root.find("hideParticles"); it != root.end() && it->is_boolean()) {
    hitStyle.showParticles = !it->get<bool>();
  }
  if (const auto it = root.find("showParticles"); it != root.end() && it->is_boolean()) {
    hitStyle.showParticles = it->get<bool>();
  }

  // Hold slicing and body behaviour. Each array is one sheet's pair of slice sizes, in the
  // order the sheet stacks them: tail first, then head. The plain sheet feeds variant 0 and
  // the highlight sheet feeds variant 1.
  const auto readSlices = [&root](
                              const char* key, int& tailSlice, int& headSlice) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_array() || it->size() < 2) return;
    if ((*it)[0].is_number()) {
      tailSlice = std::max(0, static_cast<int>((*it)[0].get<double>()));
    }
    if ((*it)[1].is_number()) {
      headSlice = std::max(0, static_cast<int>((*it)[1].get<double>()));
    }
  };
  readSlices("holdAtlas", holdTailSlice[0], holdHeadSlice[0]);
  readSlices("holdAtlasMH", holdTailSlice[1], holdHeadSlice[1]);
  if (const auto it = root.find("holdRepeat"); it != root.end() && it->is_boolean()) {
    holdRepeat = it->get<bool>();
  }
  if (const auto it = root.find("holdKeepHead"); it != root.end() && it->is_boolean()) {
    holdKeepHead = it->get<bool>();
  }
  if (const auto it = root.find("holdCompact"); it != root.end() && it->is_boolean()) {
    holdCompact = it->get<bool>();
  }

  // Colours may be a three-component array plus a separate alpha, or four components.
  const auto readColour = [&root](const char* key, float& red, float& green, float& blue,
                              float& alpha) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_array() || it->size() < 3) return;
    red = it->at(0).get<float>();
    green = it->at(1).get<float>();
    blue = it->at(2).get<float>();
    if (it->size() >= 4) alpha = it->at(3).get<float>();
  };
  readColour("colorPerfect", hitSkin.perfectRed, hitSkin.perfectGreen, hitSkin.perfectBlue,
      hitSkin.perfectAlpha);
  readColour("colorGood", hitSkin.goodRed, hitSkin.goodGreen, hitSkin.goodBlue,
      hitSkin.goodAlpha);

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
  chain.prepare(assets, chart->prprShaderSources, chart->prprEffects);
  chain.setAuxTextures(effectGlitchMap, effectGlitchDogMap, effectWireNoise, effectDisplaceMap);
  scene::buildLineDrawOrder(*chart, lineDrawOrder);
}

scene::LineTint GameRenderer::Impl::lineTintFor(const RenderFrameInput& input,
    const game::JudgementEngine* judgement) const {
  scene::LineTint tint;
  if (!input.apfcIndicator || judgement == nullptr) return tint;
  const auto& counts = judgement->counts();
  const int good = counts[1];
  const int bad = counts[2];
  const int miss = counts[3];
  if (bad + miss > 0) {
    tint = {1.0F, 1.0F, 1.0F, 1.0F};
  } else if (good > 0) {
    // Full-combo indicator colour.
    tint = {162.0F / 255.0F, 238.0F / 255.0F, 1.0F, 1.0F};
  } else {
    // All-perfect indicator colour.
    tint = {254.0F / 255.0F, 1.0F, 169.0F / 255.0F, 1.0F};
  }
  return tint;
}

void GameRenderer::Impl::drawBackgroundPass(const scene::StageLayout& layout, float artworkAlpha,
    float dimAlpha) {
  const auto& stage = layout.stage;
  const auto projection = layout.projection();

  // Side columns first, so the play field paints over the inner edges.
  if (layout.hasSideBlanks) {
    const float leftWidth = std::max(0.0F, stage.x);
    const float rightX = std::min(layout.viewWidth, stage.right());
    const float rightWidth = std::max(0.0F, layout.viewWidth - rightX);
    const auto drawColumn = [&](float x, float columnWidth) {
      if (columnWidth <= 0.5F) return;
      batch.addQuadTopLeft(x, stage.y, columnWidth, stage.height, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
          0.0F, 1.0F, 1.0F);
      if (dimAlpha > 0.001F) {
        batch.addQuadTopLeft(x, stage.y, columnWidth, stage.height, 0.0F, 0.0F, 0.0F, dimAlpha, 0.0F,
            0.0F, 1.0F, 1.0F);
      }
      if (stage.y > 0.5F) {
        batch.addQuadTopLeft(x, 0.0F, columnWidth, stage.y, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            1.0F, 1.0F);
      }
      const float bottom = stage.bottom();
      if (bottom < layout.viewHeight - 0.5F) {
        batch.addQuadTopLeft(x, bottom, columnWidth, layout.viewHeight - bottom, 0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F, 1.0F, 1.0F);
      }
    };
    batch.begin();
    drawColumn(0.0F, leftWidth);
    drawColumn(rightX, rightWidth);
    batch.flush(projection, textures.white(), nullptr);
  }

  batch.begin();
  if (background == nullptr || artworkAlpha <= 0.001F) {
    batch.addQuadTopLeft(stage.x, stage.y, stage.width, stage.height, 0.0F, 0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F);
    if (dimAlpha > 0.001F) {
      batch.addQuadTopLeft(stage.x, stage.y, stage.width, stage.height, 0.0F, 0.0F, 0.0F, dimAlpha,
          0.0F, 0.0F, 1.0F, 1.0F);
    }
    batch.flush(projection, textures.white(), nullptr);
    return;
  }

  // Cover the play field while preserving the artwork aspect ratio.
  const float textureAspect = background->aspect();
  const float stageAspect = stage.width / std::max(1.0F, stage.height);
  float drawWidth = stage.width;
  float drawHeight = stage.height;
  if (textureAspect > stageAspect) {
    drawWidth = stage.height * textureAspect;
  } else {
    drawHeight = stage.width / textureAspect;
  }
  const float drawX = stage.x + (stage.width - drawWidth) * 0.5F;
  const float drawY = stage.y + (stage.height - drawHeight) * 0.5F;
  batch.addQuadTopLeft(drawX, drawY, drawWidth, drawHeight, 1.0F, 1.0F, 1.0F, artworkAlpha, 0.0F,
      0.0F, 1.0F, 1.0F);
  if (dimAlpha > 0.001F) {
    batch.addQuadTopLeft(stage.x, stage.y, stage.width, stage.height, 0.0F, 0.0F, 0.0F, dimAlpha,
        0.0F, 0.0F, 1.0F, 1.0F);
  }
  batch.flush(projection, *background, nullptr);
}

void GameRenderer::Impl::drawLinePass(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, const scene::StageLayout& layout) {
  const auto projection = layout.projection();
  const scene::LineTint indicatorTint = lineTintFor(input, judgement);
  const bool official = chart->formatVersion > 0;
  batch.begin();

  if (input.transition.inIntro && input.transition.introLineScale < 0.999F) {
    // The opening draws a single decorative line that grows from the centre.
    scene::drawIntroLine(batch, layout, official, input.transition.introLineScale, 1.0F,
        indicatorTint);
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

  for (const int lineIndex : lineDrawOrder) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(chart->judgeLineList.size())) continue;
    const auto& line = *chart->judgeLineList[static_cast<std::size_t>(lineIndex)];
    if (line.attachUiElementId != 0) continue;
    const auto& frame = lineFrames[static_cast<std::size_t>(lineIndex)];
    if (!frame.valid || frame.hideLine) continue;
    const float alpha = std::clamp(frame.alphaRaw, 0.0F, 1.0F);
    if (alpha <= 0.0F) continue;

    const float halfLength = official
        ? layout.stage.height * scene::LINE_LENGTH_OFFICIAL * frame.scaleX
        : layout.stage.width * scene::LINE_LENGTH_EDITOR * frame.scaleX;
    const float lineWidth = official
        ? layout.stage.height * scene::LINE_WIDTH_OFFICIAL * frame.scaleY
        : layout.stage.height * scene::LINE_WIDTH_EDITOR * frame.scaleY;

    scene::LineTint tint = indicatorTint;
    if (frame.hasColorEvent) {
      tint.red = frame.colorR;
      tint.green = frame.colorG;
      tint.blue = frame.colorB;
    }
    // A storyboard line with a texture draws the texture instead of the plain quad;
    // a line with text events draws its resolved text. Both keep the anchor and the
    // line rotation.
    if (!frame.hasTextEvents && !line.texture.empty()) {
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
    } else if (!frame.hasTextEvents && !frame.text.empty()) {
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
  const auto projection = layout.projection();

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
  // The mirror mode reflects touch input rather than the picture, so nothing here flips.
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
    gl::Texture* texture = noteHeads[judged.type][judged.morebets == 1 ? 1 : 0];
    if (texture == nullptr) continue;
    const int type = std::clamp(judged.type, 0, 4);
    const float widthScale = params.widthScaleFor(type, judged.morebets == 1);
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
  badEffects.draw(batch, projection, input.visualTimeSec, textures.white());
}

void GameRenderer::Impl::drawHitEffects(const scene::StageLayout& layout,
    const RenderFrameInput& input, game::Chart* chart) {
  if (chart == nullptr || hitFx == nullptr) return;
  const float noteWidth =
      layout.stage.width * noteWidthRatio(layout.stage.width, layout.stage.height) * input.keyScale;
  hitEffects.update(*chart, input.chartTimeSec, input.visualTimeSec, layout.stage.x,
      layout.stage.y, layout.stage.width, layout.stage.height, noteWidth,
      input.practice.enabled ? input.practice.speed : 1.0F);
  hitEffects.draw(batch, layout.projection(), *hitFx, textures.white(), input.visualTimeSec);
}

void GameRenderer::Impl::drawHudPass(const RenderFrameInput& input,
    const scene::StageLayout& layout) {
  scene::HudValues values;
  values.score = input.hud.score;
  values.combo = input.hud.combo;
  values.maxCombo = input.hud.maxCombo;
  values.accuracy = input.hud.accuracy;
  values.progress = input.hud.progress;
  values.songName = input.songName;
  values.levelName = input.levelName;
  values.showAccuracy = input.hud.showAccuracy;
  scene::HudColors colors;
  scene::HudTextures textures;
  textures.pause = hudArtwork.pause;
  textures.timerLine = hudArtwork.timerLine;
  scene::drawHud(batch, text, layout, values, colors, textures, input.hud.alpha,
      layout.projection());
}

GameRenderer::GameRenderer() : impl_(std::make_unique<Impl>()) {}

GameRenderer::~GameRenderer() = default;

bool GameRenderer::initialize(const std::filesystem::path& assetRoot) {
  impl_->assets = assetRoot;
  impl_->textures.setAssetRoot(assetRoot);
  if (!impl_->text.initialize(assetRoot)) {
    // A renderer that was not initialised draws nothing at all, which reads as an empty
    // scene on the device; the reason is logged instead of failing silently.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "[render] renderer initialization failed: text layer unavailable (asset root %s)",
        assetRoot.string().c_str());
    return false;
  }
  impl_->loadGameplayTextures();
  // Both shipped shaders are loaded here and stay ready for the whole session; a missing
  // asset simply leaves the composite unavailable and the inline silhouettes in charge.
  impl_->blockComposite.prepare(assetRoot, impl_->blockDisplaceMap, impl_->blockSparkMap,
      impl_->blockNoiseMap);
  bindDefaultBlend();
  impl_->initialized = true;
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[render] renderer initialized (asset root %s)",
      assetRoot.string().c_str());
  return true;
}

void GameRenderer::shutdown() noexcept {
  impl_->releaseResources();
  impl_->initialized = false;
}

void GameRenderer::resize(int width, int height) noexcept {
  impl_->width = std::max(1, width);
  impl_->height = std::max(1, height);
  glViewport(0, 0, impl_->width, impl_->height);
  impl_->sceneFbo.resize(impl_->width, impl_->height);
  impl_->pingFbo.resize(impl_->width, impl_->height);
  impl_->pongFbo.resize(impl_->width, impl_->height);
  impl_->blockComposite.resize(impl_->width, impl_->height);
}

void GameRenderer::Impl::drawLevelEffectObjects(game::Chart* chart,
    const RenderFrameInput& input, const scene::StageLayout& layout) {
  if (chart == nullptr || chart->levelEffects == nullptr) return;
  const auto& effect = *chart->levelEffects;
  const auto chartTime = static_cast<float>(input.chartTimeSec);
  const std::string& name = effect.effectName();

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
        kaleidoNormal = textures.loadAsset("official_textures/KaleidoNormal.png");
        kaleidoColour = textures.loadAsset("official_textures/KaleidoCol.png");
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
    dsGridTexture = textures.loadAsset("official_textures/DsGrid.png");
    if (dsGridTexture != nullptr) dsGridTexture->setWrapRepeat(true, true);
  }
  if (dsGridTexture == nullptr) return;

  // The pulse freezes at the level's freeze moment.
  const float freezeTime = effect.scalar("freezeTime", 3.4e38F);
  const bool frozen = chartTime >= freezeTime;
  dsGrid.draw(*dsGridTexture, chartTime, opacity, frozen, width, height);
}

void GameRenderer::Impl::drawScene(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement, unsigned int targetFbo, bool applyEffects) {
  const scene::StageLayout layout =
      scene::computeStageLayout(static_cast<float>(width), static_cast<float>(height),
          16.0F / 9.0F, false);

  // The screen-space composite resamples the finished frame inside a red zone, so it
  // needs that frame as a texture while it writes into the caller's target. Any frame
  // with a zone on screen therefore takes the offscreen route, with or without a
  // storyboard chain. Zones are culled on the cheap appear/disappear window first: the
  // pass is a full-screen resample and must not run on frames that show none.
  bool anyZoneVisible = false;
  if (chart != nullptr && !chart->blockAreaList.empty() && !input.transition.inIntro &&
      input.chartTimeSec >= 0.0) {
    for (const auto& zone : chart->blockAreaList) {
      if (static_cast<float>(input.chartTimeSec) >= zone.appearTime &&
          static_cast<float>(input.chartTimeSec) < zone.disappearTime) {
        anyZoneVisible = true;
        break;
      }
    }
  }
  const bool useBlockComposite = anyZoneVisible && blockComposite.ready() &&
      blockComposite.allocated() && sceneFbo.valid();

  // A chart with storyboard effects renders into an offscreen target first so the
  // shader chain can process the whole play field.
  const bool useEffects = applyEffects && chart != nullptr && !chain.empty() &&
      !chart->prprEffects.empty() && sceneFbo.valid();
  if (useEffects || useBlockComposite) {
    sceneFbo.bind();
    glViewport(0, 0, width, height);
  } else {
    // The scene draws into the caller's target, so its binding and viewport are set here
    // rather than assumed: a previous frame may have left another target bound.
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width, height);
  }

  clearTarget(0.0F, 0.0F, 0.0F, 1.0F);
  bindDefaultBlend();
  // Each pass is followed by an optional error check: with diagnostics off this is a
  // no-op, and with them on the log names the first pass that failed on the device.
  gl::logErrors("scene.begin");

  // The opening rides the decorative line's growth from INTRO_START_BRIGHTNESS down to
  // the gameplay transmission; the exit walks everything to the result page's level.
  const bool introStarted = !input.transition.inIntro;
  const float introMix = input.transition.inIntro
      ? scene::introDimProgress(input.transition.introLineScale,
            chart != nullptr && chart->formatVersion > 0)
      : 1.0F;
  const float artworkAlpha = scene::stageTextureAlpha(introStarted, introMix, introMix,
      input.backgroundDim, input.transition.inOutro, input.transition.outroElapsedSec);
  const float dimAlpha = scene::backgroundDimAlpha(introStarted, introMix, input.backgroundDim,
      input.transition.inOutro, input.transition.outroElapsedSec);
  drawBackgroundPass(layout, artworkAlpha, dimAlpha);
  gl::logErrors("scene.background");

  // A level effect may draw its own objects between the backdrop and the play field.
  drawLevelEffectObjects(chart, input, layout);
  gl::logErrors("scene.levelObjects");

  if (chart != nullptr) {
    lineFrames.clear();
    math::Rect worldBounds{layout.stage.x, layout.stage.y, layout.stage.width,
        layout.stage.height};
    scene::LinePassParams lineParams;
    lineParams.chartTimeSec = input.chartTimeSec;
    lineParams.officialFormat = chart->formatVersion > 0;
    const bool blockNotes = input.transition.inIntro || input.chartTimeSec < 0.0;
    scene::evaluateLines(*chart, layout, input.chartTimeSec, input.scrollSpeed, blockNotes, false,
        1.0F, worldBounds, layout.stage.width * 2.0F, lineParams, lineFrames);
    if (lineDrawOrder.size() != chart->judgeLineList.size()) {
      scene::buildLineDrawOrder(*chart, lineDrawOrder);
    }
    drawLinePass(input, chart, judgement, layout);
    gl::logErrors("scene.lines");
    if (!blockNotes) drawNotePasses(layout, input, chart);
    gl::logErrors("scene.notes");
    // Red zones sit over the play field but under the hit effects.
    if (!chart->blockAreaList.empty() && !input.transition.inIntro &&
        input.chartTimeSec >= 0.0) {
      // The one-shot fade needs a real frame delta; a paused or seeked frame must not
      // advance it, so the delta is clamped and derived from the wall clock.
      const double nowSec = std::chrono::duration<double>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
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
            layout.projection(), textures.white());
      }
      gl::logErrors("scene.blocks");
    }
    drawHitEffects(layout, input, chart);
    gl::logErrors("scene.hitEffects");
  }

  // Tracks whether the HUD already became part of the frame the composite resamples, so
  // it is not drawn a second time on top of the torn picture.
  bool hudInsideScene = false;
  if (useBlockComposite) {
    // 1) Make the scene target hold the finished play field: the storyboard chain result
    // when it ran, the raw play field otherwise. The composite samples this texture, so
    // it can never be the target the composite writes to.
    if (useEffects) {
      gl::Texture* result = chain.apply(chart->prprEffects, &sceneFbo.color(), pingFbo, pongFbo,
          input.chartTimeSec);
      if (result != nullptr && result->id() != sceneFbo.color().id()) {
        sceneFbo.bind();
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        fx::BlitPass::instance().ensure();
        fx::BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
        bindDefaultBlend();
      }
      gl::logErrors("scene.effectChain");
    }
    // 2) The HUD belongs to the finished frame: the composite tears it with the rest.
    if (input.hud.visible && chart != nullptr) {
      drawHudPass(input, layout);
      hudInsideScene = true;
      gl::logErrors("scene.hud");
    }
    // 3) Zone masks, coverage/edge/glow pre-passes, then replace the frame inside the
    // zones with the displaced resample. The composite restores the target and viewport
    // it found, so no binding fix-up is needed afterwards.
    std::vector<fx::BlockMaskQuad> maskA;
    std::vector<fx::BlockMaskQuad> maskB;
    blockOverlay.collectMaskQuads(chart->blockAreaList, static_cast<float>(input.chartTimeSec),
        maskA, maskB);
    blockComposite.rasteriseMasks(maskA, maskB);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width, height);
    blockComposite.setStageRect(layout.stage.x, layout.stage.y, layout.stage.right(),
        layout.stage.bottom());
    blockComposite.draw(sceneFbo.color(), static_cast<float>(input.chartTimeSec));
    bindDefaultBlend();
    gl::logErrors("scene.blockComposite");
  } else if (useEffects) {
    // Run the storyboard chain over the finished play field, then present the result.
    gl::Texture* result = chain.apply(chart->prprEffects, &sceneFbo.color(), pingFbo, pongFbo,
        input.chartTimeSec);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
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

  // The HUD is drawn after the chain, so a storyboard effect never blooms the score. On
  // the composite path it was drawn into the scene instead, because it is part of the
  // frame the zones tear.
  if (input.hud.visible && chart != nullptr && !hudInsideScene) drawHudPass(input, layout);
  gl::logErrors("scene.hud");

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
      alpha = scene::resumeBackdropAlpha(input.pause.elapsedSec);
      brightness = scene::resumeBackdropBrightness(input.pause.elapsedSec);
      flatten = scene::resumeBackdropFlatten(input.pause.elapsedSec);
    } else {
      alpha = scene::pauseBackdropIntroAlpha(input.pause.elapsedSec);
    }
    if (alpha > 0.001F) {
      // Freeze the play field without the HUD and without the storyboard chain: the
      // backdrop grades the raw scene, and re-running the chain would read and write the
      // same target.
      if (pauseBlurA.width() != width || pauseBlurA.height() != height) {
        pauseBlurA.resize(width, height);
        pauseBlurB.resize(width, height);
      }
      const bool hudWasVisible = input.hud.visible;
      RenderFrameInput frozen = input;
      frozen.hud.visible = false;
      frozen.pause.mode = PauseMode::None;
      pauseBlurA.bind();
      glViewport(0, 0, width, height);
      drawScene(frozen, chart, judgement, pauseBlurA.fbo(), false);
      glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
      glViewport(0, 0, width, height);
      // The capture target is the blur source, so the second scratch target is the
      // first destination.
      // The backdrop composites into the presented target, which differs between the
      // interactive path and the offscreen one.
      pauseBackdrop.draw(pauseBlurA.color(), pauseBlurB, pauseBlurA, targetFbo, alpha,
          scene::PAUSE_BACKDROP_VIBRANCY, brightness, flatten);
      bindDefaultBlend();
      if (hudWasVisible) drawHudPass(input, layout);
      gl::logErrors("scene.pauseBackdrop");
    }
  }
}

void GameRenderer::setSkinDirectory(const std::filesystem::path& skinDirectory) {
  if (impl_ == nullptr) return;
  // Reset the resolved material settings to their defaults before re-applying, so clearing
  // a skin really restores the built-in look instead of keeping the skin's overrides.
  impl_->hitAtlas = fx::HitEffectAtlas{};
  impl_->hitStyle = fx::HitEffectStyle{};
  impl_->hitSkin = fx::HitEffectSkin{};
  impl_->holdTailSlice[0] = 50;
  impl_->holdTailSlice[1] = 50;
  impl_->holdHeadSlice[0] = 52;
  impl_->holdHeadSlice[1] = 95;
  impl_->holdRepeat = false;
  impl_->holdKeepHead = false;
  impl_->holdCompact = false;
  impl_->skinDirectory = skinDirectory;
  impl_->applyResPackSettings();
  // The slice sizes may have changed, so the hold sheet has to be cut again.
  impl_->refreshHoldSheet();
}

void GameRenderer::render(const RenderFrameInput& input, game::Chart* chart,
    const game::JudgementEngine* judgement) {
  if (!impl_->initialized) return;
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
  impl_->pingFbo.resize(width, height);
  impl_->pongFbo.resize(width, height);
  impl_->blockComposite.resize(width, height);
  impl_->prepareForChart(chart);

  const scene::StageLayout layout =
      scene::computeStageLayout(static_cast<float>(width), static_cast<float>(height), 16.0F / 9.0F,
          false);
  // The opening rides the decorative line's growth from INTRO_START_BRIGHTNESS down to
  // the gameplay transmission; the exit walks everything to the result page's level.
  const bool introStarted = !input.transition.inIntro;
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

  const auto path = outputPng.string();
  return stbi_write_png(path.c_str(), width, height, 4, flipped.data(),
             static_cast<int>(rowBytes)) != 0;
}

} // namespace rechigros::render
