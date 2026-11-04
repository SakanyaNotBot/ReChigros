#pragma once

#include <cstddef>
#include <vector>

namespace rechigros::game {

// One recorded judgement, keyed by note index. This mirrors the reference replay entry:
// the type names the note phase that produced it, so a hold contributes a press entry when
// its head activates and a release entry when it settles. Playback re-applies these
// judgements instead of synthesising touches, which makes a replay independent of the
// viewer's input device, window size and timing.
struct ReplayJudgement {
  enum class Type : int {
    Tap = 1,
    Drag = 2,
    HoldPress = 3,
    HoldRelease = 4,
    Flick = 5,
  };

  Type type{Type::Tap};
  // Index into Chart::allNotesSorted; -1 entries are ignored.
  int noteIndex{-1};
  // Judgement index: 0 perfect, 1 good, 2 bad, 3 miss.
  int judgement{0};
  // Chart time of the event in seconds.
  double timeSec{};
};

} // namespace rechigros::game
