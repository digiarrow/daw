#include "waveform/synth.hpp"
#include "waveform/later.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

constexpr double kTwoPi = 6.283185307179586;
constexpr float kPi = 3.14159265f;

float oscillator (OscillatorWave wave, double cycles) noexcept
{
    const double wrapped = cycles - std::floor (cycles);

    switch (wave)
    {
        case OscillatorWave::Sine:
            return static_cast<float> (std::sin (wrapped * kTwoPi));
        case OscillatorWave::Square:
            return wrapped < 0.5 ? 1.0f : -1.0f;
        case OscillatorWave::Triangle:
            return wrapped < 0.5 ? static_cast<float> (4.0 * wrapped - 1.0)
                                 : static_cast<float> (3.0 - 4.0 * wrapped);
        case OscillatorWave::Saw:
            break;
    }

    return static_cast<float> (2.0 * wrapped - 1.0);
}

float heldLevel (double seconds, const SynthPatch& patch) noexcept
{
    const double attack = std::max (1.0e-4, patch.attackSeconds);
    const double decay = std::max (1.0e-4, patch.decaySeconds);
    const float sustain = std::clamp (static_cast<float> (patch.sustain), 0.0f, 1.0f);

    if (seconds < attack)
        return static_cast<float> (seconds / attack);

    if (seconds < attack + decay)
    {
        const float slope = static_cast<float> ((seconds - attack) / decay);
        return 1.0f + (sustain - 1.0f) * slope;
    }

    return sustain;
}

float envelopeAt (double seconds, double sinceRelease, const SynthPatch& patch) noexcept
{
    if (sinceRelease >= 0.0)
    {
        const double release = std::max (1.0e-4, patch.releaseSeconds);
        const float start = heldLevel (std::max (0.0, seconds - sinceRelease), patch);
        const float remain = 1.0f - static_cast<float> (sinceRelease / release);
        return std::max (0.0f, start * remain);
    }

    return heldLevel (seconds, patch);
}

float filterSample (float input, int note, const SynthPatch& patch, double sampleRate, SynthVoiceMemory& memory) noexcept
{
    if (note < 0 || note > 127 || ! (sampleRate > 0.0))
        return input;

    const float cutoff = std::clamp (static_cast<float> (patch.cutoffHz), 40.0f, static_cast<float> (sampleRate * 0.45));
    const float f = 2.0f * std::sin (kPi * cutoff / static_cast<float> (sampleRate));
    const float q = std::clamp (1.0f - static_cast<float> (patch.resonance), 0.08f, 1.0f);
    float& low = memory.low[note];
    float& band = memory.band[note];
    low += f * band;
    const float high = input - low - q * band;
    band += f * high;

    if (! std::isfinite (low) || ! std::isfinite (band))
    {
        low = 0.0f;
        band = 0.0f;
        return 0.0f;
    }

    return low;
}

} // namespace

void renderSynthNotes (const SoundingNote* notes,
                       int noteCount,
                       int frames,
                       double sampleRate,
                       const SynthPatch& patch,
                       SynthVoiceMemory& memory,
                       float leftGain,
                       float rightGain,
                       float* left,
                       float* right) noexcept
{
    if (notes == nullptr || left == nullptr || right == nullptr || frames <= 0 || noteCount <= 0 || ! (sampleRate > 0.0))
        return;

    const double detune = std::pow (2.0, patch.detuneCents / 1200.0);

    for (int index = 0; index < noteCount; ++index)
    {
        const SoundingNote& note = notes[index];

        if (note.note < 0 || note.note > 127 || note.velocity <= 0)
            continue;

        const double frequency = 440.0 * std::pow (2.0, (static_cast<double> (note.note) - 69.0) / 12.0) * mpeFrequencyRatio (note.slideSemitones);
        const float velocity = static_cast<float> (std::clamp (note.velocity, 1, 127)) / 127.0f;
        const int first = std::clamp (static_cast<int> (std::floor (note.onsetSeconds * sampleRate)), 0, frames);
        const double tail = note.releaseSeconds + std::max (1.0e-4, patch.releaseSeconds);
        const int last = std::clamp (static_cast<int> (std::ceil (tail * sampleRate)), 0, frames);

        for (int frame = first; frame < last; ++frame)
        {
            const double time = static_cast<double> (frame) / sampleRate;
            const double sinceOn = time - note.onsetSeconds;
            const double sinceRelease = time - note.releaseSeconds;
            const float env = envelopeAt (sinceOn, sinceRelease, patch);

            if (env <= 0.0f)
                continue;

            const float primary = oscillator (patch.wave, frequency * sinceOn);
            const float second = oscillator (patch.wave, frequency * detune * sinceOn);
            const float mixed = (primary * 0.65f + second * 0.35f) * env * velocity;
            const float filtered = filterSample (mixed, note.note, patch, sampleRate, memory);
            const float sample = filtered * 0.22f;
            left[frame] += sample * leftGain;
            right[frame] += sample * rightGain;
        }
    }
}

} // namespace waveform
