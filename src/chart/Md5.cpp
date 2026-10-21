#include "rechigros/chart/Md5.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace rechigros::chart {
namespace {

// Per-round shift amounts.
constexpr std::array<std::uint32_t, 64> kShift = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

// Sine-derived constants: floor(2^32 * |sin(i + 1)|).
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU,
    0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U,
    0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U,
    0xffeff47dU, 0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};

std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t count) {
  return (value << count) | (value >> (32U - count));
}

std::uint32_t readLittleEndian(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
      (static_cast<std::uint32_t>(data[1]) << 8) |
      (static_cast<std::uint32_t>(data[2]) << 16) |
      (static_cast<std::uint32_t>(data[3]) << 24);
}

} // namespace

Md5::Md5() : state_{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U} {}

void Md5::transform(const std::uint8_t* block) {
  std::array<std::uint32_t, 16> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    words[index] = readLittleEndian(block + index * 4);
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];

  for (std::uint32_t step = 0; step < 64; ++step) {
    std::uint32_t function = 0;
    std::uint32_t wordIndex = 0;
    if (step < 16) {
      function = (b & c) | (~b & d);
      wordIndex = step;
    } else if (step < 32) {
      function = (d & b) | (~d & c);
      wordIndex = (5 * step + 1) % 16;
    } else if (step < 48) {
      function = b ^ c ^ d;
      wordIndex = (3 * step + 5) % 16;
    } else {
      function = c ^ (b | ~d);
      wordIndex = (7 * step) % 16;
    }
    const std::uint32_t temp = d;
    d = c;
    c = b;
    b += rotateLeft(a + function + kRoundConstants[step] + words[wordIndex], kShift[step]);
    a = temp;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
}

void Md5::update(const void* data, std::size_t size) {
  if (finished_ || data == nullptr || size == 0) return;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  bitCount_ += static_cast<std::uint64_t>(size) * 8ULL;

  std::size_t offset = 0;
  if (bufferLength_ > 0) {
    const std::size_t wanted = std::min<std::size_t>(64 - bufferLength_, size);
    std::memcpy(buffer_ + bufferLength_, bytes, wanted);
    bufferLength_ += wanted;
    offset += wanted;
    if (bufferLength_ == 64) {
      transform(buffer_);
      bufferLength_ = 0;
    }
  }
  while (offset + 64 <= size) {
    transform(bytes + offset);
    offset += 64;
  }
  if (offset < size) {
    bufferLength_ = size - offset;
    std::memcpy(buffer_, bytes + offset, bufferLength_);
  }
}

void Md5::update(std::string_view text) { update(text.data(), text.size()); }

std::string Md5::hexDigest() {
  if (!finished_) {
    // Append the padding: a single one bit, then zeros, then the length in bits.
    std::uint64_t messageBits = bitCount_;
    const std::uint8_t one = 0x80U;
    update(&one, 1);
    const std::uint8_t zero = 0;
    while (bufferLength_ != 56) update(&zero, 1);

    std::uint8_t length[8]{};
    for (int index = 0; index < 8; ++index) {
      length[index] = static_cast<std::uint8_t>((messageBits >> (8 * index)) & 0xFFU);
    }
    bitCount_ -= 8ULL;
    update(length, 8);
    finished_ = true;
  }

  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (const std::uint32_t word : state_) {
    for (int shift = 0; shift < 32; shift += 8) {
      const auto value = static_cast<std::uint8_t>((word >> shift) & 0xFFU);
      out.push_back(digits[value >> 4]);
      out.push_back(digits[value & 0x0FU]);
    }
  }
  return out;
}

std::string md5Hex(std::span<const std::uint8_t> bytes) {
  Md5 digest;
  digest.update(bytes.data(), bytes.size());
  return digest.hexDigest();
}

} // namespace rechigros::chart
