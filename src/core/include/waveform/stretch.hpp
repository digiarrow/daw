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

enum class WarpMode
{
    Beats = 0,
    Tones = 1,
    Complex = 2
};

struct WarpRenderSettings
{
    int block = 1024;
    int interval = 256;
};

/// Block size used by the stretcher. Beats stays short, tones stays long, complex is in between.
[[nodiscard]] WarpRenderSettings warpRenderSettings (WarpMode mode, int inputFrames, int outputFrames) noexcept;

/// Autocorrelation tempo guess between 60 and 180 BPM. Returns 0 when the buffer is too short.
[[nodiscard]] double detectTempoBpm (const float* samples, std::int64_t frames, double sampleRate) noexcept;

/// Two warp markers that make `fileSeconds` of audio occupy its detected musical length.
[[nodiscard]] std::vector<WarpMarker> autoWarpMarkers (double fileSeconds, double detectedBpm) noexcept;

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
                    std::vector<float>& right,
                    WarpMode mode = WarpMode::Complex);

} // namespace waveform
