#pragma once

#include "rechigros/app/AppContext.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

// Screen entry points. Each screen draws itself through ImGui for the current
// frame; navigation is performed by changing AppContext::screen.
namespace rechigros::app {

void navigateTo(AppContext& context, Screen screen);
void commitSettings(AppContext& context);
void notify(AppContext& context, std::string message);

void openChartDetail(AppContext& context, const std::string& chartId);
void startChart(AppContext& context, bool practice);
void startSavedReplay(AppContext& context, const std::string& replayId);
void handleIncomingPath(AppContext& context, const std::filesystem::path& path);
// Starts a chart from an arbitrary location with a replay file layered on top.
// Used by the batch verification mode; the normal UI path goes through
// startSavedReplay so the chart comes from the library.
void startAdhocReplay(AppContext& context, const std::filesystem::path& chartFile,
    const std::filesystem::path& replayFile);

void drawHomeScreen(AppContext& context);
void drawLibraryScreen(AppContext& context);
void drawDetailScreen(AppContext& context);
void drawSettingsScreen(AppContext& context);
void drawCalibrationScreen(AppContext& context);
void drawResultScreen(AppContext& context);
void drawGameOverlay(AppContext& context);
void drawToast(AppContext& context);

// Gameplay control used by the main loop.
bool gameEnter(AppContext& context);
void gameUpdate(AppContext& context, float frameDeltaSec);
void gameRender(AppContext& context);
void gameLeave(AppContext& context);
void gameTogglePause(AppContext& context);
bool gamePointerDown(AppContext& context, std::int64_t pointerId, float x, float y);
void gamePointerMove(AppContext& context, std::int64_t pointerId, float x, float y);
void gamePointerUp(AppContext& context, std::int64_t pointerId);
void gameCancelTouches(AppContext& context);
void gameFocusLost(AppContext& context);
void gameToggleAutoplay(AppContext& context);
void gameRestart(AppContext& context);
void gameQuitToMenu(AppContext& context);

} // namespace rechigros::app
