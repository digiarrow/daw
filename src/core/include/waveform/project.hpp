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

struct WarpMarker
{
    double sourceSeconds = 0.0;
    double timelineQuarters = 0.0;
};

struct CompSegment
{
    double startQuarter = 0.0;
    double endQuarter = 0.0;
    int takeIndex = 0;
};

/// An alternate recording. Take 0 is the clip's own file. Later takes are these.
struct ClipTake
{
    std::string name;
    std::string file;
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
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    std::vector<WarpMarker> warp;
    std::vector<AutomationPoint> envelope; ///< Clip-local seconds.
    int warpMode = 2;                      ///< 0 beats, 1 tones, 2 complex.
    std::vector<CompSegment> comp;
    std::vector<ClipTake> takes;
};

struct MidiNote
{
    int note = 60; ///< 0..127
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 1.0;
    int velocity = 100; ///< 1..127
    int channel = 1;    ///< 1..16
    float slideSemitones = 0.0f; ///< MPE pitch slide, in semitones.
};

/// Stored for later phases. Phase 0 does not synthesise notes.
struct MidiClip
{
    std::string id;
    std::string name;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 4.0;
    std::vector<MidiNote> notes;
    std::vector<AutomationPoint> envelope; ///< Clip-local quarter notes.
};

using Clip = std::variant<AudioClip, MidiClip>;

inline constexpr int kSessionScenes = 8;

enum class ClipLaunchMode
{
    Trigger,
    Gate,
    Toggle,
    Repeat
};

enum class FollowActionKind
{
    None,
    Stop,
    Next,
    Previous,
    First,
    Last
};

/// A clip that lives in the session grid. `scene` is the row, 0 .. kSessionScenes-1.
struct SessionSlot
{
    int scene = 0;
    Clip clip;
    ClipLaunchMode launch = ClipLaunchMode::Trigger;
    FollowActionKind follow = FollowActionKind::None;
    double followAfterQuarters = 4.0;
    double repeatQuarters = 1.0;
};

struct DrumPad
{
    int note = 36;
    int chokeGroup = 0; ///< 0 does not choke. Pads that share a group silence each other.
    float gain = 1.0f;
    float tuneSemitones = 0.0f;
    EffectSlot chain[2] {};
    int chainLength = 0;
};

inline constexpr int kMaxDrumPads = 16;

struct DrumRack
{
    DrumPad pads[kMaxDrumPads] {};
    int count = 0;
};

struct Send
{
    std::string targetId = "master";
    double levelDb = -96.0;
    bool preFader = false; ///< Post-fader follows the channel fader. Pre-fader does not.
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
    Drum,
    Wavetable
};

struct DeviceChain
{
    std::string name = "Chain";
    float gain = 1.0f;
    std::vector<EffectSlot> effects;
};

struct MacroAssignment
{
    int chain = 0;
    int effect = 0;
    int parameter = 0;
    double minimum = 0.0;
    double maximum = 1.0;
};

struct DeviceMacro
{
    std::string name = "Macro";
    double value = 0.5;
    std::vector<MacroAssignment> assignments;
};

struct Modulator
{
    bool enabled = false;
    double rateHz = 0.5;
    double depth = 6.0;
    std::string target = "mixer.gainDb";
};

struct DeviceRack
{
    std::vector<DeviceChain> chains;
    std::vector<DeviceMacro> macros;
};

struct PluginInsert
{
    std::string identifier;
    std::string name;
    std::string state;
};

enum class MidiEffectKind
{
    Arpeggiator,
    Chord,
    Scale,
    Pitch,
    Velocity
};

enum class ScaleKind
{
    Major,
    NaturalMinor,
    Pentatonic
};

enum class ChordKind
{
    Major,
    Minor,
    Power
};

/// One MIDI processor in front of the instrument.
struct MidiEffect
{
    MidiEffectKind kind = MidiEffectKind::Pitch;
    bool enabled = true;
    int division = 16;
    int octaves = 1;
    ChordKind chord = ChordKind::Major;
    ScaleKind scale = ScaleKind::Major;
    int root = 0;
    int semitones = 0;
    int velocityAdd = 0;
    double velocityScale = 1.0;
};

struct Track
{
    std::string id;
    std::string name;
    TrackType type = TrackType::Audio;
    bool group = false;
    bool returns = false; ///< A return bus. Sends can target it, and it has its own fader.
    std::string outputId = "master";
    MixerState mixer;
    SynthPatch synth;
    InstrumentKind instrument = InstrumentKind::Synth;
    std::vector<SamplerZone> samplerZones;
    std::vector<AutomationLane> automation;
    std::vector<Send> sends;
    std::vector<EffectSlot> effects;
    DeviceRack rack;
    Modulator modulator;
    std::vector<Clip> clips;
    std::vector<SessionSlot> session;
    std::string pluginIdentifier;
    std::string pluginName;
    std::string pluginState;
    std::vector<PluginInsert> plugins;
    std::vector<MidiEffect> midiEffects; ///< Processors in front of the instrument. MIDI tracks only.
    int sidechainTrack = -1;             ///< Track index whose output feeds this track's compressor.
    DrumRack drumRack;
    int crossfaderSide = 0;              ///< 0 none, 1 A, 2 B.
    int inputPair = 0;                   ///< Stereo input pair. 0 is channels 1-2.
    int outputPair = 0;                  ///< Stereo output pair. 0 stays on the master.
};

struct SignaturePoint
{
    double quarterNotes = 0.0;
    TimeSignature signature {};
};

struct Project
{
    std::string name = "Untitled";
    double tempoBpm = 120.0;
    TimeSignature timeSignature {};
    std::vector<SignaturePoint> signatures;
    bool punchEnabled = false;
    double punchStartQuarter = 0.0;
    double punchEndQuarter = 4.0;
    float crossfader = 0.0f; ///< 0 is crossfader A, 1 is B.
    bool loopEnabled = false;
    double loopStartQuarterNotes = 0.0;
    double loopEndQuarterNotes = 8.0;
    int countInBars = 1;
    MixerState master; ///< Only gainDb is used. Pan, mute, and solo are ignored.
    std::vector<Track> tracks;
    double sceneTempoBpm[kSessionScenes] {}; ///< 0 keeps the project tempo for that scene.

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
