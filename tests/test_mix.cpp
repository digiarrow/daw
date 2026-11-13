#include "waveform/mix.hpp"
#include "waveform/synthesis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using waveform::AudioClipView;
using waveform::MixView;
using waveform::Oscillator;
using waveform::TimeSignature;
using waveform::TrackMix;

namespace
{

constexpr float kCentre = 0.70710678118f;

AudioClipView makeClip (const std::vector<float>& data, std::int64_t start, std::int64_t offset, std::int64_t length)
{
    AudioClipView clip;
    clip.source.left = data.data();
    clip.source.right = data.data();
    clip.source.frames = static_cast<std::int64_t> (data.size());
    clip.timelineStartFrame = start;
    clip.sourceOffsetFrame = offset;
    clip.timelineLengthFrames = length;
    clip.gain = 1.0f;
    return clip;
}

} // namespace

TEST_CASE ("constant-power pan", "[mix]")
{
    const auto centre = waveform::constantPowerPan (0.0f);
    REQUIRE (centre.left == Catch::Approx (kCentre).margin (1.0e-6));
    REQUIRE (centre.right == Catch::Approx (kCentre).margin (1.0e-6));

    const auto left = waveform::constantPowerPan (-1.0f);
    REQUIRE (left.left == Catch::Approx (1.0f).margin (1.0e-6));
    REQUIRE (left.right == Catch::Approx (0.0f).margin (1.0e-6));

    const auto right = waveform::constantPowerPan (1.0f);
    REQUIRE (right.left == Catch::Approx (0.0f).margin (1.0e-6));
    REQUIRE (right.right == Catch::Approx (1.0f).margin (1.0e-6));
    REQUIRE (waveform::dbToLinear (-6.0) == Catch::Approx (0.501187).margin (1.0e-4));
    REQUIRE (waveform::dbToLinear (-120.0) == 0.0f);
}

TEST_CASE ("a clip is summed in timeline order", "[mix]")
{
    const std::vector<float> data { 0.0f, 0.5f, 1.0f, -1.0f };
    const auto clip = makeClip (data, 0, 0, 4);
    TrackMix track;
    track.clips = &clip;
    track.clipCount = 1;
    MixView mix;
    mix.tracks = &track;
    mix.trackCount = 1;

    std::vector<float> left (4), right (4);
    waveform::mixBlock (mix, 0, 4, left.data(), right.data());

    for (int index = 0; index < 4; ++index)
    {
        REQUIRE (left[static_cast<std::size_t> (index)] == Catch::Approx (data[static_cast<std::size_t> (index)] * kCentre).margin (1.0e-5));
        REQUIRE (right[static_cast<std::size_t> (index)] == Catch::Approx (left[static_cast<std::size_t> (index)]));
    }
}

TEST_CASE ("offset, start, mute, solo, and master gain", "[mix]")
{
    const std::vector<float> data { 0.25f, 0.5f, 0.75f, 1.0f };
    auto clip = makeClip (data, 2, 1, 2);
    TrackMix audible;
    audible.clips = &clip;
    audible.clipCount = 1;
    audible.pan = -1.0f;

    TrackMix muted = audible;
    muted.mute = true;

    MixView mix;
    mix.tracks = &audible;
    mix.trackCount = 1;
    std::vector<float> left (6), right (6);
    waveform::mixBlock (mix, 0, 6, left.data(), right.data());
    REQUIRE (left[0] == 0.0f);
    REQUIRE (left[1] == 0.0f);
    REQUIRE (left[2] == Catch::Approx (0.5f).margin (1.0e-6));
    REQUIRE (left[3] == Catch::Approx (0.75f).margin (1.0e-6));
    REQUIRE (left[4] == 0.0f);
    REQUIRE (right[2] == 0.0f);

    mix.tracks = &muted;
    waveform::mixBlock (mix, 0, 6, left.data(), right.data());
    REQUIRE (left[2] == 0.0f);

    TrackMix quiet = audible;
    quiet.mute = false;
    TrackMix featured = audible;
    featured.solo = true;
    featured.pan = 1.0f;
    TrackMix tracks[] = { quiet, featured };
    mix.tracks = tracks;
    mix.trackCount = 2;
    mix.masterGain = 0.5f;
    waveform::mixBlock (mix, 2, 1, left.data(), right.data());
    REQUIRE (left[0] == Catch::Approx (0.0f).margin (1.0e-6));
    REQUIRE (right[0] == Catch::Approx (0.25f).margin (1.0e-5));
}

TEST_CASE ("metronome clicks on beats and stays silent between them", "[mix]")
{
    std::vector<float> left (48000), right (48000);
    waveform::renderMetronome (left.data(), right.data(), 0, 48000, 48000.0, 120.0, TimeSignature { 4, 4 }, 1.0f);

    REQUIRE (std::abs (left[20]) > 0.01f);
    REQUIRE (left[5000] == 0.0f);
    REQUIRE (std::abs (left[24020]) > 0.01f);
    REQUIRE (left[30000] == 0.0f);
}

TEST_CASE ("test tone phase continues across blocks", "[mix]")
{
    constexpr int kFrames = 200;
    std::vector<float> wholeL (kFrames), wholeR (kFrames);
    std::vector<float> partL (kFrames), partR (kFrames);
    Oscillator whole;
    Oscillator part;
    waveform::renderTestTone (wholeL.data(), wholeR.data(), kFrames, 48000.0, 440.0, 0.2f, whole);
    waveform::renderTestTone (partL.data(), partR.data(), 80, 48000.0, 440.0, 0.2f, part);
    waveform::renderTestTone (partL.data() + 80, partR.data() + 80, kFrames - 80, 48000.0, 440.0, 0.2f, part);

    REQUIRE (std::abs (wholeL[10]) > 0.01f);
    REQUIRE (std::abs (wholeL[10]) <= 0.2f + 1.0e-5f);

    for (int index = 0; index < kFrames; ++index)
        REQUIRE (partL[static_cast<std::size_t> (index)] == Catch::Approx (wholeL[static_cast<std::size_t> (index)]).margin (1.0e-6));
}
