#include "waveform/automation.hpp"

#include "waveform/mix.hpp"
#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace waveform
{

double automationAt (const AutomationLane& lane, double quarterNotes, double fallback) noexcept
{
    if (lane.points.empty() || std::isnan (quarterNotes))
        return fallback;

    if (quarterNotes <= lane.points.front().quarterNotes)
        return lane.points.front().value;

    if (quarterNotes >= lane.points.back().quarterNotes)
        return lane.points.back().value;

    for (std::size_t index = 1; index < lane.points.size(); ++index)
    {
        const auto& right = lane.points[index];
        const auto& left = lane.points[index - 1];

        if (quarterNotes > right.quarterNotes)
            continue;

        const double span = right.quarterNotes - left.quarterNotes;

        if (! (span > 0.0))
            return right.value;

        const double mix = (quarterNotes - left.quarterNotes) / span;
        return left.value + (right.value - left.value) * mix;
    }

    return fallback;
}

void writeAutomation (const AutomationLane& lane,
                      double startQuarter,
                      int frames,
                      double sampleRate,
                      double bpm,
                      float* out,
                      double fallback) noexcept
{
    if (out == nullptr || frames <= 0)
        return;

    const double quartersPerSample = sampleRate > 0.0 && bpm > 0.0 ? (bpm / 60.0) / sampleRate : 0.0;

    for (int index = 0; index < frames; ++index)
    {
        const double quarter = startQuarter + quartersPerSample * static_cast<double> (index);
        out[index] = static_cast<float> (automationAt (lane, quarter, fallback));
    }
}

void applyGainCurve (float* left, float* right, const float* gain, int frames) noexcept
{
    if (gain == nullptr || frames <= 0)
        return;

    for (int index = 0; index < frames; ++index)
    {
        if (left != nullptr)
            left[index] *= gain[index];

        if (right != nullptr)
            right[index] *= gain[index];
    }
}

void applyPanCurve (float* left, float* right, const float* pan, int frames) noexcept
{
    if (pan == nullptr || frames <= 0 || left == nullptr || right == nullptr)
        return;

    for (int index = 0; index < frames; ++index)
    {
        const PanGains gains = constantPowerPan (pan[index]);
        left[index] *= gains.left;
        right[index] *= gains.right;
    }
}

AutomationBinding bindAutomationParameter (const char* name) noexcept
{
    AutomationBinding binding;

    if (name == nullptr)
        return binding;

    if (std::strcmp (name, "mixer.gainDb") == 0)
    {
        binding.kind = AutomationKind::GainDb;
        return binding;
    }

    if (std::strcmp (name, "mixer.pan") == 0)
    {
        binding.kind = AutomationKind::Pan;
        return binding;
    }

    if (std::strcmp (name, "synth.cutoffHz") == 0)
    {
        binding.kind = AutomationKind::Cutoff;
        return binding;
    }

    constexpr const char* prefix = "effect.";

    if (std::strncmp (name, prefix, 7) != 0)
        return binding;

    const char* cursor = name + 7;
    int effect = 0;
    bool sawDigit = false;

    while (*cursor >= '0' && *cursor <= '9')
    {
        sawDigit = true;
        effect = effect * 10 + (*cursor - '0');
        ++cursor;
    }

    if (! sawDigit || cursor[0] != '.' || cursor[1] != 'p')
        return binding;

    cursor += 2;
    int parameter = 0;
    sawDigit = false;

    while (*cursor >= '0' && *cursor <= '9')
    {
        sawDigit = true;
        parameter = parameter * 10 + (*cursor - '0');
        ++cursor;
    }

    if (! sawDigit || *cursor != '\0' || parameter > 5)
        return binding;

    binding.kind = AutomationKind::Effect;
    binding.effect = effect;
    binding.parameter = parameter;
    return binding;
}

double clipEnvelopeAt (const AutomationPoint* points, int count, double position, double fallback) noexcept
{
    if (points == nullptr || count <= 0 || std::isnan (position))
        return fallback;

    if (position <= points[0].quarterNotes)
        return points[0].value;

    if (position >= points[count - 1].quarterNotes)
        return points[count - 1].value;

    for (int index = 1; index < count; ++index)
    {
        const auto& right = points[index];
        const auto& left = points[index - 1];

        if (position > right.quarterNotes)
            continue;

        const double span = right.quarterNotes - left.quarterNotes;

        if (! (span > 0.0))
            return right.value;

        const double mix = (position - left.quarterNotes) / span;
        return left.value + (right.value - left.value) * mix;
    }

    return fallback;
}

bool isAutomatableParameter (const std::string& name) noexcept
{
    if (name == "mixer.gainDb" || name == "mixer.pan" || name == "synth.cutoffHz" || name == "clip.envelope")
        return true;

    if (name.rfind ("effect.", 0) != 0)
        return false;

    return name.find (".p") != std::string::npos;
}

double nextLaunchQuarter (double position, LaunchQuantize quantize, TimeSignature signature) noexcept
{
    if (quantize == LaunchQuantize::None || std::isnan (position))
        return std::max (0.0, position);

    const double grid = quantize == LaunchQuantize::Bar ? Timing::quarterNotesPerBar (signature)
                                                        : Timing::quarterNotesPerBeat (signature);

    if (! (grid > 0.0))
        return std::max (0.0, position);

    const double steps = std::ceil ((position - 1.0e-9) / grid);
    return std::max (0.0, steps * grid);
}

} // namespace waveform
