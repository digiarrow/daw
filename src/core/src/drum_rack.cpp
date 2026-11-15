#include "waveform/drum_rack.hpp"

#include "waveform/effects.hpp"
#include "waveform/later.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{

int strikeDrumRack (PadVoice* voices, int capacity, const DrumRack& rack, int note) noexcept
{
    if (voices == nullptr || capacity <= 0)
        return -1;

    int padIndex = -1;

    for (int index = 0; index < rack.count && index < kMaxDrumPads; ++index)
        if (rack.pads[index].note == note)
            padIndex = index;

    if (padIndex < 0)
        return -1;

    const int choke = rack.pads[padIndex].chokeGroup;

    if (choke > 0)
    {
        for (int index = 0; index < capacity; ++index)
            if (voices[index].playing && voices[index].chokeGroup == choke)
                voices[index].playing = false;
    }

    for (int index = 0; index < capacity; ++index)
    {
        if (voices[index].playing)
            continue;

        voices[index].pad = padIndex;
        voices[index].chokeGroup = choke;
        voices[index].ageSeconds = 0.0f;
        voices[index].playing = true;
        return index;
    }

    voices[0].pad = padIndex;
    voices[0].chokeGroup = choke;
    voices[0].ageSeconds = 0.0f;
    voices[0].playing = true;
    return 0;
}

void renderDrumRack (PadVoice* voices,
                     int voiceCount,
                     const DrumRack& rack,
                     int frames,
                     double sampleRate,
                     float* left,
                     float* right) noexcept
{
    if (voices == nullptr || left == nullptr || right == nullptr || frames <= 0 || ! (sampleRate > 0.0))
        return;

    constexpr int kSlice = 256;

    for (int voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
    {
        auto& voice = voices[voiceIndex];

        if (! voice.playing || voice.pad < 0 || voice.pad >= rack.count)
            continue;

        const auto& pad = rack.pads[voice.pad];
        EffectState state;

        for (int rendered = 0; rendered < frames;)
        {
            const int count = std::min (kSlice, frames - rendered);
            float padLeft[kSlice] {};
            float padRight[kSlice] {};

            for (int index = 0; index < count; ++index)
            {
                const double time = static_cast<double> (voice.ageSeconds) + static_cast<double> (index) / sampleRate;
                const float tone = 180.0f * std::pow (2.0f, pad.tuneSemitones / 12.0f);
                const float sample = drumSample (time, tone, 0.45f, 0.18f) * pad.gain;
                padLeft[index] = sample;
                padRight[index] = sample;
            }

            for (int chain = 0; chain < pad.chainLength && chain < 2; ++chain)
                processEffect (pad.chain[chain], state, padLeft, padRight, count, sampleRate);

            for (int index = 0; index < count; ++index)
            {
                left[rendered + index] += padLeft[index];
                right[rendered + index] += padRight[index];
            }

            voice.ageSeconds += static_cast<float> (static_cast<double> (count) / sampleRate);
            rendered += count;
        }

        if (voice.ageSeconds > 1.5f)
            voice.playing = false;
    }
}

} // namespace waveform
