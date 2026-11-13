#pragma once

#include "waveform/project.hpp"

#include <cstdint>

namespace waveform
{

/// Filter memory for one track. Indexed by MIDI note so a voice stays continuous across blocks.
struct SynthVoiceMemory
{
    float low[128] {};
    float band[128] {};
};

/// A note the synth should render. Times are seconds relative to the first sample of the block.
/// A negative onset means the note started before this block. `releaseSeconds` is when the key
/// goes up; a very large value means the note is still held.
struct SoundingNote
{
    int note = 60;
    int velocity = 100;
    double onsetSeconds = 0.0;
    double releaseSeconds = 1.0e9;
};

/// Adds the built-in polysynth into `left` and `right`. Does not clear the buffers and does not allocate.
void renderSynthNotes (const SoundingNote* notes,
                       int noteCount,
                       int frames,
                       double sampleRate,
                       const SynthPatch& patch,
                       SynthVoiceMemory& memory,
                       float leftGain,
                       float rightGain,
                       float* left,
                       float* right) noexcept;

} // namespace waveform
