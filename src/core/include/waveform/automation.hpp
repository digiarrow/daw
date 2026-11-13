#pragma once

#include "waveform/timing.hpp"

#include <string>
#include <vector>

namespace waveform
{

struct AutomationPoint
{
    double quarterNotes = 0.0;
    double value = 0.0;
};

struct AutomationLane
{
    std::string parameter;
    std::vector<AutomationPoint> points;
};

/// Linear value at `quarterNotes`. Returns `fallback` when the lane is empty.
[[nodiscard]] double automationAt (const AutomationLane& lane, double quarterNotes, double fallback) noexcept;

enum class LaunchQuantize
{
    None,
    Beat,
    Bar
};

/// Next quarter-note boundary at or after `position` for a quantized launch.
[[nodiscard]] double nextLaunchQuarter (double position, LaunchQuantize quantize, TimeSignature signature) noexcept;

} // namespace waveform
