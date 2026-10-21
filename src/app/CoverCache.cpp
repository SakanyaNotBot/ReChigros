#include "rechigros/app/CoverCache.hpp"

#include "rechigros/render/gl/Gl.hpp"

#include <stb_image.h>

#include <algorithm>
#include <vector>

namespace rechigros::app {
namespace {

constexpr int kLoadsPerQuery = 2;

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

} // namespace

TextureCache::~TextureCache() {
  reset();
}

std::uint64_t TextureCache::textureFor(const std::filesystem::path& image, int maxSize) {
  if (image.empty()) return 0;
  const std::string key = image.generic_string() + "#" + std::to_string(maxSize);
  const auto iterator = entries_.find(key);
  if (iterator != entries_.end()) return iterator->second.failed ? 0 : iterator->second.texture;
  for (const auto& pending : pending_) {
    if (pending.key == key) return 0;
  }
  pending_.push_back(Pending{key, image, maxSize});
  pump();
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

void TextureCache::pump() {
  int loaded = 0;
  while (!pending_.empty() && loaded < kLoadsPerQuery) {
    Pending task = std::move(pending_.front());
    pending_.erase(pending_.begin());
    Entry entry;
    Size size;
    entry.texture = load(task.key, task.path, task.maxSize, size);
    entry.width = size.width;
    entry.height = size.height;
    entry.failed = entry.texture == 0;
    entries_[task.key] = entry;
    ++loaded;
  }
}

std::uint64_t TextureCache::load(const std::string& key, const std::filesystem::path& path,
    int maxSize, Size& size) {
  (void)key;
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
  if (pixels == nullptr) return 0;
  int finalWidth = width;
  int finalHeight = height;
  const auto scaled = downscale(pixels, width, height, maxSize, finalWidth, finalHeight);
  const std::uint64_t texture =
      scaled.empty() ? createTexture(width, height, pixels)
                     : createTexture(finalWidth, finalHeight, scaled.data());
  stbi_image_free(pixels);
  if (texture == 0) return 0;
  size.width = scaled.empty() ? width : finalWidth;
  size.height = scaled.empty() ? height : finalHeight;
  sizes_[texture] = size;
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
  pending_.clear();
  sizes_.clear();
}

} // namespace rechigros::app
