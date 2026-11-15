#include "RenderGraph.hpp"

#include "waveform/automation.hpp"
#include "waveform/drum_rack.hpp"
#include "waveform/effects.hpp"
#include "waveform/midi_effects.hpp"
#include "waveform/mix.hpp"
#include "waveform/paths.hpp"
#include "waveform/routing.hpp"
#include "waveform/later.hpp"
#include "waveform/sampler.hpp"
#include "waveform/stretch.hpp"
#include "waveform/session.hpp"
#include "waveform/synth.hpp"
#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

std::vector<RenderMidiNote> effectedNotes (const std::vector<MidiNote>& notes,
                                           const std::vector<MidiEffect>& effects,
                                           double lengthQuarterNotes)
{
    std::vector<RenderMidiNote> rendered;

    if (notes.empty())
        return rendered;

    std::vector<MidiNote> input = notes;
    std::vector<MidiNote> output (std::max<std::size_t> (notes.size() * 4, 16));
    const int produced = applyMidiEffects (input.data(), static_cast<int> (input.size()),
                                           effects.data(), static_cast<int> (effects.size()),
                                           lengthQuarterNotes, output.data(), static_cast<int> (output.size()));
    rendered.reserve (static_cast<std::size_t> (std::max (0, produced)));

    for (int index = 0; index < produced; ++index)
    {
        RenderMidiNote note;
        note.note = output[static_cast<std::size_t> (index)].note;
        note.velocity = output[static_cast<std::size_t> (index)].velocity;
        note.startQuarterNotes = output[static_cast<std::size_t> (index)].startQuarterNotes;
        note.lengthQuarterNotes = output[static_cast<std::size_t> (index)].lengthQuarterNotes;
        note.slideSemitones = output[static_cast<std::size_t> (index)].slideSemitones;
        rendered.push_back (note);
    }

    return rendered;
}

} // namespace

namespace
{

constexpr int kMaxTracks = 64;
constexpr int kMaxClips = 256;
constexpr float kMetronomeGain = 0.85f;
constexpr float kTestToneGain = 0.15f;
constexpr double kTestToneFrequency = 440.0;

int appendShapedLive (const RenderTrack& track,
                      const SynthSchedule* synth,
                      int trackIndex,
                      double tempoBpm,
                      SoundingNote* sounding,
                      int noteCount,
                      int capacity)
{
    if (synth == nullptr || synth->liveTrack != trackIndex || synth->liveNotes == nullptr || synth->liveNoteCount <= 0)
        return noteCount;

    double oldest = 0.0;

    for (int index = 0; index < synth->liveNoteCount; ++index)
        oldest = std::min (oldest, synth->liveNotes[index].onsetSeconds);

    SoundingNote shaped[128];
    const int shapedCount = shapeHeldNotes (synth->liveNotes, synth->liveNoteCount,
                                            track.midiEffects.data(), static_cast<int> (track.midiEffects.size()),
                                            -oldest, tempoBpm, shaped, 128);

    for (int index = 0; index < shapedCount && noteCount < capacity; ++index)
        sounding[noteCount++] = shaped[index];

    return noteCount;
}

int collectTrackNotes (const RenderTrack& track,
                       int trackIndex,
                       const SessionVoice* session,
                       int sessionCount,
                       std::int64_t timelineStart,
                       int frames,
                       double sampleRate,
                       double tempoBpm,
                       SoundingNote* destination,
                       int capacity,
                       bool& overflow) noexcept
{
    if (destination == nullptr || capacity <= 0)
        return 0;

    const int scene = session != nullptr && trackIndex < sessionCount ? session[trackIndex].active : -1;
    const bool launched = scene >= 0 && scene < kSessionScenes && track.session[scene].used && track.session[scene].midi;

    if (launched)
    {
        SessionNoteView views[128];
        int viewCount = 0;

        for (const auto& note : track.session[scene].notes)
        {
            if (viewCount >= 128)
            {
                overflow = true;
                break;
            }

            views[viewCount].note = note.note;
            views[viewCount].velocity = note.velocity;
            views[viewCount].startQuarterNotes = note.startQuarterNotes;
            views[viewCount].lengthQuarterNotes = note.lengthQuarterNotes;
            views[viewCount].slideSemitones = note.slideSemitones;
            ++viewCount;
        }

        const double samplesPerQuarter = sampleRate * 60.0 / tempoBpm;
        const double playhead = samplesPerQuarter > 0.0 ? static_cast<double> (session[trackIndex].playhead) / samplesPerQuarter : 0.0;
        const double block = samplesPerQuarter > 0.0 ? static_cast<double> (frames) / samplesPerQuarter : 0.0;
        return appendSessionNotes (views, viewCount, playhead, block, track.session[scene].lengthQuarterNotes,
                                   60.0 / tempoBpm, destination, 0, capacity);
    }

    const double blockStart = Timing::quarterNotesFromSamples (timelineStart, sampleRate, tempoBpm);
    const double secondsPerQuarter = 60.0 / tempoBpm;
    int count = 0;

    for (const auto& clip : track.midiClips)
    {
        for (const auto& note : clip.notes)
        {
            if (count >= capacity)
            {
                overflow = true;
                return count;
            }

            const double onset = (clip.startQuarterNotes + note.startQuarterNotes - blockStart) * secondsPerQuarter;
            const double release = onset + note.lengthQuarterNotes * secondsPerQuarter;

            if (release + track.patch.releaseSeconds < 0.0 || onset > static_cast<double> (frames) / sampleRate)
                continue;

            destination[count].note = note.note;
            destination[count].velocity = note.velocity;
            destination[count].onsetSeconds = onset;
            destination[count].releaseSeconds = release;
            destination[count].slideSemitones = note.slideSemitones;
            ++count;
        }
    }

    return count;
}

} // namespace

GraphBuildResult buildGraph (const Project& project,
                             const std::filesystem::path* projectFile,
                             ClipLibrary& library,
                             double sampleRate)
{
    GraphBuildResult result;
    auto graph = std::make_shared<RenderGraph>();
    graph->masterGain = dbToLinear (project.master.gainDb);
    graph->projectTempo = project.tempoBpm;

    for (int scene = 0; scene < kSessionScenes; ++scene)
        graph->sceneTempoBpm[scene] = project.sceneTempoBpm[scene];

    for (const auto& track : project.tracks)
    {
        RenderTrack rendered;
        rendered.gainDb = static_cast<float> (track.mixer.gainDb);
        rendered.gain = dbToLinear (track.mixer.gainDb);
        rendered.sampler = track.instrument == InstrumentKind::Sampler;
        rendered.instrument = track.instrument;
        rendered.automation = track.automation;
        rendered.midiEffects = track.midiEffects;
        rendered.drumRack = track.drumRack;
        rendered.sidechainTrack = track.sidechainTrack;

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
        rendered.group = track.group || track.returns;
        rendered.patch = track.synth;
        rendered.effectCount = static_cast<int> (std::min<std::size_t> (track.effects.size(), static_cast<std::size_t> (kMaxInserts)));

        for (int effect = 0; effect < rendered.effectCount; ++effect)
            rendered.effects[effect] = track.effects[static_cast<std::size_t> (effect)];

        for (std::size_t chainIndex = 0; chainIndex < track.rack.chains.size() && chainIndex < 4; ++chainIndex)
        {
            RackChain chain;
            chain.gain = track.rack.chains[chainIndex].gain;
            chain.effects = track.rack.chains[chainIndex].effects;

            for (const auto& macro : track.rack.macros)
            {
                for (const auto& assignment : macro.assignments)
                {
                    if (assignment.chain != static_cast<int> (chainIndex) || assignment.effect < 0
                        || assignment.effect >= static_cast<int> (chain.effects.size()))
                        continue;

                    auto& effect = chain.effects[static_cast<std::size_t> (assignment.effect)];
                    const double value = assignment.minimum + std::clamp (macro.value, 0.0, 1.0) * (assignment.maximum - assignment.minimum);
                    double* slots[] = { &effect.p0, &effect.p1, &effect.p2, &effect.p3, &effect.p4, &effect.p5 };

                    if (assignment.parameter >= 0 && assignment.parameter < 6)
                        *slots[assignment.parameter] = value;
                }
            }

            rendered.chains.push_back (std::move (chain));
        }

        rendered.modulator = track.modulator;

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
                renderedClip.notes = effectedNotes (clip->notes, track.midiEffects, clip->lengthQuarterNotes);
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
                renderedClip.fadeInSeconds = clip->fadeInSeconds;
                renderedClip.fadeOutSeconds = clip->fadeOutSeconds;
                renderedClip.envelopeCount = 0;

                if (clip->lengthSeconds > 0.0)
                {
                    const int count = std::min (8, static_cast<int> (clip->envelope.size()));

                    for (int point = 0; point < count; ++point)
                    {
                        renderedClip.envelopePosition[point] = clip->envelope[static_cast<std::size_t> (point)].quarterNotes / clip->lengthSeconds;
                        renderedClip.envelopeValue[point] = clip->envelope[static_cast<std::size_t> (point)].value;
                    }

                    renderedClip.envelopeCount = count;
                }

                if (clip->warp.size() >= 2 || ! clip->comp.empty())
                {
                    std::vector<std::shared_ptr<const ClipAudio>> loadedTakes;
                    StretchSource extrasStorage[8];
                    int extraCount = 0;

                    for (const auto& take : clip->takes)
                    {
                        if (extraCount >= 8)
                            break;

                        auto takePath = pathFromUtf8 (take.file);

                        if (projectFile != nullptr && ! takePath.is_absolute())
                            takePath = resolveAudioPath (*projectFile, take.file);

                        std::string takeError;
                        auto takeAudio = library.load (pathToUtf8 (takePath), sampleRate, takeError);

                        if (takeAudio == nullptr)
                        {
                            result.warnings.push_back (track.name + " / " + clip->name + " take: " + takeError);
                            continue;
                        }

                        extrasStorage[extraCount].left = takeAudio->left.data();
                        extrasStorage[extraCount].right = takeAudio->right.empty() ? nullptr : takeAudio->right.data();
                        extrasStorage[extraCount].frames = takeAudio->frames();
                        loadedTakes.push_back (std::move (takeAudio));
                        ++extraCount;
                    }

                    StretchSource primary;
                    primary.left = renderedClip.audio->left.data();
                    primary.right = renderedClip.audio->right.empty() ? nullptr : renderedClip.audio->right.data();
                    primary.frames = renderedClip.audio->frames();
                    std::vector<float> bakedLeft;
                    std::vector<float> bakedRight;
                    const double secondsPerQuarter = 60.0 / std::max (1.0, project.tempoBpm);
                    renderStretch (primary, extrasStorage, extraCount, clip->warp, clip->comp, sampleRate, secondsPerQuarter,
                                   clip->offsetSeconds, clip->lengthSeconds, bakedLeft, bakedRight,
                                   static_cast<WarpMode> (std::clamp (clip->warpMode, 0, 2)));

                    if (! bakedLeft.empty())
                    {
                        auto baked = std::make_shared<ClipAudio>();
                        baked->sampleRate = sampleRate;
                        baked->left = std::move (bakedLeft);
                        baked->right = std::move (bakedRight);
                        renderedClip.audio = std::move (baked);
                        renderedClip.offsetSeconds = 0.0;
                        renderedClip.lengthSeconds = static_cast<double> (renderedClip.audio->frames()) / sampleRate;
                    }
                }

                rendered.clips.push_back (std::move (renderedClip));
                ++result.loadedClips;
            }
        }

        for (const auto& slot : track.session)
        {
            if (slot.scene < 0 || slot.scene >= kSessionScenes)
                continue;

            auto& destination = rendered.session[slot.scene];

            if (const auto* midi = std::get_if<MidiClip> (&slot.clip))
            {
                destination.used = true;
                destination.midi = true;
                destination.lengthQuarterNotes = midi->lengthQuarterNotes;
                destination.notes = effectedNotes (midi->notes, track.midiEffects, midi->lengthQuarterNotes);
            }
            else if (const auto* audioClip = std::get_if<AudioClip> (&slot.clip))
            {
                auto location = pathFromUtf8 (audioClip->file);

                if (projectFile != nullptr && ! location.is_absolute())
                    location = resolveAudioPath (*projectFile, audioClip->file);

                std::string error;
                auto audio = library.load (pathToUtf8 (location), sampleRate, error);

                if (audio == nullptr)
                {
                    result.warnings.push_back (track.name + " scene: " + error);
                    continue;
                }

                destination.used = true;
                destination.audio = std::move (audio);
                destination.offsetSeconds = audioClip->offsetSeconds;
                destination.lengthSeconds = audioClip->lengthSeconds;
                destination.gain = dbToLinear (audioClip->gainDb);
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
        if (id.empty() || id == "none")
            return -1;

        if (id == "master")
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
            rendered.sendPre[send] = source.sends[static_cast<std::size_t> (send)].preFader;
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
    if (track.instrument == InstrumentKind::Fm)
    {
        renderFmNotes (notes, noteCount, frames, sampleRate, track.patch, leftGain, rightGain, left, right);
        return;
    }

    if (track.instrument == InstrumentKind::Drum)
    {
        if (track.drumRack.count > 0)
        {
            static PadVoice voices[64][4] {};
            static int counts[64] {};
            const int slot = 0;

            for (int index = 0; index < noteCount; ++index)
                if (notes[index].onsetSeconds >= -1.0e-4 && notes[index].onsetSeconds < static_cast<double> (frames) / sampleRate)
                    strikeDrumRack (voices[slot], 4, track.drumRack, notes[index].note);

            float rackLeft[2048];
            float rackRight[2048];
            const int limited = std::min (frames, 2048);
            std::fill (rackLeft, rackLeft + limited, 0.0f);
            std::fill (rackRight, rackRight + limited, 0.0f);
            renderDrumRack (voices[slot], 4, track.drumRack, limited, sampleRate, rackLeft, rackRight);
            counts[slot] = 4;

            for (int index = 0; index < limited; ++index)
            {
                left[index] += rackLeft[index] * leftGain;
                right[index] += rackRight[index] * rightGain;
            }

            return;
        }

        renderDrumNotes (notes, noteCount, frames, sampleRate, track.patch, leftGain, rightGain, left, right);
        return;
    }

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
                        BlockScratch* scratch,
                        SessionVoice* session,
                        int sessionCount,
                        ExternalInsert* external,
                        double* lfoPhase,
                        float* trackPeaks) noexcept
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
            const int scene = session != nullptr && trackCount < sessionCount ? session[trackCount].active : -1;
            const bool launched = scene >= 0 && scene < kSessionScenes && track.session[scene].used;

            if (launched && ! track.session[scene].midi && track.session[scene].audio != nullptr)
            {
                const auto& slot = track.session[scene];
                const auto loopFrames = std::max<std::int64_t> (0, std::llround (slot.lengthSeconds * sampleRate));
                const auto fileOffset = std::max<std::int64_t> (0, std::llround (slot.offsetSeconds * slot.audio->sampleRate));
                const auto playhead = session[trackCount].playhead;
                SessionAudioPiece pieces[2];
                const int pieceCount = sessionAudioPieces (timelineStart, frames, playhead, loopFrames, fileOffset, pieces);

                for (int piece = 0; piece < pieceCount && clipCursor < kMaxClips; ++piece)
                {
                    auto& view = clipViews[clipCursor++];
                    view.source.left = slot.audio->left.data();
                    view.source.right = slot.audio->right.empty() ? nullptr : slot.audio->right.data();
                    view.source.frames = slot.audio->frames();
                    view.timelineStartFrame = pieces[piece].timelineStartFrame;
                    view.sourceOffsetFrame = pieces[piece].sourceOffsetFrame;
                    view.timelineLengthFrames = pieces[piece].timelineLengthFrames;
                    view.gain = slot.gain;
                }
            }
            else if (! launched)
            {
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
                    view.fadeInFrames = std::max<std::int64_t> (0, std::llround (clip.fadeInSeconds * sampleRate));
                    view.fadeOutFrames = std::max<std::int64_t> (0, std::llround (clip.fadeOutSeconds * sampleRate));
                    view.envelopeCount = std::min (8, clip.envelopeCount);

                    for (int point = 0; point < view.envelopeCount; ++point)
                    {
                        view.envelopePosition[point] = clip.envelopePosition[point];
                        view.envelopeValue[point] = clip.envelopeValue[point];
                    }

                    view.gain = clip.gain;
                }
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

                if (lfoPhase != nullptr && track.modulator.enabled && track.modulator.target == "mixer.gainDb")
                    decibels += std::sin (lfoPhase[trackCount]) * track.modulator.depth;

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
    {
        if (external != nullptr && external->process != nullptr)
            routed = true;

        for (const auto& track : graph->tracks)
            if (! track.chains.empty())
                routed = true;

        for (const auto& track : graph->tracks)
            if (track.group || track.effectCount > 0 || track.outputBus != 0 || track.sendGain[0] > 0.0f || track.sendGain[1] > 0.0f)
                routed = true;
    }

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
                int noteCount = collectTrackNotes (source, index, session, sessionCount, timelineStart, frames,
                                                   sampleRate, tempoBpm, sounding, 128, overflow);
                const bool arrangement = session == nullptr || index >= sessionCount || session[index].active < 0;

                if (arrangement)
                    noteCount = appendShapedLive (source, synth, index, tempoBpm, sounding, noteCount, 128);

                renderTrackInstrument (source, sounding, noteCount, frames, sampleRate, synth->memories[index],
                                      1.0f, 1.0f, scratch->trackLeft[index], scratch->trackRight[index]);
            }

            if (scratch->effects != nullptr)
            {
                const float* sideLeft = nullptr;
                const float* sideRight = nullptr;

                if (source.sidechainTrack >= 0 && source.sidechainTrack < kMaxRouteTracks
                    && scratch->trackLeft[source.sidechainTrack] != nullptr)
                {
                    sideLeft = scratch->trackLeft[source.sidechainTrack];
                    sideRight = scratch->trackRight[source.sidechainTrack];
                }

                float automation[2048];
                const int automatedFrames = std::min (frames, 2048);

                for (const auto& lane : source.automation)
                {
                    if (lane.parameter != "mixer.gainDb" || lane.points.empty() || ! (tempoBpm > 0.0))
                        continue;

                    const double startQuarter = Timing::quarterNotesFromSamples (timelineStart, sampleRate, tempoBpm);
                    writeAutomation (lane, startQuarter, automatedFrames, sampleRate, tempoBpm, automation, source.gainDb);

                    for (int sample = 0; sample < automatedFrames; ++sample)
                        automation[sample] = dbToLinear (automation[sample]);

                    applyGainCurve (scratch->trackLeft[index], scratch->trackRight[index], automation, automatedFrames);
                    break;
                }

                processInserts (source.effects, source.effectCount, scratch->effects + index * kMaxInserts,
                                scratch->trackLeft[index], scratch->trackRight[index], frames, sampleRate, sideLeft, sideRight);
            }

            if (external != nullptr && external->process != nullptr)
                external->process (external->context, index, scratch->trackLeft[index], scratch->trackRight[index], frames);

            if (! source.chains.empty() && scratch->rackStates != nullptr && scratch->rackOutLeft != nullptr
                && scratch->rackScratchLeft != nullptr && index < 8)
            {
                mixRack (source.chains.data(), static_cast<int> (source.chains.size()),
                         scratch->rackStates + index * 4 * kMaxInserts, kMaxInserts,
                         scratch->trackLeft[index], scratch->trackRight[index],
                         scratch->rackOutLeft, scratch->rackOutRight,
                         scratch->rackScratchLeft, scratch->rackScratchRight,
                         frames, sampleRate);
                std::copy (scratch->rackOutLeft, scratch->rackOutLeft + frames, scratch->trackLeft[index]);
                std::copy (scratch->rackOutRight, scratch->rackOutRight + frames, scratch->trackRight[index]);
            }

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
            endpoints[index].sendPre[0] = source.sendPre[0];
            endpoints[index].sendPre[1] = source.sendPre[1];

            if (trackPeaks != nullptr)
            {
                float peak = 0.0f;

                for (int frame = 0; frame < frames; ++frame)
                    peak = std::max (peak, std::max (std::abs (scratch->trackLeft[index][frame]), std::abs (scratch->trackRight[index][frame])));

                trackPeaks[index] = std::max (trackPeaks[index], peak);
            }
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

        SoundingNote sounding[128];

        for (int trackIndex = 0; trackIndex < static_cast<int> (graph->tracks.size()) && trackIndex < synth->memoryCount; ++trackIndex)
        {
            const auto& track = graph->tracks[static_cast<std::size_t> (trackIndex)];

            if (! track.isMidi || track.mute || (anySolo && ! track.solo))
                continue;

            int count = collectTrackNotes (track, trackIndex, session, sessionCount, timelineStart, frames,
                                           sampleRate, tempoBpm, sounding, 128, overflow);
            const bool arrangement = session == nullptr || trackIndex >= sessionCount || session[trackIndex].active < 0;

            if (arrangement)
                count = appendShapedLive (track, synth, trackIndex, tempoBpm, sounding, count, 128);

            const PanGains pan = constantPowerPan (track.pan);
            renderTrackInstrument (track, sounding, count, frames, sampleRate, synth->memories[trackIndex],
                                  track.gain * pan.left * graph->masterGain,
                                  track.gain * pan.right * graph->masterGain,
                                  left, right);
        }
    }

    if (overflow && overflowCount != nullptr)
        overflowCount->store (1, std::memory_order_relaxed);

    if (session != nullptr && graph != nullptr && tempoBpm > 0.0 && Timing::isValidSampleRate (sampleRate))
    {
        const int count = std::min (sessionCount, static_cast<int> (graph->tracks.size()));

        for (int index = 0; index < count; ++index)
        {
            if (session[index].active < 0 || session[index].active >= kSessionScenes)
                continue;

            const auto& slot = graph->tracks[static_cast<std::size_t> (index)].session[session[index].active];
            std::int64_t loop = 0;

            if (slot.midi)
                loop = Timing::samplesFromQuarterNotes (slot.lengthQuarterNotes, sampleRate, tempoBpm);
            else if (slot.used)
                loop = std::max<std::int64_t> (0, std::llround (slot.lengthSeconds * sampleRate));

            const double samplesPerQuarter = sampleRate > 0.0 && tempoBpm > 0.0 ? sampleRate * 60.0 / tempoBpm : 0.0;
            LaunchStep launchStep;
            launchStep.activeScene = session[index].active;
            launchStep.playheadQuarters = samplesPerQuarter > 0.0 ? static_cast<double> (session[index].playhead) / samplesPerQuarter : 0.0;
            launchStep.gateHeld = session[index].gateHeld;
            launchStep.launch = session[index].launch;
            launchStep.follow = session[index].follow;
            launchStep.followAfterQuarters = session[index].followAfterQuarters;
            launchStep.repeatQuarters = session[index].repeatQuarters;
            launchStep.pressedScene = session[index].active;

            session[index].playhead += frames;

            if (loop > 0)
                session[index].playhead %= loop;

            if (samplesPerQuarter > 0.0)
            {
                const auto stepped = stepClipLaunch (launchStep, kSessionScenes, static_cast<double> (frames) / samplesPerQuarter, false, false);

                if (stepped.activeScene != launchStep.activeScene || stepped.retrigger)
                {
                    session[index].active = stepped.activeScene;
                    session[index].playhead = 0;
                }
            }
        }
    }

    if (lfoPhase != nullptr && graph != nullptr && sampleRate > 0.0)
    {
        const int count = std::min (static_cast<int> (graph->tracks.size()), kMaxTracks);

        for (int index = 0; index < count; ++index)
        {
            if (! graph->tracks[static_cast<std::size_t> (index)].modulator.enabled)
                continue;

            lfoPhase[index] += graph->tracks[static_cast<std::size_t> (index)].modulator.rateHz * 6.283185307179586 * frames / sampleRate;
            lfoPhase[index] = std::fmod (lfoPhase[index], 6.283185307179586);
        }
    }

    if (metronome)
        renderMetronome (left, right, timelineStart, frames, sampleRate, tempoBpm, signature, kMetronomeGain);

    if (testTone && tone != nullptr)
        renderTestTone (left, right, frames, sampleRate, kTestToneFrequency, kTestToneGain, *tone);
}

} // namespace waveform
