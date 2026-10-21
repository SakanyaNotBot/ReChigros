#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rechigros::chart {

// Streaming MD5. Used to identify a chart by the digest of its normalized bytes.
class Md5 final {
public:
  Md5();
  ~Md5() = default;
  Md5(const Md5&) = delete;
  Md5& operator=(const Md5&) = delete;

  void update(const void* data, std::size_t size);
  void update(std::string_view text);
  // Finalises the digest. The object must not be fed afterwards.
  [[nodiscard]] std::string hexDigest();

private:
  void transform(const std::uint8_t* block);

  std::uint32_t state_[4]{};
  std::uint64_t bitCount_{};
  std::uint8_t buffer_[64]{};
  std::size_t bufferLength_{};
  bool finished_{};
};

// MD5 of a byte range, as lowercase hex.
[[nodiscard]] std::string md5Hex(std::span<const std::uint8_t> bytes);

} // namespace rechigros::chart
