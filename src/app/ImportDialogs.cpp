#include "ImportDialogs.hpp"

#include "ImportCore.hpp"

#include "rechigros/app/Screens.hpp"
#include "rechigros/platform/FileDialog.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace rechigros::app {
namespace {

// Shared tail of a chart import: report the result, refresh the library and open the
// imported chart, exactly like the reference import action does.
bool finishChartImport(AppContext& context, const ImportOutcome& outcome) {
  notify(context, outcome.message);
  if (!outcome.imported) return true;
  context.library.invalidate();
  if (!outcome.chartId.empty()) openChartDetail(context, outcome.chartId);
  return true;
}

// The picker copies a chosen document into the application cache before it reports the
// path, so that copy can be deleted once the pack has been consumed. A document addressed
// by its own path (for example a file opened from shared storage) belongs to the user and
// is never touched.
bool isPickerCacheCopy(const std::filesystem::path& path) {
  if (path.empty()) return false;
  const auto parent = path.parent_path();
  if (parent.filename() == "incoming" && parent.parent_path().filename() == "cache") {
    return true;
  }
  // A picker may give every answer its own request directory below incoming, which keeps
  // identical file names from different providers from overwriting each other.
  const auto grandParent = parent.parent_path();
  return grandParent.filename() == "incoming"
      && grandParent.parent_path().filename() == "cache";
}

void removePickerCacheCopy(const std::filesystem::path& path) {
  if (!isPickerCacheCopy(path)) return;
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    std::filesystem::remove_all(path, error);
  } else {
    std::filesystem::remove(path, error);
  }
  // Drop the request directory once its last answer has been consumed.
  const auto parent = path.parent_path();
  if (parent.filename() == "incoming") return;
  if (!std::filesystem::is_directory(parent, error)) return;
  if (!std::filesystem::is_empty(parent, error)) return;
  std::filesystem::remove(parent, error);
}

void removePickerCacheCopies(const std::array<std::filesystem::path, 3>& paths) {
  for (const auto& path : paths) removePickerCacheCopy(path);
}

// Loose files are selected one at a time so the chart, audio track and illustration may
// come from different folders or document providers. The async mobile picker answers
// through handlePickerResult(), which advances this state.
enum class LooseStage { Idle, Chart, Audio, Image };

struct LooseSelection {
  LooseStage stage{LooseStage::Idle};
  std::array<std::filesystem::path, 3> paths{};
};

LooseSelection& looseSelection() {
  static LooseSelection selection;
  return selection;
}

void clearLooseSelection(bool removeCopies) {
  LooseSelection& selection = looseSelection();
  if (removeCopies) removePickerCacheCopies(selection.paths);
  selection.paths = {};
  selection.stage = LooseStage::Idle;
}

bool beginLooseFileSelection() {
  clearLooseSelection(true);
  looseSelection().stage = LooseStage::Chart;
  (void)platform::FileDialog::openChartFile();
  return true;
}

bool handleLooseFileResult(AppContext& context, std::string_view kind,
    const std::filesystem::path& path) {
  LooseSelection& selection = looseSelection();
  if (kind == "chartfile") {
    if (selection.stage != LooseStage::Chart) {
      removePickerCacheCopy(path);
      return true;
    }
    selection.paths[0] = path;
    selection.stage = LooseStage::Audio;
    (void)platform::FileDialog::openAudioFile();
    return true;
  }
  if (kind == "audiofile") {
    if (selection.stage != LooseStage::Audio) {
      removePickerCacheCopy(path);
      return true;
    }
    selection.paths[1] = path;
    selection.stage = LooseStage::Image;
    (void)platform::FileDialog::openImageFile();
    return true;
  }
  if (kind == "imagefile") {
    if (selection.stage != LooseStage::Image) {
      removePickerCacheCopy(path);
      return true;
    }
    selection.paths[2] = path;
    selection.stage = LooseStage::Idle;
    const ImportOutcome outcome = importLooseChartFiles(context.library, {
        selection.paths[0], selection.paths[1], selection.paths[2]});
    (void)finishChartImport(context, outcome);
    if (!outcome.chartId.empty()) removePickerCacheCopies(selection.paths);
    selection.paths = {};
    return true;
  }
  return false;
}

} // namespace

bool importChartPackageFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openChartPackage();
  if (chosen.empty()) return false;
  const ImportOutcome outcome = importChartPackage(context.library, chosen, false);
  (void)finishChartImport(context, outcome);
  if (!outcome.chartId.empty()) removePickerCacheCopy(chosen);
  return true;
}

bool importChartFolderFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openChartFolder();
  if (chosen.empty()) return false;
  const ImportOutcome outcome = importChartDirectory(context.library, chosen);
  (void)finishChartImport(context, outcome);
  if (!outcome.chartId.empty()) removePickerCacheCopy(chosen);
  return true;
}

bool importChartFilesFromDialog(AppContext& context) {
  if (platform::FileDialog::asynchronous()) return beginLooseFileSelection();

  const auto chartFile = platform::FileDialog::openChartFile();
  if (chartFile.empty()) return false;
  const auto audioFile = platform::FileDialog::openAudioFile();
  if (audioFile.empty()) {
    removePickerCacheCopy(chartFile);
    return false;
  }
  const auto imageFile = platform::FileDialog::openImageFile();
  if (imageFile.empty()) {
    removePickerCacheCopy(chartFile);
    removePickerCacheCopy(audioFile);
    return false;
  }
  const ImportOutcome outcome =
      importLooseChartFiles(context.library, {chartFile, audioFile, imageFile});
  (void)finishChartImport(context, outcome);
  if (!outcome.chartId.empty()) {
    removePickerCacheCopy(chartFile);
    removePickerCacheCopy(audioFile);
    removePickerCacheCopy(imageFile);
  }
  return true;
}

bool importReplayFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openReplayFile();
  if (chosen.empty()) return false;
  const ImportOutcome outcome = importReplayArchive(chosen, context.replays);
  notify(context, outcome.message);
  if (outcome.imported || outcome.duplicate) removePickerCacheCopy(chosen);
  return true;
}

bool importSkinFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openSkinPackage();
  if (chosen.empty()) return false;
  const SkinImportOutcome outcome =
      importSkinPackage(chosen, context.dataRoot / "skins");
  notify(context, outcome.message);
  if (outcome.imported && !outcome.skinId.empty()) {
    context.settings.selectedSkin = outcome.skinId;
    context.settingsDirty = true;
  }
  if (outcome.imported) removePickerCacheCopy(chosen);
  return true;
}

bool handlePickerResult(AppContext& context, std::string_view payload) {
  const auto separator = payload.find('|');
  if (separator == std::string_view::npos) return false;
  const std::string_view kind = payload.substr(0, separator);
  const std::string_view text = payload.substr(separator + 1);
  if (kind == "pickercancel") {
    clearLooseSelection(true);
    return true;
  }
  if (kind == "pickererror") {
    notify(context, std::string(text));
    clearLooseSelection(true);
    return true;
  }
  if (text.empty()) return false;
  const std::filesystem::path path(std::u8string(text.begin(), text.end()));
  if (kind == "chartfile" || kind == "audiofile" || kind == "imagefile") {
    return handleLooseFileResult(context, kind, path);
  }
  if (kind == "chartzip") {
    const ImportOutcome outcome = importChartPackage(context.library, path, false);
    (void)finishChartImport(context, outcome);
    // The cache copy has served its purpose once the pack is installed, including the
    // "already in the library" answer; a rejected package is kept for diagnosis.
    if (!outcome.chartId.empty()) removePickerCacheCopy(path);
    return true;
  }
  if (kind == "chartdir") {
    const ImportOutcome outcome = importChartDirectory(context.library, path);
    (void)finishChartImport(context, outcome);
    if (!outcome.chartId.empty()) removePickerCacheCopy(path);
    return true;
  }
  if (kind == "replay") {
    const ImportOutcome outcome = importReplayArchive(path, context.replays);
    notify(context, outcome.message);
    if (outcome.imported || outcome.duplicate) removePickerCacheCopy(path);
    return true;
  }
  if (kind == "skin") {
    const SkinImportOutcome outcome = importSkinPackage(path, context.dataRoot / "skins");
    notify(context, outcome.message);
    if (outcome.imported && !outcome.skinId.empty()) {
      context.settings.selectedSkin = outcome.skinId;
      context.settingsDirty = true;
    }
    if (outcome.imported) removePickerCacheCopy(path);
    return true;
  }
  // An untagged or plain-chart payload belongs to the regular drop handling, which knows how
  // to start a chart that is not a package.
  return false;
}

} // namespace rechigros::app
