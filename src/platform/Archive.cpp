#include "rechigros/platform/Archive.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace rechigros::platform {
namespace {

constexpr std::uint64_t EOCD_SIGNATURE = 0x06054b50ULL;
constexpr std::uint64_t CEN_SIGNATURE = 0x02014b50ULL;
constexpr std::uint64_t LOC_SIGNATURE = 0x04034b50ULL;
constexpr std::uint64_t ZIP64_MAGICVAL = 0xffffffffULL;
constexpr int ZIP64_MAGICCOUNT = 0xffff;
constexpr int METHOD_STORED = 0;
constexpr int METHOD_DEFLATED = 8;

ArchiveLimits g_limits{};
std::mutex g_limitsMutex;

ArchiveLimits limitsSnapshot() {
  std::lock_guard<std::mutex> guard(g_limitsMutex);
  return g_limits;
}

std::uint16_t readU16(const std::uint8_t* data) noexcept {
  return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t readU32(const std::uint8_t* data) noexcept {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
      (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint64_t readU64(const std::uint8_t* data) noexcept {
  return static_cast<std::uint64_t>(readU32(data)) |
      (static_cast<std::uint64_t>(readU32(data + 4)) << 32);
}

bool readExact(std::ifstream& stream, void* buffer, std::size_t size) {
  stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
  return static_cast<std::size_t>(stream.gcount()) == size;
}

// Rejects absolute paths, drive letters, parent traversal and embedded separators
// that would escape the extraction root.
bool normaliseEntryName(const std::string& raw, std::string& out) {
  if (raw.empty()) return false;
  // Names written by older packagers carry the legacy Chinese code page. They are decoded
  // so the extracted file names match the paths the package metadata refers to.
  std::string name = decodeLegacyText(raw);
  std::replace(name.begin(), name.end(), '\\', '/');
  if (name.find('\0') != std::string::npos) return false;

  out.clear();
  std::size_t cursor = 0;
  while (cursor <= name.size()) {
    const auto next = name.find('/', cursor);
    const auto end = next == std::string::npos ? name.size() : next;
    const std::string part = name.substr(cursor, end - cursor);
    cursor = end == std::string::npos ? name.size() + 1 : end + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") return false;
    if (part.find(':') != std::string::npos) return false;
    if (!out.empty()) out.push_back('/');
    out += part;
  }
  return !out.empty();
}

struct RawEntry {
  std::string name;
  int method{};
  std::uint64_t compressedSize{};
  std::uint64_t uncompressedSize{};
  std::uint64_t localHeaderOffset{};
  bool directory{};
};

// Reads the 64-bit values out of the 0x0001 extended field. Only the values the entry
// actually replaced with the sentinel are present, in a fixed order. Returns false when a
// needed field is absent.
bool readZip64Extra(const std::vector<std::uint8_t>& extra, bool needsUncompressed,
    bool needsCompressed, bool needsOffset, std::uint64_t& uncompressed,
    std::uint64_t& compressed, std::uint64_t& offset);

// Locates and parses the central directory. Throws for malformed archives and for a
// genuinely ZIP64 container, but an individual entry may still carry its 64-bit sizes in
// the 0x0001 extended field, which is read back before the entry is used.
std::vector<RawEntry> readCentralDirectory(std::ifstream& stream, std::uint64_t fileLength) {
  const std::uint64_t searchLength = std::min<std::uint64_t>(fileLength, 22ULL + 65535ULL);
  std::vector<std::uint8_t> tail(static_cast<std::size_t>(searchLength));
  stream.seekg(static_cast<std::streamoff>(fileLength - searchLength), std::ios::beg);
  if (!readExact(stream, tail.data(), tail.size())) throw std::runtime_error("ZIP tail truncated");

  std::ptrdiff_t eocd = -1;
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(tail.size()) - 22; i >= 0; --i) {
    if (readU32(&tail[static_cast<std::size_t>(i)]) == EOCD_SIGNATURE) { eocd = i; break; }
  }
  if (eocd < 0) throw std::runtime_error("ZIP end header not found");

  const auto* base = &tail[static_cast<std::size_t>(eocd)];
  const int entryCount = readU16(base + 10);
  const std::uint64_t centralSize = readU32(base + 12);
  const std::uint64_t centralOffset = readU32(base + 16);
  if (entryCount == ZIP64_MAGICCOUNT || centralSize == ZIP64_MAGICVAL || centralOffset == ZIP64_MAGICVAL) {
    throw std::runtime_error("ZIP64 archives are not supported");
  }
  if (centralOffset + centralSize > fileLength) {
    throw std::runtime_error("Invalid ZIP central directory");
  }
  if (static_cast<std::uint64_t>(entryCount) > limitsSnapshot().maxEntries) {
    throw std::runtime_error("Archive has too many entries");
  }

  stream.seekg(static_cast<std::streamoff>(centralOffset), std::ios::beg);
  std::vector<RawEntry> entries;
  entries.reserve(static_cast<std::size_t>(entryCount));
  const std::uint64_t centralEnd = centralOffset + centralSize;
  for (int i = 0; i < entryCount && static_cast<std::uint64_t>(stream.tellg()) < centralEnd; ++i) {
    std::array<std::uint8_t, 46> header{};
    if (!readExact(stream, header.data(), header.size())) throw std::runtime_error("ZIP central header truncated");
    if (readU32(header.data()) != CEN_SIGNATURE) throw std::runtime_error("Invalid ZIP central header");

    const int method = readU16(&header[10]);
    RawEntry entry;
    entry.compressedSize = readU32(&header[20]);
    entry.uncompressedSize = readU32(&header[24]);
    const int nameLength = readU16(&header[28]);
    const int extraLength = readU16(&header[30]);
    const int commentLength = readU16(&header[32]);
    entry.localHeaderOffset = readU32(&header[42]);
    entry.method = method;

    std::vector<char> name(static_cast<std::size_t>(nameLength));
    if (nameLength > 0 && !readExact(stream, name.data(), name.size())) throw std::runtime_error("ZIP name truncated");
    std::vector<std::uint8_t> extra(static_cast<std::size_t>(extraLength));
    if (extraLength > 0 && !readExact(stream, extra.data(), extra.size())) {
      throw std::runtime_error("ZIP extra field truncated");
    }
    if (commentLength > 0) stream.seekg(commentLength, std::ios::cur);

    // A size or offset of all ones points at the 64-bit values in the extended field; the
    // rest of the archive is ordinary, so only those fields need re-reading.
    const bool needsUncompressed = entry.uncompressedSize == ZIP64_MAGICVAL;
    const bool needsCompressed = entry.compressedSize == ZIP64_MAGICVAL;
    const bool needsOffset = entry.localHeaderOffset == ZIP64_MAGICVAL;
    if (needsUncompressed || needsCompressed || needsOffset) {
      std::uint64_t uncompressed = 0;
      std::uint64_t compressed = 0;
      std::uint64_t offset = 0;
      if (!readZip64Extra(extra, needsUncompressed, needsCompressed, needsOffset, uncompressed,
              compressed, offset)) {
        throw std::runtime_error("Missing ZIP64 extended information");
      }
      if (needsUncompressed) entry.uncompressedSize = uncompressed;
      if (needsCompressed) entry.compressedSize = compressed;
      if (needsOffset) entry.localHeaderOffset = offset;
    }

    entry.directory = !name.empty() && (name.back() == '/' || name.back() == '\\');
    if (!normaliseEntryName(std::string(name.begin(), name.end()), entry.name)) continue;
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Reads the 64-bit values out of the 0x0001 extended field. Only the values the entry
// actually replaced with the sentinel are present, in a fixed order. Returns false when a
// needed field is absent.
bool readZip64Extra(const std::vector<std::uint8_t>& extra, bool needsUncompressed,
    bool needsCompressed, bool needsOffset, std::uint64_t& uncompressed,
    std::uint64_t& compressed, std::uint64_t& offset) {
  std::size_t position = 0;
  while (position + 4 <= extra.size()) {
    const int headerId = readU16(&extra[position]);
    const int dataSize = readU16(&extra[position + 2]);
    position += 4;
    if (position + static_cast<std::size_t>(dataSize) > extra.size()) break;
    if (headerId == 0x0001) {
      std::size_t cursor = position;
      const auto take = [&extra, &cursor](std::uint64_t& target) {
        if (cursor + 8 > extra.size()) return false;
        target = readU64(&extra[cursor]);
        cursor += 8;
        return true;
      };
      if (needsUncompressed && !take(uncompressed)) return false;
      if (needsCompressed && !take(compressed)) return false;
      if (needsOffset && !take(offset)) return false;
      return true;
    }
    position += static_cast<std::size_t>(dataSize);
  }
  return false;
}

std::uint64_t localDataOffset(std::ifstream& stream, std::uint64_t localHeaderOffset) {
  stream.seekg(static_cast<std::streamoff>(localHeaderOffset), std::ios::beg);
  std::array<std::uint8_t, 30> header{};
  if (!readExact(stream, header.data(), header.size())) throw std::runtime_error("ZIP local header truncated");
  if (readU32(header.data()) != LOC_SIGNATURE) throw std::runtime_error("Invalid ZIP local header");
  const std::uint64_t nameLength = readU16(&header[26]);
  const std::uint64_t extraLength = readU16(&header[28]);
  return localHeaderOffset + 30 + nameLength + extraLength;
}

// A ZIP entry stores a raw deflate stream: no zlib wrapper and no trailing checksum.
// `tinfl_decompress_mem_to_mem` inflates such a block into a flat output buffer, so the
// zlib-header flag stays clear. It returns the number of bytes written, or a sentinel on
// failure.
int inflateRaw(const std::uint8_t* source, std::size_t sourceSize, std::uint8_t* target,
    std::size_t targetSize) {
  if (targetSize == 0) return sourceSize == 0 ? 0 : -1;
  const auto written = tinfl_decompress_mem_to_mem(target, targetSize, source, sourceSize, 0);
  if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) return -1;
  return written == targetSize ? 0 : -1;
}

std::vector<std::uint8_t> inflateEntry(std::ifstream& stream, const RawEntry& entry,
    const ArchiveLimits& limits) {
  if (entry.uncompressedSize > limits.maxEntryBytes) throw std::runtime_error("Archive entry is too large");
  const auto dataOffset = localDataOffset(stream, entry.localHeaderOffset);
  stream.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);

  if (entry.method == METHOD_STORED) {
    std::vector<std::uint8_t> output(static_cast<std::size_t>(entry.uncompressedSize));
    if (!output.empty() && !readExact(stream, output.data(), output.size())) {
      throw std::runtime_error("ZIP stored entry truncated");
    }
    return output;
  }
  if (entry.method != METHOD_DEFLATED) throw std::runtime_error("Unsupported ZIP compression method");

  std::vector<std::uint8_t> compressed(static_cast<std::size_t>(entry.compressedSize));
  if (!compressed.empty() && !readExact(stream, compressed.data(), compressed.size())) {
    throw std::runtime_error("ZIP compressed entry truncated");
  }
  std::vector<std::uint8_t> output(static_cast<std::size_t>(entry.uncompressedSize));
  if (output.empty()) return output;
  if (inflateRaw(compressed.data(), compressed.size(), output.data(), output.size()) != 0) {
    throw std::runtime_error("ZIP entry decompression failed");
  }
  return output;
}

} // namespace

void setArchiveLimits(const ArchiveLimits& limits) {
  std::lock_guard<std::mutex> guard(g_limitsMutex);
  g_limits = limits;
}

ArchiveLimits archiveLimits() { return limitsSnapshot(); }

std::vector<ArchiveEntry> listArchive(const std::filesystem::path& archive) {
  std::ifstream stream(archive, std::ios::binary);
  if (!stream) return {};
  stream.seekg(0, std::ios::end);
  const auto fileLength = static_cast<std::uint64_t>(stream.tellg());
  if (fileLength < 22) return {};

  std::vector<ArchiveEntry> result;
  try {
    for (const auto& entry : readCentralDirectory(stream, fileLength)) {
      result.push_back({entry.name, entry.uncompressedSize});
    }
  } catch (const std::exception&) {
    return {};
  }
  return result;
}

std::vector<std::filesystem::path> extractArchive(const std::filesystem::path& archive,
    const std::filesystem::path& outputDirectory) {
  std::ifstream stream(archive, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open archive");
  stream.seekg(0, std::ios::end);
  const auto fileLength = static_cast<std::uint64_t>(stream.tellg());
  if (fileLength < 22) throw std::runtime_error("Archive is truncated");

  const auto limits = limitsSnapshot();
  const auto entries = readCentralDirectory(stream, fileLength);
  std::error_code error;
  std::filesystem::create_directories(outputDirectory, error);
  if (error) throw std::runtime_error("Cannot create extraction directory");

  const auto root = std::filesystem::weakly_canonical(outputDirectory, error);
  if (error) throw std::runtime_error("Cannot resolve extraction directory");

  std::vector<std::filesystem::path> extracted;
  std::uint64_t total = 0;
  for (const auto& entry : entries) {
    const auto target = outputDirectory / pathFromUtf8(entry.name);
    const auto resolved = std::filesystem::weakly_canonical(target, error);
    if (error) { error.clear(); continue; }
    const auto relative = resolved.lexically_relative(root);
    if (relative.empty() || relative.native().rfind(std::filesystem::path(L"..").native(), 0) == 0) continue;

    if (entry.directory) continue;

    std::filesystem::create_directories(resolved.parent_path(), error);
    error.clear();
    auto bytes = inflateEntry(stream, entry, limits);
    total += bytes.size();
    if (total > limits.maxTotalBytes) throw std::runtime_error("Archive expands beyond the size limit");

    std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write extracted file");
    if (!bytes.empty()) {
      output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) throw std::runtime_error("Cannot write extracted file");
    extracted.push_back(std::move(resolved));
  }
  return extracted;
}

bool extractArchiveEntry(const std::filesystem::path& archive, const std::string& entry,
    const std::filesystem::path& outputFile) {
  const auto bytes = readArchiveEntry(archive, entry);
  if (bytes.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(outputFile.parent_path(), error);
  std::ofstream output(outputFile, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

std::vector<std::uint8_t> readArchiveEntry(const std::filesystem::path& archive,
    const std::string& entry) {
  std::ifstream stream(archive, std::ios::binary);
  if (!stream) return {};
  stream.seekg(0, std::ios::end);
  const auto fileLength = static_cast<std::uint64_t>(stream.tellg());
  if (fileLength < 22) return {};

  std::string wanted;
  if (!normaliseEntryName(entry, wanted)) return {};

  const auto limits = limitsSnapshot();
  try {
    for (const auto& candidate : readCentralDirectory(stream, fileLength)) {
      if (candidate.name != wanted) continue;
      return inflateEntry(stream, candidate, limits);
    }
  } catch (const std::exception&) {
    return {};
  }
  return {};
}

} // namespace rechigros::platform
