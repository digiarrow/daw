#include "waveform/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace waveform
{
namespace
{

constexpr float kPi = 3.14159265f;

float clampUnit (double value, double lo, double hi) noexcept
{
    return static_cast<float> (std::clamp (value, lo, hi));
}

} // namespace

EffectSlot defaultEffect (EffectKind kind) noexcept
{
    EffectSlot slot;
    slot.kind = kind;

    switch (kind)
    {
        case EffectKind::Eq:
            slot.p0 = 0.0;    // low gain dB
            slot.p1 = 0.0;    // mid gain dB
            slot.p2 = 1000.0; // mid Hz
            slot.p3 = 0.0;    // high gain dB
            break;
        case EffectKind::Compressor:
            slot.p0 = -18.0; // threshold dB
            slot.p1 = 4.0;   // ratio
            slot.p2 = 0.01;  // attack seconds
            slot.p3 = 0.1;   // release seconds
            slot.p4 = 0.0;   // makeup dB
            break;
        case EffectKind::Delay:
            slot.p0 = 0.25; // time seconds, capped by the buffer
            slot.p1 = 0.35; // feedback
            slot.p2 = 0.35; // mix
            break;
        case EffectKind::Reverb:
            slot.p0 = 0.4; // size
            slot.p1 = 0.3; // damp
            slot.p2 = 0.25; // mix
            break;
        case EffectKind::Limiter:
            slot.p0 = -1.0; // ceiling dB
            slot.p1 = 0.05; // release seconds
            break;
        case EffectKind::Utility:
            slot.p0 = 0.0; // gain dB
            slot.p1 = 1.0; // width 0..2
            break;
        case EffectKind::Chorus:
            slot.p0 = 0.6;  // rate Hz
            slot.p1 = 0.4;  // depth
            slot.p2 = 0.5;  // mix
            break;
        case EffectKind::Saturator:
            slot.p0 = 0.4; // drive
            slot.p1 = 0.7; // mix
            break;
    }

    return slot;
}

const char* effectName (EffectKind kind) noexcept
{
    switch (kind)
    {
        case EffectKind::Eq: return "eq";
        case EffectKind::Compressor: return "compressor";
        case EffectKind::Delay: return "delay";
        case EffectKind::Reverb: return "reverb";
        case EffectKind::Limiter: return "limiter";
        case EffectKind::Utility: return "utility";
        case EffectKind::Chorus: return "chorus";
        case EffectKind::Saturator: return "saturator";
    }

    return "utility";
}

EffectKind effectFromName (const char* name) noexcept
{
    if (name == nullptr)
        return EffectKind::Utility;

    const std::string text (name);

    if (text == "eq") return EffectKind::Eq;
    if (text == "compressor") return EffectKind::Compressor;
    if (text == "delay") return EffectKind::Delay;
    if (text == "reverb") return EffectKind::Reverb;
    if (text == "limiter") return EffectKind::Limiter;
    if (text == "chorus") return EffectKind::Chorus;
    if (text == "saturator") return EffectKind::Saturator;
    return EffectKind::Utility;
}

void resetEffect (EffectState& state) noexcept
{
    state = {};
}

void processEffect (const EffectSlot& slot, EffectState& state, float* left, float* right, int frames, double sampleRate) noexcept
{
    if (slot.bypass || left == nullptr || right == nullptr || frames <= 0 || ! (sampleRate > 0.0))
        return;

    const float sr = static_cast<float> (sampleRate);

    switch (slot.kind)
    {
        case EffectKind::Utility:
        {
            const float gain = std::pow (10.0f, clampUnit (slot.p0, -48.0, 24.0) / 20.0f);
            const float width = clampUnit (slot.p1, 0.0, 2.0);

            for (int i = 0; i < frames; ++i)
            {
                const float mid = 0.5f * (left[i] + right[i]);
                const float side = 0.5f * (left[i] - right[i]) * width;
                left[i] = (mid + side) * gain;
                right[i] = (mid - side) * gain;
            }
            break;
        }
        case EffectKind::Eq:
        {
            const float low = std::pow (10.0f, clampUnit (slot.p0, -18.0, 18.0) / 20.0f);
            const float mid = std::pow (10.0f, clampUnit (slot.p1, -18.0, 18.0) / 20.0f);
            const float high = std::pow (10.0f, clampUnit (slot.p3, -18.0, 18.0) / 20.0f);
            const float centre = clampUnit (slot.p2, 80.0, 8000.0);
            const float lowCoef = std::exp (-2.0f * kPi * 180.0f / sr);
            const float highCoef = std::exp (-2.0f * kPi * centre / sr);

            for (int i = 0; i < frames; ++i)
            {
                state.z1 = lowCoef * state.z1 + (1.0f - lowCoef) * left[i];
                state.z2 = lowCoef * state.z2 + (1.0f - lowCoef) * right[i];
                const float leftHigh = left[i] - (highCoef * state.y1 + (1.0f - highCoef) * left[i]);
                state.y1 = highCoef * state.y1 + (1.0f - highCoef) * left[i];
                const float rightHigh = right[i] - (highCoef * state.y2 + (1.0f - highCoef) * right[i]);
                state.y2 = highCoef * state.y2 + (1.0f - highCoef) * right[i];
                const float leftMid = left[i] - state.z1 - leftHigh;
                const float rightMid = right[i] - state.z2 - rightHigh;
                left[i] = state.z1 * low + leftMid * mid + leftHigh * high;
                right[i] = state.z2 * low + rightMid * mid + rightHigh * high;
            }
            break;
        }
        case EffectKind::Compressor:
        {
            const float threshold = std::pow (10.0f, clampUnit (slot.p0, -60.0, 0.0) / 20.0f);
            const float ratio = std::max (1.0f, clampUnit (slot.p1, 1.0, 20.0));
            const float attack = std::exp (-1.0f / (sr * std::max (0.001f, clampUnit (slot.p2, 0.001, 0.5))));
            const float release = std::exp (-1.0f / (sr * std::max (0.005f, clampUnit (slot.p3, 0.005, 1.0))));
            const float makeup = std::pow (10.0f, clampUnit (slot.p4, 0.0, 24.0) / 20.0f);

            for (int i = 0; i < frames; ++i)
            {
                const float detector = std::max (std::abs (left[i]), std::abs (right[i]));
                const float over = detector > threshold && threshold > 0.0f ? detector / threshold : 1.0f;
                const float target = over > 1.0f ? std::pow (over, 1.0f / ratio - 1.0f) : 1.0f;
                const float coef = target < state.env ? attack : release;
                state.env = coef * state.env + (1.0f - coef) * target;
                left[i] *= state.env * makeup;
                right[i] *= state.env * makeup;
            }
            break;
        }
        case EffectKind::Delay:
        {
            const int delayFrames = std::clamp (static_cast<int> (std::clamp (slot.p0, 0.0, 2.0) * sr), 1, kDelayCapacity - 1);
            const float feedback = clampUnit (slot.p1, 0.0, 0.9);
            const float mix = clampUnit (slot.p2, 0.0, 1.0);

            for (int i = 0; i < frames; ++i)
            {
                const int read = (state.write + kDelayCapacity - delayFrames) % kDelayCapacity;
                const float wetL = state.delayL[read];
                const float wetR = state.delayR[read];
                state.delayL[state.write] = left[i] + wetL * feedback;
                state.delayR[state.write] = right[i] + wetR * feedback;
                state.write = (state.write + 1) % kDelayCapacity;
                left[i] = left[i] * (1.0f - mix) + wetL * mix;
                right[i] = right[i] * (1.0f - mix) + wetR * mix;
            }
            break;
        }
        case EffectKind::Reverb:
        {
            const float size = clampUnit (slot.p0, 0.05, 1.0);
            const float damp = clampUnit (slot.p1, 0.0, 0.95);
            const float mix = clampUnit (slot.p2, 0.0, 1.0);
            const int lengths[4] = {
                std::clamp (static_cast<int> (300 * size), 32, 1023),
                std::clamp (static_cast<int> (470 * size), 32, 1023),
                std::clamp (static_cast<int> (630 * size), 32, 1023),
                std::clamp (static_cast<int> (820 * size), 32, 1023)
            };

            for (int i = 0; i < frames; ++i)
            {
                const float input = 0.5f * (left[i] + right[i]);
                float wetL = 0.0f;
                float wetR = 0.0f;

                for (int comb = 0; comb < 4; ++comb)
                {
                    const int read = (state.combPos + 1024 - lengths[comb]) & 1023;
                    const float sampleL = state.combL[comb][read];
                    const float sampleR = state.combR[comb][read];
                    state.combL[comb][state.combPos] = input + sampleL * (0.55f + 0.3f * size) * (1.0f - damp);
                    state.combR[comb][state.combPos] = input + sampleR * (0.55f + 0.3f * size) * (1.0f - damp);
                    wetL += sampleL;
                    wetR += sampleR * (comb % 2 == 0 ? 1.0f : 0.8f);
                }

                state.combPos = (state.combPos + 1) & 1023;
                left[i] = left[i] * (1.0f - mix) + wetL * 0.25f * mix;
                right[i] = right[i] * (1.0f - mix) + wetR * 0.25f * mix;
            }
            break;
        }
        case EffectKind::Limiter:
        {
            const float ceiling = std::pow (10.0f, clampUnit (slot.p0, -12.0, 0.0) / 20.0f);
            const float release = std::exp (-1.0f / (sr * std::max (0.01f, clampUnit (slot.p1, 0.01, 0.5))));

            for (int i = 0; i < frames; ++i)
            {
                const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
                const float target = peak > ceiling && peak > 0.0f ? ceiling / peak : 1.0f;
                state.env = target < state.env ? target : release * state.env + (1.0f - release) * target;
                left[i] = std::clamp (left[i] * state.env, -ceiling, ceiling);
                right[i] = std::clamp (right[i] * state.env, -ceiling, ceiling);
            }
            break;
        }
        case EffectKind::Chorus:
        {
            const float rate = clampUnit (slot.p0, 0.05, 5.0);
            const float depth = clampUnit (slot.p1, 0.0, 1.0);
            const float mix = clampUnit (slot.p2, 0.0, 1.0);

            for (int i = 0; i < frames; ++i)
            {
                state.env += rate / sr;
                if (state.env > 1.0f)
                    state.env -= 1.0f;

                const int delay = 200 + static_cast<int> (depth * 180.0f * (0.5f + 0.5f * std::sin (state.env * 2.0f * kPi)));
                const int read = (state.write + kDelayCapacity - std::clamp (delay, 1, kDelayCapacity - 1)) % kDelayCapacity;
                const float wetL = state.delayL[read];
                const float wetR = state.delayR[(read + 90) % kDelayCapacity];
                state.delayL[state.write] = left[i];
                state.delayR[state.write] = right[i];
                state.write = (state.write + 1) % kDelayCapacity;
                left[i] = left[i] * (1.0f - mix) + wetL * mix;
                right[i] = right[i] * (1.0f - mix) + wetR * mix;
            }
            break;
        }
        case EffectKind::Saturator:
        {
            const float drive = 1.0f + clampUnit (slot.p0, 0.0, 1.0) * 8.0f;
            const float mix = clampUnit (slot.p1, 0.0, 1.0);

            for (int i = 0; i < frames; ++i)
            {
                const float wetL = std::tanh (left[i] * drive);
                const float wetR = std::tanh (right[i] * drive);
                left[i] = left[i] * (1.0f - mix) + wetL * mix;
                right[i] = right[i] * (1.0f - mix) + wetR * mix;
            }
            break;
        }
    }
}

void processInserts (const EffectSlot* slots, int slotCount, EffectState* states, float* left, float* right, int frames, double sampleRate) noexcept
{
    if (slots == nullptr || states == nullptr)
        return;

    const int count = std::min (slotCount, kMaxInserts);

    for (int index = 0; index < count; ++index)
        processEffect (slots[index], states[index], left, right, frames, sampleRate);
}

} // namespace waveform
