#include "waveform/stretch.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace
{

int dominantLag (const std::vector<float>& samples, int minLag, int maxLag)
{
    int best = minLag;
    double bestSum = -1.0e30;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;

        for (int index = 0; index + lag < static_cast<int> (samples.size()); index += 4)
            sum += static_cast<double> (samples[static_cast<std::size_t> (index)]) * samples[static_cast<std::size_t> (index + lag)];

        if (sum > bestSum)
        {
            bestSum = sum;
            best = lag;
        }
    }

    return best;
}

} // namespace

TEST_CASE ("warp stretches duration and keeps the pitch", "[stretch]")
{
    constexpr double kRate = 48000.0;
    constexpr int kFrames = 24000;
    std::vector<float> sine (static_cast<std::size_t> (kFrames));

    for (int index = 0; index < kFrames; ++index)
        sine[static_cast<std::size_t> (index)] = std::sin (2.0 * 3.141592653589793 * 440.0 * index / kRate);

    waveform::StretchSource source;
    source.left = sine.data();
    source.right = sine.data();
    source.frames = kFrames;
    std::vector<waveform::WarpMarker> markers { { 0.0, 0.0 }, { 0.5, 4.0 } };
    std::vector<float> left;
    std::vector<float> right;
    waveform::renderStretch (source, nullptr, 0, markers, {}, kRate, 0.5, 0.0, 0.5, left, right);
    REQUIRE (left.size() > static_cast<std::size_t> (kFrames));
    const int lag = dominantLag (std::vector<float> (left.begin() + 4000, left.begin() + 16000), 80, 160);
    REQUIRE (std::abs (lag - 109) <= 4);
}

TEST_CASE ("a comp segment plays the second take", "[stretch]")
{
    std::vector<float> first (4800, 0.1f);
    std::vector<float> second (4800, 0.8f);
    waveform::StretchSource primary { first.data(), first.data(), static_cast<std::int64_t> (first.size()) };
    waveform::StretchSource extra { second.data(), second.data(), static_cast<std::int64_t> (second.size()) };
    std::vector<waveform::CompSegment> comp { { 0.0, 0.05, 1 } };
    std::vector<float> left;
    std::vector<float> right;
    waveform::renderStretch (primary, &extra, 1, {}, comp, 48000.0, 0.5, 0.0, 0.1, left, right);
    REQUIRE (! left.empty());
    REQUIRE (left[100] == Catch::Approx (0.8f).margin (0.05f));
    REQUIRE (left.back() == Catch::Approx (0.1f).margin (0.05f));
}
