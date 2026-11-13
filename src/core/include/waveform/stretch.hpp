#pragma once

#include "waveform/project.hpp"

#include <cstdint>
#include <vector>

namespace waveform
{

struct StretchSource
{
    const float* left = nullptr;
    const float* right = nullptr; ///< Null duplicates `left`.
    std::int64_t frames = 0;
};

/// Bakes warp markers and comp choices into a linear buffer at `sampleRate`.
/// Pitch stays put: duration follows the markers via Signalsmith Stretch (MIT).
/// Take 0 is `primary`. Later takes are `extra`, in order. A missing take is silence.
void renderStretch (const StretchSource& primary,
                    const StretchSource* extra,
                    int extraCount,
                    const std::vector<WarpMarker>& warp,
                    const std::vector<CompSegment>& comp,
                    double sampleRate,
                    double secondsPerQuarter,
                    double offsetSeconds,
                    double lengthSeconds,
                    std::vector<float>& left,
                    std::vector<float>& right);

} // namespace waveform
