#pragma once

#include "waveform/timing.hpp"

#include <cstdint>

namespace waveform
{

/// Phase state for the free-running test tone. The metronome does not need state:
/// its clicks are a function of the timeline so they stay sample-accurate.
struct Oscillator
{
    double phase = 0.0;
};

/// Adds a short sine click on every time-signature beat. Downbeats are higher and louder.
/// `left` and `right` are not cleared; the click is summed in.
void renderMetronome (float* left,
                      float* right,
                      std::int64_t startFrame,
                      int frames,
                      double sampleRate,
                      double bpm,
                      TimeSignature signature,
                      float gain) noexcept;

/// Adds a constant sine. Phase carries across blocks so the tone does not click.
void renderTestTone (float* left,
                     float* right,
                     int frames,
                     double sampleRate,
                     double frequencyHertz,
                     float gain,
                     Oscillator& oscillator) noexcept;

} // namespace waveform
