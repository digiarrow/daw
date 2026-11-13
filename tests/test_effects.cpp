#include "waveform/effects.hpp"
#include "waveform/routing.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE ("utility gain and width", "[effects]")
{
    auto slot = waveform::defaultEffect (waveform::EffectKind::Utility);
    slot.p0 = 6.0;
    slot.p1 = 1.0;
    waveform::EffectState state;
    float left[4] { 0.1f, 0.1f, 0.1f, 0.1f };
    float right[4] { 0.1f, 0.1f, 0.1f, 0.1f };
    waveform::processEffect (slot, state, left, right, 4, 48000.0);
    REQUIRE (left[0] == Catch::Approx (0.1 * std::pow (10.0, 6.0 / 20.0)).margin (1.0e-4));
}

TEST_CASE ("delay repeats an impulse one buffer later", "[effects]")
{
    auto slot = waveform::defaultEffect (waveform::EffectKind::Delay);
    slot.p0 = 100.0 / 48000.0;
    slot.p1 = 0.0;
    slot.p2 = 1.0;
    waveform::EffectState state;
    waveform::resetEffect (state);
    std::vector<float> left (200, 0.0f), right (200, 0.0f);
    left[0] = right[0] = 1.0f;
    waveform::processEffect (slot, state, left.data(), right.data(), 200, 48000.0);
    REQUIRE (std::abs (left[0]) < 1.0e-4f);
    REQUIRE (left[100] == Catch::Approx (1.0f).margin (1.0e-3));
}

TEST_CASE ("a group bus reaches the master and a direct track does too", "[routing]")
{
    constexpr int kFrames = 8;
    float track0L[kFrames], track0R[kFrames], track1L[kFrames], track1R[kFrames];
    std::fill (track0L, track0L + kFrames, 0.5f);
    std::fill (track0R, track0R + kFrames, 0.5f);
    std::fill (track1L, track1L + kFrames, 0.0f);
    std::fill (track1R, track1R + kFrames, 0.0f);
    const float* lefts[] = { track0L, track1L };
    const float* rights[] = { track0R, track1R };
    float masterL[kFrames], masterR[kFrames], groupL[kFrames], groupR[kFrames];
    float* busL[] = { masterL, groupL };
    float* busR[] = { masterR, groupR };

    waveform::RouteEndpoint routes[2] {};
    routes[0].outputBus = 1;
    routes[0].gain = 1.0f;
    routes[0].panLeft = 1.0f;
    routes[0].panRight = 1.0f;
    routes[1].group = true;
    routes[1].outputBus = 1;
    routes[1].gain = 1.0f;
    routes[1].panLeft = 1.0f;
    routes[1].panRight = 1.0f;

    waveform::routeTracks (lefts, rights, routes, 2, busL, busR, 2, kFrames);
    REQUIRE (masterL[0] == Catch::Approx (0.5f).margin (1.0e-4));
    REQUIRE (groupL[0] == Catch::Approx (0.5f).margin (1.0e-4));
}

TEST_CASE ("an external delay line can hold a one second repeat", "[effects]")
{
    constexpr int kRate = 48000;
    constexpr int kDelay = kRate;
    constexpr int kCapacity = kDelay + 16;
    std::vector<float> memory (static_cast<std::size_t> (kCapacity * 2), 0.0f);
    waveform::EffectState state;
    state.externalLeft = memory.data();
    state.externalRight = memory.data() + kCapacity;
    state.externalCapacity = kCapacity;
    auto slot = waveform::defaultEffect (waveform::EffectKind::Delay);
    slot.p0 = 1.0;
    slot.p1 = 0.0;
    slot.p2 = 1.0;
    std::vector<float> left (kDelay + 8, 0.0f), right (kDelay + 8, 0.0f);
    left[0] = right[0] = 1.0f;
    waveform::processEffect (slot, state, left.data(), right.data(), kDelay + 8, kRate);
    REQUIRE (std::abs (left[0]) < 1.0e-4f);
    REQUIRE (left[kDelay] == Catch::Approx (1.0f).margin (1.0e-3));
}

TEST_CASE ("compressor turns down a hot signal", "[effects]")
{
    auto slot = waveform::defaultEffect (waveform::EffectKind::Compressor);
    slot.p0 = -12.0;
    slot.p1 = 20.0;
    slot.p2 = 0.001;
    slot.p3 = 0.05;
    slot.p4 = 0.0;
    waveform::EffectState state;
    state.env = 1.0f;
    std::vector<float> left (4000, 0.8f), right (4000, 0.8f);
    waveform::processEffect (slot, state, left.data(), right.data(), 4000, 48000.0);
    REQUIRE (std::abs (left.back()) < 0.5f);
}
