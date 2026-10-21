#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rechigros::platform {

// Root directory that holds `assets/`. Resolved once from, in order: the
// RECHIGROS_ASSETS override, the executable directory, the working directory.
[[nodiscard]] const std::filesystem::path& assetRoot();
void setAssetRootOverride(const std::filesystem::path& root);

// Writable per-user directory for imported packs, replays, settings and caches.
[[nodiscard]] std::filesystem::path userDataRoot();
[[nodiscard]] std::filesystem::path cacheRoot();

// Absolute path of the running executable; empty when it cannot be determined.
[[nodiscard]] std::filesystem::path executablePath();

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path);
[[nodiscard]] std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);

bool writeTextFile(const std::filesystem::path& path, std::string_view content);
bool writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& content);

bool fileExists(const std::filesystem::path& path);
bool directoryExists(const std::filesystem::path& path);
std::uint64_t fileSize(const std::filesystem::path& path);

// Recursive listing of regular files, sorted so the result is stable across platforms.
std::vector<std::filesystem::path> listFiles(const std::filesystem::path& directory);

// Best-effort recursive removal; returns false only when something survived.
bool removeRecursively(const std::filesystem::path& path);

// Replaces characters that are illegal in a file name on any supported platform.
std::string sanitiseFileName(const std::string& name);

// Joins two path fragments using `/` semantics regardless of the host separator.
std::filesystem::path joinPortable(const std::filesystem::path& base, std::string_view relative);

} // namespace rechigros::platform
