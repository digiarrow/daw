#include "waveform/stretch.hpp"

#include "waveform/later.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wall"
#    pragma GCC diagnostic ignored "-Wextra"
#    pragma GCC diagnostic ignored "-Wpedantic"
#endif
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"
#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
#if defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

namespace waveform
{
namespace
{

struct Piece
{
    double sourceStart = 0.0;
    double sourceEnd = 0.0;
    int outputFrames = 0;
    int take = 0;
};

std::vector<Piece> planPieces (const std::vector<WarpMarker>& warp,
                               const std::vector<CompSegment>& comp,
                               double sampleRate,
                               double secondsPerQuarter,
                               double offsetSeconds,
                               double lengthSeconds)
{
    std::vector<WarpMarker> markers = warp;
    std::sort (markers.begin(), markers.end(), [] (const WarpMarker& left, const WarpMarker& right) {
        return left.timelineQuarters < right.timelineQuarters;
    });

    if (markers.size() < 2)
    {
        const double quarters = secondsPerQuarter > 0.0 ? lengthSeconds / secondsPerQuarter : lengthSeconds;
        markers = { WarpMarker { offsetSeconds, 0.0 }, WarpMarker { offsetSeconds + lengthSeconds, std::max (0.01, quarters) } };
    }

    std::vector<Piece> pieces;

    for (std::size_t index = 1; index < markers.size(); ++index)
    {
        const double quarterStart = markers[index - 1].timelineQuarters;
        const double quarterEnd = markers[index].timelineQuarters;
        const double sourceStart = markers[index - 1].sourceSeconds;
        const double sourceEnd = markers[index].sourceSeconds;

        if (! (quarterEnd > quarterStart) || ! (secondsPerQuarter > 0.0))
            continue;

        std::vector<double> cuts { quarterStart, quarterEnd };

        for (const auto& segment : comp)
        {
            if (segment.startQuarter > quarterStart && segment.startQuarter < quarterEnd)
                cuts.push_back (segment.startQuarter);

            if (segment.endQuarter > quarterStart && segment.endQuarter < quarterEnd)
                cuts.push_back (segment.endQuarter);
        }

        std::sort (cuts.begin(), cuts.end());
        cuts.erase (std::unique (cuts.begin(), cuts.end(), [] (double left, double right) { return std::abs (left - right) < 1.0e-6; }), cuts.end());

        for (std::size_t cut = 1; cut < cuts.size(); ++cut)
        {
            const double span = quarterEnd - quarterStart;
            const double from = (cuts[cut - 1] - quarterStart) / span;
            const double to = (cuts[cut] - quarterStart) / span;
            Piece piece;
            piece.sourceStart = sourceStart + (sourceEnd - sourceStart) * from;
            piece.sourceEnd = sourceStart + (sourceEnd - sourceStart) * to;
            piece.outputFrames = std::max (1, static_cast<int> (std::llround ((cuts[cut] - cuts[cut - 1]) * secondsPerQuarter * sampleRate)));
            piece.take = compTakeAt (comp, 0.5 * (cuts[cut - 1] + cuts[cut]));
            pieces.push_back (piece);
        }
    }

    return pieces;
}

void appendCopy (const float* left, const float* right, int frames, std::vector<float>& outLeft, std::vector<float>& outRight)
{
    outLeft.insert (outLeft.end(), left, left + frames);
    outRight.insert (outRight.end(), right, right + frames);
}

void appendStretch (const float* inLeft, const float* inRight, int inputFrames, int outputFrames, double, std::vector<float>& outLeft, std::vector<float>& outRight)
{
    if (inputFrames <= 0 || outputFrames <= 0)
        return;

    if (std::abs (inputFrames - outputFrames) <= 1)
    {
        const int count = std::min (inputFrames, outputFrames);
        appendCopy (inLeft, inRight, count, outLeft, outRight);

        if (outputFrames > count)
        {
            outLeft.insert (outLeft.end(), static_cast<std::size_t> (outputFrames - count), inLeft[inputFrames - 1]);
            outRight.insert (outRight.end(), static_cast<std::size_t> (outputFrames - count), inRight[inputFrames - 1]);
        }

        return;
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch (42);
    const int block = std::clamp (std::min (inputFrames, outputFrames) / 3, 128, 2048);
    const int interval = std::max (32, block / 4);
    stretch.configure (2, block, interval, false);
    const float* inputs[] = { inLeft, inRight };
    std::vector<float> stretchedLeft (static_cast<std::size_t> (outputFrames));
    std::vector<float> stretchedRight (static_cast<std::size_t> (outputFrames));
    float* outputs[] = { stretchedLeft.data(), stretchedRight.data() };
    stretch.process (inputs, inputFrames, outputs, outputFrames);
    appendCopy (stretchedLeft.data(), stretchedRight.data(), outputFrames, outLeft, outRight);
}

const StretchSource* sourceForTake (int take, const StretchSource& primary, const StretchSource* extra, int extraCount)
{
    if (take <= 0)
        return &primary;

    const int index = take - 1;
    return index < extraCount ? extra + index : nullptr;
}

} // namespace

void renderStretch (const StretchSource& primary,
                    const StretchSource* extra,
                    int extraCount,
                    const std::vector<WarpMarker>& warp,
                    const std::vector<CompSegment>& comp,
                    double sampleRate,
                    double secondsPerQuarter,
                    double offsetSeconds,
                    double lengthSeconds,
                    std::vector<float>& left,
                    std::vector<float>& right)
{
    left.clear();
    right.clear();

    if (! (sampleRate > 0.0) || ! (lengthSeconds > 0.0) || primary.left == nullptr || primary.frames <= 0)
        return;

    const auto pieces = planPieces (warp, comp, sampleRate, secondsPerQuarter, offsetSeconds, lengthSeconds);

    for (const auto& piece : pieces)
    {
        const auto* source = sourceForTake (piece.take, primary, extra, extraCount);
        const int start = static_cast<int> (std::llround (std::min (piece.sourceStart, piece.sourceEnd) * sampleRate));
        const int end = static_cast<int> (std::llround (std::max (piece.sourceStart, piece.sourceEnd) * sampleRate));

        if (source == nullptr || source->left == nullptr || source->frames <= 0 || end <= start)
        {
            left.insert (left.end(), static_cast<std::size_t> (piece.outputFrames), 0.0f);
            right.insert (right.end(), static_cast<std::size_t> (piece.outputFrames), 0.0f);
            continue;
        }

        const int first = std::clamp (start, 0, static_cast<int> (source->frames));
        const int last = std::clamp (end, 0, static_cast<int> (source->frames));
        const int count = std::max (0, last - first);
        const float* inRight = source->right != nullptr ? source->right : source->left;

        if (count < 2)
        {
            left.insert (left.end(), static_cast<std::size_t> (piece.outputFrames), 0.0f);
            right.insert (right.end(), static_cast<std::size_t> (piece.outputFrames), 0.0f);
            continue;
        }

        std::vector<float> sliceRight (inRight + first, inRight + first + count);
        appendStretch (source->left + first, sliceRight.data(), count, piece.outputFrames, sampleRate, left, right);
    }
}

} // namespace waveform
