#include "waveform/mix.hpp"

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

    for (std::int64_t frame = from; frame < to; ++frame)
    {
        const auto sourceFrame = clip.sourceOffsetFrame + (frame - clip.timelineStartFrame);

        if (sourceFrame < 0 || sourceFrame >= clip.source.frames)
            continue;

        const auto index = static_cast<int> (frame - timelineStart);
        const auto sourceIndex = static_cast<std::size_t> (sourceFrame);
        outLeft[index] += sourceLeft[sourceIndex] * gain * leftGain;
        outRight[index] += sourceRight[sourceIndex] * gain * rightGain;
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
