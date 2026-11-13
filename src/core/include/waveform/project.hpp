#pragma once

#include "waveform/automation.hpp"
#include "waveform/effects.hpp"
#include "waveform/timing.hpp"

#include <string>
#include <variant>
#include <vector>

namespace waveform
{

enum class TrackType
{
    Audio,
    Midi
};

struct MixerState
{
    double gainDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

enum class OscillatorWave
{
    Sine,
    Saw,
    Square,
    Triangle
};

/// Built-in instrument used by every MIDI track. Plug-in hosting can replace this later.
struct SynthPatch
{
    OscillatorWave wave = OscillatorWave::Saw;
    double detuneCents = 6.0;
    double cutoffHz = 1800.0;
    double resonance = 0.2;
    double attackSeconds = 0.01;
    double decaySeconds = 0.12;
    double sustain = 0.7;
    double releaseSeconds = 0.2;
};

/// Unwarped audio. The file plays at its natural speed.
/// `startQuarterNotes` is where it sits on the timeline.
/// `offsetSeconds` / `lengthSeconds` select a region of the source file.
/// Musical length follows the tempo because the audio is not time-stretched.
struct AudioClip
{
    std::string id;
    std::string name;
    std::string file;
    double startQuarterNotes = 0.0;
    double offsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    double gainDb = 0.0;
};

struct MidiNote
{
    int note = 60; ///< 0..127
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 1.0;
    int velocity = 100; ///< 1..127
    int channel = 1;    ///< 1..16
};

/// Stored for later phases. Phase 0 does not synthesise notes.
struct MidiClip
{
    std::string id;
    std::string name;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 4.0;
    std::vector<MidiNote> notes;
};

using Clip = std::variant<AudioClip, MidiClip>;

inline constexpr int kSessionScenes = 8;

/// A clip that lives in the session grid. `scene` is the row, 0 .. kSessionScenes-1.
struct SessionSlot
{
    int scene = 0;
    Clip clip;
};

struct Send
{
    std::string targetId = "master";
    double levelDb = -96.0;
};

struct SamplerZone
{
    std::string file;
    int rootNote = 60;
    int lowNote = 0;
    int highNote = 127;
};

enum class InstrumentKind
{
    Synth,
    Sampler,
    Fm,
    Drum
};

struct Track
{
    std::string id;
    std::string name;
    TrackType type = TrackType::Audio;
    bool group = false;
    std::string outputId = "master";
    MixerState mixer;
    SynthPatch synth;
    InstrumentKind instrument = InstrumentKind::Synth;
    std::vector<SamplerZone> samplerZones;
    std::vector<AutomationLane> automation;
    std::vector<Send> sends;
    std::vector<EffectSlot> effects;
    std::vector<Clip> clips;
    std::vector<SessionSlot> session;
    std::string pluginIdentifier;
    std::string pluginName;
    std::string pluginState;
};

struct Project
{
    std::string name = "Untitled";
    double tempoBpm = 120.0;
    TimeSignature timeSignature {};
    bool loopEnabled = false;
    double loopStartQuarterNotes = 0.0;
    double loopEndQuarterNotes = 8.0;
    int countInBars = 1;
    MixerState master; ///< Only gainDb is used. Pan, mute, and solo are ignored.
    std::vector<Track> tracks;

    [[nodiscard]] static Project makeDefault();
};

[[nodiscard]] std::string makeId();

[[nodiscard]] const std::string& clipId (const Clip& clip);
[[nodiscard]] Track* findTrack (Project& project, const std::string& id);
[[nodiscard]] const Track* findTrack (const Project& project, const std::string& id);

void clampMixer (MixerState& mixer) noexcept;

[[nodiscard]] inline bool isAudioClip (const Clip& clip) noexcept
{
    return std::holds_alternative<AudioClip> (clip);
}

[[nodiscard]] inline bool isMidiClip (const Clip& clip) noexcept
{
    return std::holds_alternative<MidiClip> (clip);
}

} // namespace waveform
