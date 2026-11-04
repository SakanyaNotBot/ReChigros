#include "ImportDialogs.hpp"

#include "ImportCore.hpp"

#include "rechigros/app/Screens.hpp"
#include "rechigros/platform/FileDialog.hpp"

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

// The asynchronous picker copies a chosen document into the application cache before it
// reports the path, so that copy can be deleted once the pack has been consumed. A
// document addressed by its own path (for example a file opened from shared storage)
// belongs to the user and is never touched.
bool isPickerCacheCopy(const std::filesystem::path& path) {
  if (path.empty()) return false;
  const auto parent = path.parent_path();
  return parent.filename() == "incoming" && parent.parent_path().filename() == "cache";
}

void removePickerCacheCopy(const std::filesystem::path& path) {
  if (!isPickerCacheCopy(path)) return;
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    std::filesystem::remove_all(path, error);
  } else {
    std::filesystem::remove(path, error);
  }
}

} // namespace

bool importChartPackageFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openChartPackage();
  if (chosen.empty()) return false;
  return finishChartImport(context, importChartPackage(context.library, chosen, false));
}

bool importChartFolderFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openChartFolder();
  if (chosen.empty()) return false;
  return finishChartImport(context, importChartDirectory(context.library, chosen));
}

bool importChartFilesFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openChartSourceFiles();
  if (chosen.empty()) return false;
  return finishChartImport(context, importLooseChartFiles(context.library, chosen));
}

bool importReplayFromDialog(AppContext& context) {
  const auto chosen = platform::FileDialog::openReplayFile();
  if (chosen.empty()) return false;
  notify(context, importReplayArchive(chosen, context.replays).message);
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
  return true;
}

bool handlePickerResult(AppContext& context, std::string_view payload) {
  const auto separator = payload.find('|');
  if (separator == std::string_view::npos) return false;
  const std::string_view kind = payload.substr(0, separator);
  const std::string_view text = payload.substr(separator + 1);
  if (text.empty()) return false;
  const std::filesystem::path path(std::u8string(text.begin(), text.end()));
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
    if (outcome.imported) removePickerCacheCopy(path);
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
