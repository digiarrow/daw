#pragma once

#include "waveform/project.hpp"

#include <atomic>
#include <cstdint>

namespace waveform
{

/// Tracks that share a wave do not depend on each other and can run together.
/// `dependsOn[i]` is the track that must finish first, or -1. Cycles stay in wave 0.
[[nodiscard]] int assignTrackWaves (const int* dependsOn, int count, int* waves) noexcept;

class WorkerPool
{
public:
    explicit WorkerPool (int threads);
    ~WorkerPool();

    WorkerPool (const WorkerPool&) = delete;
    WorkerPool& operator= (const WorkerPool&) = delete;

    /// Runs `fn(context, index)` for `0 .. count-1`. The caller joins before returning.
    void parallel (int count, void (*fn) (void*, int), void* context);

    [[nodiscard]] int threadCount() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

[[nodiscard]] TimeSignature signatureAt (const SignaturePoint* points, int count, double quarterNotes, TimeSignature fallback) noexcept;

/// Equal-power fades where two clips meet or overlap, so the join is a crossfade.
void crossfadeAdjacent (AudioClip& earlier, AudioClip& later, double tempoBpm) noexcept;

/// Copies a clip through its gain and equal-power fades. This is the freeze/bounce render.
void freezeClipSamples (const float* source, int frames, float gain, int fadeInFrames, int fadeOutFrames, float* destination) noexcept;

void overdubNotes (MidiClip& clip, const MidiNote* notes, int count);

struct PunchSpan
{
    bool enabled = false;
    double startQuarter = 0.0;
    double endQuarter = 4.0;
};

[[nodiscard]] bool noteInPunch (PunchSpan punch, double startQuarter, double lengthQuarter) noexcept;
void applyPunch (std::vector<MidiNote>& notes, PunchSpan punch);

enum class CrossfaderSide
{
    None,
    A,
    B
};

/// Position 0 is fully A. Position 1 is fully B. None stays at unity.
[[nodiscard]] float crossfaderLevel (CrossfaderSide side, float position) noexcept;

/// Linear resample. Returns the number of output frames written, capped by `outCapacity`.
[[nodiscard]] int resampleLinear (const float* input, int inputFrames, double inputRate, float* output, int outCapacity, double outputRate) noexcept;

[[nodiscard]] int hardwareChannel (int pair, int channelInPair, int channelCount) noexcept;

/// Computer keyboard piano. Returns -1 when the key is not a note.
[[nodiscard]] int noteFromTypingKey (int ascii, int octave) noexcept;

struct MidiClockClock
{
    double quarters = 0.0;
};

/// MIDI timing clock is 24 pulses per quarter note. Returns how many pulses fall in this block.
[[nodiscard]] int midiClocksInBlock (double bpm, double sampleRate, int frames, double& quarterPhase) noexcept;

[[nodiscard]] double bpmFromMidiClocks (int clocks, double seconds) noexcept;

struct MediaRecord
{
    const char* name = nullptr;
    const char* tags = nullptr; ///< Space-separated. May be null.
};

[[nodiscard]] bool mediaMatches (MediaRecord record, const char* query) noexcept;

/// Magnitude bands across the buffer. `bands` is not cleared by the caller; this function writes every band.
void spectrumBands (const float* samples, int frames, float* bands, int bandCount) noexcept;

void fillWavetable (float* table, int size, float morph) noexcept;
[[nodiscard]] float wavetableSample (const float* table, int size, double phase) noexcept;

} // namespace waveform
