#pragma once

#include <cstdint>
#include <filesystem>

namespace rechigros::app {

// Menu backdrop rendered by the GPU. The first pass paints a blurred artwork
// cover-fit to the viewport; the second pass overlays a triangle grid sampled
// from a mask texture with a bend warp, a moving band, a vignette and
// scanlines. Both passes follow fixed formulas, so every aspect ratio is
// handled by the shaders themselves.
class TriGridBackdrop final {
public:
  TriGridBackdrop() = default;
  ~TriGridBackdrop();
  TriGridBackdrop(const TriGridBackdrop&) = delete;
  TriGridBackdrop& operator=(const TriGridBackdrop&) = delete;

  // Compiles both programs and uploads the bundled textures. A current OpenGL
  // context is required; returns false when any resource is unavailable.
  bool prepare(const std::filesystem::path& assetRoot);
  // Releases every GL object. Requires a current context and is idempotent.
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Draws the backdrop into the bound framebuffer, covering the whole viewport.
  // A non-zero `sourceTexture` replaces the bundled artwork (the cover fit and
  // breath/vignette/scanline terms stay the same). GLES cannot query a texture's
  // dimensions, so the caller passes them for the replacement texture.
  void render(int width, int height, double timeSec, unsigned int sourceTexture = 0,
      bool hasSource = false, int sourceWidth = 0, int sourceHeight = 0);

private:
  struct Texture {
    unsigned int id{};
    int width{1};
    int height{1};
  };

  static unsigned int buildProgram(const char* vertexSource, const char* fragmentSource);
  static bool loadTexture(const std::filesystem::path& assetRoot, const char* relativePath,
      Texture& texture);
  static void destroyTexture(Texture& texture) noexcept;
  void drawQuad() const;

  unsigned int gridProgram_{};
  unsigned int backgroundProgram_{};
  unsigned int vertexArray_{};
  unsigned int vertexBuffer_{};
  int gridBaseLocation_{-1};
  int gridResolutionLocation_{-1};
  int gridTimeLocation_{-1};
  int backgroundTextureLocation_{-1};
  int backgroundResolutionLocation_{-1};
  int backgroundSizeLocation_{-1};
  int backgroundTimeLocation_{-1};
  Texture triGridMask_;
  Texture blurredBackground_;
  bool ready_{};
};

} // namespace rechigros::app
