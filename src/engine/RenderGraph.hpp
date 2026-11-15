#pragma once

#include "ClipLibrary.hpp"
#include "waveform/effects.hpp"
#include "waveform/project.hpp"
#include "waveform/later.hpp"
#include "waveform/routing.hpp"
#include "waveform/session.hpp"
#include "waveform/synth.hpp"
#include "waveform/synthesis.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>

namespace waveform
{

struct RenderClip
{
    std::shared_ptr<const ClipAudio> audio;
    double startQuarterNotes = 0.0;
    double offsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    double envelopePosition[8] {};
    double envelopeValue[8] {};
    int envelopeCount = 0;
    float gain = 1.0f;
};

struct RenderMidiNote
{
    int note = 60;
    int velocity = 100;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 1.0;
    float slideSemitones = 0.0f;
};

struct RenderSamplerZone
{
    int rootNote = 60;
    int lowNote = 0;
    int highNote = 127;
    std::shared_ptr<const ClipAudio> audio;
};

struct RenderMidiClip
{
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 4.0;
    std::vector<RenderMidiNote> notes;
};

struct RenderSessionSlot
{
    bool used = false;
    bool midi = false;
    std::shared_ptr<const ClipAudio> audio;
    double offsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    float gain = 1.0f;
    double lengthQuarterNotes = 4.0;
    std::vector<RenderMidiNote> notes;
};

struct RenderTrack
{
    std::vector<RenderClip> clips;
    std::vector<RenderMidiClip> midiClips;
    RenderSessionSlot session[kSessionScenes] {};
    SynthPatch patch;
    bool isMidi = false;
    bool group = false;
    int outputBus = 0;
    int sendBus[kMaxSends] { -1, -1 };
    float sendGain[kMaxSends] { 0.0f, 0.0f };
    bool sendPre[kMaxSends] { false, false };
    EffectSlot effects[kMaxInserts] {};
    int effectCount = 0;
    std::vector<RackChain> chains;
    Modulator modulator;
    bool sampler = false;
    InstrumentKind instrument = InstrumentKind::Synth;
    std::vector<RenderSamplerZone> zones;
    std::vector<AutomationLane> automation;
    std::vector<MidiEffect> midiEffects;
    DrumRack drumRack;
    int sidechainTrack = -1;
    float gain = 1.0f;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
};

/// Audio-thread scratch for per-track processing. Pointers address `capacity` frames each.
/// Optional per-track insert invoked on the audio thread after the built-in effects.
/// `process` must not allocate. `delaySamples` is applied by the callback itself.
struct ExternalInsert
{
    void (*process) (void* context, int track, float* left, float* right, int frames) = nullptr;
    void* context = nullptr;
};

struct BlockScratch
{
    float* trackLeft[kMaxRouteTracks] {};
    float* trackRight[kMaxRouteTracks] {};
    float* busLeft[kMaxBuses] {};
    float* busRight[kMaxBuses] {};
    EffectState* effects = nullptr; ///< Row-major, track * kMaxInserts + slot.
    float* rackOutLeft = nullptr;
    float* rackOutRight = nullptr;
    float* rackScratchLeft = nullptr;
    float* rackScratchRight = nullptr;
    EffectState* rackStates = nullptr;
    int capacity = 0;
};

inline void bindBlockScratch (BlockScratch& scratch, float* storage, int frames, EffectState* effects) noexcept
{
    int cursor = 0;

    for (int index = 0; index < kMaxRouteTracks; ++index)
    {
        scratch.trackLeft[index] = storage + cursor;
        cursor += frames;
        scratch.trackRight[index] = storage + cursor;
        cursor += frames;
    }

    for (int index = 0; index < kMaxBuses; ++index)
    {
        scratch.busLeft[index] = storage + cursor;
        cursor += frames;
        scratch.busRight[index] = storage + cursor;
        cursor += frames;
    }

    scratch.effects = effects;
    scratch.capacity = frames;
}

/// Per-track synth memory plus any keys currently held from a MIDI input.
/// The audio thread owns the memory. Null skips MIDI rendering.
struct SynthSchedule
{
    SynthVoiceMemory* memories = nullptr;
    int memoryCount = 0;
    const SoundingNote* liveNotes = nullptr;
    int liveNoteCount = 0;
    int liveTrack = -1;
};

/// Immutable snapshot the audio thread reads. Built on the message thread.
struct RenderGraph
{
    std::vector<RenderTrack> tracks;
    float masterGain = 1.0f;
    double projectTempo = 120.0;
    double sceneTempoBpm[kSessionScenes] {};
};

struct GraphBuildResult
{
    std::shared_ptr<const RenderGraph> graph;
    std::vector<std::string> warnings;
    int loadedClips = 0;
    int failedClips = 0;
};

/// Resolves clip paths against `projectFile` when it is non-null and the stored path is relative.
[[nodiscard]] GraphBuildResult buildGraph (const Project& project,
                                           const std::filesystem::path* projectFile,
                                           ClipLibrary& library,
                                           double sampleRate);

void renderTrackInstrument (const RenderTrack& track,
                           const SoundingNote* notes,
                           int noteCount,
                           int frames,
                           double sampleRate,
                           SynthVoiceMemory& memory,
                           float leftGain,
                           float rightGain,
                           float* left,
                           float* right,
                           int trackIndex = 0,
                           const AutomationLane* cutoff = nullptr,
                           double startQuarter = 0.0,
                           double tempoBpm = 0.0) noexcept;

/// Mixes one contiguous timeline region into `left`/`right`, then adds metronome and test tone.
/// Clears the destination first. Does not allocate. `tone` may be null when `testTone` is false.
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
                        BlockScratch* scratch = nullptr,
                        SessionVoice* session = nullptr,
                        int sessionCount = 0,
                        ExternalInsert* external = nullptr,
                        double* lfoPhase = nullptr,
                        float* trackPeaks = nullptr) noexcept;

} // namespace waveform
