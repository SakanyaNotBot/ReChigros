#include "rechigros/app/CoverCache.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/asset/ImageCache.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace rechigros::app {
namespace {

// Work is shared by the frame pump and visible queries. A transfer is split into short row
// runs and its wall cost is charged immediately, so queries cannot restart the allowance.
constexpr int kUploadsPerFrame = 2;
constexpr double kUploadBudgetMs = 1.5;
constexpr std::size_t kUploadBytesPerFrame = 4U * 1024U * 1024U;
constexpr std::size_t kUploadBytesPerChunk = 64U * 1024U;
constexpr int kUploadRowsPerChunk = 8;

std::uint64_t allocateTexture(int width, int height) {
  if (width <= 0 || height <= 0 || !rechigros::render::gl::contextAvailable()) return 0;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (texture == 0) return 0;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef RECHIGROS_GLES
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
#else
  using Storage = void (APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  const auto storage = SDL_GL_GetCurrentContext() == nullptr ? nullptr
      : reinterpret_cast<Storage>(SDL_GL_GetProcAddress("glTexStorage2D"));
  if (storage != nullptr) {
    storage(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
#endif
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

TextureCache::Decoded TextureCache::decode(const Pending& task) {
  Decoded decoded;
  decoded.key = task.key;
  decoded.generation = task.generation;
  const auto root = platform::assetRoot();
  const auto bytes = platform::AssetManager::readBinary(root,
      task.path.lexically_relative(root).generic_string());
  auto image = bytes.empty() ? rechigros::render::asset::decodeImage(task.path)
                            : rechigros::render::asset::decodeImageMemory(bytes.data(), bytes.size());
  if (!image.valid()) {
    decoded.failed = true;
    return decoded;
  }
  if (task.settlementBlur) {
    decoded.width = std::max(1, image.width / 3);
    decoded.height = std::max(1, image.height / 3);
    auto small = downscaleBilinear(image.pixels.data(), image.width, image.height,
        decoded.width, decoded.height);
    const int radius = std::max(4,
        static_cast<int>(std::lround(static_cast<double>(image.width) * 0.05 / 3.0)));
    for (int pass = 0; pass < 3; ++pass) {
      small = boxBlurRgba(small, decoded.width, decoded.height, radius, 0);
      small = boxBlurRgba(small, decoded.width, decoded.height, radius, 1);
    }
    decoded.pixels = std::move(small);
  } else {
    decoded.width = image.width;
    decoded.height = image.height;
    auto scaled = downscale(image.pixels.data(), image.width, image.height, task.maxSize,
        decoded.width, decoded.height);
    decoded.pixels = scaled.empty() ? std::move(image.pixels) : std::move(scaled);
  }
  return decoded;
}

void TextureCache::startWorker() {
  if (worker_.joinable()) return;
  worker_ = std::thread([this] {
    for (;;) {
      Pending task;
      std::deque<std::vector<std::uint8_t>> retired;
      bool haveTask = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] {
          return stopping_.load() || !queue_.empty() || !retiredPixels_.empty();
        });
        retired.swap(retiredPixels_);
        if (!stopping_.load() && !queue_.empty()) {
          task = std::move(queue_.front());
          queue_.pop_front();
          haveTask = true;
        }
      }
      // Large decoded buffers are released here, away from the frame that publishes a texture.
      retired.clear();
      if (stopping_.load()) return;
      if (!haveTask) continue;
      Decoded decoded = decode(task);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (task.generation == generation_) finished_.push_back(std::move(decoded));
      }
      decodedReady_.notify_all();
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

void TextureCache::enqueue(const std::filesystem::path& image, const std::string& key,
    int maxSize, bool settlementBlur, bool visible) {
  if (image.empty() || entries_.contains(key)) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (visible) visibleKeys_.insert(key);
    if (pendingKeys_.insert(key).second) {
      Pending task{key, image, maxSize, generation_, settlementBlur};
      if (visible) queue_.push_front(std::move(task));
      else queue_.push_back(std::move(task));
    } else if (visible) {
      const auto queued = std::find_if(queue_.begin(), queue_.end(),
          [&](const Pending& task) { return task.key == key; });
      if (queued != queue_.end() && queued != queue_.begin()) {
        Pending task = std::move(*queued);
        queue_.erase(queued);
        queue_.push_front(std::move(task));
      }
    }
  }
  wake_.notify_one();
}

void TextureCache::prefetch(const std::filesystem::path& image, int maxSize) {
  if (image.empty()) return;
  enqueue(image, image.generic_string() + "#" + std::to_string(maxSize), maxSize, false);
}

bool TextureCache::textureReady(const std::filesystem::path& image, int maxSize) const {
  return image.empty() || entries_.contains(image.generic_string() + "#" + std::to_string(maxSize));
}

std::uint64_t TextureCache::textureFor(const std::filesystem::path& image, int maxSize) {
  if (image.empty()) return 0;
  const std::string key = image.generic_string() + "#" + std::to_string(maxSize);
  if (const auto found = entries_.find(key); found != entries_.end()) {
    return found->second.failed ? 0 : found->second.texture;
  }
  enqueue(image, key, maxSize, false, true);
  drainDecoded(key);
  const auto found = entries_.find(key);
  return found == entries_.end() || found->second.failed ? 0 : found->second.texture;
}

int TextureCache::width(std::uint64_t texture) const {
  const auto iterator = sizes_.find(texture);
  return iterator == sizes_.end() ? 0 : iterator->second.width;
}

int TextureCache::height(std::uint64_t texture) const {
  const auto iterator = sizes_.find(texture);
  return iterator == sizes_.end() ? 0 : iterator->second.height;
}

void TextureCache::accountUploadTime(double elapsedMs) {
  uploadMsRemaining_ = std::max(0.0, uploadMsRemaining_ - elapsedMs);
}

void TextureCache::retirePixels(std::vector<std::uint8_t>& pixels) {
  if (pixels.empty()) return;
  if (stopping_.load()) {
    std::vector<std::uint8_t>{}.swap(pixels);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    retiredPixels_.push_back(std::move(pixels));
  }
  wake_.notify_one();
}

bool TextureCache::advanceUpload(Upload& upload, bool blocking) {
  auto& decoded = upload.decoded;
  const auto rowBytes = static_cast<std::size_t>(std::max(0, decoded.width)) * 4U;
  const bool valid = !decoded.failed && decoded.width > 0 && decoded.height > 0
      && rowBytes <= std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(decoded.height)
      && decoded.pixels.size() == rowBytes * static_cast<std::size_t>(decoded.height);
  if (!valid) {
    decoded.failed = true;
    return true;
  }
  const auto hasBudget = [&] {
    return blocking || (uploadsRemaining_ > 0 && uploadMsRemaining_ > 0.0
        && uploadBytesRemaining_ >= rowBytes);
  };
  if (!hasBudget()) return false;
  if (upload.texture == 0) {
    const auto started = std::chrono::steady_clock::now();
    upload.texture = allocateTexture(decoded.width, decoded.height);
    accountUploadTime(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count());
    if (upload.texture == 0) {
      decoded.failed = true;
      return true;
    }
  }
  while (upload.nextRow < decoded.height && hasBudget()) {
    int rows = std::min(kUploadRowsPerChunk, decoded.height - upload.nextRow);
    rows = std::min(rows, static_cast<int>(std::max<std::size_t>(1, kUploadBytesPerChunk / rowBytes)));
    if (!blocking) {
      rows = std::min(rows, static_cast<int>(uploadBytesRemaining_ / rowBytes));
    }
    const auto bytes = static_cast<std::size_t>(rows) * rowBytes;
    const auto started = std::chrono::steady_clock::now();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(upload.texture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, upload.nextRow, decoded.width, rows,
        GL_RGBA, GL_UNSIGNED_BYTE,
        decoded.pixels.data() + static_cast<std::size_t>(upload.nextRow) * rowBytes);
    glBindTexture(GL_TEXTURE_2D, 0);
    upload.nextRow += rows;
    if (!blocking) uploadBytesRemaining_ -= bytes;
    accountUploadTime(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count());
  }
  return upload.nextRow == decoded.height;
}

void TextureCache::publishUpload(const std::string& key, Upload& upload) {
  Entry entry;
  entry.texture = upload.texture;
  entry.width = upload.decoded.width;
  entry.height = upload.decoded.height;
  entry.failed = upload.decoded.failed || entry.texture == 0;
  if (!entry.failed) sizes_[entry.texture] = Size{entry.width, entry.height};
  entries_[key] = entry;
  upload.texture = 0;
  retirePixels(upload.decoded.pixels);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingKeys_.erase(key);
    visibleKeys_.erase(key);
  }
}

void TextureCache::drainDecoded(const std::string& preferredKey) {
  const auto started = std::chrono::steady_clock::now();
  const double allowanceAtStart = uploadMsRemaining_;
  while (uploadsRemaining_ > 0 && uploadMsRemaining_ > 0.0 && uploadBytesRemaining_ > 0) {
    std::string key;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!preferredKey.empty()) {
        if (uploads_.contains(preferredKey)) key = preferredKey;
        else {
          const auto ready = std::find_if(finished_.begin(), finished_.end(),
              [&](const Decoded& decoded) { return decoded.key == preferredKey; });
          if (ready != finished_.end()) {
            key = ready->key;
            uploads_.emplace(key, Upload{std::move(*ready)});
            finished_.erase(ready);
          }
        }
      } else {
        for (const auto& [candidate, upload] : uploads_) {
          if (visibleKeys_.contains(candidate)) { key = candidate; break; }
        }
        if (key.empty()) {
          auto ready = std::find_if(finished_.begin(), finished_.end(),
              [&](const Decoded& decoded) { return visibleKeys_.contains(decoded.key); });
          if (ready == finished_.end() && uploads_.empty() && !finished_.empty()) ready = finished_.begin();
          if (ready != finished_.end()) {
            key = ready->key;
            uploads_.emplace(key, Upload{std::move(*ready)});
            finished_.erase(ready);
          }
        }
        if (key.empty() && !uploads_.empty()) key = uploads_.begin()->first;
      }
    }
    if (key.empty()) break;
    auto found = uploads_.find(key);
    if (found == uploads_.end()) break;
    auto& upload = found->second;
    if (upload.decoded.generation != generation_) {
      if (upload.texture != 0) {
        const auto texture = static_cast<GLuint>(upload.texture);
        glDeleteTextures(1, &texture);
      }
      retirePixels(upload.decoded.pixels);
      uploads_.erase(found);
      continue;
    }
    if (!advanceUpload(upload, false)) break;
    publishUpload(key, upload);
    uploads_.erase(found);
    --uploadsRemaining_;
    if (!preferredKey.empty()) break;
  }
  const double elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  uploadMsRemaining_ = std::max(0.0,
      std::min(uploadMsRemaining_, allowanceAtStart - elapsed));
}

void TextureCache::prefetchBlurredBackdrop(const std::filesystem::path& image) {
  if (image.empty()) return;
  enqueue(image, image.generic_string() + "#settlement-blur", 0, true);
}

bool TextureCache::blurredBackdropReady(const std::filesystem::path& image) const {
  return image.empty() || entries_.contains(image.generic_string() + "#settlement-blur");
}

std::uint64_t TextureCache::blurredBackdropFor(const std::filesystem::path& image) {
  if (image.empty()) return 0;
  const std::string key = image.generic_string() + "#settlement-blur";
  if (const auto iterator = entries_.find(key); iterator != entries_.end()) {
    return iterator->second.failed ? 0 : iterator->second.texture;
  }
  // Direct captures and a reset near the exit can require a texture immediately. An existing
  // decode is collected here so that this exceptional path still reads each image only once.
  auto found = uploads_.find(key);
  if (found == uploads_.end()) {
    Decoded decoded;
    bool haveDecoded = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (pendingKeys_.contains(key)) {
        decodedReady_.wait(lock, [&] {
          return !pendingKeys_.contains(key) || std::any_of(finished_.begin(), finished_.end(),
              [&](const Decoded& ready) { return ready.key == key; });
        });
        const auto ready = std::find_if(finished_.begin(), finished_.end(),
            [&](const Decoded& item) { return item.key == key; });
        if (ready != finished_.end()) {
          decoded = std::move(*ready);
          finished_.erase(ready);
          haveDecoded = true;
        }
      }
    }
    if (!haveDecoded) decoded = decode(Pending{key, image, 0, generation_, true});
    found = uploads_.emplace(key, Upload{std::move(decoded)}).first;
  }
  (void)advanceUpload(found->second, true);
  publishUpload(key, found->second);
  uploads_.erase(found);
  const auto& entry = entries_.at(key);
  return entry.failed ? 0 : entry.texture;
}

void TextureCache::pump() {
  uploadsRemaining_ = kUploadsPerFrame;
  uploadBytesRemaining_ = kUploadBytesPerFrame;
  uploadMsRemaining_ = kUploadBudgetMs;
  drainDecoded();
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
  for (auto& [key, upload] : uploads_) {
    (void)key;
    if (upload.texture != 0) {
      const auto texture = static_cast<GLuint>(upload.texture);
      glDeleteTextures(1, &texture);
    }
    retirePixels(upload.decoded.pixels);
  }
  uploads_.clear();
  // Anything still queued or decoded but not uploaded belongs to the old context and is
  // dropped; a later query re-requests it.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    queue_.clear();
    while (!finished_.empty()) {
      retiredPixels_.push_back(std::move(finished_.front().pixels));
      finished_.pop_front();
    }
    pendingKeys_.clear();
    visibleKeys_.clear();
  }
  wake_.notify_one();
  decodedReady_.notify_all();
  uploadsRemaining_ = 0;
  uploadBytesRemaining_ = 0;
  uploadMsRemaining_ = 0.0;
  sizes_.clear();
}

} // namespace rechigros::app
