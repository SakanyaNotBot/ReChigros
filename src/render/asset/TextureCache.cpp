#include "rechigros/render/asset/TextureCache.hpp"

#include "rechigros/platform/Gif.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "rechigros/platform/AssetManager.hpp"

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
  auto it = textures_.find(key);
  if (it != textures_.end()) {
    if (it->second.valid()) return &it->second;
    textures_.erase(it);
  }
  if (!image.valid()) return nullptr;
  if (white) forceWhite(image);
  premultiply(image);
  gl::Texture texture;
  if (!texture.uploadRgba(image.width, image.height, image.pixels.data())) return nullptr;
  texture.setFilter(true);
  const auto inserted = textures_.emplace(key, std::move(texture));
  return &inserted.first->second;
}

gl::Texture* TextureCache::loadAsset(const std::string& relativePath, bool white) {
  const std::string key = "asset:" + relativePath + (white ? "#w" : "");
  if (const auto it = textures_.find(key); it != textures_.end() && it->second.valid()) {
    return &it->second;
  }
  const auto bytes = platform::AssetManager::readBinary(assetRoot_, relativePath);
  if (bytes.empty()) return nullptr;
  return store(key, decodeImageMemory(bytes.data(), bytes.size()), white);
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
