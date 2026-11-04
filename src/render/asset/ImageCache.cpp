#include "rechigros/render/asset/ImageCache.hpp"

#include "WebPDecoder.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <SDL3/SDL_log.h>
#include <stb_image.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rechigros::render::asset {
namespace {

constexpr std::size_t kCacheThresholdBytes = 16U * 1024U * 1024U;
constexpr std::uint64_t kMaxCachedBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr unsigned char kMagic[8] = {'R', 'C', 'I', 'M', 'G', '0', '0', '5'};
constexpr std::size_t kHeaderBytes = 48;

std::mutex g_lockMapMutex;
std::unordered_map<std::string, std::shared_ptr<std::mutex>> g_pathLocks;

std::shared_ptr<std::mutex> pathLock(const std::string& key) {
  std::lock_guard<std::mutex> guard(g_lockMapMutex);
  auto& lock = g_pathLocks[key];
  if (!lock) lock = std::make_shared<std::mutex>();
  return lock;
}

std::uint32_t read32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U)
      | (static_cast<std::uint32_t>(data[2]) << 16U) | (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint64_t read64(const std::uint8_t* data) {
  return static_cast<std::uint64_t>(read32(data)) | (static_cast<std::uint64_t>(read32(data + 4)) << 32U);
}

void write32(std::uint8_t* data, std::uint32_t value) {
  for (int index = 0; index < 4; ++index) data[index] = static_cast<std::uint8_t>(value >> (index * 8));
}

void write64(std::uint8_t* data, std::uint64_t value) {
  write32(data, static_cast<std::uint32_t>(value));
  write32(data + 4, static_cast<std::uint32_t>(value >> 32U));
}

std::uint64_t sourceHash(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void writeLength(std::vector<std::uint8_t>& output, std::size_t length) {
  while (length >= 255) {
    output.push_back(255);
    length -= 255;
  }
  output.push_back(static_cast<std::uint8_t>(length));
}

void emitSequence(std::vector<std::uint8_t>& output, const std::uint8_t* input,
    std::size_t literalBegin, std::size_t cursor, std::size_t matchLength, std::size_t offset) {
  const std::size_t literalLength = cursor - literalBegin;
  const std::size_t encodedMatch = matchLength - 4;
  output.push_back(static_cast<std::uint8_t>((std::min<std::size_t>(literalLength, 15) << 4U)
      | std::min<std::size_t>(encodedMatch, 15)));
  if (literalLength >= 15) writeLength(output, literalLength - 15);
  output.insert(output.end(), input + literalBegin, input + cursor);
  output.push_back(static_cast<std::uint8_t>(offset));
  output.push_back(static_cast<std::uint8_t>(offset >> 8U));
  if (encodedMatch >= 15) writeLength(output, encodedMatch - 15);
}

std::vector<std::uint8_t> compressPixels(const std::vector<std::uint8_t>& input) {
  std::vector<std::uint8_t> output;
  output.reserve(input.size() / 3);
  std::vector<std::int32_t> positions(1U << 16U, -1);
  std::size_t cursor = 0;
  std::size_t literalBegin = 0;
  while (cursor + 4 <= input.size()) {
    const std::uint32_t word = read32(input.data() + cursor);
    const std::size_t hash = (word * 2654435761U) >> 16U;
    const auto reference = positions[hash];
    positions[hash] = static_cast<std::int32_t>(cursor);
    if (reference < 0 || cursor - static_cast<std::size_t>(reference) > 65535
        || read32(input.data() + reference) != word) {
      ++cursor;
      continue;
    }
    std::size_t matchLength = 4;
    while (cursor + matchLength < input.size()
        && input[static_cast<std::size_t>(reference) + matchLength] == input[cursor + matchLength]) {
      ++matchLength;
    }
    emitSequence(output, input.data(), literalBegin, cursor, matchLength,
        cursor - static_cast<std::size_t>(reference));
    cursor += matchLength;
    literalBegin = cursor;
  }
  const std::size_t literalLength = input.size() - literalBegin;
  output.push_back(static_cast<std::uint8_t>(std::min<std::size_t>(literalLength, 15) << 4U));
  if (literalLength >= 15) writeLength(output, literalLength - 15);
  output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(literalBegin), input.end());
  return output;
}

bool decompressPixels(const std::uint8_t* input, std::size_t inputSize,
    std::uint8_t* output, std::size_t outputSize) {
  std::size_t source = 0;
  std::size_t target = 0;
  while (source < inputSize) {
    const std::uint8_t token = input[source++];
    std::size_t literalLength = token >> 4U;
    if (literalLength == 15) {
      std::uint8_t extra = 0;
      do {
        if (source >= inputSize) return false;
        extra = input[source++];
        literalLength += extra;
      } while (extra == 255);
    }
    if (literalLength > inputSize - source || literalLength > outputSize - target) return false;
    std::memcpy(output + target, input + source, literalLength);
    source += literalLength;
    target += literalLength;
    if (source == inputSize) return target == outputSize;
    if (inputSize - source < 2) return false;
    const std::size_t offset = input[source] | (static_cast<std::size_t>(input[source + 1]) << 8U);
    source += 2;
    if (offset == 0 || offset > target) return false;
    std::size_t matchLength = token & 15U;
    if (matchLength == 15) {
      std::uint8_t extra = 0;
      do {
        if (source >= inputSize) return false;
        extra = input[source++];
        matchLength += extra;
      } while (extra == 255);
    }
    matchLength += 4;
    if (matchLength > outputSize - target) return false;
    if (offset >= matchLength) {
      std::memcpy(output + target, output + target - offset, matchLength);
    } else {
      std::size_t copied = 0;
      if (offset >= 8) {
        for (; copied + 8 <= matchLength; copied += 8) {
          std::memcpy(output + target + copied, output + target + copied - offset, 8);
        }
      } else {
        std::uint8_t pattern[8];
        for (std::size_t index = 0; index < 8; ++index) {
          pattern[index] = output[target - offset + index % offset];
        }
        for (; copied + 8 <= matchLength; copied += 8) {
          std::memcpy(output + target + copied, pattern, 8);
        }
      }
      for (; copied < matchLength; ++copied) {
        output[target + copied] = output[target + copied - offset];
      }
    }
    target += matchLength;
  }
  return target == outputSize;
}

std::filesystem::path sidecarPath(const std::filesystem::path& path) {
  auto result = path;
  result += ".rcimg";
  return result;
}

Image readCachedImage(const std::filesystem::path& path, std::uint64_t expectedHash) {
  Image image;
  const auto bytes = platform::readBinaryFile(sidecarPath(path));
  if (bytes.size() <= kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return image;
  const auto width = read32(bytes.data() + 8);
  const auto height = read32(bytes.data() + 12);
  const auto rawBytes = read64(bytes.data() + 16);
  const auto hash = read64(bytes.data() + 24);
  const auto compressedBytes = read64(bytes.data() + 32);
  const auto storedBytes = read64(bytes.data() + 40);
  if (hash != expectedHash || width == 0 || height == 0 || width > std::numeric_limits<int>::max()
      || height > std::numeric_limits<int>::max() || rawBytes > kMaxCachedBytes
      || rawBytes != static_cast<std::uint64_t>(width) * height * 4ULL
      || (storedBytes != rawBytes && storedBytes != rawBytes / 2ULL)
      || compressedBytes != bytes.size() - kHeaderBytes) {
    return image;
  }
  const auto started = std::chrono::steady_clock::now();
  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.dualChannel = storedBytes != rawBytes;
  image.pixels.resize(static_cast<std::size_t>(storedBytes));
  if (!decompressPixels(bytes.data() + kHeaderBytes, static_cast<std::size_t>(compressedBytes),
      image.pixels.data(), image.pixels.size())) {
    image = Image{};
    return image;
  }
  image.premultiplied = true;
  if (rawBytes >= kCacheThresholdBytes) {
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] cache %dx%d %.1f ms %zu bytes channels=%d",
        image.width, image.height, elapsed, bytes.size(), image.dualChannel ? 2 : 4);
  }
  return image;
}

void writeCachedImage(const std::filesystem::path& path, Image& image, std::uint64_t hash) {
  if (!image.valid() || image.pixels.size() < kCacheThresholdBytes
      || image.pixels.size() > kMaxCachedBytes) {
    return;
  }
  const auto started = std::chrono::steady_clock::now();
  premultiply(image);
  optimizeChannels(image);
  const auto compressed = compressPixels(image.pixels);
  std::vector<std::uint8_t> output(kHeaderBytes + compressed.size());
  std::memcpy(output.data(), kMagic, sizeof(kMagic));
  write32(output.data() + 8, static_cast<std::uint32_t>(image.width));
  write32(output.data() + 12, static_cast<std::uint32_t>(image.height));
  write64(output.data() + 16, static_cast<std::uint64_t>(image.width) * image.height * 4ULL);
  write64(output.data() + 24, hash);
  write64(output.data() + 32, compressed.size());
  write64(output.data() + 40, image.pixels.size());
  std::memcpy(output.data() + kHeaderBytes, compressed.data(), compressed.size());
  const auto destination = sidecarPath(path);
  auto temporary = destination;
  temporary += ".tmp";
  if (!platform::writeBinaryFile(temporary, output)) return;
  std::error_code error;
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(temporary, destination, error);
  if (error) std::filesystem::remove(temporary, error);
  const double elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] cache prepared %dx%d %.1f ms %zu bytes channels=%d",
      image.width, image.height, elapsed, output.size(), image.dualChannel ? 2 : 4);
}

bool imageLogEnabled() {
  static const bool enabled = std::getenv("RECHIGROS_TEXTURE_LOG") != nullptr;
  return enabled;
}

} // namespace

Image decodeImageMemory(const std::uint8_t* data, std::size_t size) {
  Image image;
  if (data == nullptr || size == 0) return image;
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
  if (pixels == nullptr) return decodeWebpMemory(data, size);
  image.width = width;
  image.height = height;
  image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
  stbi_image_free(pixels);
  return image;
}

Image decodeImage(const std::filesystem::path& path) {
  const auto bytes = platform::readBinaryFile(path);
  if (bytes.empty()) return {};
  const auto hash = sourceHash(bytes);
  const auto key = path.generic_string();
  auto lock = pathLock(key);
  std::lock_guard<std::mutex> guard(*lock);
  if (Image cached = readCachedImage(path, hash); cached.valid()) return cached;
  const auto started = std::chrono::steady_clock::now();
  Image image = decodeImageMemory(bytes.data(), bytes.size());
  if (imageLogEnabled() || image.pixels.size() >= kCacheThresholdBytes) {
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[tex] decoded %s %.1f ms %dx%d",
        path.filename().string().c_str(), elapsed, image.width, image.height);
  }
  writeCachedImage(path, image, hash);
  return image;
}

bool prepareImageCache(const std::filesystem::path& path) {
  return decodeImage(path).valid();
}

void expandChannels(Image& image) {
  if (!image.dualChannel) return;
  std::vector<std::uint8_t> expanded(image.pixels.size() * 2);
  for (std::size_t index = 0; index < image.pixels.size() / 2; ++index) {
    const auto gray = image.pixels[index * 2];
    expanded[index * 4] = gray;
    expanded[index * 4 + 1] = gray;
    expanded[index * 4 + 2] = gray;
    expanded[index * 4 + 3] = image.pixels[index * 2 + 1];
  }
  image.pixels = std::move(expanded);
  image.dualChannel = false;
}

void optimizeChannels(Image& image) {
  if (image.dualChannel || !image.premultiplied || image.pixels.size() < kCacheThresholdBytes
      || image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
    return;
  }
  for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
    const auto value = image.pixels[index];
    if (image.pixels[index + 1] != value || image.pixels[index + 2] != value) return;
  }
  std::vector<std::uint8_t> reduced(image.pixels.size() / 2);
  for (std::size_t index = 0; index < reduced.size() / 2; ++index) {
    reduced[index * 2] = image.pixels[index * 4];
    reduced[index * 2 + 1] = image.pixels[index * 4 + 3];
  }
  image.pixels = std::move(reduced);
  image.dualChannel = true;
}

void forceWhite(Image& image) {
  expandChannels(image);
  for (std::size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
    image.pixels[index] = 255;
    image.pixels[index + 1] = 255;
    image.pixels[index + 2] = 255;
  }
  image.premultiplied = false;
}

void premultiply(Image& image) {
  if (image.premultiplied) return;
  if (image.dualChannel) {
    image.premultiplied = true;
    return;
  }
  for (std::size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
    const auto alpha = static_cast<std::uint32_t>(image.pixels[index + 3]);
    if (alpha == 255) continue;
    for (int channel = 0; channel < 3; ++channel) {
      image.pixels[index + channel] = static_cast<std::uint8_t>(
          (static_cast<std::uint32_t>(image.pixels[index + channel]) * alpha + 127) / 255);
    }
  }
  image.premultiplied = true;
}

} // namespace rechigros::render::asset
