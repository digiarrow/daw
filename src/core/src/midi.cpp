#include "waveform/midi.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{

double gridQuarterNotes (int division) noexcept
{
    if (division <= 0)
        return 0.0;

    // A quarter note is division 4. An eighth is 8, so the cell is 4/division quarters.
    return 4.0 / static_cast<double> (division);
}

double snapToGrid (double quarterNotes, double grid, double amount) noexcept
{
    if (! (grid > 0.0) || std::isnan (quarterNotes))
        return quarterNotes;

    const double clamped = std::clamp (amount, 0.0, 1.0);
    const double snapped = std::round (quarterNotes / grid) * grid;
    return quarterNotes + (snapped - quarterNotes) * clamped;
}

void quantizeNotes (std::vector<MidiNote>& notes, double grid, double amount, bool quantizeEnds) noexcept
{
    if (! (grid > 0.0))
        return;

    for (auto& note : notes)
    {
        const double end = note.startQuarterNotes + note.lengthQuarterNotes;
        note.startQuarterNotes = std::max (0.0, snapToGrid (note.startQuarterNotes, grid, amount));

        if (quantizeEnds)
        {
            const double snappedEnd = std::max (note.startQuarterNotes + grid * 0.25, snapToGrid (end, grid, amount));
            note.lengthQuarterNotes = snappedEnd - note.startQuarterNotes;
        }
    }
}

std::int64_t countInSamples (int bars, TimeSignature signature, double sampleRate, double bpm) noexcept
{
    if (bars <= 0)
        return 0;

    const double quarters = static_cast<double> (bars) * Timing::quarterNotesPerBar (signature);
    return Timing::samplesFromQuarterNotes (quarters, sampleRate, bpm);
}

} // namespace waveform
