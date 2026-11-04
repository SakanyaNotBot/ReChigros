#pragma once

namespace rechigros::game {

// One hold-body hit burst produced by the gameplay session: the reference spawns one at
// the point where a held note currently meets its judge line, every half beat while the
// hold is active. The renderer consumes these from the frame input and draws one
// hit-effect burst per entry; the index addresses Chart::allNotesSorted.
struct HoldBodyBurst {
  int noteIndex{-1};
  double timeSec{};
  bool perfect{false};
};

// One hold-head hit burst: the reference spawns it when the head is hit, at the place the
// head was at that moment (above the line for an early hit, on the line once the head has
// arrived). The renderer resolves the position because only it knows the judged frame's
// line state.
struct HoldHeadBurst {
  int noteIndex{-1};
  double timeSec{};
  bool perfect{false};
};

} // namespace rechigros::game