#include "waveform/routing.hpp"

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

    auto add = [&] (int bus, const float* left, const float* right, float gainLeft, float gainRight)
    {
        if (bus < 0 || bus >= busCount || left == nullptr || right == nullptr)
            return;

        if (busLeft[bus] == nullptr || busRight[bus] == nullptr)
            return;

        for (int frame = 0; frame < frames; ++frame)
        {
            busLeft[bus][frame] += left[frame] * gainLeft;
            busRight[bus][frame] += right[frame] * gainRight;
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
        add (destination, trackLeft[index], trackRight[index], route.gain * route.panLeft, route.gain * route.panRight);

        for (int send = 0; send < kMaxSends; ++send)
        {
            if (! (route.sendGain[send] > 0.0f))
                continue;

            const float fader = route.sendPre[send] ? 1.0f : route.gain;
            add (route.sendBus[send], trackLeft[index], trackRight[index],
                 route.sendGain[send] * fader * route.panLeft,
                 route.sendGain[send] * fader * route.panRight);
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

        add (destination, busLeft[source], busRight[source], route.gain * route.panLeft, route.gain * route.panRight);
    }
}

} // namespace waveform
