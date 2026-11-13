#include "waveform/sampler.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

float envelope (double seconds, double sinceRelease, const SynthPatch& patch) noexcept
{
    const double attack = std::max (1.0e-4, patch.attackSeconds);
    const double decay = std::max (1.0e-4, patch.decaySeconds);
    const float sustain = std::clamp (static_cast<float> (patch.sustain), 0.0f, 1.0f);
    auto held = [&] (double time) {
        if (time < attack)
            return static_cast<float> (time / attack);

        if (time < attack + decay)
            return 1.0f + (sustain - 1.0f) * static_cast<float> ((time - attack) / decay);

        return sustain;
    };

    if (sinceRelease >= 0.0)
    {
        const double release = std::max (1.0e-4, patch.releaseSeconds);
        const float start = held (std::max (0.0, seconds - sinceRelease));
        return std::max (0.0f, start * (1.0f - static_cast<float> (sinceRelease / release)));
    }

    return held (seconds);
}

const SamplerZoneView* findZone (const SamplerZoneView* zones, int count, int note) noexcept
{
    for (int index = 0; index < count; ++index)
        if (note >= zones[index].lowNote && note <= zones[index].highNote && zones[index].left != nullptr)
            return &zones[index];

    return nullptr;
}

} // namespace

void renderSamplerNotes (const SoundingNote* notes,
                         int noteCount,
                         const SamplerZoneView* zones,
                         int zoneCount,
                         int frames,
                         double sampleRate,
                         const SynthPatch& patch,
                         float leftGain,
                         float rightGain,
                         float* left,
                         float* right) noexcept
{
    if (notes == nullptr || zones == nullptr || left == nullptr || right == nullptr || frames <= 0 || ! (sampleRate > 0.0))
        return;

    for (int index = 0; index < noteCount; ++index)
    {
        const auto& note = notes[index];
        const auto* zone = findZone (zones, zoneCount, note.note);

        if (zone == nullptr || zone->frames <= 0)
            continue;

        const double ratio = std::pow (2.0, (static_cast<double> (note.note) - zone->rootNote) / 12.0)
                           * (zone->sourceSampleRate / sampleRate);
        const float velocity = static_cast<float> (std::clamp (note.velocity, 1, 127)) / 127.0f;
        const int first = std::clamp (static_cast<int> (std::floor (note.onsetSeconds * sampleRate)), 0, frames);
        const double tail = note.releaseSeconds + std::max (1.0e-4, patch.releaseSeconds);
        const int last = std::clamp (static_cast<int> (std::ceil (tail * sampleRate)), 0, frames);

        for (int frame = first; frame < last; ++frame)
        {
            const double time = static_cast<double> (frame) / sampleRate;
            const double sinceOn = time - note.onsetSeconds;
            const float env = envelope (sinceOn, time - note.releaseSeconds, patch);

            if (env <= 0.0f)
                continue;

            const double source = sinceOn * sampleRate * ratio;
            const auto index0 = static_cast<std::int64_t> (source);

            if (index0 < 0 || index0 >= zone->frames)
                continue;

            const auto index1 = std::min (index0 + 1, zone->frames - 1);
            const float fraction = static_cast<float> (source - static_cast<double> (index0));
            const float sampleL = zone->left[index0] * (1.0f - fraction) + zone->left[index1] * fraction;
            const float* rightChannel = zone->right != nullptr ? zone->right : zone->left;
            const float sampleR = rightChannel[index0] * (1.0f - fraction) + rightChannel[index1] * fraction;
            left[frame] += sampleL * env * velocity * leftGain;
            right[frame] += sampleR * env * velocity * rightGain;
        }
    }
}

} // namespace waveform
