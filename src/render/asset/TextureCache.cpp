#include "rechigros/render/asset/TextureCache.hpp"

#include "rechigros/platform/Gif.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "rechigros/platform/AssetManager.hpp"

#include <SDL3/SDL_log.h>
#include <stb_image.h>

#include <algorithm>

namespace rechigros::render::asset {

// Per-asset cost of the startup texture load, so the largest block of the launch can be
// attributed. Enabled with RECHIGROS_TEXTURE_LOG so a normal run pays nothing.
bool textureLoadLogEnabled() {
  static const bool enabled = std::getenv("RECHIGROS_TEXTURE_LOG") != nullptr;
  return enabled;
}

gl::Texture& TextureCache::white() {
  if (!white_.valid()) {
    const std::uint8_t pixel[4] = {255, 255, 255, 255};
    white_.uploadRgba(1, 1, pixel);
    white_.setFilter(false);
  }
  return white_;
}

gl::Texture* TextureCache::store(const std::string& key, Image image, bool white) {
  const auto uploadStart = std::chrono::steady_clock::now();
  auto it = textures_.find(key);
  if (it != textures_.end()) {
    if (it->second.valid()) return &it->second;
    textures_.erase(it);
  }
  if (!image.valid()) return nullptr;
  const int imageWidth = image.width;
  const int imageHeight = image.height;
  if (white) forceWhite(image);
  const bool large = image.pixels.size() >= 16U * 1024U * 1024U;
  premultiply(image);
  const auto gpuStart = std::chrono::steady_clock::now();
  gl::Texture texture;
  const bool uploaded = image.dualChannel
      ? texture.uploadDualChannel(image.width, image.height, image.pixels.data())
      : texture.uploadRgba(image.width, image.height, image.pixels.data());
  if (!uploaded) return nullptr;
  const auto gpuEnd = std::chrono::steady_clock::now();
  texture.setFilter(true);
  const auto inserted = textures_.emplace(key, std::move(texture));
  if (large || textureLoadLogEnabled()) {
    const double uploadMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count();
    const double gpuMs = std::chrono::duration<double, std::milli>(gpuEnd - gpuStart).count();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] upload %-40s %6.1f ms gpu=%6.1f ms %dx%d",
        key.c_str(), uploadMs, gpuMs, imageWidth, imageHeight);
  }
  return &inserted.first->second;
}

namespace {
struct PendingTaskGuard {
  std::atomic<std::size_t>& counter;
  ~PendingTaskGuard() { counter.fetch_sub(1, std::memory_order_release); }
};
} // namespace

void TextureCache::beginPrefetchAssets(const std::vector<std::string>& relativePaths) {
  auto paths = std::make_shared<std::vector<std::string>>();
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    for (const auto& relativePath : relativePaths) {
      const std::string key = "asset:" + relativePath;
      if (!textures_.contains(key) && !decoded_.contains(key)) paths->push_back(relativePath);
    }
  }
  if (paths->empty()) return;
  const unsigned cores = std::thread::hardware_concurrency();
  const std::size_t workers = std::min<std::size_t>(paths->size(),
      std::clamp<std::size_t>(cores == 0 ? 2U : cores, 1U, 4U));
  auto next = std::make_shared<std::atomic<std::size_t>>(0);
  prefetchPending_.fetch_add(paths->size(), std::memory_order_release);
  prefetchThreads_.reserve(prefetchThreads_.size() + workers);
  for (std::size_t worker = 0; worker < workers; ++worker) {
    prefetchThreads_.emplace_back([this, next, paths] {
      for (;;) {
        const std::size_t index = next->fetch_add(1, std::memory_order_relaxed);
        if (index >= paths->size()) return;
        const std::string& relativePath = (*paths)[index];
        PendingTaskGuard guard{prefetchPending_};
        const std::string key = "asset:" + relativePath;
        {
          std::lock_guard<std::mutex> guardLock(prefetchMutex_);
          if (decoded_.find(key) != decoded_.end()) continue;
        }
        const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
        if (bytes.empty()) continue;
        Image image = decodeImageMemory(bytes.data(), bytes.size());
        if (!image.valid()) continue;
        premultiply(image);
        optimizeChannels(image);
        std::lock_guard<std::mutex> guardLock(prefetchMutex_);
        decoded_.emplace(key, std::move(image));
      }
    });
  }
}

void TextureCache::beginPrefetchFiles(const std::vector<std::filesystem::path>& requested) {
  auto paths = std::make_shared<std::vector<std::filesystem::path>>();
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    for (const auto& path : requested) {
      const std::string key = "file:" + path.generic_string();
      if (!textures_.contains(key) && !decoded_.contains(key)) paths->push_back(path);
    }
  }
  if (paths->empty()) return;
  const unsigned cores = std::thread::hardware_concurrency();
  const std::size_t workers = std::min<std::size_t>(paths->size(),
      std::clamp<std::size_t>(cores == 0 ? 2U : cores, 1U, 4U));
  auto next = std::make_shared<std::atomic<std::size_t>>(0);
  prefetchPending_.fetch_add(paths->size(), std::memory_order_release);
  prefetchThreads_.reserve(prefetchThreads_.size() + workers);
  for (std::size_t worker = 0; worker < workers; ++worker) {
    prefetchThreads_.emplace_back([this, next, paths] {
      for (;;) {
        const std::size_t index = next->fetch_add(1, std::memory_order_relaxed);
        if (index >= paths->size()) return;
        const auto& path = (*paths)[index];
        PendingTaskGuard guard{prefetchPending_};
        const std::string key = "file:" + path.generic_string();
        {
          std::lock_guard<std::mutex> guardLock(prefetchMutex_);
          if (decoded_.find(key) != decoded_.end()) continue;
        }
        Image image = decodeImage(path);
        if (!image.valid()) continue;
        premultiply(image);
        optimizeChannels(image);
        std::lock_guard<std::mutex> guardLock(prefetchMutex_);
        decoded_.emplace(key, std::move(image));
      }
    });
  }
}

TextureCache::~TextureCache() { awaitPrefetchAssets(); }

void TextureCache::awaitPrefetchAssets() {
  for (auto& thread : prefetchThreads_) {
    if (thread.joinable()) thread.join();
  }
  prefetchThreads_.clear();
}

gl::Texture* TextureCache::loadAsset(const std::string& relativePath, bool white) {
  const std::string key = "asset:" + relativePath + (white ? "#w" : "");
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  if (!white) {
    if (const auto it = deferred_.find(key); it != deferred_.end()) return &it->second.texture;
  }
  // An image decoded ahead of time only needs its upload, which is the part that has to be here.
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find(key); ready != decoded_.end()) {
      Image image = std::move(ready->second);
      decoded_.erase(ready);
      return store(key, std::move(image), white);
    }
  }
  const auto started = std::chrono::steady_clock::now();
  const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
  if (bytes.empty()) return nullptr;
  const auto decodedAt = std::chrono::steady_clock::now();
  auto image = decodeImageMemory(bytes.data(), bytes.size());
  const auto uploadedAt = std::chrono::steady_clock::now();
  gl::Texture* result = store(key, std::move(image), white);
  if (textureLoadLogEnabled()) {
    const auto ms = [](auto from, auto to) {
      return std::chrono::duration<double, std::milli>(to - from).count();
    };
    const auto finished = std::chrono::steady_clock::now();
    std::printf("[tex] %-44s read=%.1f decode=%.1f upload=%.1f ms bytes=%zu\n",
        relativePath.c_str(), ms(started, decodedAt), ms(decodedAt, uploadedAt),
        ms(uploadedAt, finished), bytes.size());
  }
  return result;
}

gl::Texture* TextureCache::loadAssetDeferred(const std::string& relativePath) {
  const std::string key = "asset:" + relativePath;
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  if (const auto it = deferred_.find(key); it != deferred_.end()) return &it->second.texture;
  Image image;
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find(key); ready != decoded_.end()) {
      image = std::move(ready->second);
      decoded_.erase(ready);
    }
  }
  if (!image.valid()) {
    const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
    if (bytes.empty()) return nullptr;
    image = decodeImageMemory(bytes.data(), bytes.size());
  }
  premultiply(image);
  optimizeChannels(image);
  if (!image.valid()) return nullptr;
  DeferredUpload upload;
  upload.key = key;
  if (!upload.texture.allocateStorage(image.width, image.height, image.dualChannel)) return nullptr;
  upload.image = std::move(image);
  const auto inserted = deferred_.emplace(key, std::move(upload));
  return &inserted.first->second.texture;
}

gl::Texture* TextureCache::loadFileDeferred(const std::filesystem::path& path) {
  const std::string key = "file:" + path.generic_string();
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  if (const auto it = deferred_.find(key); it != deferred_.end()) return &it->second.texture;
  Image image;
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find(key); ready != decoded_.end()) {
      image = std::move(ready->second);
      decoded_.erase(ready);
    }
  }
  if (!image.valid()) image = decodeImage(path);
  premultiply(image);
  optimizeChannels(image);
  if (!image.valid()) return nullptr;
  DeferredUpload upload;
  upload.key = key;
  if (!upload.texture.allocateStorage(image.width, image.height, image.dualChannel)) return nullptr;
  upload.image = std::move(image);
  const auto inserted = deferred_.emplace(key, std::move(upload));
  return &inserted.first->second.texture;
}

void TextureCache::advanceDeferredUploads(double budgetMs) {
  const auto started = std::chrono::steady_clock::now();
  for (auto it = deferred_.begin(); it != deferred_.end();) {
    auto& upload = it->second;
    const int channels = upload.image.dualChannel ? 2 : 4;
    while (upload.nextRow < static_cast<std::size_t>(upload.image.height)) {
      const int row = static_cast<int>(upload.nextRow);
      const int rows = std::min(8, upload.image.height - row);
      const auto offset = upload.nextRow * static_cast<std::size_t>(upload.image.width) * channels;
      upload.texture.updateRows(row, rows, upload.image.pixels.data() + offset,
          upload.image.dualChannel);
      upload.nextRow += static_cast<std::size_t>(rows);
      const double elapsed = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count();
      if (elapsed >= budgetMs && upload.nextRow < static_cast<std::size_t>(upload.image.height)) {
        return;
      }
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] deferred upload complete %s %dx%d",
        upload.key.c_str(), upload.image.width, upload.image.height);
    textures_.emplace(upload.key, std::move(upload.texture));
    it = deferred_.erase(it);
    if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
        >= budgetMs) {
      return;
    }
  }
}

void TextureCache::completeDeferredUploads() {
  while (!deferred_.empty()) advanceDeferredUploads(std::numeric_limits<double>::max());
}

void TextureCache::cancelDeferredUploads() {
  deferred_.clear();
}

gl::Texture* TextureCache::loadAssetFlipped(const std::string& relativePath) {
  const std::string key = "asset:" + relativePath;
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  Image source;
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find(key); ready != decoded_.end()) {
      source = std::move(ready->second);
      decoded_.erase(ready);
    }
  }
  if (!source.valid()) {
    const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
    if (bytes.empty()) return nullptr;
    source = decodeImageMemory(bytes.data(), bytes.size());
  }
  if (!source.valid()) return nullptr;
  expandChannels(source);
  Image flipped;
  flipped.width = source.width;
  flipped.height = source.height;
  flipped.premultiplied = source.premultiplied;
  flipped.pixels.resize(source.pixels.size());
  const auto rowBytes = static_cast<std::size_t>(source.width) * 4U;
  for (int row = 0; row < source.height; ++row) {
    std::copy_n(source.pixels.data() + static_cast<std::size_t>(source.height - 1 - row) * rowBytes,
        rowBytes, flipped.pixels.data() + static_cast<std::size_t>(row) * rowBytes);
  }
  return store(key, std::move(flipped), false);
}

Image TextureCache::imageForFile(const std::filesystem::path& path) {
  Image image;
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find("file:" + path.generic_string()); ready != decoded_.end()) {
      image = ready->second;
    }
  }
  if (!image.valid()) image = decodeImage(path);
  expandChannels(image);
  return image;
}

gl::Texture* TextureCache::loadFile(const std::filesystem::path& path, bool white) {
  const std::string key = "file:" + path.generic_string() + (white ? "#w" : "");
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  if (!white) {
    if (const auto it = deferred_.find(key); it != deferred_.end()) return &it->second.texture;
  }
  {
    std::lock_guard<std::mutex> guard(prefetchMutex_);
    if (const auto ready = decoded_.find("file:" + path.generic_string()); ready != decoded_.end()) {
      Image image = std::move(ready->second);
      decoded_.erase(ready);
      return store(key, std::move(image), white);
    }
  }
  return store(key, decodeImage(path), white);
}

gl::Texture* TextureCache::loadPixels(const std::string& key, Image image) {
  return store(key, std::move(image), false);
}

void TextureCache::clear() noexcept {
  deferred_.clear();
  textures_.clear();
  white_.reset();
}


// --- AnimatedTexture --------------------------------------------------------

int AnimatedTexture::width() const noexcept {
  return frames_.empty() ? 0 : frames_.front().width();
}

int AnimatedTexture::height() const noexcept {
  return frames_.empty() ? 0 : frames_.front().height();
}

gl::Texture& AnimatedTexture::frameAt(std::size_t index) noexcept {
  return frames_[std::min(index, frames_.size() - 1)];
}

const gl::Texture& AnimatedTexture::frameAt(std::size_t index) const noexcept {
  return frames_[std::min(index, frames_.size() - 1)];
}

std::size_t AnimatedTexture::frameForTime(double elapsedSec) const noexcept {
  if (frames_.empty()) return 0;
  if (totalMs_ <= 0) return 0;
  const double durationMs = static_cast<double>(totalMs_);
  double remaining = std::fmod(elapsedSec * 1000.0, durationMs);
  if (remaining < 0.0) remaining += durationMs;
  for (std::size_t index = 0; index < frames_.size(); ++index) {
    const double delay = static_cast<double>(delaysMs_[index]);
    if (remaining < delay) return index;
    remaining -= delay;
  }
  return frames_.size() - 1;
}

std::size_t AnimatedTexture::frameForPosition(float position) const noexcept {
  if (frames_.empty()) return 0;
  if (!(position >= 0.0F && position <= 1.0F)) return 0;
  const float scaled = position * static_cast<float>(frames_.size() - 1);
  const auto index = static_cast<std::size_t>(std::lround(scaled));
  return std::min(index, frames_.size() - 1);
}

void AnimatedTexture::addFrame(gl::Texture texture, int delayMs) {
  delaysMs_.push_back(std::max(1, delayMs));
  totalMs_ += std::max(1, delayMs);
  frames_.push_back(std::move(texture));
}

void AnimatedTexture::clear() noexcept {
  frames_.clear();
  delaysMs_.clear();
  totalMs_ = 0;
}

bool TextureCache::isGifPath(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
      [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return extension == ".gif";
}

AnimatedTexture* TextureCache::loadGifFile(const std::filesystem::path& path) {
  if (path.empty()) return nullptr;
  const auto key = path.string();
  if (const auto cached = animated_.find(key); cached != animated_.end()) {
    return cached->second.valid() ? &cached->second : nullptr;
  }

  std::vector<std::uint8_t> bytes;
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0) {
    animated_.emplace(key, AnimatedTexture{});
    return nullptr;
  }
  bytes.resize(static_cast<std::size_t>(size));
  std::FILE* file = nullptr;
#ifdef _MSC_VER
  if (fopen_s(&file, path.string().c_str(), "rb") != 0) file = nullptr;
#else
  file = std::fopen(path.string().c_str(), "rb");
#endif
  if (file == nullptr) {
    animated_.emplace(key, AnimatedTexture{});
    return nullptr;
  }
  std::fread(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);

  platform::GifImage decoded;
  if (!platform::decodeGif(bytes, decoded) || decoded.frames.empty()) {
    animated_.emplace(key, AnimatedTexture{});
    return nullptr;
  }

  AnimatedTexture animated;
  for (auto& frame : decoded.frames) {
    gl::Texture texture;
    if (!texture.uploadRgba(decoded.width, decoded.height, frame.rgba.data())) continue;
    animated.addFrame(std::move(texture), frame.delayMs);
  }
  auto& stored = animated_.emplace(key, std::move(animated)).first->second;
  return stored.valid() ? &stored : nullptr;
}
} // namespace rechigros::render::asset
