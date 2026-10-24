#pragma once

#include "rechigros/render/math/Mat4.hpp"

#include <cstdint>
#include <vector>

namespace rechigros::render::gl { class Texture; class ShaderProgram; }
namespace rechigros::render::math { struct Rect; }

namespace rechigros::render::batch {

// One instanced sprite record. The layout is uploaded verbatim, so it doubles as
// the interleaved vertex format consumed by the sprite shader.
struct SpriteInstance {
  float centerX{};
  float centerY{};
  float width{};
  float height{};
  float rotationCos{1.0F};
  float rotationSin{0.0F};
  float u0{0.0F};
  float v0{0.0F};
  float u1{1.0F};
  float v1{1.0F};
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
  // 0 selects the primary sampler, 1 the body sampler.
  float textureSelect{0.0F};
};
static_assert(sizeof(SpriteInstance) == 15 * sizeof(float), "instance layout must stay packed");

// Upper bound on sprites buffered between two flushes.
inline constexpr int MAX_BATCH_SPRITES = 131072;

// Accumulates sprites in screen space and submits them as one instanced draw.
class SpriteBatch final {
public:
  SpriteBatch();
  ~SpriteBatch();
  SpriteBatch(const SpriteBatch&) = delete;
  SpriteBatch& operator=(const SpriteBatch&) = delete;

  void begin() noexcept;
  [[nodiscard]] int size() const noexcept { return count_; }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  // Core primitive: an axis-aligned-in-local-space quad rotated about its centre.
  // Returns false when the batch is full and the caller must flush.
  bool addQuad(float centerX, float centerY, float width, float height, float rotationDeg,
      float red, float green, float blue, float alpha,
      float u0, float v0, float u1, float v1);

  // Same as addQuad but with an explicit body-sampler selector.
  bool addQuadSelect(float centerX, float centerY, float width, float height, float rotationDeg,
      float red, float green, float blue, float alpha,
      float u0, float v0, float u1, float v1, float textureSelect);

  // Quad anchored by a point inside it: (0.5,0.5) is the centre, (0,0) the top-left.
  bool addQuadAnchored(float x, float y, float width, float height, float rotationDeg,
      float red, float green, float blue, float alpha,
      float anchorX, float anchorY,
      float u0, float v0, float u1, float v1);

  // Axis-aligned rectangle addressed by its top-left corner.
  bool addQuadTopLeft(float x, float y, float width, float height,
      float red, float green, float blue, float alpha,
      float u0, float v0, float u1, float v1);

  // Thick segment as a rotated quad of the requested width.
  bool addLine(float x0, float y0, float x1, float y1, float width,
      float red, float green, float blue, float alpha);

  // Clips incoming sprites to a rectangle in the same space as the geometry.
  void setClip(bool enabled, const math::Rect& rect) noexcept;
  [[nodiscard]] bool clipEnabled() const noexcept { return clipEnabled_; }

  // Confines the submitted draw to a rectangle of the current render target, in the same
  // pixel space the geometry is projected into. Unlike setClip, which drops whole sprites,
  // this clips partially covered ones, which is what confines the play field's overflow to
  // the stage. A disabled scissor restores the whole target.
  void setScissor(bool enabled, float left, float top, float right, float bottom) noexcept;
  [[nodiscard]] bool scissorEnabled() const noexcept { return scissorEnabled_; }

  // Uploads and draws everything accumulated, then clears. The body sampler falls
  // back to the primary texture when none is supplied.
  void flush(const math::Mat4& projection, const gl::Texture& texture,
      const gl::Texture* bodyTexture);

  // Discards buffered sprites without drawing.
  void discard() noexcept;

  [[nodiscard]] bool ready() const noexcept { return program_ != nullptr; }
  // Compiles the shared program; safe to call repeatedly.
  bool ensureProgram() noexcept;
  void release() noexcept;

private:
  bool ensureBuffers() noexcept;

  SpriteInstance* buffer_{};
  int count_{};
  unsigned int vao_{};
  unsigned int quadVbo_{};
  unsigned int instanceVbo_{};
  gl::ShaderProgram* program_{};
  bool clipEnabled_{};
  bool allocFailure_{};
  float clipLeft_{}, clipTop_{}, clipRight_{}, clipBottom_{};
  math::Rect clipRect_{};
  // Pixel-space scissor applied while the batch is submitted. The rectangle is stored in the
  // top-left origin space the geometry uses and converted to GL's bottom-left origin on use.
  bool scissorEnabled_{};
  float scissorLeft_{}, scissorTop_{}, scissorRight_{}, scissorBottom_{};
};

} // namespace rechigros::render::batch
