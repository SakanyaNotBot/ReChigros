#include "rechigros/platform/Archive.hpp"
#include "rechigros/platform/Gif.hpp"
#include "rechigros/platform/FileDialog.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include "TestScratch.hpp"
#include <string>
#include <vector>

#include <miniz.h>

namespace {

int g_failures = 0;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

// Minimal ZIP writer. Only used to build fixtures, so it stays deliberately small: no
// timestamps, no ZIP64. Entries are stored unless `deflate` is requested, which runs the
// real codec so the reader's raw-inflate path is genuinely exercised.
class ZipWriter {
 public:
  void add(const std::string& name, const std::string& content, bool deflate = false) {
    Entry entry;
    entry.name = name;
    entry.payload.assign(content.begin(), content.end());
    entry.uncompressedSize = static_cast<std::uint32_t>(entry.payload.size());
    if (!deflate) {
      entry.data = entry.payload;
      entry.method = 0;
      entries_.push_back(std::move(entry));
      return;
    }
    // `mz_compress` emits a zlib stream; strip the two-byte header and the four-byte
    // trailing checksum to leave the raw deflate body that a ZIP entry stores.
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(entry.payload.size()));
    std::vector<unsigned char> wrapped(static_cast<std::size_t>(bound));
    if (mz_compress(wrapped.data(), &bound,
            reinterpret_cast<const unsigned char*>(entry.payload.data()),
            static_cast<mz_ulong>(entry.payload.size())) != MZ_OK) {
      return;
    }
    if (bound <= 6) return;
    entry.data.assign(wrapped.begin() + 2,
        wrapped.begin() + static_cast<std::ptrdiff_t>(bound) - 4);
    entry.method = 8;
    entries_.push_back(std::move(entry));
  }

  bool write(const std::filesystem::path& path) const {
    std::string buffer;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(entries_.size());
    for (const auto& entry : entries_) {
      offsets.push_back(static_cast<std::uint32_t>(buffer.size()));
      append32(buffer, 0x04034b50);         // local file header signature
      append16(buffer, 20);                 // version needed
      append16(buffer, 0);                  // flags
      append16(buffer, entry.method);
      append16(buffer, 0);                  // time
      append16(buffer, 0);                  // date
      append32(buffer, crc32(entry.payload));
      append32(buffer, static_cast<std::uint32_t>(entry.data.size()));
      append32(buffer, entry.uncompressedSize);
      append16(buffer, static_cast<std::uint16_t>(entry.name.size()));
      append16(buffer, 0);
      buffer += entry.name;
      buffer.append(entry.data.begin(), entry.data.end());
    }

    const auto centralOffset = static_cast<std::uint32_t>(buffer.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      const auto& entry = entries_[i];
      append32(buffer, 0x02014b50);
      append16(buffer, 20);
      append16(buffer, 20);
      append16(buffer, 0);
      append16(buffer, entry.method);
      append16(buffer, 0);
      append16(buffer, 0);
      append32(buffer, crc32(entry.payload));
      append32(buffer, static_cast<std::uint32_t>(entry.data.size()));
      append32(buffer, entry.uncompressedSize);
      append16(buffer, static_cast<std::uint16_t>(entry.name.size()));
      append16(buffer, 0);
      append16(buffer, 0);
      append16(buffer, 0);
      append16(buffer, 0);
      append32(buffer, 0);
      append32(buffer, offsets[i]);
      buffer += entry.name;
    }
    const auto centralSize = static_cast<std::uint32_t>(buffer.size()) - centralOffset;

    append32(buffer, 0x06054b50);
    append16(buffer, 0);
    append16(buffer, 0);
    append16(buffer, static_cast<std::uint16_t>(entries_.size()));
    append16(buffer, static_cast<std::uint16_t>(entries_.size()));
    append32(buffer, centralSize);
    append32(buffer, centralOffset);
    append16(buffer, 0);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return static_cast<bool>(output);
  }

 private:
  struct Entry {
    std::string name;
    // Bytes written into the archive, compressed when `method` is deflate.
    std::vector<std::uint8_t> data;
    // Bytes after decompression, which is what the reader must reproduce.
    std::vector<std::uint8_t> payload;
    std::uint32_t uncompressedSize{};
    std::uint16_t method{};
  };

  static std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
      }
    }
    return ~crc;
  }

  static void append16(std::string& buffer, std::uint16_t value) {
    buffer.push_back(static_cast<char>(value & 0xFF));
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
  }

  static void append32(std::string& buffer, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      buffer.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
  }

  std::vector<Entry> entries_;
};

} // namespace

namespace {

// Runs every assertion. Any escaping exception becomes a reported failure instead of
// terminating the process, so a broken fixture still prints what went wrong.
void runChecks() {
  using namespace rechigros::platform;

  const auto scratch = rechigros::test::scratchDirectory("platform");
  std::error_code error;
  std::filesystem::remove_all(scratch, error);
  std::filesystem::create_directories(scratch, error);

  // --- entry name sanitising -------------------------------------------------
  ZipWriter writer;
  writer.add("chart.json", "{\"formatVersion\":3}");
  writer.add("music.ogg", "ogg");
  writer.add("sub/dir/info.json", "{}");
  writer.add("../escape.txt", "nope");
  writer.add("/absolute.txt", "nope");
  writer.add("a/../../b.txt", "nope");
  writer.add("win\\style\\name.txt", "slash");
  writer.add("drive:C.txt", "nope");
  // A deflated entry of realistic size: the compressed path is the one real chart
  // packages use, and it is the one a raw-inflate mistake breaks.
  const std::string compressedPayload =
      "{\"formatVersion\":3,\"judgeLineList\":[]}\n" + std::string(2000, 'x');
  writer.add("compressible.json", compressedPayload, true);
  const auto archive = scratch / "fixture.zip";
  require(writer.write(archive), "fixture archive written");

  const auto listed = listArchive(archive);
  // Entries that survive normalisation: chart.json, music.ogg, sub/dir/info.json,
  // absolute.txt (leading separator stripped) and win/style/name.txt (separator
  // rewritten). Rejected: ../escape.txt, a/../../b.txt and drive:C.txt.
  require(listed.size() == 6, "unsafe entries rejected, got " + std::to_string(listed.size()));
  require(std::any_of(listed.begin(), listed.end(),
      [](const ArchiveEntry& e) { return e.name == "chart.json"; }), "chart.json listed");
  require(std::any_of(listed.begin(), listed.end(),
      [](const ArchiveEntry& e) { return e.name == "win/style/name.txt"; }), "backslash normalised");
  require(std::none_of(listed.begin(), listed.end(),
      [](const ArchiveEntry& e) { return e.name.find("..") != std::string::npos; }), "traversal rejected");

  // --- extraction ------------------------------------------------------------
  const auto outputDir = scratch / "out";
  const auto extracted = extractArchive(archive, outputDir);
  require(extracted.size() == 6, "six files extracted");
  require(fileExists(outputDir / "chart.json"), "chart.json extracted");
  require(fileExists(outputDir / "sub" / "dir" / "info.json"), "nested entry extracted");
  require(!fileExists(scratch / "escape.txt"), "traversal never escaped the root");

  const auto chartBytes = readArchiveEntry(archive, "chart.json");
  require(std::string(chartBytes.begin(), chartBytes.end()) == "{\"formatVersion\":3}",
      "entry read into memory");
  require(readArchiveEntry(archive, "missing.json").empty(), "missing entry returns empty");

  // A deflated entry must decompress to exactly the bytes that went in. Chart packages
  // are stored deflated, so this is the path real imports take.
  const auto decompressed = readArchiveEntry(archive, "compressible.json");
  require(std::string(decompressed.begin(), decompressed.end()) == compressedPayload,
      "deflated entry decompresses byte for byte");
  require(fileExists(outputDir / "compressible.json"), "deflated entry extracted");

  // --- limits ----------------------------------------------------------------
  ArchiveLimits tight;
  tight.maxEntries = 2;
  setArchiveLimits(tight);
  require(listArchive(archive).empty(), "entry cap enforced");
  setArchiveLimits(ArchiveLimits{});
  require(archiveLimits().maxEntries == 4096, "limits restored");

  // --- file system helpers ---------------------------------------------------
  const auto textPath = scratch / "nested" / "dir" / "note.txt";
  require(writeTextFile(textPath, "hello"), "nested write creates directories");
  require(readTextFile(textPath) == "hello", "text round trip");

  const std::vector<std::uint8_t> blob{1, 2, 3, 4, 5};
  const auto blobPath = scratch / "blob.bin";
  require(writeBinaryFile(blobPath, blob), "binary write");
  require(readBinaryFile(blobPath) == blob, "binary round trip");
  require(fileSize(blobPath) == 5, "file size");

  require(sanitiseFileName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j", "file name sanitising");
  require(sanitiseFileName("") == "unnamed", "empty name fallback");
  require(sanitiseFileName(std::string(200, 'x')).size() == 160, "long name truncated");

  require(joinPortable(scratch, "sub/dir/file.txt") == scratch / "sub" / "dir" / "file.txt",
      "portable join");
  require(joinPortable(scratch, "single.txt") == scratch / "single.txt", "portable join single");

  require(pathFromDialogResult("/storage/emulated/0/Download/a.zip") ==
      std::filesystem::u8path("/storage/emulated/0/Download/a.zip"),
      "plain dialog path unchanged");
  require(pathFromDialogResult(
      "file:///data/user/0/pkg/cache/a%20b/%E6%B5%8B%E8%AF%95.zip") ==
      std::filesystem::u8path("/data/user/0/pkg/cache/a b/\xe6\xb5\x8b\xe8\xaf\x95.zip"),
      "file URI decoded to a local UTF-8 path");
  require(pathFromDialogResult("file:///tmp/a%ZZb%2.zip") ==
      std::filesystem::u8path("/tmp/a%ZZb%2.zip"),
      "invalid URI escapes preserved");

  // Legacy encodings: valid UTF-8 passes through untouched, while a byte string that is not
  // UTF-8 is treated as the legacy Chinese code page (bytes "测试" in GBK).
  require(isValidUtf8("plain ascii"), "ascii is valid UTF-8");
  require(isValidUtf8("\xe6\xb5\x8b\xe8\xaf\x95"), "UTF-8 text is valid");
  require(!isValidUtf8("\xb2\xe2\xca\xd4"), "legacy text is not valid UTF-8");
  require(decodeLegacyText("plain ascii") == "plain ascii", "valid text untouched");
  require(decodeLegacyText("\xe6\xb5\x8b\xe8\xaf\x95") == "\xe6\xb5\x8b\xe8\xaf\x95",
      "UTF-8 text untouched");
#if defined(_WIN32)
  require(decodeLegacyText("\xb2\xe2\xca\xd4") == "\xe6\xb5\x8b\xe8\xaf\x95",
      "legacy code page decoded on this platform");
#else
  require(!decodeLegacyText("\xb2\xe2\xca\xd4").empty(), "legacy text keeps a best-effort result");
#endif

  const auto listedFiles = listFiles(outputDir);
  require(listedFiles.size() == 6, "recursive listing");
  require(std::is_sorted(listedFiles.begin(), listedFiles.end()), "listing is sorted");

  require(removeRecursively(outputDir), "recursive removal");
  require(!directoryExists(outputDir), "directory gone");

  std::filesystem::remove_all(scratch, error);
}

} // namespace

int main() {
  try {
    runChecks();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
    ++g_failures;
  }

  if (g_failures != 0) {
    std::cerr << g_failures << " platform test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros platform tests passed\n";
  return 0;
}
