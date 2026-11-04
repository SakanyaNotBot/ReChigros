#pragma once

#include "rechigros/game/Event.hpp"

#include "rechigros/game/EventCursor.hpp"

#include <array>
#include <string>
#include <vector>

namespace rechigros::game {

class JudgeLine;

namespace EventUtils {

double lerp(double a, double b, double progress) noexcept;

double applyEasing(double progress, const EasingSpec& event) noexcept;
double applyEasing(double progress, const LineEvent& event) noexcept;
double applyEasing(double progress, const MoveEvent& event) noexcept;
double applyEasing(double progress, const ColorEvent& event) noexcept;

double getEventVal(double time, const std::vector<LineEvent>& events) noexcept;
double getEventVal(double time, const std::vector<LineEvent>& events, EventCursor& cursor) noexcept;
double getMoveEventVal1(double time, const std::vector<MoveEvent>& events) noexcept;
double getMoveEventVal1(double time, const std::vector<MoveEvent>& events, EventCursor& cursor) noexcept;
double getMoveEventVal2(double time, const std::vector<MoveEvent>& events) noexcept;
double getMoveEventVal2(double time, const std::vector<MoveEvent>& events, EventCursor& cursor) noexcept;

bool getColorVal(double time, const std::vector<ColorEvent>& events, EventCursor& cursor,
    std::array<float, 3>& out) noexcept;
bool getColorVal(double time, const std::vector<ColorEvent>& events, std::array<float, 3>& out) noexcept;
bool getTextVal(double time, const std::vector<TextEvent>& events, EventCursor& cursor,
    std::string& out);
bool getTextVal(double time, const std::vector<TextEvent>& events, std::string& out);
float getGifVal(double time, const std::vector<GifEvent>& events, EventCursor& cursor) noexcept;

std::string stripStaticTextMarkers(const std::string& text);
std::string calculateTextValue(const std::string& start, const std::string& end, double progress);

double speedAt(const std::vector<SpeedEvent>& events, double time) noexcept;
void initSpeedEvents(std::vector<SpeedEvent>& events);
double getFloorPosition(double time, const std::vector<SpeedEvent>& events) noexcept;
double getFloorPosition(double time, const std::vector<SpeedEvent>& events,
    const std::vector<std::vector<SpeedEvent>>& layers) noexcept;
double getFloorPosition(double time, const JudgeLine& line) noexcept;

// Sequential-time variant that reuses `index` as a hint and keeps it updated.
double floorPositionMonotonic(double time, const std::vector<SpeedEvent>& events, int& index) noexcept;
// Layered variant: sums the per-layer monotonic evaluation, with one cursor per layer. The
// result is identical to getFloorPosition(time, events, layers).
double floorPositionMonotonic(double time, const std::vector<SpeedEvent>& events,
    const std::vector<std::vector<SpeedEvent>>& layers, int& index,
    std::vector<int>& layerIndices) noexcept;

} // namespace EventUtils
} // namespace rechigros::game
