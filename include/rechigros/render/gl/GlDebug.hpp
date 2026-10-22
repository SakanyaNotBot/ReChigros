#pragma once

// Optional OpenGL error reporting for device bring-up.
//
// A release run pays nothing for this: every entry point is a no-op until error
// reporting is switched on, which the application does when it is started with the
// diagnostic flag. Once on, each named stage drains the error queue and reports a new
// status code once, so the log names the pass that first failed instead of repeating
// the same message every frame.

namespace rechigros::render::gl {

void setErrorLoggingEnabled(bool enabled) noexcept;
[[nodiscard]] bool errorLoggingEnabled() noexcept;

// Drains the error queue and reports any status code not yet seen for `stage`. The
// queue is always drained so a later stage does not inherit an earlier failure. When
// reporting is off the function does nothing at all.
void logErrors(const char* stage) noexcept;

// Logs the driver, version and shading language strings. Requires a current context.
void logContextInfo() noexcept;

} // namespace rechigros::render::gl
