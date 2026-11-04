#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rechigros::app {

// Loads images into OpenGL textures on demand and keeps the result.
//
// Decoding runs on a worker thread. Decoding is the expensive part and touches no GL state,
// so doing it on the render thread stalls the frame that first shows a screen full of
// artwork; the worker decodes and downscales into plain memory and the render thread only
// uploads the finished pixels. A queued image reports a zero texture until its pixels are
// ready, so a caller draws its placeholder and picks the real texture up a frame or two later.
//
// Requires a current GL context for the uploads; call reset() before the context is destroyed.
class TextureCache final {
public:
  TextureCache();
  ~TextureCache();
  TextureCache(const TextureCache&) = delete;
  TextureCache& operator=(const TextureCache&) = delete;

  // Returns an ImGui-compatible texture id, or 0 while the image is still
  // decoding or when it cannot be decoded. `maxSize` > 0 downscales the image so
  // its longest edge fits that many pixels.
  [[nodiscard]] std::uint64_t textureFor(const std::filesystem::path& image, int maxSize = 0);

  // Returns an ImGui-compatible texture holding the settlement-page backdrop: the image is
  // decoded, downscaled to a third of its size, blurred with three separable box passes of
  // radius ~5% of the artwork width and uploaded so the view upsamples it with bilinear
  // filtering. This is the reference result page's background blur and is cached. It runs on
  // the calling thread because it is requested once, during the exit transition, where a
  // one-off cost is hidden.
  [[nodiscard]] std::uint64_t blurredBackdropFor(const std::filesystem::path& image);

  // Releases every texture. A current GL context is required.
  void reset() noexcept;

  // Uploads the images the worker has finished, a bounded number per call. The frame loop
  // calls this once per frame so a prewarmed image becomes visible before the screen that
  // needs it is opened, even while another screen is drawing.
  void pump();

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
  // A decoded image on its way from the worker to the render thread.
  struct Decoded {
    std::string key;
    std::vector<std::uint8_t> pixels;
    int width{};
    int height{};
    bool failed{};
  };
  struct Size {
    int width{};
    int height{};
  };

  // Uploads finished pixels and records the entry.
  [[nodiscard]] std::uint64_t upload(const Decoded& decoded);
  // Decodes on the calling thread; used only where a caller cannot wait a frame.
  [[nodiscard]] std::uint64_t loadBlocking(const std::string& key,
      const std::filesystem::path& path, int maxSize, Size& size);
  // Moves up to kUploadsPerFrame finished images onto the GPU.
  void drainDecoded();
  void startWorker();
  void stopWorker();

  std::unordered_map<std::string, Entry> entries_;
  std::unordered_map<std::uint64_t, Size> sizes_;
  std::vector<Pending> pending_;

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Pending> queue_;
  std::deque<Decoded> finished_;
  std::atomic<bool> stopping_{false};
};

} // namespace rechigros::app
