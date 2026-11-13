#pragma once

#include "waveform/project.hpp"
#include "waveform/synth.hpp"

#include <vector>

namespace waveform
{

struct WarpMarker
{
    double sourceSeconds = 0.0;
    double timelineQuarters = 0.0;
};

/// Source time for an unwarped or marker-warped clip. Empty markers keep a constant seconds-per-quarter.
[[nodiscard]] double warpedSourceSeconds (double timelineQuarter,
                                          double clipStartQuarter,
                                          double secondsPerQuarter,
                                          const std::vector<WarpMarker>& markers) noexcept;

struct CompSegment
{
    double startQuarter = 0.0;
    double endQuarter = 0.0;
    int takeIndex = 0;
};

/// Which take is audible at this quarter. Returns 0 when no segment matches.
[[nodiscard]] int compTakeAt (const std::vector<CompSegment>& segments, double quarter) noexcept;

struct RackChain
{
    float gain = 1.0f;
    std::vector<EffectSlot> effects;
};

/// Parallel rack: each chain is processed from a copy of the dry buffer and summed.
void mixRack (const RackChain* chains,
              int chainCount,
              EffectState* states,
              int statesPerChain,
              const float* dryLeft,
              const float* dryRight,
              float* left,
              float* right,
              int frames,
              double sampleRate) noexcept;

/// Link is not linked. The upstream library is GPLv2 and is not vendored.
struct LinkStub
{
    bool enabled = false;
    double tempoBpm = 120.0;
    double beat = 0.0;
};

[[nodiscard]] inline LinkStub linkStatus() noexcept { return {}; }

struct MusicalKey
{
    int root = 0; ///< 0 = C
    bool minor = false;
};

[[nodiscard]] bool pitchInKey (int note, MusicalKey key) noexcept;
[[nodiscard]] int constrainToKey (int note, MusicalKey key) noexcept;

[[nodiscard]] std::vector<MidiNote> reverseNotes (std::vector<MidiNote> notes, double clipLengthQuarters);
[[nodiscard]] std::vector<MidiNote> makeLegato (std::vector<MidiNote> notes);
[[nodiscard]] std::vector<MidiNote> humanizeNotes (std::vector<MidiNote> notes, double timingAmount, int velocityAmount, unsigned seed);

struct MpeExpression
{
    float pitchBendSemitones = 0.0f;
    float pressure = 0.0f;
    float timbre = 0.0f;
};

[[nodiscard]] double mpeFrequencyRatio (float pitchBendSemitones) noexcept;

struct Lfo
{
    double rateHz = 1.0;
    double depth = 1.0;
    double phase = 0.0;
};

[[nodiscard]] float lfoValue (Lfo& lfo, int frames, double sampleRate) noexcept;

[[nodiscard]] float fmSample (double carrierHz, double modulatorHz, double index, double timeSeconds) noexcept;

void renderFmNotes (const SoundingNote* notes, int noteCount, int frames, double sampleRate, const SynthPatch& patch, float leftGain, float rightGain, float* left, float* right) noexcept;
void renderDrumNotes (const SoundingNote* notes, int noteCount, int frames, double sampleRate, const SynthPatch& patch, float leftGain, float rightGain, float* left, float* right) noexcept;
[[nodiscard]] float drumSample (double timeSeconds, float toneHz, float noise, float decaySeconds) noexcept;

struct ScriptResult
{
    bool ok = false;
    std::string message;
};

/// Tiny project script: `tempo <bpm>`, `add audio <name>`, `add midi <name>`.
[[nodiscard]] ScriptResult runScript (Project& project, const std::string& text);

struct ControlMapping
{
    int cc = 1;
    std::string parameter; ///< "mixer.gainDb"
};

[[nodiscard]] bool applyControlChange (Project& project, const std::string& trackId, int cc, int value, const std::vector<ControlMapping>& map);

struct DetectedNote
{
    int note = 60;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
};

/// Monophonic pitch estimate. Returns empty when the signal is too quiet.
[[nodiscard]] std::vector<DetectedNote> audioToMidi (const float* samples, int frames, double sampleRate) noexcept;

/// Fixed delay used for plugin delay compensation. `history` holds at least `delayFrames` samples.
void compensateDelay (const float* input,
                      float* output,
                      int frames,
                      float* history,
                      int delayFrames,
                      int& cursor) noexcept;

} // namespace waveform
