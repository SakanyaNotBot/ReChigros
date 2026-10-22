#include "ImportDialogs.hpp"

#include "ImportCore.hpp"

#include "rechigros/app/Screens.hpp"
#include "rechigros/platform/FileDialog.hpp"

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

} // namespace rechigros::app
