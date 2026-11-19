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
#include <unordered_set>
#include <vector>

namespace rechigros::app {

// Loads images into OpenGL textures on demand and keeps the result.
//
// Decoding and downscaling run on a worker thread. The render thread transfers the pixels
// within a shared frame budget and publishes each texture when its upload is complete.
// Callers draw a placeholder while a request is pending.
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
  // Queues artwork without claiming visible-screen priority or spending the frame's uploads.
  void prefetch(const std::filesystem::path& image, int maxSize = 0);

  // Requests the settlement pixels on the decode worker. pump() uploads the finished image.
  void prefetchBlurredBackdrop(const std::filesystem::path& image);
  [[nodiscard]] bool blurredBackdropReady(const std::filesystem::path& image) const;
  // Returns a prepared settlement texture. A direct capture or a context reset can collect
  // an outstanding decode synchronously; gameplay prepares and uploads it during loading.
  [[nodiscard]] std::uint64_t blurredBackdropFor(const std::filesystem::path& image);

  // Releases every texture. A current GL context is required.
  void reset() noexcept;

  // Starts the frame's shared time, byte and completion allowances. Queries use the same
  // remaining allowances when their pixels finish decoding later in the frame.
  void pump();
  // True once a request has resolved, including a failed decode or upload.
  [[nodiscard]] bool textureReady(const std::filesystem::path& image, int maxSize = 0) const;

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
    std::uint64_t generation{};
    bool settlementBlur{};
  };
  // A decoded image on its way from the worker to the render thread.
  struct Decoded {
    std::string key;
    std::vector<std::uint8_t> pixels;
    int width{};
    int height{};
    bool failed{};
    std::uint64_t generation{};
  };
  struct Upload {
    Decoded decoded;
    std::uint64_t texture{};
    int nextRow{};
  };
  struct Size {
    int width{};
    int height{};
  };

  // Publishes only completed uploads; partial textures remain private to this cache.
  void publishUpload(const std::string& key, Upload& upload);
  bool advanceUpload(Upload& upload, bool blocking);
  void accountUploadTime(double elapsedMs);
  void retirePixels(std::vector<std::uint8_t>& pixels);
  // Decodes, scales and optionally blurs plain pixels without touching GL state.
  [[nodiscard]] static Decoded decode(const Pending& task);
  void enqueue(const std::filesystem::path& image, const std::string& key,
      int maxSize, bool settlementBlur, bool visible = false);
  // Uses the remaining allowance, taking a visible image first when its pixels are ready.
  void drainDecoded(const std::string& preferredKey = {});
  void startWorker();
  void stopWorker();

  std::unordered_map<std::string, Entry> entries_;
  std::unordered_map<std::uint64_t, Size> sizes_;
  std::unordered_map<std::string, Upload> uploads_;
  int uploadsRemaining_{};
  std::size_t uploadBytesRemaining_{};
  double uploadMsRemaining_{};

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable decodedReady_;
  std::deque<Pending> queue_;
  std::deque<Decoded> finished_;
  std::deque<std::vector<std::uint8_t>> retiredPixels_;
  // Protected with the task queues; keys remain present while a task is decoding.
  std::unordered_set<std::string> pendingKeys_;
  std::unordered_set<std::string> visibleKeys_;
  std::uint64_t generation_{};
  std::atomic<bool> stopping_{false};
};

} // namespace rechigros::app
