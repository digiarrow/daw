#include "waveform/synthesis.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

constexpr double kTwoPi = 6.283185307179586;

void addClick (float* left,
               float* right,
               std::int64_t startFrame,
               int frames,
               std::int64_t clickFrame,
               std::int64_t clickLength,
               double sampleRate,
               double frequency,
               float amplitude) noexcept
{
    const auto from = std::max (clickFrame, startFrame);
    const auto to = std::min (clickFrame + clickLength, startFrame + frames);

    for (std::int64_t frame = from; frame < to; ++frame)
    {
        const double time = static_cast<double> (frame - clickFrame) / sampleRate;
        const double envelope = std::exp (-time * 28.0);
        const float sample = static_cast<float> (std::sin (kTwoPi * frequency * time) * envelope) * amplitude;
        const auto index = static_cast<int> (frame - startFrame);
        left[index] += sample;
        right[index] += sample;
    }
}

} // namespace

void renderMetronome (float* left,
                      float* right,
                      std::int64_t startFrame,
                      int frames,
                      double sampleRate,
                      double bpm,
                      TimeSignature signature,
                      float gain) noexcept
{
    if (left == nullptr || right == nullptr || frames <= 0 || gain == 0.0f)
        return;

    if (! Timing::isValidSampleRate (sampleRate) || ! (bpm > 0.0) || ! signature.isValid())
        return;

    const double quartersPerBeat = Timing::quarterNotesPerBeat (signature);
    const auto clickLength = std::max<std::int64_t> (1, static_cast<std::int64_t> (std::llround (sampleRate * 0.020)));
    const auto searchFrom = std::max<std::int64_t> (0, startFrame - clickLength);
    const double firstQuarter = Timing::quarterNotesFromSamples (searchFrom, sampleRate, bpm);
    auto beatIndex = static_cast<std::int64_t> (std::floor (firstQuarter / quartersPerBeat));

    if (beatIndex < 0)
        beatIndex = 0;

    const double lastQuarter = Timing::quarterNotesFromSamples (startFrame + frames, sampleRate, bpm);
    const auto lastBeat = static_cast<std::int64_t> (std::floor (lastQuarter / quartersPerBeat)) + 1;

    for (std::int64_t beat = beatIndex; beat <= lastBeat; ++beat)
    {
        const double quarterNote = static_cast<double> (beat) * quartersPerBeat;
        const auto clickAt = Timing::samplesFromQuarterNotes (quarterNote, sampleRate, bpm);
        const bool accent = signature.numerator > 0 && (beat % signature.numerator) == 0;
        const double frequency = accent ? 1760.0 : 988.0;
        const float amplitude = (accent ? 0.35f : 0.22f) * gain;
        addClick (left, right, startFrame, frames, clickAt, clickLength, sampleRate, frequency, amplitude);
    }
}

void renderTestTone (float* left,
                     float* right,
                     int frames,
                     double sampleRate,
                     double frequencyHertz,
                     float gain,
                     Oscillator& oscillator) noexcept
{
    if (left == nullptr || right == nullptr || frames <= 0 || ! (sampleRate > 0.0) || gain == 0.0f)
        return;

    if (! (frequencyHertz > 0.0) || std::isnan (frequencyHertz))
        return;

    const double delta = kTwoPi * frequencyHertz / sampleRate;

    for (int index = 0; index < frames; ++index)
    {
        const float sample = static_cast<float> (std::sin (oscillator.phase)) * gain;
        left[index] += sample;
        right[index] += sample;
        oscillator.phase += delta;

        if (oscillator.phase > kTwoPi)
            oscillator.phase -= kTwoPi;
    }
}

} // namespace waveform
