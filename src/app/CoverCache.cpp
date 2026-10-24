#include "rechigros/app/CoverCache.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace rechigros::app {
namespace {

// Images uploaded to the GPU per frame. Decoding happens off-thread, but the upload makes
// the driver allocate storage for the texture, and a screen full of artwork would otherwise
// pay for all of it on its first frame.
constexpr int kUploadsPerFrame = 2;

std::uint64_t createTexture(int width, int height, const unsigned char* pixels) {
  if (width <= 0 || height <= 0 || pixels == nullptr) return 0;
  // A screen may be built before a graphics context exists; report the missing texture
  // instead of calling through a null entry point.
  if (!rechigros::render::gl::contextAvailable()) return 0;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (texture == 0) return 0;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

std::vector<unsigned char> downscale(const unsigned char* pixels, int width, int height,
    int maxSize, int& outWidth, int& outHeight) {
  outWidth = width;
  outHeight = height;
  if (maxSize <= 0 || std::max(width, height) <= maxSize) return {};
  const float scale = static_cast<float>(maxSize) / static_cast<float>(std::max(width, height));
  outWidth = std::max(1, static_cast<int>(static_cast<float>(width) * scale));
  outHeight = std::max(1, static_cast<int>(static_cast<float>(height) * scale));
  std::vector<unsigned char> result(static_cast<std::size_t>(outWidth) * outHeight * 4);
  for (int y = 0; y < outHeight; ++y) {
    const int sourceY = std::min(height - 1, y * height / outHeight);
    for (int x = 0; x < outWidth; ++x) {
      const int sourceX = std::min(width - 1, x * width / outWidth);
      const auto* source = pixels + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4;
      auto* target = result.data() + (static_cast<std::size_t>(y) * outWidth + x) * 4;
      target[0] = source[0];
      target[1] = source[1];
      target[2] = source[2];
      target[3] = source[3];
    }
  }
  return result;
}

// Bilinear downscale, used by the settlement backdrop so the small copy matches the
// reference's filtered scaling (the nearest sampler would leave block edges that the
// following blur cannot fully remove).
std::vector<unsigned char> downscaleBilinear(const unsigned char* pixels, int width, int height,
    int targetWidth, int targetHeight) {
  std::vector<unsigned char> result(
      static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4U);
  if (pixels == nullptr || width <= 0 || height <= 0 || targetWidth <= 0 || targetHeight <= 0) {
    return result;
  }
  const float scaleX = static_cast<float>(width) / static_cast<float>(targetWidth);
  const float scaleY = static_cast<float>(height) / static_cast<float>(targetHeight);
  for (int y = 0; y < targetHeight; ++y) {
    const float sourceY = (static_cast<float>(y) + 0.5F) * scaleY - 0.5F;
    const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, height - 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const float fy = std::clamp(sourceY - static_cast<float>(y0), 0.0F, 1.0F);
    for (int x = 0; x < targetWidth; ++x) {
      const float sourceX = (static_cast<float>(x) + 0.5F) * scaleX - 0.5F;
      const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, width - 1);
      const int x1 = std::min(width - 1, x0 + 1);
      const float fx = std::clamp(sourceX - static_cast<float>(x0), 0.0F, 1.0F);
      const auto* p00 = pixels + (static_cast<std::size_t>(y0) * width + x0) * 4U;
      const auto* p10 = pixels + (static_cast<std::size_t>(y0) * width + x1) * 4U;
      const auto* p01 = pixels + (static_cast<std::size_t>(y1) * width + x0) * 4U;
      const auto* p11 = pixels + (static_cast<std::size_t>(y1) * width + x1) * 4U;
      auto* target = result.data() + (static_cast<std::size_t>(y) * targetWidth + x) * 4U;
      for (int channel = 0; channel < 4; ++channel) {
        const float top = static_cast<float>(p00[channel]) * (1.0F - fx) +
            static_cast<float>(p10[channel]) * fx;
        const float bottom = static_cast<float>(p01[channel]) * (1.0F - fx) +
            static_cast<float>(p11[channel]) * fx;
        target[channel] = static_cast<unsigned char>(
            std::clamp(top * (1.0F - fy) + bottom * fy, 0.0F, 255.0F) + 0.5F);
      }
    }
  }
  return result;
}

// One separable box-blur pass (axis 0 horizontal, 1 vertical) with clamped edges. The
// settlement reference runs three of each over the downscaled artwork.
std::vector<unsigned char> boxBlurRgba(const std::vector<unsigned char>& source, int width,
    int height, int radius, int axis) {
  if (radius <= 0 || width <= 0 || height <= 0
      || source.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
    return source;
  }
  std::vector<unsigned char> result(source.size());
  const int window = radius * 2 + 1;
  const int lines = axis == 0 ? height : width;
  const int length = axis == 0 ? width : height;
  const auto sourceAt = [&](int line, int index) {
    return (axis == 0 ? static_cast<std::size_t>(line) * width + index
                      : static_cast<std::size_t>(index) * width + line) * 4U;
  };
  const auto resultAt = [&](int line, int index) {
    return (axis == 0 ? static_cast<std::size_t>(line) * width + index
                      : static_cast<std::size_t>(index) * width + line) * 4U;
  };
  for (int line = 0; line < lines; ++line) {
    int sums[4] = {0, 0, 0, 0};
    for (int offset = -radius; offset <= radius; ++offset) {
      const auto index = std::clamp(offset, 0, length - 1);
      const auto sourceIndex = sourceAt(line, index);
      for (int channel = 0; channel < 4; ++channel) sums[channel] += source[sourceIndex + channel];
    }
    for (int position = 0; position < length; ++position) {
      const auto targetIndex = resultAt(line, position);
      for (int channel = 0; channel < 4; ++channel) {
        result[targetIndex + channel] = static_cast<unsigned char>(sums[channel] / window);
      }
      const int removed = std::clamp(position - radius, 0, length - 1);
      const int added = std::clamp(position + radius + 1, 0, length - 1);
      const auto removedIndex = sourceAt(line, removed);
      const auto addedIndex = sourceAt(line, added);
      for (int channel = 0; channel < 4; ++channel) {
        sums[channel] += static_cast<int>(source[addedIndex + channel]) -
            static_cast<int>(source[removedIndex + channel]);
      }
    }
  }
  return result;
}

} // namespace

TextureCache::TextureCache() { startWorker(); }

TextureCache::~TextureCache() {
  stopWorker();
  reset();
}

void TextureCache::startWorker() {
  if (worker_.joinable()) return;
  worker_ = std::thread([this] {
    for (;;) {
      Pending task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
        if (stopping_.load() && queue_.empty()) return;
        task = std::move(queue_.front());
        queue_.pop_front();
      }
      // Decoding and downscaling happen here, off the render thread; only the finished
      // pixels cross back.
      Decoded decoded;
      decoded.key = task.key;
      int width = 0;
      int height = 0;
      int channels = 0;
      const auto bytes = platform::AssetManager::readBinary(platform::assetRoot(),
          task.path.lexically_relative(platform::assetRoot()).generic_string());
      unsigned char* pixels = bytes.empty()
          ? stbi_load(task.path.string().c_str(), &width, &height, &channels, 4)
          : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                &channels, 4);
      if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) stbi_image_free(pixels);
        decoded.failed = true;
      } else {
        int finalWidth = width;
        int finalHeight = height;
        const auto scaled = downscale(pixels, width, height, task.maxSize, finalWidth,
            finalHeight);
        if (scaled.empty()) {
          decoded.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4U);
          decoded.width = width;
          decoded.height = height;
        } else {
          decoded.pixels = scaled;
          decoded.width = finalWidth;
          decoded.height = finalHeight;
        }
        stbi_image_free(pixels);
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.push_back(std::move(decoded));
      }
    }
  });
}

void TextureCache::stopWorker() {
  if (!worker_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_.store(true);
  }
  wake_.notify_all();
  worker_.join();
  stopping_.store(false);
}

std::uint64_t TextureCache::textureFor(const std::filesystem::path& image, int maxSize) {
  if (image.empty()) return 0;
  // Upload whatever the worker finished first, so an image becomes visible on the frame it
  // is queried rather than one frame later.
  drainDecoded();
  const std::string key = image.generic_string() + "#" + std::to_string(maxSize);
  const auto iterator = entries_.find(key);
  if (iterator != entries_.end()) return iterator->second.failed ? 0 : iterator->second.texture;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& queued : queue_) {
      if (queued.key == key) return 0;
    }
    for (const auto& done : finished_) {
      if (done.key == key) return 0;
    }
    queue_.push_back(Pending{key, image, maxSize});
  }
  wake_.notify_one();
  return 0;
}

int TextureCache::width(std::uint64_t texture) const {
  const auto iterator = sizes_.find(texture);
  return iterator == sizes_.end() ? 0 : iterator->second.width;
}

int TextureCache::height(std::uint64_t texture) const {
  const auto iterator = sizes_.find(texture);
  return iterator == sizes_.end() ? 0 : iterator->second.height;
}

std::uint64_t TextureCache::upload(const Decoded& decoded) {
  Entry entry;
  if (!decoded.failed && decoded.width > 0 && decoded.height > 0 && !decoded.pixels.empty()) {
    entry.texture = createTexture(decoded.width, decoded.height, decoded.pixels.data());
    entry.width = decoded.width;
    entry.height = decoded.height;
  }
  entry.failed = entry.texture == 0;
  if (!entry.failed) sizes_[entry.texture] = Size{entry.width, entry.height};
  const std::uint64_t result = entry.failed ? 0 : entry.texture;
  entries_[decoded.key] = entry;
  return result;
}

void TextureCache::drainDecoded() {
  std::deque<Decoded> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ready.swap(finished_);
  }
  // Upload a bounded number per frame. The decode cost is already paid off the render
  // thread, but the upload itself is not free: a texture that large makes the driver
  // allocate storage and can stall the first frame that touches it. Spreading the uploads
  // over a few frames keeps every frame short and fills the screen within a handful of
  // frames, which is what the placeholder path is for.
  int uploaded = 0;
  while (!ready.empty() && uploaded < kUploadsPerFrame) {
    (void)upload(ready.front());
    ready.pop_front();
    ++uploaded;
  }
  if (!ready.empty()) {
    // Whatever did not fit goes back to the front of the finished queue, ahead of anything
    // the worker produced in the meantime, so the order stays oldest-first.
    std::lock_guard<std::mutex> lock(mutex_);
    finished_.insert(finished_.begin(), ready.begin(), ready.end());
  }
}

std::uint64_t TextureCache::blurredBackdropFor(const std::filesystem::path& image) {
  if (image.empty()) return 0;
  // The settlement backdrop is the reference result page blur: a third-size copy, three
  // separable box passes of radius ~5% of the artwork width, then bilinear upsampling.
  const std::string key = image.generic_string() + "#settlement-blur";
  if (const auto iterator = entries_.find(key); iterator != entries_.end()) {
    return iterator->second.failed ? 0 : iterator->second.texture;
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  const auto bytes = platform::AssetManager::readBinary(platform::assetRoot(),
      image.lexically_relative(platform::assetRoot()).generic_string());
  unsigned char* pixels = bytes.empty()
      ? stbi_load(image.string().c_str(), &width, &height, &channels, 4)
      : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
            &channels, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    Entry entry;
    entry.failed = true;
    entries_[key] = entry;
    if (pixels != nullptr) stbi_image_free(pixels);
    return 0;
  }
  const int smallWidth = std::max(1, width / 3);
  const int smallHeight = std::max(1, height / 3);
  auto small = downscaleBilinear(pixels, width, height, smallWidth, smallHeight);
  stbi_image_free(pixels);
  const int radius = std::max(4,
      static_cast<int>(std::lround(static_cast<double>(width) * 0.05 / 3.0)));
  for (int pass = 0; pass < 3; ++pass) {
    small = boxBlurRgba(small, smallWidth, smallHeight, radius, 0);
    small = boxBlurRgba(small, smallWidth, smallHeight, radius, 1);
  }
  const std::uint64_t texture = createTexture(smallWidth, smallHeight, small.data());
  Entry entry;
  entry.texture = texture;
  entry.width = smallWidth;
  entry.height = smallHeight;
  entry.failed = texture == 0;
  entries_[key] = entry;
  if (texture != 0) sizes_[texture] = Size{smallWidth, smallHeight};
  return texture;
}

void TextureCache::reset() noexcept {
  for (const auto& [key, entry] : entries_) {
    (void)key;
    if (entry.texture != 0) {
      const auto handle = static_cast<GLuint>(entry.texture);
      glDeleteTextures(1, &handle);
    }
  }
  entries_.clear();
  // Anything still queued or decoded but not uploaded belongs to the old context and is
  // dropped; a later query re-requests it.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    finished_.clear();
  }
  sizes_.clear();
}

} // namespace rechigros::app
