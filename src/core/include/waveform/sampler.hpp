#pragma once

#include "waveform/synth.hpp"

#include <cstdint>

namespace waveform
{

struct SamplerZoneView
{
    int rootNote = 60;
    int lowNote = 0;
    int highNote = 127;
    const float* left = nullptr;
    const float* right = nullptr;
    std::int64_t frames = 0;
    double sourceSampleRate = 48000.0;
};

/// Plays sampler zones for notes that are sounding in this block. Adds into the buffers.
void renderSamplerNotes (const SoundingNote* notes,
                         int noteCount,
                         const SamplerZoneView* zones,
                         int zoneCount,
                         int frames,
                         double sampleRate,
                         const SynthPatch& envelope,
                         float leftGain,
                         float rightGain,
                         float* left,
                         float* right) noexcept;

} // namespace waveform
