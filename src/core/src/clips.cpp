#include "waveform/clips.hpp"

#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

void clampFades (AudioClip& clip) noexcept
{
    const double half = std::max (0.0, clip.lengthSeconds * 0.5);
    clip.fadeInSeconds = std::clamp (clip.fadeInSeconds, 0.0, half);
    clip.fadeOutSeconds = std::clamp (clip.fadeOutSeconds, 0.0, half);
}

void splitMarkers (const AudioClip& source, double leftQuarters, AudioClip& left, AudioClip& right)
{
    for (const auto& marker : source.warp)
    {
        if (marker.timelineQuarters <= leftQuarters)
            left.warp.push_back (marker);
        else
            right.warp.push_back (WarpMarker { marker.sourceSeconds, marker.timelineQuarters - leftQuarters });
    }

    for (const auto& segment : source.comp)
    {
        if (segment.endQuarter <= leftQuarters)
            left.comp.push_back (segment);
        else if (segment.startQuarter >= leftQuarters)
            right.comp.push_back (CompSegment { segment.startQuarter - leftQuarters, segment.endQuarter - leftQuarters, segment.takeIndex });
    }
}

} // namespace

double secondsPerQuarter (double bpm) noexcept
{
    if (! (bpm > 0.0) || std::isnan (bpm))
        return 0.5;

    return 60.0 / bpm;
}

float clipFadeGain (double secondsFromStart, double lengthSeconds, double fadeInSeconds, double fadeOutSeconds) noexcept
{
    if (! (lengthSeconds > 0.0) || std::isnan (secondsFromStart))
        return 1.0f;

    constexpr float kHalfPi = 1.5707963267948966f;
    float gain = 1.0f;
    const double fadeIn = std::max (0.0, fadeInSeconds);
    const double fadeOut = std::max (0.0, fadeOutSeconds);

    if (fadeIn > 0.0 && secondsFromStart < fadeIn)
        gain *= std::sin (static_cast<float> (std::clamp (secondsFromStart / fadeIn, 0.0, 1.0)) * kHalfPi);

    const double fromEnd = lengthSeconds - secondsFromStart;

    if (fadeOut > 0.0 && fromEnd < fadeOut)
    {
        const double progress = std::clamp ((fadeOut - fromEnd) / fadeOut, 0.0, 1.0);
        gain *= std::cos (static_cast<float> (progress) * kHalfPi);
    }

    return gain;
}

std::vector<float> waveformPeaks (const float* left, const float* right, std::int64_t frames, int columns)
{
    if (columns <= 0)
        return {};

    std::vector<float> peaks (static_cast<std::size_t> (columns), 0.0f);

    if (left == nullptr || frames <= 0)
        return peaks;

    for (int column = 0; column < columns; ++column)
    {
        const auto start = frames * column / columns;
        auto end = frames * (column + 1) / columns;

        if (end <= start)
            end = std::min (frames, start + 1);

        float peak = 0.0f;

        for (std::int64_t frame = start; frame < end; ++frame)
        {
            peak = std::max (peak, std::abs (left[static_cast<std::size_t> (frame)]));

            if (right != nullptr)
                peak = std::max (peak, std::abs (right[static_cast<std::size_t> (frame)]));
        }

        peaks[static_cast<std::size_t> (column)] = peak;
    }

    return peaks;
}

bool splitClip (const Clip& clip, double splitQuarter, double bpm, Clip& left, Clip& right)
{
    const double quarterSeconds = secondsPerQuarter (bpm);

    if (const auto* audio = std::get_if<AudioClip> (&clip))
    {
        const double lengthQuarters = audio->lengthSeconds / quarterSeconds;

        if (splitQuarter <= audio->startQuarterNotes + 1.0e-4
            || splitQuarter >= audio->startQuarterNotes + lengthQuarters - 1.0e-4)
            return false;

        const double leftQuarters = splitQuarter - audio->startQuarterNotes;
        const double leftSeconds = leftQuarters * quarterSeconds;
        AudioClip leftAudio = *audio;
        AudioClip rightAudio = *audio;
        leftAudio.lengthSeconds = leftSeconds;
        leftAudio.fadeOutSeconds = 0.0;
        clampFades (leftAudio);
        rightAudio.id = makeId();
        rightAudio.name = audio->name.empty() ? "Clip" : audio->name + " b";
        rightAudio.startQuarterNotes = splitQuarter;
        rightAudio.offsetSeconds = audio->offsetSeconds + leftSeconds;
        rightAudio.lengthSeconds = audio->lengthSeconds - leftSeconds;
        rightAudio.fadeInSeconds = 0.0;
        rightAudio.warp.clear();
        rightAudio.comp.clear();
        leftAudio.warp.clear();
        leftAudio.comp.clear();
        splitMarkers (*audio, leftQuarters, leftAudio, rightAudio);
        clampFades (rightAudio);
        left = leftAudio;
        right = rightAudio;
        return true;
    }

    const auto* midi = std::get_if<MidiClip> (&clip);

    if (midi == nullptr)
        return false;

    const double local = splitQuarter - midi->startQuarterNotes;

    if (local <= 1.0e-4 || local >= midi->lengthQuarterNotes - 1.0e-4)
        return false;

    MidiClip leftMidi = *midi;
    MidiClip rightMidi = *midi;
    leftMidi.lengthQuarterNotes = local;
    leftMidi.notes.clear();
    rightMidi.id = makeId();
    rightMidi.name = midi->name.empty() ? "Clip" : midi->name + " b";
    rightMidi.startQuarterNotes = splitQuarter;
    rightMidi.lengthQuarterNotes = midi->lengthQuarterNotes - local;
    rightMidi.notes.clear();

    for (const auto& note : midi->notes)
    {
        const double noteEnd = note.startQuarterNotes + note.lengthQuarterNotes;

        if (noteEnd <= local + 1.0e-6)
        {
            leftMidi.notes.push_back (note);
        }
        else if (note.startQuarterNotes >= local - 1.0e-6)
        {
            MidiNote moved = note;
            moved.startQuarterNotes = std::max (0.0, note.startQuarterNotes - local);
            rightMidi.notes.push_back (moved);
        }
        else
        {
            MidiNote first = note;
            first.lengthQuarterNotes = local - note.startQuarterNotes;
            leftMidi.notes.push_back (first);
            MidiNote second = note;
            second.startQuarterNotes = 0.0;
            second.lengthQuarterNotes = noteEnd - local;
            rightMidi.notes.push_back (second);
        }
    }

    left = leftMidi;
    right = rightMidi;
    return true;
}

bool resizeClip (Clip& clip, double newStartQuarter, double newLengthQuarters, double bpm)
{
    if (! (newLengthQuarters > 0.01) || newStartQuarter < 0.0 || std::isnan (newStartQuarter) || std::isnan (newLengthQuarters))
        return false;

    const double quarterSeconds = secondsPerQuarter (bpm);

    if (auto* audio = std::get_if<AudioClip> (&clip))
    {
        const double startDelta = (newStartQuarter - audio->startQuarterNotes) * quarterSeconds;
        double offset = audio->offsetSeconds + startDelta;
        double start = newStartQuarter;

        if (offset < 0.0)
        {
            start += -offset / quarterSeconds;
            offset = 0.0;
        }

        audio->startQuarterNotes = std::max (0.0, start);
        audio->offsetSeconds = offset;
        audio->lengthSeconds = newLengthQuarters * quarterSeconds;
        clampFades (*audio);
        return true;
    }

    auto* midi = std::get_if<MidiClip> (&clip);

    if (midi == nullptr)
        return false;

    const double delta = newStartQuarter - midi->startQuarterNotes;
    midi->startQuarterNotes = newStartQuarter;
    midi->lengthQuarterNotes = newLengthQuarters;
    std::vector<MidiNote> kept;
    kept.reserve (midi->notes.size());

    for (auto note : midi->notes)
    {
        note.startQuarterNotes -= delta;
        const double end = note.startQuarterNotes + note.lengthQuarterNotes;

        if (end <= 1.0e-4 || note.startQuarterNotes >= newLengthQuarters - 1.0e-4)
            continue;

        if (note.startQuarterNotes < 0.0)
        {
            note.lengthQuarterNotes += note.startQuarterNotes;
            note.startQuarterNotes = 0.0;
        }

        if (end > newLengthQuarters)
            note.lengthQuarterNotes = newLengthQuarters - note.startQuarterNotes;

        if (note.lengthQuarterNotes > 1.0e-4)
            kept.push_back (note);
    }

    midi->notes = std::move (kept);
    return true;
}

bool deleteClip (Track& track, const std::string& clipIdToRemove)
{
    const auto it = std::find_if (track.clips.begin(), track.clips.end(),
                                  [&] (const Clip& clip) { return clipId (clip) == clipIdToRemove; });

    if (it == track.clips.end())
        return false;

    track.clips.erase (it);
    return true;
}

} // namespace waveform
