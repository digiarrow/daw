#pragma once

#include "waveform/project.hpp"
#include "waveform/timing.hpp"

#include <vector>

namespace waveform
{

/// Quarter-note length of one grid cell. `division` is 1, 2, 4, 8, or 16 (the note value).
/// A value of 4 is a quarter note. Returns 0 when snapping is off (`division <= 0`).
[[nodiscard]] double gridQuarterNotes (int division) noexcept;

/// Moves `quarterNotes` toward the nearest grid line. `amount` is 0 (unchanged) to 1 (fully snapped).
[[nodiscard]] double snapToGrid (double quarterNotes, double gridQuarterNotes, double amount) noexcept;

/// Quantizes note starts. When `quantizeEnds` is set, note ends snap as well and the length stays positive.
void quantizeNotes (std::vector<MidiNote>& notes, double gridQuarterNotes, double amount, bool quantizeEnds) noexcept;

/// Samples of metronome that play before a recording punch-in of `bars` bars.
[[nodiscard]] std::int64_t countInSamples (int bars, TimeSignature signature, double sampleRate, double bpm) noexcept;

} // namespace waveform
