#include "RenderGraph.hpp"

#include "waveform/effects.hpp"
#include "waveform/mix.hpp"
#include "waveform/paths.hpp"
#include "waveform/routing.hpp"
#include "waveform/sampler.hpp"
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
        rendered.gainDb = static_cast<float> (track.mixer.gainDb);
        rendered.gain = dbToLinear (track.mixer.gainDb);
        rendered.sampler = track.instrument == InstrumentKind::Sampler;
        rendered.automation = track.automation;

        for (const auto& zone : track.samplerZones)
        {
            std::string error;
            auto location = pathFromUtf8 (zone.file);

            if (projectFile != nullptr && ! location.is_absolute())
                location = resolveAudioPath (*projectFile, zone.file);

            auto audio = library.load (pathToUtf8 (location), sampleRate, error);

            if (audio == nullptr)
            {
                result.warnings.push_back (track.name + " sampler: " + error);
                continue;
            }

            RenderSamplerZone renderedZone;
            renderedZone.rootNote = zone.rootNote;
            renderedZone.lowNote = zone.lowNote;
            renderedZone.highNote = zone.highNote;
            renderedZone.audio = std::move (audio);
            rendered.zones.push_back (std::move (renderedZone));
        }
        rendered.pan = static_cast<float> (track.mixer.pan);
        rendered.mute = track.mixer.mute;
        rendered.solo = track.mixer.solo;

        rendered.isMidi = track.type == TrackType::Midi;
        rendered.group = track.group;
        rendered.patch = track.synth;
        rendered.effectCount = static_cast<int> (std::min<std::size_t> (track.effects.size(), static_cast<std::size_t> (kMaxInserts)));

        for (int effect = 0; effect < rendered.effectCount; ++effect)
            rendered.effects[effect] = track.effects[static_cast<std::size_t> (effect)];

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

    int nextBus = 1;

    for (auto& track : graph->tracks)
        if (track.group && nextBus < kMaxBuses)
            track.outputBus = nextBus++;

    std::vector<std::string> ids;
    ids.reserve (project.tracks.size());

    for (const auto& track : project.tracks)
        ids.push_back (track.id);

    auto findBus = [&] (const std::string& id) {
        if (id.empty() || id == "master")
            return 0;

        for (std::size_t index = 0; index < ids.size() && index < graph->tracks.size(); ++index)
            if (ids[index] == id && graph->tracks[index].group)
                return graph->tracks[index].outputBus;

        return 0;
    };

    for (std::size_t index = 0; index < graph->tracks.size() && index < project.tracks.size(); ++index)
    {
        auto& rendered = graph->tracks[index];
        const auto& source = project.tracks[index];

        if (! rendered.group)
            rendered.outputBus = findBus (source.outputId);

        for (int send = 0; send < kMaxSends && send < static_cast<int> (source.sends.size()); ++send)
        {
            rendered.sendBus[send] = findBus (source.sends[static_cast<std::size_t> (send)].targetId);
            rendered.sendGain[send] = dbToLinear (source.sends[static_cast<std::size_t> (send)].levelDb);
        }
    }

    result.graph = std::move (graph);
    return result;
}

void renderTrackInstrument (const RenderTrack& track,
                           const SoundingNote* notes,
                           int noteCount,
                           int frames,
                           double sampleRate,
                           SynthVoiceMemory& memory,
                           float leftGain,
                           float rightGain,
                           float* left,
                           float* right) noexcept
{
    if (track.sampler && ! track.zones.empty())
    {
        SamplerZoneView views[8];
        int count = 0;

        for (const auto& zone : track.zones)
        {
            if (count >= 8 || zone.audio == nullptr || zone.audio->left.empty())
                continue;

            views[count].rootNote = zone.rootNote;
            views[count].lowNote = zone.lowNote;
            views[count].highNote = zone.highNote;
            views[count].left = zone.audio->left.data();
            views[count].right = zone.audio->right.empty() ? nullptr : zone.audio->right.data();
            views[count].frames = zone.audio->frames();
            views[count].sourceSampleRate = zone.audio->sampleRate;
            ++count;
        }

        renderSamplerNotes (notes, noteCount, views, count, frames, sampleRate, track.patch, leftGain, rightGain, left, right);
        return;
    }

    renderSynthNotes (notes, noteCount, frames, sampleRate, track.patch, memory, leftGain, rightGain, left, right);
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
                        SynthSchedule* synth,
                        BlockScratch* scratch) noexcept
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

            if (Timing::isValidSampleRate (sampleRate) && tempoBpm > 0.0)
            {
                const double quarter = Timing::quarterNotesFromSamples (timelineStart, sampleRate, tempoBpm);
                double decibels = track.gainDb;

                for (const auto& lane : track.automation)
                    if (lane.parameter == "mixer.gainDb")
                        decibels = automationAt (lane, quarter, decibels);

                mixTrack.gain = dbToLinear (decibels);
            }
            mixTrack.mute = track.mute;
            mixTrack.solo = track.solo;
        }
    }

    if (overflow && overflowCount != nullptr)
        overflowCount->store (1, std::memory_order_relaxed);

    bool routed = false;

    if (scratch != nullptr && graph != nullptr && scratch->capacity >= frames)
        for (const auto& track : graph->tracks)
            if (track.group || track.effectCount > 0 || track.outputBus != 0 || track.sendGain[0] > 0.0f || track.sendGain[1] > 0.0f)
                routed = true;

    if (routed)
    {
        RouteEndpoint endpoints[kMaxRouteTracks];
        const int count = std::min (trackCount, kMaxRouteTracks);
        int busCount = 1;

        for (int index = 0; index < count; ++index)
        {
            if (scratch->trackLeft[index] == nullptr || scratch->trackRight[index] == nullptr)
            {
                routed = false;
                break;
            }

            const auto& source = graph->tracks[static_cast<std::size_t> (index)];
            auto saved = tracks[index];
            tracks[index].gain = 1.0f;
            tracks[index].pan = 0.0f;
            tracks[index].mute = false;
            tracks[index].solo = false;
            MixView one;
            one.tracks = &tracks[index];
            one.trackCount = 1;
            one.masterGain = 1.0f;
            mixBlock (one, timelineStart, frames, scratch->trackLeft[index], scratch->trackRight[index]);
            tracks[index] = saved;

            if (synth != nullptr && synth->memories != nullptr && source.isMidi)
            {
                SoundingNote sounding[128];
                int noteCount = 0;
                const double blockStart = Timing::quarterNotesFromSamples (timelineStart, sampleRate, tempoBpm);
                const double secondsPerQuarter = 60.0 / tempoBpm;

                for (const auto& clip : source.midiClips)
                    for (const auto& note : clip.notes)
                    {
                        if (noteCount >= 128)
                            break;

                        const double onset = (clip.startQuarterNotes + note.startQuarterNotes - blockStart) * secondsPerQuarter;
                        const double release = onset + note.lengthQuarterNotes * secondsPerQuarter;

                        if (release + source.patch.releaseSeconds < 0.0 || onset > static_cast<double> (frames) / sampleRate)
                            continue;

                        sounding[noteCount].note = note.note;
                        sounding[noteCount].velocity = note.velocity;
                        sounding[noteCount].onsetSeconds = onset;
                        sounding[noteCount].releaseSeconds = release;
                        ++noteCount;
                    }

                renderTrackInstrument (source, sounding, noteCount, frames, sampleRate, synth->memories[index],
                                      1.0f, 1.0f, scratch->trackLeft[index], scratch->trackRight[index]);
            }

            if (scratch->effects != nullptr)
                processInserts (source.effects, source.effectCount, scratch->effects + index * kMaxInserts,
                                scratch->trackLeft[index], scratch->trackRight[index], frames, sampleRate);

            const PanGains pan = constantPowerPan (saved.pan);
            endpoints[index].outputBus = source.outputBus;
            endpoints[index].group = source.group;
            endpoints[index].mute = saved.mute;
            endpoints[index].solo = saved.solo;
            endpoints[index].gain = saved.gain;
            endpoints[index].panLeft = pan.left;
            endpoints[index].panRight = pan.right;
            endpoints[index].sendBus[0] = source.sendBus[0];
            endpoints[index].sendBus[1] = source.sendBus[1];
            endpoints[index].sendGain[0] = source.sendGain[0];
            endpoints[index].sendGain[1] = source.sendGain[1];
            busCount = std::max (busCount, source.outputBus + 1);
        }

        if (routed)
        {
            routeTracks (scratch->trackLeft, scratch->trackRight, endpoints, count,
                         scratch->busLeft, scratch->busRight, std::min (busCount, kMaxBuses), frames);
            const float master = graph->masterGain;

            for (int frame = 0; frame < frames; ++frame)
            {
                left[frame] = scratch->busLeft[0][frame] * master;
                right[frame] = scratch->busRight[0][frame] * master;
            }
        }
    }

    if (! routed)
    {
        MixView mix;
        mix.tracks = tracks;
        mix.trackCount = trackCount;
        mix.masterGain = graph != nullptr ? graph->masterGain : 1.0f;
        mixBlock (mix, timelineStart, frames, left, right);
    }

    if (! routed && graph != nullptr && synth != nullptr && synth->memories != nullptr
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
            renderTrackInstrument (track, sounding, count, frames, sampleRate, synth->memories[trackIndex],
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
