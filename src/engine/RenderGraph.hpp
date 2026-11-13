#pragma once

#include "ClipLibrary.hpp"
#include "waveform/project.hpp"
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
    float gain = 1.0f;
};

struct RenderMidiNote
{
    int note = 60;
    int velocity = 100;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 1.0;
};

struct RenderMidiClip
{
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 4.0;
    std::vector<RenderMidiNote> notes;
};

struct RenderTrack
{
    std::vector<RenderClip> clips;
    std::vector<RenderMidiClip> midiClips;
    SynthPatch patch;
    bool isMidi = false;
    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
};

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
                        SynthSchedule* synth) noexcept;

} // namespace waveform
