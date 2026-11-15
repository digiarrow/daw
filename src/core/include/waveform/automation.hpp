#pragma once

#include "waveform/timing.hpp"

#include <string>
#include <vector>

namespace waveform
{

struct AutomationPoint
{
    double quarterNotes = 0.0;
    double value = 0.0;
};

struct AutomationLane
{
    std::string parameter;
    std::vector<AutomationPoint> points;
};

/// Linear value at `quarterNotes`. Returns `fallback` when the lane is empty.
[[nodiscard]] double automationAt (const AutomationLane& lane, double quarterNotes, double fallback) noexcept;

/// Sample-accurate ramp. `out[i]` is the lane at the quarter note of sample `i`.
void writeAutomation (const AutomationLane& lane,
                      double startQuarter,
                      int frames,
                      double sampleRate,
                      double bpm,
                      float* out,
                      double fallback) noexcept;

/// Multiplies a stereo block by a per-sample gain. `gain` may be null.
void applyGainCurve (float* left, float* right, const float* gain, int frames) noexcept;

/// Constant-power pan per sample. `pan` is -1..1. Null leaves the block unchanged.
void applyPanCurve (float* left, float* right, const float* pan, int frames) noexcept;

enum class AutomationKind
{
    None,
    GainDb,
    Pan,
    Cutoff,
    Effect
};

struct AutomationBinding
{
    AutomationKind kind = AutomationKind::None;
    int effect = -1;
    int parameter = -1; ///< 0..5 for an effect lane.
};

/// Parses `mixer.gainDb`, `mixer.pan`, `synth.cutoffHz`, and `effect.<n>.p<0-5>`.
[[nodiscard]] AutomationBinding bindAutomationParameter (const char* name) noexcept;

/// Clip envelope. `position` is clip-local (seconds or quarters, matching the points).
[[nodiscard]] double clipEnvelopeAt (const AutomationPoint* points, int count, double position, double fallback) noexcept;

/// Names the mixer and device parameters a lane can target.
[[nodiscard]] bool isAutomatableParameter (const std::string& name) noexcept;

enum class LaunchQuantize
{
    None,
    Beat,
    Bar
};

/// Next quarter-note boundary at or after `position` for a quantized launch.
[[nodiscard]] double nextLaunchQuarter (double position, LaunchQuantize quantize, TimeSignature signature) noexcept;

} // namespace waveform
