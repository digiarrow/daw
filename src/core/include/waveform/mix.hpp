#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace waveform
{

struct PanGains
{
    float left = 1.0f;
    float right = 1.0f;
};

/// Constant-power pan. -1 is hard left, 0 is centre (-3 dB each side), +1 is hard right.
[[nodiscard]] inline PanGains constantPowerPan (float pan) noexcept
{
    constexpr float kPiOverFour = 0.7853981633974483f;
    const float clamped = std::clamp (pan, -1.0f, 1.0f);
    const float angle = (clamped + 1.0f) * kPiOverFour;
    return PanGains { std::cos (angle), std::sin (angle) };
}

/// Gains at or below -100 dB become silence. NaN becomes silence.
[[nodiscard]] inline float dbToLinear (double decibels) noexcept
{
    if (! (decibels > -100.0) || std::isnan (decibels))
        return 0.0f;

    return static_cast<float> (std::pow (10.0, decibels / 20.0));
}

struct ClipBufferView
{
    const float* left = nullptr;
    const float* right = nullptr; ///< Null means the clip is mono and `left` feeds both sides.
    std::int64_t frames = 0;
};

struct AudioClipView
{
    ClipBufferView source;
    std::int64_t timelineStartFrame = 0;
    std::int64_t sourceOffsetFrame = 0;
    std::int64_t timelineLengthFrames = 0;
    std::int64_t fadeInFrames = 0;
    std::int64_t fadeOutFrames = 0;
    double envelopePosition[8] {};
    double envelopeValue[8] {};
    int envelopeCount = 0;
    float gain = 1.0f;
};

struct TrackMix
{
    const AudioClipView* clips = nullptr;
    int clipCount = 0;
    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
};

struct MixView
{
    const TrackMix* tracks = nullptr;
    int trackCount = 0;
    float masterGain = 1.0f;
};

/// Overwrites `outLeft` and `outRight` with a linear mix of one contiguous timeline region.
/// The caller splits loop wraps into separate calls. No allocation, no locks.
void mixBlock (const MixView& mix,
               std::int64_t timelineStart,
               int frames,
               float* outLeft,
               float* outRight) noexcept;

} // namespace waveform
