#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace waveform
{

bool TimeSignature::isValid() const noexcept
{
    return Timing::isValidTimeSignature (*this);
}

bool Timing::isValidTimeSignature (TimeSignature signature) noexcept
{
    if (signature.numerator < 1 || signature.numerator > 32)
        return false;

    if (signature.denominator < 1 || signature.denominator > 32)
        return false;

    return (signature.denominator & (signature.denominator - 1)) == 0;
}

double Timing::clampBpm (double bpm) noexcept
{
    if (! (bpm > 0.0) || std::isnan (bpm))
        return 120.0;

    return std::clamp (bpm, kMinBpm, kMaxBpm);
}

bool Timing::isValidSampleRate (double sampleRate) noexcept
{
    return sampleRate >= 8000.0 && sampleRate <= 384000.0 && ! std::isnan (sampleRate);
}

double Timing::samplesPerQuarterNote (double sampleRate, double bpm) noexcept
{
    if (! isValidSampleRate (sampleRate) || ! (bpm > 0.0))
        return 0.0;

    return sampleRate * 60.0 / bpm;
}

double Timing::quarterNotesPerBeat (TimeSignature signature) noexcept
{
    if (! isValidTimeSignature (signature))
        return 1.0;

    return 4.0 / static_cast<double> (signature.denominator);
}

double Timing::quarterNotesPerBar (TimeSignature signature) noexcept
{
    if (! isValidTimeSignature (signature))
        return 4.0;

    return static_cast<double> (signature.numerator) * quarterNotesPerBeat (signature);
}

int Timing::ticksPerBeat (TimeSignature signature) noexcept
{
    const double ticks = static_cast<double> (kTicksPerQuarter) * quarterNotesPerBeat (signature);
    return std::max (1, static_cast<int> (std::lround (ticks)));
}

double Timing::quarterNotesFromSamples (std::int64_t samples, double sampleRate, double bpm) noexcept
{
    if (samples <= 0 || ! (sampleRate > 0.0) || ! (bpm > 0.0))
        return 0.0;

    return static_cast<double> (samples) * bpm / (sampleRate * 60.0);
}

std::int64_t Timing::samplesFromQuarterNotes (double quarterNotes, double sampleRate, double bpm) noexcept
{
    if (! (quarterNotes > 0.0) || ! (sampleRate > 0.0) || ! (bpm > 0.0) || std::isnan (quarterNotes))
        return 0;

    const double samples = quarterNotes * sampleRate * 60.0 / bpm;

    if (! (samples > 0.0) || samples > static_cast<double> (INT64_C (0x7fffffffffffffff)))
        return 0;

    return static_cast<std::int64_t> (std::llround (samples));
}

MusicalPosition Timing::positionFromSamples (std::int64_t samples,
                                             double sampleRate,
                                             double bpm,
                                             TimeSignature signature) noexcept
{
    MusicalPosition position;
    position.samples = std::max<std::int64_t> (0, samples);
    position.quarterNotes = quarterNotesFromSamples (position.samples, sampleRate, bpm);

    const double quartersPerBar = quarterNotesPerBar (signature);
    const double quartersPerBeat = quarterNotesPerBeat (signature);

    if (! (quartersPerBar > 0.0) || ! (quartersPerBeat > 0.0))
        return position;

    const double bars = position.quarterNotes / quartersPerBar;
    const int barIndex = static_cast<int> (std::floor (bars + 1.0e-12));
    position.bar = barIndex + 1;

    const double quartersIntoBar = position.quarterNotes - static_cast<double> (barIndex) * quartersPerBar;
    const double beats = quartersIntoBar / quartersPerBeat;
    int beatIndex = static_cast<int> (std::floor (beats + 1.0e-12));
    beatIndex = std::clamp (beatIndex, 0, signature.numerator - 1);

    const double fraction = beats - static_cast<double> (beatIndex);
    const int ticksInBeat = ticksPerBeat (signature);
    int tick = static_cast<int> (std::floor (fraction * static_cast<double> (ticksInBeat) + 1.0e-9));
    tick = std::clamp (tick, 0, ticksInBeat - 1);

    position.beat = beatIndex + 1;
    position.tick = tick;
    return position;
}

std::string formatMusical (const MusicalPosition& position)
{
    std::ostringstream stream;
    const int barWidth = position.bar > 999 ? 0 : 3;
    stream << std::setfill ('0')
           << std::setw (barWidth) << position.bar << '.'
           << std::setw (2) << position.beat << '.'
           << std::setw (3) << position.tick;
    return stream.str();
}

std::string formatSeconds (std::int64_t samples, double sampleRate)
{
    if (! (sampleRate > 0.0))
        return "0:00.000";

    const double seconds = static_cast<double> (std::max<std::int64_t> (0, samples)) / sampleRate;
    auto totalMilliseconds = static_cast<long long> (std::llround (seconds * 1000.0));

    if (totalMilliseconds < 0)
        totalMilliseconds = 0;

    const int milliseconds = static_cast<int> (totalMilliseconds % 1000);
    const long long totalSeconds = totalMilliseconds / 1000;
    const int secs = static_cast<int> (totalSeconds % 60);
    const long long minutes = totalSeconds / 60;

    std::ostringstream stream;
    stream << minutes << ':'
           << std::setfill ('0') << std::setw (2) << secs << '.'
           << std::setw (3) << milliseconds;
    return stream.str();
}

} // namespace waveform
