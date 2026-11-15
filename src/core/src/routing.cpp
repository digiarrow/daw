#include "waveform/routing.hpp"

#include "waveform/mix.hpp"

#include <algorithm>

namespace waveform
{

void routeTracks (const float* const* trackLeft,
                  const float* const* trackRight,
                  const RouteEndpoint* routes,
                  int trackCount,
                  float* const* busLeft,
                  float* const* busRight,
                  int busCount,
                  int frames) noexcept
{
    if (trackLeft == nullptr || trackRight == nullptr || routes == nullptr || busLeft == nullptr || busRight == nullptr)
        return;

    if (frames <= 0 || trackCount <= 0 || busCount <= 0)
        return;

    for (int bus = 0; bus < busCount; ++bus)
    {
        if (busLeft[bus] != nullptr)
            std::fill (busLeft[bus], busLeft[bus] + frames, 0.0f);

        if (busRight[bus] != nullptr)
            std::fill (busRight[bus], busRight[bus] + frames, 0.0f);
    }

    bool anySolo = false;

    for (int index = 0; index < trackCount; ++index)
        if (! routes[index].group && routes[index].solo)
            anySolo = true;

    auto add = [&] (int bus, const float* left, const float* right, float gainLeft, float gainRight, const float* sampleGain, const float* samplePan, bool applyGain, float extra)
    {
        if (bus < 0 || bus >= busCount || left == nullptr || right == nullptr)
            return;

        if (busLeft[bus] == nullptr || busRight[bus] == nullptr)
            return;

        for (int frame = 0; frame < frames; ++frame)
        {
            float panLeft = gainLeft;
            float panRight = gainRight;
            float fader = 1.0f;

            if (samplePan != nullptr)
            {
                const PanGains pan = constantPowerPan (samplePan[frame]);
                panLeft = pan.left;
                panRight = pan.right;
            }

            if (applyGain)
                fader = sampleGain != nullptr ? sampleGain[frame] : gainLeft;

            if (samplePan == nullptr && ! (sampleGain != nullptr && applyGain))
            {
                busLeft[bus][frame] += left[frame] * gainLeft * extra;
                busRight[bus][frame] += right[frame] * gainRight * extra;
                continue;
            }

            const float leftGain = (applyGain ? fader : 1.0f) * (samplePan != nullptr ? panLeft : gainLeft) * extra;
            const float rightGain = (applyGain ? fader : 1.0f) * (samplePan != nullptr ? panRight : gainRight) * extra;
            busLeft[bus][frame] += left[frame] * leftGain;
            busRight[bus][frame] += right[frame] * rightGain;
        }
    };

    for (int index = 0; index < trackCount; ++index)
    {
        const auto& route = routes[index];

        if (route.group || route.mute || (anySolo && ! route.solo))
            continue;

        if (trackLeft[index] == nullptr || trackRight[index] == nullptr)
            continue;

        const int destination = route.outputBus >= 0 && route.outputBus < busCount ? route.outputBus : 0;
        const bool curved = route.sampleGain != nullptr || route.samplePan != nullptr;

        if (! curved)
            add (destination, trackLeft[index], trackRight[index], route.gain * route.panLeft, route.gain * route.panRight, nullptr, nullptr, false, 1.0f);
        else
            add (destination, trackLeft[index], trackRight[index], route.panLeft, route.panRight, route.sampleGain, route.samplePan, true, 1.0f);

        for (int send = 0; send < kMaxSends; ++send)
        {
            if (! (route.sendGain[send] > 0.0f))
                continue;

            if (! curved)
            {
                const float fader = route.sendPre[send] ? 1.0f : route.gain;
                add (route.sendBus[send], trackLeft[index], trackRight[index],
                     route.sendGain[send] * fader * route.panLeft,
                     route.sendGain[send] * fader * route.panRight,
                     nullptr, nullptr, false, 1.0f);
            }
            else if (route.sendPre[send])
                add (route.sendBus[send], trackLeft[index], trackRight[index], route.panLeft, route.panRight, nullptr, route.samplePan, false, route.sendGain[send]);
            else
                add (route.sendBus[send], trackLeft[index], trackRight[index], route.panLeft, route.panRight, route.sampleGain, route.samplePan, true, route.sendGain[send]);
        }
    }

    for (int index = 0; index < trackCount; ++index)
    {
        const auto& route = routes[index];

        if (! route.group || route.mute)
            continue;

        // Group bus index is outputBus of the group track itself (where sources were summed).
        const int source = route.outputBus;
        const int destination = 0;

        if (source <= 0 || source >= busCount || busLeft[source] == nullptr)
            continue;

        const bool curved = route.sampleGain != nullptr || route.samplePan != nullptr;

        if (! curved)
            add (destination, busLeft[source], busRight[source], route.gain * route.panLeft, route.gain * route.panRight, nullptr, nullptr, false, 1.0f);
        else
            add (destination, busLeft[source], busRight[source], route.panLeft, route.panRight, route.sampleGain, route.samplePan, true, 1.0f);
    }
}

} // namespace waveform
