#include "rechigros/app/ChartLibrary.hpp"

#include "rechigros/platform/Archive.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rechigros::app {
namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string readHead(const std::filesystem::path& file, std::size_t maximum) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  std::string head(maximum, '\0');
  input.read(head.data(), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(input.gcount()));
  return head;
}

std::string relativize(const std::filesystem::path& base, const std::filesystem::path& file) {
  std::error_code error;
  const auto relative = std::filesystem::relative(file, base, error);
  if (error || relative.empty()) return file.filename().string();
  return relative.generic_string();
}

std::filesystem::path resolveRelative(const std::filesystem::path& root,
    const std::string& relativePath) {
  if (relativePath.empty()) return {};
  std::string cleaned = relativePath;
  while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '\t' ||
                                 cleaned.back() == '\r' || cleaned.back() == '\n')) {
    cleaned.pop_back();
  }
  while (!cleaned.empty() && (cleaned.front() == ' ' || cleaned.front() == '\t' ||
                                 cleaned.front() == '\r' || cleaned.front() == '\n')) {
    cleaned.erase(cleaned.begin());
  }
  if (cleaned.empty()) return {};
  std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
  while (!cleaned.empty() && cleaned.front() == '/') cleaned.erase(cleaned.begin());
  while (cleaned.rfind("./", 0) == 0) cleaned.erase(0, 2);
  if (cleaned.empty()) return {};
  return platform::joinPortable(root, cleaned);
}

std::vector<std::filesystem::path> listRegularFiles(const std::filesystem::path& directory) {
  return platform::listFiles(directory);
}

std::filesystem::path chooseShortestPath(const std::vector<std::filesystem::path>& files) {
  if (files.empty()) return {};
  std::filesystem::path best = files.front();
  for (const auto& file : files) {
    if (file.generic_string().size() < best.generic_string().size()) best = file;
  }
  return best;
}

// Chooses the candidate closest to a preferred directory. The reference importer returns the
// first file in archive order that lives beside the metadata file and only then falls back to
// the first candidate anywhere; extraction order is not preserved by the platform listing, so
// the stable (sorted) order stands in for it.
std::filesystem::path choosePreferredPath(const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& preferDirectory) {
  if (files.empty()) return {};
  if (!preferDirectory.empty()) {
    for (const auto& file : files) {
      if (file.parent_path() == preferDirectory) return file;
    }
  }
  return files.front();
}

bool looksLikeChartJson(const std::string& head) {
  if (head.empty()) return false;
  const bool judgeLines = head.find("\"judgeLineList\"") != std::string::npos
      || head.find("\"JudgeLineList\"") != std::string::npos;
  if (!judgeLines) return false;
  return head.find("\"formatVersion\"") != std::string::npos
      || head.find("\"BPMList\"") != std::string::npos
      || head.find("\"META\"") != std::string::npos
      || head.find("\"RPEVersion\"") != std::string::npos;
}

bool looksLikeEditorChartText(const std::string& head) {
  if (head.empty()) return false;
  const std::string lowered = toLower(head);
  return lowered.rfind("bp ", 0) == 0 || lowered.find("\nbp ") != std::string::npos
      || lowered.find("\ncv ") != std::string::npos || lowered.find("\ncp ") != std::string::npos
      || lowered.find("\ncm ") != std::string::npos || lowered.find("\nn1 ") != std::string::npos
      || lowered.find("\nn2 ") != std::string::npos || lowered.find("\nn3 ") != std::string::npos
      || lowered.find("\nn4 ") != std::string::npos;
}

bool isLikelyChartFile(const std::filesystem::path& file) {
  if (!platform::fileExists(file)) return false;
  const std::string extension = toLower(file.extension().string());
  if (extension == ".json") return looksLikeChartJson(readHead(file, 64 * 1024));
  if (extension == ".pec" || extension == ".rpe") return true;
  return looksLikeEditorChartText(readHead(file, 32 * 1024));
}

std::filesystem::path findPackChart(const std::filesystem::path& root,
    const std::filesystem::path& preferDirectory) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isLikelyChartFile(file)) found.push_back(file);
  }
  return choosePreferredPath(found, preferDirectory);
}

std::filesystem::path findPackAudio(const std::filesystem::path& root,
    const std::filesystem::path& preferDirectory) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isAudioCandidate(file)) found.push_back(file);
  }
  return choosePreferredPath(found, preferDirectory);
}

std::filesystem::path findPackImage(const std::filesystem::path& root,
    const std::filesystem::path& preferDirectory) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isImageCandidate(file)) found.push_back(file);
  }
  return choosePreferredPath(found, preferDirectory);
}


std::filesystem::path findInfoFile(const std::filesystem::path& root) {
  const auto canonical = root / "info.json";
  if (platform::fileExists(canonical)) return canonical;
  const auto files = listRegularFiles(root);
  std::vector<std::filesystem::path> found;
  for (const auto& file : files) {
    const std::string name = toLower(file.filename().string());
    if (name == "info.json" || name == "info.yml" || name == "info.yaml" || name == "info.txt"
        || name == "info.csv") {
      found.push_back(file);
    }
  }
  return chooseShortestPath(found);
}

std::filesystem::path findChartFile(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isChartCandidate(file)) found.push_back(file);
  }
  return chooseShortestPath(found);
}

std::filesystem::path findAudioFile(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isAudioCandidate(file)) found.push_back(file);
  }
  return chooseShortestPath(found);
}

std::filesystem::path findImageFile(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (isImageCandidate(file)) found.push_back(file);
  }
  return chooseShortestPath(found);
}

std::filesystem::path findByInfoName(const std::filesystem::path& root, const std::string& name,
    bool (*predicate)(const std::filesystem::path&)) {
  if (name.empty()) return {};
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const auto slash = normalized.find_last_of('/');
  const std::string fileName = slash == std::string::npos ? normalized : normalized.substr(slash + 1);
  if (fileName.empty()) return {};
  std::vector<std::filesystem::path> found;
  for (const auto& file : listRegularFiles(root)) {
    if (toLower(file.filename().string()) == toLower(fileName) && predicate(file)) {
      found.push_back(file);
    }
  }
  return chooseShortestPath(found);
}

std::int64_t lastModified(const std::filesystem::path& file) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(file, error);
  if (error) return 0;
  return static_cast<std::int64_t>(time.time_since_epoch().count());
}

std::int64_t recordModifiedAt(const std::filesystem::path& root,
    const std::filesystem::path& chartFile, const std::filesystem::path& musicFile,
    const std::filesystem::path& bgFile, const std::filesystem::path& infoFile) {
  std::int64_t newest = lastModified(root);
  for (const auto& file : {chartFile, musicFile, bgFile, infoFile}) {
    if (file.empty()) continue;
    newest = std::max(newest, lastModified(file));
  }
  return newest;
}

std::string hashFile(const std::filesystem::path& file, std::uint64_t& sizeOut) {
  std::ifstream input(file, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open package: " + file.string());
  std::uint64_t hash = 1469598103934665603ULL;
  std::uint64_t size = 0;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(input.gcount());
    for (std::size_t index = 0; index < count; ++index) {
      hash ^= static_cast<unsigned char>(buffer[index]);
      hash *= 1099511628211ULL;
    }
    size += count;
  }
  sizeOut = size;
  char text[32];
  std::snprintf(text, sizeof(text), "%016llx%08llx",
      static_cast<unsigned long long>(hash), static_cast<unsigned long long>(size));
  return text;
}

std::string hashPath(const std::filesystem::path& path) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char character : path.generic_string()) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  char text[32];
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
  return text;
}

bool isInfoName(const std::string& lowerName) {
  return lowerName == "info.json" || lowerName == "info.yml" || lowerName == "info.yaml"
      || lowerName == "info.txt" || lowerName == "info.csv";
}

struct DetectedPack {
  std::filesystem::path infoFile;
  std::filesystem::path chartFile;
  std::filesystem::path musicFile;
  std::filesystem::path bgFile;
};

DetectedPack detectPack(const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& extracted) {
  DetectedPack pack;
  const auto baseName = [](const std::filesystem::path& file) {
    return toLower(file.filename().string());
  };

  // First pass: match the conventional file names.
  for (const auto& file : extracted) {
    const std::string name = baseName(file);
    if (pack.infoFile.empty() && isInfoName(name)) pack.infoFile = file;
    if (pack.chartFile.empty()
        && (name == "chart.json" || name == "chart.pec" || name == "chart.rpe")) {
      pack.chartFile = file;
    }
    if (pack.bgFile.empty() && isImageCandidate(file)
        && (name == "image.png" || name == "bg.png" || name.find("image") != std::string::npos
            || name.find("bg") != std::string::npos)) {
      pack.bgFile = file;
    }
    if (pack.musicFile.empty() && isAudioCandidate(file)
        && (name.find("music") != std::string::npos || name.find("song") != std::string::npos)) {
      pack.musicFile = file;
    }
  }

  // Second pass: any file with a matching extension.
  for (const auto& file : extracted) {
    if (pack.musicFile.empty() && isAudioCandidate(file)) pack.musicFile = file;
    if (pack.bgFile.empty() && isImageCandidate(file)) pack.bgFile = file;
    if (pack.infoFile.empty() && isInfoName(baseName(file))) pack.infoFile = file;
  }

  // Third pass: detect the chart from its content. Metadata and replay files are never
  // charts even when their payload looks similar.
  if (pack.chartFile.empty() || !isLikelyChartFile(pack.chartFile)) {
    pack.chartFile.clear();
    for (const auto& file : extracted) {
      const std::string name = baseName(file);
      if (name == "info.json" || name == "extra.json" || name == "respack.json"
          || name == "replay.json") {
        continue;
      }
      if (isLikelyChartFile(file)) {
        pack.chartFile = file;
        break;
      }
    }
  }

  // Resolve the references the metadata file declares; when a declared path does not exist,
  // look for the file by name and then for any matching file beside the metadata file.
  bool infoHasBg = false;
  if (!pack.infoFile.empty()) {
    const auto info = readInfoFile(pack.infoFile);
    const auto infoDir = pack.infoFile.parent_path();

    const auto chartFromInfo = resolveRelative(infoDir, info.chart);
    if (!chartFromInfo.empty() && platform::fileExists(chartFromInfo)
        && isLikelyChartFile(chartFromInfo)) {
      pack.chartFile = chartFromInfo;
    } else if (!chartFromInfo.empty() && !platform::fileExists(chartFromInfo)) {
      auto found = findByInfoName(root, info.chart, isLikelyChartFile);
      if (found.empty()) found = findPackChart(root, infoDir);
      if (!found.empty()) pack.chartFile = found;
    }

    const auto musicFromInfo = resolveRelative(infoDir, info.music);
    if (!musicFromInfo.empty() && platform::fileExists(musicFromInfo)) {
      pack.musicFile = musicFromInfo;
    } else if (!musicFromInfo.empty()) {
      auto found = findByInfoName(root, info.music, isAudioCandidate);
      if (found.empty()) found = findPackAudio(root, infoDir);
      if (!found.empty()) pack.musicFile = found;
    }

    const auto bgFromInfo = resolveRelative(infoDir, info.illustration);
    if (!bgFromInfo.empty() && platform::fileExists(bgFromInfo)) {
      pack.bgFile = bgFromInfo;
      infoHasBg = true;
    } else if (!bgFromInfo.empty()) {
      auto found = findByInfoName(root, info.illustration, isImageCandidate);
      if (found.empty()) found = findPackImage(root, infoDir);
      if (!found.empty()) {
        pack.bgFile = found;
        infoHasBg = true;
      }
    }
  }

  // Chart-embedded metadata fills the background (unless the metadata file declared one) and
  // a missing music track.
  if (!pack.chartFile.empty() && isLikelyChartFile(pack.chartFile)) {
    const auto meta = readInfoFromChartFile(pack.chartFile);
    const auto chartDir = pack.chartFile.parent_path();

    if (!infoHasBg) {
      const auto bgFromMeta = resolveRelative(chartDir, meta.illustration);
      if (!bgFromMeta.empty() && platform::fileExists(bgFromMeta)) {
        pack.bgFile = bgFromMeta;
      } else if (!bgFromMeta.empty()) {
        auto found = findByInfoName(root, meta.illustration, isImageCandidate);
        if (found.empty()) found = findPackImage(root, chartDir);
        if (!found.empty()) pack.bgFile = found;
      } else {
        auto found = findPackImage(root, chartDir);
        if (!found.empty()) pack.bgFile = found;
      }
    }

    if (pack.musicFile.empty() || !platform::fileExists(pack.musicFile)) {
      const auto musicFromMeta = resolveRelative(chartDir, meta.music);
      if (!musicFromMeta.empty() && platform::fileExists(musicFromMeta)) {
        pack.musicFile = musicFromMeta;
      } else if (!musicFromMeta.empty()) {
        auto found = findByInfoName(root, meta.music, isAudioCandidate);
        if (found.empty()) found = findPackAudio(root, chartDir);
        if (!found.empty()) pack.musicFile = found;
      }
    }
  }

  // Final safety net: scan everything once more, preferring files at the package root.
  if (pack.chartFile.empty() || !isLikelyChartFile(pack.chartFile)) {
    pack.chartFile = findPackChart(root, root);
  }
  if (pack.musicFile.empty() || !platform::fileExists(pack.musicFile)) {
    pack.musicFile = findPackAudio(root, root);
  }
  if (pack.bgFile.empty() || !platform::fileExists(pack.bgFile)) {
    pack.bgFile = findPackImage(root, root);
  }
  return pack;
}

} // namespace

bool ChartRecord::complete() const noexcept {
  return platform::fileExists(chartFile) && platform::fileExists(musicFile)
      && platform::fileExists(bgFile);
}

int ChartRecord::gradeIndex() const noexcept {
  return gradeIndexForScore(bestScore, false);
}

int ChartRecord::bestGradeIndex() const noexcept {
  return bestScore > 0 ? gradeIndexForScore(bestScore, false) : -1;
}

const char* ChartRecord::gradeName() const noexcept {
  static constexpr const char* kNames[] = {"F", "C", "B", "A", "S", "V", "FC", "AP"};
  return kNames[gradeIndexForScore(bestScore, false)];
}

const char* ChartRecord::bestGradeName() const noexcept {
  static constexpr const char* kNames[] = {"F", "C", "B", "A", "S", "V", "FC", "AP"};
  return kNames[gradeIndexForScore(bestScore, false)];
}

std::string formatScore(int score) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", std::clamp(score, 0, 9999999));
  return buffer;
}

std::string formatAccuracy(double accuracy) {
  const int hundredths = static_cast<int>(std::lround(std::clamp(accuracy, 0.0, 1.0) * 10000.0));
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d.%02d%%", hundredths / 100, hundredths % 100);
  return buffer;
}

int gradeIndexForScore(int score, bool fullCombo) {
  if (score >= 1000000) return 7;
  if (fullCombo) return 6;
  if (score < 700000) return 0;
  if (score < 820000) return 1;
  if (score < 880000) return 2;
  if (score < 920000) return 3;
  if (score < 960000) return 4;
  return 5;
}

ChartLibrary::ChartLibrary(std::filesystem::path packsRoot, std::filesystem::path scoreFile)
    : packsRoot_(std::move(packsRoot)), scoreFile_(std::move(scoreFile)) {}

const std::vector<ChartRecord>& ChartLibrary::records(bool force) {
  if (loaded_ && !force) return records_;
  if (!scoresLoaded_) loadScores();
  records_.clear();
  std::error_code error;
  std::filesystem::create_directories(packsRoot_, error);
  if (std::filesystem::is_directory(packsRoot_, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(packsRoot_, error)) {
      if (error) break;
      if (!entry.is_directory(error)) continue;
      const std::string id = entry.path().filename().string();
      if (id.size() > 4 && id.compare(id.size() - 4, 4, ".tmp") == 0) continue;
      auto record = loadRecordInternal(id);
      if (record && record->complete()) records_.push_back(std::move(*record));
    }
  }
  sortRecords();
  loaded_ = true;
  return records_;
}

std::optional<ChartRecord> ChartLibrary::find(const std::string& id) {
  const auto& all = records();
  for (const auto& record : all) {
    if (record.id == id) return record;
  }
  return std::nullopt;
}

std::optional<ChartRecord> ChartLibrary::loadRecord(const std::string& id) {
  return loadRecordInternal(id);
}

std::string ChartLibrary::findIdForPath(const std::filesystem::path& path) {
  std::error_code error;
  const auto target = std::filesystem::weakly_canonical(path, error);
  if (error) return {};
  const auto root = std::filesystem::weakly_canonical(packsRoot_, error);
  if (error) return {};
  const auto targetText = target.generic_string();
  const auto rootText = root.generic_string();
  if (targetText.rfind(rootText + "/", 0) != 0 && targetText != rootText) return {};
  const auto relative = std::filesystem::relative(target, root, error);
  if (error || relative.empty()) return {};
  return relative.begin()->string();
}

std::optional<ChartRecord> ChartLibrary::loadRecordInternal(const std::string& id) {
  if (id.empty()) return std::nullopt;
  const auto root = packsRoot_ / id;
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) return std::nullopt;

  const auto infoFile = findInfoFile(root);
  ChartInfo info = infoFile.empty() ? ChartInfo{} : readInfoFile(infoFile);

  auto chartFile = resolveRelative(root, info.chart);
  if (!chartFile.empty() && !platform::fileExists(chartFile)) chartFile.clear();
  auto musicFile = resolveRelative(root, info.music);
  if (!musicFile.empty() && !platform::fileExists(musicFile)) musicFile.clear();
  auto bgFile = resolveRelative(root, info.illustration);
  if (!bgFile.empty() && !platform::fileExists(bgFile)) bgFile.clear();

  if (chartFile.empty()) chartFile = findByInfoName(root, info.chart, isChartCandidate);
  if (musicFile.empty()) musicFile = findByInfoName(root, info.music, isAudioCandidate);
  if (bgFile.empty()) bgFile = findByInfoName(root, info.illustration, isImageCandidate);
  if (chartFile.empty()) chartFile = findChartFile(root);
  if (musicFile.empty()) musicFile = findAudioFile(root);
  if (bgFile.empty()) bgFile = findImageFile(root);

  const auto canonicalInfo = root / "info.json";
  bool needsWrite = !platform::fileExists(canonicalInfo);
  if (!chartFile.empty() && relativize(root, chartFile) != info.chart) {
    info.chart = relativize(root, chartFile);
    needsWrite = true;
  }
  if (!musicFile.empty() && relativize(root, musicFile) != info.music) {
    info.music = relativize(root, musicFile);
    needsWrite = true;
  }
  if (!bgFile.empty() && relativize(root, bgFile) != info.illustration) {
    info.illustration = relativize(root, bgFile);
    needsWrite = true;
  }
  // The chart's own metadata block back-fills every field the metadata file left out, one
  // field at a time; it is not gated on the song name being absent.
  if (!chartFile.empty()) {
    const auto meta = readInfoFromChartFile(chartFile);
    const auto fill = [&needsWrite](std::string& target, const std::string& candidate) {
      if (!target.empty() || candidate.empty()) return;
      target = candidate;
      needsWrite = true;
    };
    fill(info.name, meta.name);
    fill(info.level, meta.level);
    fill(info.difficulty, meta.difficulty);
    fill(info.composer, meta.composer);
    fill(info.charter, meta.charter);
    fill(info.illustrator, meta.illustrator);
    fill(info.music, meta.music);
    fill(info.illustration, meta.illustration);
  }
  if (info.name.empty()) {
    info.name = chartFile.empty() ? id : chartFile.stem().string();
    needsWrite = true;
  }
  if (!info.hasChartOffset) {
    info.chartOffsetMs = 0;
    needsWrite = true;
  }
  if (needsWrite) writeInfoJson(canonicalInfo, info);

  ChartRecord record;
  record.id = id;
  record.rootDir = root;
  record.chartFile = chartFile;
  record.musicFile = musicFile;
  record.bgFile = bgFile;
  record.infoFile = canonicalInfo;
  record.info = info;
  record.songName = info.name;
  record.difficulty = buildDifficultyLabel(info, chartFile);
  if (record.difficulty.empty()) record.difficulty = std::string("SP") + "\xE2\x80\x82" + "Lv.?";
  record.chartOffsetMs = info.chartOffsetMs;
  const auto scores = scoreFor(id);
  record.bestScore = scores.bestScore;
  record.bestAcc = scores.bestAcc;
  record.lastPlayedAtMs = scores.lastPlayedAtMs;
  record.modifiedAtMs = recordModifiedAt(root, chartFile, musicFile, bgFile, canonicalInfo);
  return record;
}

ChartLibrary::ImportResult ChartLibrary::importZip(const std::filesystem::path& archive,
    bool requireComplete) {
  if (!platform::fileExists(archive)) throw std::runtime_error("Package not found");
  std::uint64_t size = 0;
  const std::string id = hashFile(archive, size);
  std::error_code error;
  std::filesystem::create_directories(packsRoot_, error);
  const auto destination = packsRoot_ / id;
  if (std::filesystem::is_directory(destination, error)) {
    if (auto existing = loadRecordInternal(id); existing && existing->complete()) {
      return ImportResult{std::move(*existing), false};
    }
  }
  const auto temporary = packsRoot_ / (id + ".tmp");
  platform::removeRecursively(temporary);
  std::filesystem::create_directories(temporary, error);
  if (error) throw std::runtime_error("Cannot create import directory");
  try {
    platform::extractArchive(archive, temporary);
    return installDirectory(temporary, id, archive.stem().string(), requireComplete);
  } catch (...) {
    platform::removeRecursively(temporary);
    throw;
  }
}

ChartLibrary::ImportResult ChartLibrary::importDirectory(const std::filesystem::path& directory) {
  if (!platform::directoryExists(directory)) throw std::runtime_error("Directory not found");
  const auto absolute = std::filesystem::weakly_canonical(directory);
  const std::string id = hashPath(absolute);
  std::error_code error;
  std::filesystem::create_directories(packsRoot_, error);
  const auto temporary = packsRoot_ / (id + ".tmp");
  platform::removeRecursively(temporary);
  std::filesystem::create_directories(temporary, error);
  if (error) throw std::runtime_error("Cannot create import directory");
  try {
    std::filesystem::copy(directory, temporary,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) throw std::runtime_error("Cannot copy chart directory");
    return installDirectory(temporary, id, absolute.filename().string(), false);
  } catch (...) {
    platform::removeRecursively(temporary);
    throw;
  }
}

ChartLibrary::ImportResult ChartLibrary::installDirectory(const std::filesystem::path& temporary,
    const std::string& id, const std::string& fallbackName, bool requireComplete) {
  const auto extracted = platform::listFiles(temporary);
  const DetectedPack pack = detectPack(temporary, extracted);
  const bool complete = !pack.chartFile.empty() && !pack.musicFile.empty() && !pack.bgFile.empty();
  if (requireComplete && !complete) throw std::runtime_error("Package is missing required files");

  ChartInfo info = pack.infoFile.empty() ? ChartInfo{} : readInfoFile(pack.infoFile);
  const ChartInfo meta =
      pack.chartFile.empty() ? ChartInfo{} : readInfoFromChartFile(pack.chartFile);
  const auto fill = [](std::string& target, const std::string& candidate) {
    if (target.empty() && !candidate.empty()) target = candidate;
  };
  fill(info.name, meta.name);
  fill(info.level, meta.level);
  fill(info.difficulty, meta.difficulty);
  fill(info.composer, meta.composer);
  fill(info.charter, meta.charter);
  fill(info.illustrator, meta.illustrator);
  if (info.name.empty()) info.name = fallbackName.empty() ? id : fallbackName;
  if (!pack.chartFile.empty()) info.chart = relativize(temporary, pack.chartFile);
  if (!pack.musicFile.empty()) info.music = relativize(temporary, pack.musicFile);
  if (!pack.bgFile.empty()) info.illustration = relativize(temporary, pack.bgFile);
  if (!info.hasChartOffset) {
    info.chartOffsetMs = 0;
    info.hasChartOffset = true;
  }
  const std::string label = buildDifficultyLabel(info, pack.chartFile);
  if (info.level.empty() && !label.empty()) info.level = label;
  writeInfoJson(temporary / "info.json", info);

  const auto destination = packsRoot_ / id;
  platform::removeRecursively(destination);
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::error_code copyError;
    std::filesystem::copy(temporary, destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        copyError);
    if (copyError) throw std::runtime_error("Cannot install package");
    platform::removeRecursively(temporary);
  }
  auto record = loadRecordInternal(id);
  if (!record || !record->complete()) {
    platform::removeRecursively(destination);
    throw std::runtime_error("Package is missing required files");
  }
  invalidate();
  return ImportResult{std::move(*record), true};
}

bool ChartLibrary::remove(const std::vector<std::string>& ids) {
  bool removed = false;
  for (const auto& id : ids) {
    if (id.empty()) continue;
    const auto directory = packsRoot_ / platform::sanitiseFileName(id);
    if (platform::removeRecursively(directory)) removed = true;
    scores_.erase(id);
  }
  if (removed) saveScores();
  invalidate();
  return removed;
}

bool ChartLibrary::updateInfo(const std::string& id, const std::string& name,
    const std::string& difficulty, int chartOffsetMs) {
  auto record = loadRecordInternal(id);
  if (!record) return false;
  auto info = record->info;
  info.name = name;
  info.level = difficulty;
  info.difficulty = difficulty;
  info.chartOffsetMs = chartOffsetMs;
  info.hasChartOffset = true;
  if (!writeInfoJson(record->rootDir / "info.json", info)) return false;
  invalidate();
  return true;
}

void ChartLibrary::recordBestResult(const std::string& id, const game::PlayResult& result) {
  if (id.empty()) return;
  if (!scoresLoaded_) loadScores();
  auto& entry = scores_[id];
  if (result.score > entry.bestScore) entry.bestScore = result.score;
  const int accuracy = static_cast<int>(std::lround(result.accuracy * 10000.0));
  if (accuracy > entry.bestAcc) entry.bestAcc = accuracy;
  saveScores();
  invalidate();
}

void ChartLibrary::recordPlayTime(const std::string& id, std::int64_t timestampMs) {
  if (id.empty()) return;
  if (!scoresLoaded_) loadScores();
  scores_[id].lastPlayedAtMs = std::max<std::int64_t>(scores_[id].lastPlayedAtMs, timestampMs);
  saveScores();
  invalidate();
}

ChartLibrary::ScoreEntry ChartLibrary::scoreFor(const std::string& id) const {
  const auto iterator = scores_.find(id);
  return iterator == scores_.end() ? ScoreEntry{} : iterator->second;
}

void ChartLibrary::loadScores() {
  if (scoresLoaded_) return;
  scoresLoaded_ = true;
  scores_.clear();
  std::ifstream input(scoreFile_, std::ios::binary);
  if (!input) return;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::istringstream stream(line);
    std::string id;
    long long bestScore = 0;
    long long bestAcc = -1;
    long long lastPlayed = 0;
    if (!std::getline(stream, id, '\t')) continue;
    stream >> bestScore >> bestAcc >> lastPlayed;
    if (id.empty()) continue;
    ScoreEntry entry;
    entry.bestScore = static_cast<int>(bestScore);
    entry.bestAcc = static_cast<int>(bestAcc);
    entry.lastPlayedAtMs = lastPlayed;
    scores_[id] = entry;
  }
}

void ChartLibrary::saveScores() const {
  std::ostringstream stream;
  std::vector<std::string> ids;
  ids.reserve(scores_.size());
  for (const auto& [id, entry] : scores_) {
    if (entry.bestScore == 0 && entry.bestAcc < 0 && entry.lastPlayedAtMs == 0) continue;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  for (const auto& id : ids) {
    const auto& entry = scores_.at(id);
    stream << id << '\t' << entry.bestScore << '\t' << entry.bestAcc << '\t'
           << entry.lastPlayedAtMs << '\n';
  }
  platform::writeTextFile(scoreFile_, stream.str());
}

void ChartLibrary::sortRecords() {
  std::sort(records_.begin(), records_.end(), [](const ChartRecord& left, const ChartRecord& right) {
    const bool leftPlayed = left.lastPlayedAtMs > 0;
    const bool rightPlayed = right.lastPlayedAtMs > 0;
    if (leftPlayed != rightPlayed) return leftPlayed;
    if (leftPlayed && left.lastPlayedAtMs != right.lastPlayedAtMs) {
      return left.lastPlayedAtMs > right.lastPlayedAtMs;
    }
    if (left.modifiedAtMs != right.modifiedAtMs) return left.modifiedAtMs > right.modifiedAtMs;
    const std::string leftName = toLower(left.songName);
    const std::string rightName = toLower(right.songName);
    if (leftName != rightName) return leftName < rightName;
    return toLower(left.id) < toLower(right.id);
  });
}

namespace {

// One candidate from the directory itself, sorted by path so parallel files resolve the same
// way on every platform. Subdirectories are deliberately not visited.
template <typename Predicate>
std::filesystem::path sidecarDirectFile(
    const std::filesystem::path& directory, Predicate predicate) {
  if (directory.empty()) return {};
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) return {};
  std::vector<std::filesystem::path> found;
  for (std::filesystem::directory_iterator iterator(directory, error), end; iterator != end;
       iterator.increment(error)) {
    if (error) break;
    if (!iterator->is_regular_file(error)) continue;
    const std::filesystem::path path = iterator->path();
    if (predicate(path)) found.push_back(path);
  }
  std::sort(found.begin(), found.end());
  return found.empty() ? std::filesystem::path{} : found.front();
}

bool isInfoFileName(const std::filesystem::path& file) {
  const std::string name = toLower(file.filename().string());
  return name == "info.json" || name == "info.yml" || name == "info.yaml" || name == "info.txt"
      || name == "info.csv";
}

} // namespace

std::filesystem::path findSidecarInfoFile(const std::filesystem::path& directory) {
  return sidecarDirectFile(directory, isInfoFileName);
}

std::filesystem::path findSidecarMusicFile(const std::filesystem::path& directory) {
  return sidecarDirectFile(directory, isAudioCandidate);
}

std::filesystem::path findSidecarImageFile(const std::filesystem::path& directory) {
  return sidecarDirectFile(directory, isImageCandidate);
}

} // namespace rechigros::app
