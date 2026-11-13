#include "RenderGraph.hpp"

#include "waveform/mix.hpp"
#include "waveform/paths.hpp"
#include "waveform/synth.hpp"
#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

constexpr int kMaxTracks = 64;
constexpr int kMaxClips = 256;
constexpr float kMetronomeGain = 0.85f;
constexpr float kTestToneGain = 0.15f;
constexpr double kTestToneFrequency = 440.0;

} // namespace

GraphBuildResult buildGraph (const Project& project,
                             const std::filesystem::path* projectFile,
                             ClipLibrary& library,
                             double sampleRate)
{
    GraphBuildResult result;
    auto graph = std::make_shared<RenderGraph>();
    graph->masterGain = dbToLinear (project.master.gainDb);

    for (const auto& track : project.tracks)
    {
        RenderTrack rendered;
        rendered.gain = dbToLinear (track.mixer.gainDb);
        rendered.pan = static_cast<float> (track.mixer.pan);
        rendered.mute = track.mixer.mute;
        rendered.solo = track.mixer.solo;

        rendered.isMidi = track.type == TrackType::Midi;
        rendered.patch = track.synth;

        if (track.type == TrackType::Midi)
        {
            for (const auto& clipValue : track.clips)
            {
                const auto* clip = std::get_if<MidiClip> (&clipValue);

                if (clip == nullptr)
                    continue;

                RenderMidiClip renderedClip;
                renderedClip.startQuarterNotes = clip->startQuarterNotes;
                renderedClip.lengthQuarterNotes = clip->lengthQuarterNotes;

                for (const auto& note : clip->notes)
                {
                    RenderMidiNote renderedNote;
                    renderedNote.note = note.note;
                    renderedNote.velocity = note.velocity;
                    renderedNote.startQuarterNotes = note.startQuarterNotes;
                    renderedNote.lengthQuarterNotes = note.lengthQuarterNotes;
                    renderedClip.notes.push_back (renderedNote);
                }

                rendered.midiClips.push_back (std::move (renderedClip));
            }
        }

        if (track.type == TrackType::Audio && Timing::isValidSampleRate (sampleRate))
        {
            for (const auto& clipValue : track.clips)
            {
                const auto* clip = std::get_if<AudioClip> (&clipValue);

                if (clip == nullptr)
                    continue;

                auto location = pathFromUtf8 (clip->file);

                if (projectFile != nullptr && ! location.is_absolute())
                    location = resolveAudioPath (*projectFile, clip->file);

                std::string error;
                auto audio = library.load (pathToUtf8 (location), sampleRate, error);

                if (audio == nullptr)
                {
                    result.warnings.push_back (track.name + " / " + clip->name + ": " + error);
                    ++result.failedClips;
                    continue;
                }

                RenderClip renderedClip;
                renderedClip.audio = std::move (audio);
                renderedClip.startQuarterNotes = clip->startQuarterNotes;
                renderedClip.offsetSeconds = clip->offsetSeconds;
                renderedClip.lengthSeconds = clip->lengthSeconds;
                renderedClip.gain = dbToLinear (clip->gainDb);
                rendered.clips.push_back (std::move (renderedClip));
                ++result.loadedClips;
            }
        }

        graph->tracks.push_back (std::move (rendered));
    }

    result.graph = std::move (graph);
    return result;
}

void renderLinearBlock (const RenderGraph* graph,
                        double sampleRate,
                        double tempoBpm,
                        TimeSignature signature,
                        std::int64_t timelineStart,
                        int frames,
                        float* left,
                        float* right,
                        bool metronome,
                        bool testTone,
                        Oscillator* tone,
                        std::atomic<int>* overflowCount,
                        SynthSchedule* synth) noexcept
{
    if (left == nullptr || right == nullptr || frames <= 0)
        return;

    AudioClipView clipViews[kMaxClips];
    TrackMix tracks[kMaxTracks];
    int clipCursor = 0;
    int trackCount = 0;
    bool overflow = false;

    if (graph != nullptr && Timing::isValidSampleRate (sampleRate) && tempoBpm > 0.0)
    {
        for (const auto& track : graph->tracks)
        {
            if (trackCount >= kMaxTracks)
            {
                overflow = true;
                break;
            }

            const int clipBegin = clipCursor;

            for (const auto& clip : track.clips)
            {
                if (clipCursor >= kMaxClips)
                {
                    overflow = true;
                    break;
                }

                if (clip.audio == nullptr || clip.audio->left.empty())
                    continue;

                if (std::abs (clip.audio->sampleRate - sampleRate) > 1.0)
                    continue;

                auto& view = clipViews[clipCursor++];
                view.source.left = clip.audio->left.data();
                view.source.right = clip.audio->right.empty() ? nullptr : clip.audio->right.data();
                view.source.frames = clip.audio->frames();
                view.timelineStartFrame = Timing::samplesFromQuarterNotes (clip.startQuarterNotes, sampleRate, tempoBpm);
                view.sourceOffsetFrame = std::max<std::int64_t> (0, std::llround (clip.offsetSeconds * clip.audio->sampleRate));
                view.timelineLengthFrames = std::max<std::int64_t> (0, std::llround (clip.lengthSeconds * clip.audio->sampleRate));
                view.gain = clip.gain;
            }

            auto& mixTrack = tracks[trackCount++];
            mixTrack.clips = clipViews + clipBegin;
            mixTrack.clipCount = clipCursor - clipBegin;
            mixTrack.gain = track.gain;
            mixTrack.pan = track.pan;
            mixTrack.mute = track.mute;
            mixTrack.solo = track.solo;
        }
    }

    if (overflow && overflowCount != nullptr)
        overflowCount->store (1, std::memory_order_relaxed);

    MixView mix;
    mix.tracks = tracks;
    mix.trackCount = trackCount;
    mix.masterGain = graph != nullptr ? graph->masterGain : 1.0f;
    mixBlock (mix, timelineStart, frames, left, right);

    if (graph != nullptr && synth != nullptr && synth->memories != nullptr
        && Timing::isValidSampleRate (sampleRate) && tempoBpm > 0.0)
    {
        bool anySolo = false;

        for (const auto& track : graph->tracks)
            if (track.solo)
                anySolo = true;

        const double blockStart = Timing::quarterNotesFromSamples (timelineStart, sampleRate, tempoBpm);
        const double secondsPerQuarter = 60.0 / tempoBpm;
        SoundingNote sounding[128];

        for (int trackIndex = 0; trackIndex < static_cast<int> (graph->tracks.size()) && trackIndex < synth->memoryCount; ++trackIndex)
        {
            const auto& track = graph->tracks[static_cast<std::size_t> (trackIndex)];

            if (! track.isMidi || track.mute || (anySolo && ! track.solo))
                continue;

            int count = 0;

            for (const auto& clip : track.midiClips)
            {
                for (const auto& note : clip.notes)
                {
                    if (count >= 128)
                    {
                        overflow = true;
                        break;
                    }

                    const double onsetQuarter = clip.startQuarterNotes + note.startQuarterNotes;
                    const double onset = (onsetQuarter - blockStart) * secondsPerQuarter;
                    const double release = onset + note.lengthQuarterNotes * secondsPerQuarter;

                    if (release + track.patch.releaseSeconds < 0.0 || onset > static_cast<double> (frames) / sampleRate)
                        continue;

                    sounding[count].note = note.note;
                    sounding[count].velocity = note.velocity;
                    sounding[count].onsetSeconds = onset;
                    sounding[count].releaseSeconds = release;
                    ++count;
                }
            }

            if (synth->liveTrack == trackIndex && synth->liveNotes != nullptr)
            {
                for (int live = 0; live < synth->liveNoteCount && count < 128; ++live)
                    sounding[count++] = synth->liveNotes[live];
            }

            const PanGains pan = constantPowerPan (track.pan);
            renderSynthNotes (sounding, count, frames, sampleRate, track.patch, synth->memories[trackIndex],
                              track.gain * pan.left * graph->masterGain,
                              track.gain * pan.right * graph->masterGain,
                              left, right);
        }
    }

    if (overflow && overflowCount != nullptr)
        overflowCount->store (1, std::memory_order_relaxed);

    if (metronome)
        renderMetronome (left, right, timelineStart, frames, sampleRate, tempoBpm, signature, kMetronomeGain);

    if (testTone && tone != nullptr)
        renderTestTone (left, right, frames, sampleRate, kTestToneFrequency, kTestToneGain, *tone);
}

} // namespace waveform
