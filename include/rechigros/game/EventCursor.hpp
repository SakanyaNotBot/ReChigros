#pragma once

namespace rechigros::game {

// Cursor over a time-sorted event list. Playback advances monotonically, so the
// cursor turns each lookup into an amortised constant-time step instead of a
// scan from the beginning. -1 means "not primed yet".
struct EventCursor {
  int index{-1};
  void reset() noexcept { index = -1; }
};

} // namespace rechigros::game
