#include "waveform/mix.hpp"

#include "waveform/automation.hpp"

#include <cmath>

namespace waveform
{
namespace
{

void addClip (const AudioClipView& clip,
              std::int64_t timelineStart,
              int frames,
              float leftGain,
              float rightGain,
              float* outLeft,
              float* outRight) noexcept
{
    if (clip.source.left == nullptr || clip.timelineLengthFrames <= 0 || clip.gain == 0.0f || frames <= 0)
        return;

    const auto blockEnd = timelineStart + frames;
    const auto clipEnd = clip.timelineStartFrame + clip.timelineLengthFrames;
    const auto from = std::max (timelineStart, clip.timelineStartFrame);
    const auto to = std::min (blockEnd, clipEnd);

    if (to <= from)
        return;

    const float* sourceLeft = clip.source.left;
    const float* sourceRight = clip.source.right != nullptr ? clip.source.right : sourceLeft;
    const float gain = clip.gain;
    constexpr float kHalfPi = 1.5707963267948966f;

    for (std::int64_t frame = from; frame < to; ++frame)
    {
        const auto sourceFrame = clip.sourceOffsetFrame + (frame - clip.timelineStartFrame);

        if (sourceFrame < 0 || sourceFrame >= clip.source.frames)
            continue;

        const auto position = frame - clip.timelineStartFrame;
        const auto remaining = clip.timelineLengthFrames - position;
        float envelope = 1.0f;

        if (clip.fadeInFrames > 0 && position < clip.fadeInFrames)
            envelope *= std::sin (static_cast<float> (position) / static_cast<float> (clip.fadeInFrames) * kHalfPi);

        if (clip.fadeOutFrames > 0 && remaining < clip.fadeOutFrames)
        {
            const float progress = static_cast<float> (clip.fadeOutFrames - remaining) / static_cast<float> (clip.fadeOutFrames);
            envelope *= std::cos (progress * kHalfPi);
        }

        if (clip.envelopeCount >= 2 && clip.timelineLengthFrames > 0)
        {
            const double local = static_cast<double> (position) / static_cast<double> (clip.timelineLengthFrames);
            AutomationPoint points[8];
            const int count = std::min (clip.envelopeCount, 8);

            for (int point = 0; point < count; ++point)
                points[point] = AutomationPoint { clip.envelopePosition[point], clip.envelopeValue[point] };

            envelope *= static_cast<float> (clipEnvelopeAt (points, count, local, 1.0));
        }

        const auto index = static_cast<int> (frame - timelineStart);
        const auto sourceIndex = static_cast<std::size_t> (sourceFrame);
        outLeft[index] += sourceLeft[sourceIndex] * gain * leftGain * envelope;
        outRight[index] += sourceRight[sourceIndex] * gain * rightGain * envelope;
    }
}

} // namespace

void mixBlock (const MixView& mix,
               std::int64_t timelineStart,
               int frames,
               float* outLeft,
               float* outRight) noexcept
{
    if (outLeft == nullptr || outRight == nullptr || frames <= 0)
        return;

    std::fill (outLeft, outLeft + frames, 0.0f);
    std::fill (outRight, outRight + frames, 0.0f);

    bool anySolo = false;

    for (int trackIndex = 0; trackIndex < mix.trackCount; ++trackIndex)
        if (mix.tracks[trackIndex].solo)
            anySolo = true;

    for (int trackIndex = 0; trackIndex < mix.trackCount; ++trackIndex)
    {
        const TrackMix& track = mix.tracks[trackIndex];

        if (track.mute || (anySolo && ! track.solo) || track.clips == nullptr)
            continue;

        const PanGains pan = constantPowerPan (track.pan);
        const float leftGain = track.gain * pan.left * mix.masterGain;
        const float rightGain = track.gain * pan.right * mix.masterGain;

        for (int clipIndex = 0; clipIndex < track.clipCount; ++clipIndex)
            addClip (track.clips[clipIndex], timelineStart, frames, leftGain, rightGain, outLeft, outRight);
    }
}

} // namespace waveform
