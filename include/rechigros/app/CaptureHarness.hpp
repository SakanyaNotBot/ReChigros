#pragma once

#include <string>

namespace rechigros::app {

struct AppContext;

// Opens a named screen with representative state before the main loop starts. Used by the
// automated capture runs so one screen can be rendered and compared without walking the
// interface with synthetic input. Unknown names leave the application on the start screen.
//
// Supported names: home, library, library-online, library-manage, settings-video,
// settings-audio, settings-replay, settings-more, detail, result, calibration, game,
// game-pause, game-practice.
// An aspect ratio override of 0 keeps the reference capture value; a positive value lets a
// capture reproduce the narrow-stage treatment.
void setupCaptureScreen(AppContext& context, const std::string& screen,
    float aspectRatioOverride = 0.0F);

} // namespace rechigros::app
