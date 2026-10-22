#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::app {

// Loads images into OpenGL textures on demand and keeps the decoded result.
// Loading is throttled to a couple of images per frame and takes long paths
// through a pending queue, so a large library never stalls the first frame.
// Requires a current GL context; call reset() before the context is destroyed.
class TextureCache final {
public:
  TextureCache() = default;
  ~TextureCache();
  TextureCache(const TextureCache&) = delete;
  TextureCache& operator=(const TextureCache&) = delete;

  // Returns an ImGui-compatible texture id, or 0 while the image is still
  // queued or when it cannot be decoded. `maxSize` > 0 downscales the image so
  // its longest edge fits that many pixels.
  [[nodiscard]] std::uint64_t textureFor(const std::filesystem::path& image, int maxSize = 0);

  // Returns an ImGui-compatible texture holding the settlement-page backdrop: the image is
  // decoded, downscaled to a third of its size, blurred with three separable box passes of
  // radius ~5% of the artwork width and uploaded so the view upsamples it with bilinear
  // filtering. This is the reference result page's background blur and is cached.
  [[nodiscard]] std::uint64_t blurredBackdropFor(const std::filesystem::path& image);

  // Releases every texture. A current GL context is required.
  void reset() noexcept;

  [[nodiscard]] int width(std::uint64_t texture) const;
  [[nodiscard]] int height(std::uint64_t texture) const;

private:
  struct Entry {
    std::uint64_t texture{};
    int width{};
    int height{};
    bool failed{};
  };
  struct Pending {
    std::string key;
    std::filesystem::path path;
    int maxSize{};
  };
  struct Size {
    int width{};
    int height{};
  };

  [[nodiscard]] std::uint64_t load(const std::string& key, const std::filesystem::path& path,
      int maxSize, Size& size);
  void pump();

  std::unordered_map<std::string, Entry> entries_;
  std::unordered_map<std::uint64_t, Size> sizes_;
  std::vector<Pending> pending_;
};

} // namespace rechigros::app
