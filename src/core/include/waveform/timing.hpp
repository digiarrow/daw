#pragma once

#include <cstdint>
#include <string>

namespace waveform
{

/// Musical meter. The denominator is the note value that receives one beat and must be a power of two.
struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    [[nodiscard]] bool isValid() const noexcept;
};

/// A position on the transport, derived from an integer sample index.
///
/// Tempo is quarter-notes per minute. A "beat" here is one note of the time-signature
/// denominator (a quarter note in 4/4, an eighth note in 6/8). Ticks divide a quarter note
/// into 960 parts, which is the usual MIDI resolution; a time-signature beat therefore has
/// `960 * (4 / denominator)` ticks.
struct MusicalPosition
{
    std::int64_t samples = 0;
    double quarterNotes = 0.0;
    int bar = 1;   ///< 1-based.
    int beat = 1;  ///< 1-based beat inside the bar.
    int tick = 0;  ///< 0 .. ticksPerBeat-1.
};

struct Timing
{
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 999.0;
    static constexpr int kTicksPerQuarter = 960;

    [[nodiscard]] static double clampBpm (double bpm) noexcept;
    [[nodiscard]] static bool isValidSampleRate (double sampleRate) noexcept;
    [[nodiscard]] static bool isValidTimeSignature (TimeSignature signature) noexcept;

    /// Samples spanned by one quarter note at this tempo. BPM is quarter notes per minute.
    [[nodiscard]] static double samplesPerQuarterNote (double sampleRate, double bpm) noexcept;

    [[nodiscard]] static double quarterNotesPerBar (TimeSignature signature) noexcept;
    [[nodiscard]] static double quarterNotesPerBeat (TimeSignature signature) noexcept;
    [[nodiscard]] static int ticksPerBeat (TimeSignature signature) noexcept;

    [[nodiscard]] static double quarterNotesFromSamples (std::int64_t samples,
                                                         double sampleRate,
                                                         double bpm) noexcept;

    /// Rounds to the nearest sample. Negative musical time maps to sample 0.
    [[nodiscard]] static std::int64_t samplesFromQuarterNotes (double quarterNotes,
                                                               double sampleRate,
                                                               double bpm) noexcept;

    [[nodiscard]] static MusicalPosition positionFromSamples (std::int64_t samples,
                                                              double sampleRate,
                                                              double bpm,
                                                              TimeSignature signature) noexcept;
};

/// "001.01.000" — bar, beat, tick. Bars grow past three digits when needed.
[[nodiscard]] std::string formatMusical (const MusicalPosition& position);

/// "m:ss.mmm" from a sample index.
[[nodiscard]] std::string formatSeconds (std::int64_t samples, double sampleRate);

} // namespace waveform
