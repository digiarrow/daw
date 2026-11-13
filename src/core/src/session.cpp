#include "waveform/session.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

void consider (const SessionNoteView& note,
               double windowStart,
               double windowEnd,
               double blockOffsetQuarters,
               double secondsPerQuarter,
               SoundingNote* destination,
               int& count,
               int capacity) noexcept
{
    if (count >= capacity)
        return;

    const double noteStart = note.startQuarterNotes;
    const double noteEnd = noteStart + note.lengthQuarterNotes;
    const double from = std::max (noteStart, windowStart);
    const double to = std::min (noteEnd, windowEnd);

    if (! (to > from))
        return;

    destination[count].note = note.note;
    destination[count].velocity = note.velocity;
    destination[count].onsetSeconds = (blockOffsetQuarters + (noteStart - windowStart)) * secondsPerQuarter;
    destination[count].releaseSeconds = (blockOffsetQuarters + (noteEnd - windowStart)) * secondsPerQuarter;
    ++count;
}

} // namespace

int sessionAudioPieces (std::int64_t timelineStart,
                        int frames,
                        std::int64_t playhead,
                        std::int64_t loopFrames,
                        std::int64_t fileOffset,
                        SessionAudioPiece* pieces) noexcept
{
    if (pieces == nullptr || frames <= 0)
        return 0;

    if (loopFrames <= 0)
    {
        pieces[0].timelineStartFrame = timelineStart - std::max<std::int64_t> (0, playhead);
        pieces[0].timelineLengthFrames = std::max<std::int64_t> (0, playhead) + frames;
        pieces[0].sourceOffsetFrame = fileOffset;
        return 1;
    }

    if (playhead < 0)
        playhead = 0;

    playhead %= loopFrames;
    const auto first = std::min<std::int64_t> (frames, loopFrames - playhead);
    pieces[0].timelineStartFrame = timelineStart - playhead;
    pieces[0].timelineLengthFrames = playhead + first;
    pieces[0].sourceOffsetFrame = fileOffset;

    if (first >= frames)
        return 1;

    pieces[1].timelineStartFrame = timelineStart + first;
    pieces[1].timelineLengthFrames = frames - first;
    pieces[1].sourceOffsetFrame = fileOffset;
    return 2;
}

int appendSessionNotes (const SessionNoteView* notes,
                        int noteCount,
                        double playheadQuarters,
                        double blockQuarters,
                        double loopQuarters,
                        double secondsPerQuarter,
                        SoundingNote* destination,
                        int destinationCount,
                        int capacity) noexcept
{
    if (notes == nullptr || destination == nullptr || noteCount <= 0 || ! (blockQuarters > 0.0) || ! (secondsPerQuarter > 0.0))
        return destinationCount;

    auto window = [&] (double start, double end, double blockOffset)
    {
        for (int index = 0; index < noteCount; ++index)
            consider (notes[index], start, end, blockOffset, secondsPerQuarter, destination, destinationCount, capacity);
    };

    if (! (loopQuarters > 0.0))
    {
        window (playheadQuarters, playheadQuarters + blockQuarters, 0.0);
        return destinationCount;
    }

    double start = std::fmod (playheadQuarters, loopQuarters);

    if (start < 0.0)
        start += loopQuarters;

    const double end = start + blockQuarters;

    if (end <= loopQuarters)
    {
        window (start, end, 0.0);
        return destinationCount;
    }

    window (start, loopQuarters, 0.0);
    window (0.0, end - loopQuarters, loopQuarters - start);
    return destinationCount;
}

} // namespace waveform
