#pragma once

#include <cstdint>

namespace waveform
{

inline constexpr int kMaxRouteTracks = 64;
inline constexpr int kMaxBuses = 16;
inline constexpr int kMaxSends = 2;

struct RouteEndpoint
{
    int outputBus = 0; ///< 0 is the master. Later buses are groups, in group order.
    bool group = false;
    bool mute = false;
    bool solo = false;
    float gain = 1.0f;
    float panLeft = 0.70710678f;
    float panRight = 0.70710678f;
    int sendBus[kMaxSends] { -1, -1 };
    float sendGain[kMaxSends] { 0.0f, 0.0f };
    bool sendPre[kMaxSends] { false, false };
};

/// Sums track buffers into buses. Group tracks contribute their bus back to the master.
/// `trackL/R` and `busL/R` each hold `frames` samples. Does not allocate.
void routeTracks (const float* const* trackLeft,
                  const float* const* trackRight,
                  const RouteEndpoint* routes,
                  int trackCount,
                  float* const* busLeft,
                  float* const* busRight,
                  int busCount,
                  int frames) noexcept;

} // namespace waveform
