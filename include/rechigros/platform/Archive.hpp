#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rechigros::platform {

// One file inside a package archive.
struct ArchiveEntry {
  std::string name;      // normalised, forward slashes, no leading separator
  std::uint64_t size{};  // uncompressed size
};

// Directory listing of an archive without extracting anything.
std::vector<ArchiveEntry> listArchive(const std::filesystem::path& archive);

// Extracts `archive` beneath `outputDirectory`.
// Rejects absolute paths, drive letters, `..` segments and NUL bytes; caps the entry
// count, the per-entry size and the total expanded size so a hostile package cannot
// exhaust the disk. Returns the extracted files in archive order.
std::vector<std::filesystem::path> extractArchive(const std::filesystem::path& archive,
    const std::filesystem::path& outputDirectory);

// Extracts a single entry to `outputFile`. Returns false when the entry is absent.
bool extractArchiveEntry(const std::filesystem::path& archive, const std::string& entry,
    const std::filesystem::path& outputFile);

// Reads an entry into memory. Returns an empty vector when the entry is absent.
std::vector<std::uint8_t> readArchiveEntry(const std::filesystem::path& archive,
    const std::string& entry);

struct ArchiveLimits {
  std::uint64_t maxEntries{4096};
  std::uint64_t maxEntryBytes{512ULL * 1024ULL * 1024ULL};
  std::uint64_t maxTotalBytes{1024ULL * 1024ULL * 1024ULL};
};

// Overrides the default guard rails; used by tests and by import paths that need
// tighter limits.
void setArchiveLimits(const ArchiveLimits& limits);
[[nodiscard]] ArchiveLimits archiveLimits();

} // namespace rechigros::platform
