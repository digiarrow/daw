#include "waveform/automation.hpp"

#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{

double automationAt (const AutomationLane& lane, double quarterNotes, double fallback) noexcept
{
    if (lane.points.empty() || std::isnan (quarterNotes))
        return fallback;

    if (quarterNotes <= lane.points.front().quarterNotes)
        return lane.points.front().value;

    if (quarterNotes >= lane.points.back().quarterNotes)
        return lane.points.back().value;

    for (std::size_t index = 1; index < lane.points.size(); ++index)
    {
        const auto& right = lane.points[index];
        const auto& left = lane.points[index - 1];

        if (quarterNotes > right.quarterNotes)
            continue;

        const double span = right.quarterNotes - left.quarterNotes;

        if (! (span > 0.0))
            return right.value;

        const double mix = (quarterNotes - left.quarterNotes) / span;
        return left.value + (right.value - left.value) * mix;
    }

    return fallback;
}

double nextLaunchQuarter (double position, LaunchQuantize quantize, TimeSignature signature) noexcept
{
    if (quantize == LaunchQuantize::None || std::isnan (position))
        return std::max (0.0, position);

    const double grid = quantize == LaunchQuantize::Bar ? Timing::quarterNotesPerBar (signature)
                                                        : Timing::quarterNotesPerBeat (signature);

    if (! (grid > 0.0))
        return std::max (0.0, position);

    const double steps = std::ceil ((position - 1.0e-9) / grid);
    return std::max (0.0, steps * grid);
}

} // namespace waveform
