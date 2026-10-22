#pragma once

namespace rechigros::game {

// One hold-body hit burst produced by the gameplay session. The reference spawns one at the
// point where a held note currently meets its judge line, every half beat while the hold is
// active. The renderer consumes these from the frame input and draws one hit-effect burst
// per entry; the index addresses Chart::allNotesSorted.
struct HoldBodyBurst {
  int noteIndex{-1};
  double timeSec{};
  bool perfect{false};
};

} // namespace rechigros::game
