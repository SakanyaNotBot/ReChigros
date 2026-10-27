#pragma once

#include "rechigros/render/gl/Texture.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rechigros::render::asset {

// Decoded RGBA image, ready for upload. Kept around so a texture can be
// re-created after a context loss without touching the filesystem again.
struct Image {
  int width{};
  int height{};
  std::vector<std::uint8_t> pixels;
  [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0 && !pixels.empty(); }
};

// Decodes an image file (PNG/JPEG/GIF frame) into RGBA. Images are not flipped:
// row 0 is the top row, which matches the top-left UV convention used here.
[[nodiscard]] Image decodeImage(const std::filesystem::path& path);
[[nodiscard]] Image decodeImageMemory(const std::uint8_t* data, std::size_t size);
// Forces every texel to opaque white while keeping the alpha channel.
void forceWhite(Image& image);

// An animated image decoded from a GIF: one texture per frame plus the timing needed to
// pick the frame for a moment in time.
class AnimatedTexture final {
public:
  AnimatedTexture() = default;
  ~AnimatedTexture() = default;
  AnimatedTexture(const AnimatedTexture&) = delete;
  AnimatedTexture& operator=(const AnimatedTexture&) = delete;
  AnimatedTexture(AnimatedTexture&& other) noexcept = default;
  AnimatedTexture& operator=(AnimatedTexture&& other) noexcept = default;

  [[nodiscard]] bool valid() const noexcept { return !frames_.empty(); }
  [[nodiscard]] std::size_t frameCount() const noexcept { return frames_.size(); }
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  [[nodiscard]] gl::Texture& frameAt(std::size_t index) noexcept;
  [[nodiscard]] const gl::Texture& frameAt(std::size_t index) const noexcept;

  // Frame for a normal playback time, wrapping on the total duration.
  [[nodiscard]] std::size_t frameForTime(double elapsedSec) const noexcept;
  // Frame pinned by a storyboard GIF event, where `position` is progress in [0,1].
  [[nodiscard]] std::size_t frameForPosition(float position) const noexcept;

  void addFrame(gl::Texture texture, int delayMs);
  void clear() noexcept;

private:
  std::vector<gl::Texture> frames_;
  std::vector<int> delaysMs_;
  int totalMs_{};
};

// Loads texture files from disk and the packaged asset tree, caching by path.
class TextureCache final {
public:
  void setAssetRoot(std::filesystem::path root) { assetRoot_ = std::move(root); }
  [[nodiscard]] const std::filesystem::path& assetRoot() const noexcept { return assetRoot_; }

  // Texture from the packaged asset root, e.g. "res/click.png"; null when missing.
  gl::Texture* loadAsset(const std::string& relativePath, bool white = false);

  // Starts decoding the named assets on worker threads. Decoding is pure computation on bytes and
  // is the expensive half of a texture load, so it runs beside whatever the caller does next; the
  // upload still happens on the caller's thread, which owns the graphics context. A name that
  // fails stays uncached and loadAsset decodes it again.
  //
  // Split in two so the work can overlap a stage that does not load textures: begin, do that
  // stage, then await. Starting and waiting back to back is still correct, just not overlapped.
  void beginPrefetchAssets(const std::vector<std::string>& relativePaths);
  // Waits for a started prefetch. Every decoded image is then available to loadAsset, which only
  // has to upload it.
  void awaitPrefetchAssets();
  // Texture from an absolute path on disk.
  gl::Texture* loadFile(const std::filesystem::path& path, bool white = false);
  // Animated texture from a GIF on disk; null when the file is not a decodable GIF.
  AnimatedTexture* loadGifFile(const std::filesystem::path& path);
  // True when `path` names a GIF, decided by its extension.
  [[nodiscard]] static bool isGifPath(const std::filesystem::path& path);
  // Uploads raw RGBA pixels under a synthetic cache key.
  gl::Texture* loadPixels(const std::string& key, Image image);
  // Shared 1x1 white texture used by untextured geometry.
  gl::Texture& white();

  void clear() noexcept;

private:
  gl::Texture* store(const std::string& key, Image image, bool white);

  std::filesystem::path assetRoot_;
  // Decoded but not yet uploaded images, keyed the same way as the textures. Filled by
  // prefetchAssets so the startup load only has to upload.
  std::unordered_map<std::string, Image> decoded_;
  std::vector<std::thread> prefetchThreads_;
  std::mutex prefetchMutex_;
  std::unordered_map<std::string, gl::Texture> textures_;
  std::unordered_map<std::string, AnimatedTexture> animated_;
  gl::Texture white_;
};

} // namespace rechigros::render::asset
