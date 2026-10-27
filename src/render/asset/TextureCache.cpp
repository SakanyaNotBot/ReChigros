#include "rechigros/render/asset/TextureCache.hpp"

#include "rechigros/platform/Gif.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "rechigros/platform/AssetManager.hpp"

#include <SDL3/SDL_log.h>
#include <stb_image.h>

#include <algorithm>

namespace rechigros::render::asset {

Image decodeImageMemory(const std::uint8_t* data, std::size_t size) {
  Image image;
  if (data == nullptr || size == 0) return image;
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
  if (pixels == nullptr) return image;
  image.width = width;
  image.height = height;
  image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
  stbi_image_free(pixels);
  return image;
}

// Per-asset cost of the startup texture load, so the largest block of the launch can be
// attributed. Enabled with RECHIGROS_TEXTURE_LOG so a normal run pays nothing.
bool textureLoadLogEnabled() {
  static const bool enabled = std::getenv("RECHIGROS_TEXTURE_LOG") != nullptr;
  return enabled;
}

Image decodeImage(const std::filesystem::path& path) {
  const auto bytes = [&path] {
    std::vector<std::uint8_t> data;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0) return data;
    data.resize(static_cast<std::size_t>(size));
    std::FILE* file = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&file, path.string().c_str(), "rb") != 0) file = nullptr;
#else
    file = std::fopen(path.string().c_str(), "rb");
#endif
    if (file == nullptr) return std::vector<std::uint8_t>{};
    std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    return data;
  }();
  return decodeImageMemory(bytes.data(), bytes.size());
}

void forceWhite(Image& image) {
  for (std::size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
    image.pixels[index] = 255;
    image.pixels[index + 1] = 255;
    image.pixels[index + 2] = 255;
  }
}

void premultiply(Image& image) {
  // The sprite pipeline composites with premultiplied alpha (ONE, ONE_MINUS_SRC_ALPHA),
  // so colour channels have to be scaled by alpha before upload.
  for (std::size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
    const auto alpha = static_cast<std::uint32_t>(image.pixels[index + 3]);
    if (alpha == 255) continue;
    for (int channel = 0; channel < 3; ++channel) {
      image.pixels[index + channel] = static_cast<std::uint8_t>(
          (static_cast<std::uint32_t>(image.pixels[index + channel]) * alpha + 127) / 255);
    }
  }
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
  premultiply(image);
  gl::Texture texture;
  if (!texture.uploadRgba(image.width, image.height, image.pixels.data())) return nullptr;
  texture.setFilter(true);
  const auto inserted = textures_.emplace(key, std::move(texture));
  if (textureLoadLogEnabled()) {
    // The upload is the part that has to run on the thread owning the graphics context, so its
    // cost is what actually remains on the launch path once decoding moved to workers.
    const double uploadMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] upload %-40s %6.1f ms %dx%d",
        key.c_str(), uploadMs, imageWidth, imageHeight);
  }
  return &inserted.first->second;
}

void TextureCache::beginPrefetchAssets(const std::vector<std::string>& relativePaths) {
  awaitPrefetchAssets();
  if (relativePaths.empty()) return;
  // Decoding is the expensive half of a texture load and touches no graphics state, so it is the
  // half that can run beside the drawing thread. The context belongs to the caller and is never
  // touched here; the decoded pixels are handed over through the map instead.
  const unsigned cores = std::thread::hardware_concurrency();
  const std::size_t workers = std::clamp<std::size_t>(cores == 0 ? 2U : cores, 1U, 4U);
  // The threads outlive this call: the caller runs a stage that does not touch textures and only
  // then awaits them, which is what makes the decode overlap instead of adding to the total.
  auto next = std::make_shared<std::atomic<std::size_t>>(0);
  auto paths = std::make_shared<std::vector<std::string>>(relativePaths);
  prefetchThreads_.reserve(workers);
  for (std::size_t worker = 0; worker < workers; ++worker) {
    prefetchThreads_.emplace_back([this, next, paths] {
      for (;;) {
        const std::size_t index = next->fetch_add(1, std::memory_order_relaxed);
        if (index >= paths->size()) return;
        const std::string& relativePath = (*paths)[index];
        const std::string key = "asset:" + relativePath;
        {
          std::lock_guard<std::mutex> guard(prefetchMutex_);
          if (decoded_.find(key) != decoded_.end()) continue;
        }
        const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
        if (bytes.empty()) continue;
        Image image = decodeImageMemory(bytes.data(), bytes.size());
        if (!image.valid()) continue;
        std::lock_guard<std::mutex> guard(prefetchMutex_);
        decoded_.emplace(key, std::move(image));
      }
    });
  }
}

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
  // An image decoded ahead of time only needs its upload, which is the part that has to be here.
  if (const auto ready = decoded_.find(key); ready != decoded_.end()) {
    Image image = std::move(ready->second);
    decoded_.erase(ready);
    return store(key, std::move(image), white);
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

gl::Texture* TextureCache::loadFile(const std::filesystem::path& path, bool white) {
  const std::string key = "file:" + path.generic_string() + (white ? "#w" : "");
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  return store(key, decodeImage(path), white);
}

gl::Texture* TextureCache::loadPixels(const std::string& key, Image image) {
  return store(key, std::move(image), false);
}

void TextureCache::clear() noexcept {
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
