#pragma once

#include "waveform/project.hpp"

namespace waveform
{

struct PadVoice
{
    int pad = -1;
    int chokeGroup = 0;
    float ageSeconds = 0.0f;
    bool playing = false;
};

/// Starts a pad and silences any other playing pad in the same choke group.
[[nodiscard]] int strikeDrumRack (PadVoice* voices, int capacity, const DrumRack& rack, int note) noexcept;

/// Renders playing pads and advances their ages. Each pad's chain runs on that pad alone.
void renderDrumRack (PadVoice* voices,
                     int voiceCount,
                     const DrumRack& rack,
                     int frames,
                     double sampleRate,
                     float* left,
                     float* right) noexcept;

/// Four voices owned by one drum track. Tracks do not share a pool.
[[nodiscard]] PadVoice* drumVoicePool (int trackIndex) noexcept;

/// Renders the whole block in short slices, with no upper bound on `frames`.
void mixDrumRack (PadVoice* voices,
                  int voiceCount,
                  const DrumRack& rack,
                  int frames,
                  double sampleRate,
                  float leftGain,
                  float rightGain,
                  float* left,
                  float* right) noexcept;

} // namespace waveform
