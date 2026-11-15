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
    destination[count].slideSemitones = note.slideSemitones;
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

LaunchResult stepClipLaunch (LaunchStep step, int sceneCount, double blockQuarters, bool pressed, bool released) noexcept
{
    LaunchResult result;
    result.activeScene = step.activeScene;
    result.playheadQuarters = step.playheadQuarters;
    result.gateHeld = step.gateHeld;
    const int scenes = std::max (1, sceneCount);
    const int requested = std::clamp (step.pressedScene, 0, scenes - 1);

    if (pressed)
    {
        if (step.launch == ClipLaunchMode::Toggle && step.activeScene == requested)
        {
            result.activeScene = -1;
            result.playheadQuarters = 0.0;
            result.retrigger = false;
            result.gateHeld = false;
            return result;
        }

        result.activeScene = requested;
        result.playheadQuarters = 0.0;
        result.retrigger = true;
        result.gateHeld = step.launch == ClipLaunchMode::Gate;
        return result;
    }

    if (released && step.launch == ClipLaunchMode::Gate && step.gateHeld)
    {
        result.activeScene = -1;
        result.playheadQuarters = 0.0;
        result.gateHeld = false;
        return result;
    }

    if (result.activeScene < 0)
        return result;

    result.playheadQuarters += std::max (0.0, blockQuarters);

    if (step.launch == ClipLaunchMode::Repeat && step.repeatQuarters > 0.0 && result.playheadQuarters >= step.repeatQuarters)
    {
        result.playheadQuarters = std::fmod (result.playheadQuarters, step.repeatQuarters);
        result.retrigger = true;
    }

    if (step.follow != FollowActionKind::None && step.followAfterQuarters > 0.0 && result.playheadQuarters >= step.followAfterQuarters)
    {
        const int current = result.activeScene;

        switch (step.follow)
        {
            case FollowActionKind::Stop: result.activeScene = -1; break;
            case FollowActionKind::Next: result.activeScene = (current + 1) % scenes; break;
            case FollowActionKind::Previous: result.activeScene = (current + scenes - 1) % scenes; break;
            case FollowActionKind::First: result.activeScene = 0; break;
            case FollowActionKind::Last: result.activeScene = scenes - 1; break;
            case FollowActionKind::None: break;
        }

        result.playheadQuarters = 0.0;
        result.retrigger = result.activeScene >= 0;
    }

    return result;
}

double tempoForScene (const double* sceneTempoBpm, int sceneCount, int activeScene, double projectTempo) noexcept
{
    if (sceneTempoBpm == nullptr || activeScene < 0 || activeScene >= sceneCount)
        return projectTempo;

    const double tempo = sceneTempoBpm[activeScene];
    return tempo > 0.0 ? tempo : projectTempo;
}

SceneTempoDecision holdSceneTempo (SceneTempoHold hold,
                                   double transportTempo,
                                   int activeScene,
                                   const double* sceneTempoBpm,
                                   int sceneCount,
                                   double projectTempo) noexcept
{
    SceneTempoDecision decision;
    decision.hold = hold;

    if (activeScene >= 0)
    {
        const double sceneTempo = tempoForScene (sceneTempoBpm, sceneCount, activeScene, projectTempo);

        if (! (hold.applied > 0.0))
            decision.hold.restore = transportTempo;
        else if (std::abs (transportTempo - hold.applied) > 0.05)
            decision.hold.restore = transportTempo;

        decision.hold.applied = sceneTempo;
        decision.playback = sceneTempo;
        return decision;
    }

    if (hold.applied > 0.0)
    {
        decision.playback = hold.restore;
        decision.hold.applied = 0.0;
        return decision;
    }

    decision.playback = transportTempo;
    return decision;
}

void recordIntoSessionSlot (Track& track, int scene, Clip clip)
{
    const int row = std::clamp (scene, 0, kSessionScenes - 1);

    for (auto& slot : track.session)
    {
        if (slot.scene != row)
            continue;

        slot.clip = std::move (clip);
        return;
    }

    SessionSlot slot;
    slot.scene = row;
    slot.clip = std::move (clip);
    track.session.push_back (std::move (slot));
}

} // namespace waveform
