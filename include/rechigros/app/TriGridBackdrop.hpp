#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

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
  // Decodes the bundled textures on a worker thread. Touches no graphics state, so it can
  // run before the context exists and while the interface still initialises; the caller
  // only pays for the upload.
  void beginPrepare(const std::filesystem::path& assetRoot);
  // Uploads the decoded textures and compiles both programs on the calling thread. Returns
  // false while the worker is still decoding, so a frame loop can poll it once per frame.
  bool finishPrepare();
  // Releases every GL object. Requires a current context and is idempotent.
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }
  // True once preparation finished, either with a usable backdrop or with a failure that
  // should stop the frame loop from polling it again.
  [[nodiscard]] bool prepareSettled() const noexcept { return ready_ || prepareFailed_; }

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

  struct Image {
    std::vector<std::uint8_t> pixels;
    int width{};
    int height{};
    [[nodiscard]] bool valid() const {
      return width > 0 && height > 0 && !pixels.empty();
    }
  };

  static unsigned int buildProgram(const char* vertexSource, const char* fragmentSource);
  static bool loadTexture(const std::filesystem::path& assetRoot, const char* relativePath,
      Texture& texture);
  static bool uploadTexture(Image& image, Texture& texture);
  static Image decodeAsset(const std::filesystem::path& assetRoot, const char* relativePath);
  static void destroyTexture(Texture& texture) noexcept;
  bool createProgramsAndGeometry();
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
  bool prepareFailed_{};
  std::thread decodeThread_;
  std::mutex decodeMutex_;
  std::atomic<bool> decodeDone_{false};
  bool decodeStarted_{};
  Image decodedMask_;
  Image decodedBackground_;
};

} // namespace rechigros::app
