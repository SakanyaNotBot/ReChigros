#pragma once

#include <array>
#include <cstddef>
#include <vector>

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
// As above, but the chart and the audio are read on a worker thread behind the play screen's
// loading panel. The interface uses this so a press answers immediately.
void startChartWithLoading(AppContext& context, bool practice);
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
// Loads the chart the recording page describes, so the page can start a render without a play
// session having been started first. Returns false when the chart cannot be loaded.
bool prepareRenderChart(AppContext& context, std::string& error);
// Releases the chart the recording page loaded, if it is the only thing holding one.
void releaseRenderChart(AppContext& context);
// Recording configuration: the parameters a render uses, the start action and its progress.
void drawRenderScreen(AppContext& context);
// Draws the recording progress overlay above the current screen, and advances the recording.
// Returns true while a recording is running.
bool updateRecording(AppContext& context);
// Draws the modal progress plate with its cancel action.
void drawRecordingProgress(AppContext& context);
void drawCalibrationScreen(AppContext& context);

// Leaves the calibration page, optionally keeping the adjusted offset, and returns to the page it
// was opened from. Exposed so the exit path can be exercised without a pointer.
void exitCalibration(AppContext& context, bool apply);
// Read-only stage diagnostics for exercising taps while the falling note is between draws.
void calibrationTapMarkerDiagnostics(float& markerY, bool& markerVisible,
    std::size_t& touchMarkerCount);

// Pressable rectangles the settings page registered while drawing, as {x0, y0, x1, y1} in view
// pixels. The page reports them so a test can press a row without hard-coding coordinates that
// shift whenever a row is added above it.
[[nodiscard]] std::vector<std::array<float, 4>> registeredSettingsControlRects();
void drawResultScreen(AppContext& context);
// Whether the settlement's judgement distribution overlay is open. The page keeps it as
// per-visit state, so the entrance and a re-entry can be checked without a pointer.
[[nodiscard]] bool resultHistogramVisible();
void drawGameOverlay(AppContext& context);
void drawToast(AppContext& context);

// Gameplay control used by the main loop.
bool gameEnter(AppContext& context);
// Starts a run whose chart is parsed on a worker thread. The loading panel is drawn until it
// finishes; pollChartLoad consumes the result once per frame.
void gameEnterWithLoading(AppContext& context);
void pollChartLoad(AppContext& context);
void gameUpdate(AppContext& context, float frameDeltaSec);
void gameRender(AppContext& context);
bool captureGameplayFrame(AppContext& context, int width, int height,
    std::vector<std::uint8_t>& pixels);
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
