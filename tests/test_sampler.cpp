#include "waveform/automation.hpp"
#include "waveform/sampler.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

TEST_CASE ("automation interpolates and holds the ends", "[automation]")
{
    waveform::AutomationLane lane;
    lane.parameter = "mixer.gainDb";
    lane.points.push_back ({ 0.0, 0.0 });
    lane.points.push_back ({ 4.0, -12.0 });
    REQUIRE (waveform::automationAt (lane, -1.0, 3.0) == Catch::Approx (0.0));
    REQUIRE (waveform::automationAt (lane, 2.0, 3.0) == Catch::Approx (-6.0));
    REQUIRE (waveform::automationAt (lane, 8.0, 3.0) == Catch::Approx (-12.0));
}

TEST_CASE ("launch quantize lands on the next beat or bar", "[automation]")
{
    waveform::TimeSignature four { 4, 4 };
    REQUIRE (waveform::nextLaunchQuarter (1.5, waveform::LaunchQuantize::Beat, four) == Catch::Approx (2.0));
    REQUIRE (waveform::nextLaunchQuarter (1.5, waveform::LaunchQuantize::Bar, four) == Catch::Approx (4.0));
    REQUIRE (waveform::nextLaunchQuarter (4.0, waveform::LaunchQuantize::Bar, four) == Catch::Approx (4.0));
    REQUIRE (waveform::nextLaunchQuarter (1.5, waveform::LaunchQuantize::None, four) == Catch::Approx (1.5));
}

TEST_CASE ("sampler plays the root sample and stays quiet for an unmapped note", "[sampler]")
{
    std::vector<float> data (480, 0.25f);
    waveform::SamplerZoneView zone;
    zone.rootNote = 60;
    zone.lowNote = 60;
    zone.highNote = 60;
    zone.left = data.data();
    zone.right = data.data();
    zone.frames = static_cast<std::int64_t> (data.size());
    zone.sourceSampleRate = 48000.0;

    waveform::SoundingNote note;
    note.note = 60;
    note.velocity = 127;
    note.onsetSeconds = 0.0;
    note.releaseSeconds = 1.0;
    waveform::SynthPatch patch;
    patch.attackSeconds = 0.0;
    patch.decaySeconds = 0.0;
    patch.sustain = 1.0;
    patch.releaseSeconds = 0.01;

    std::vector<float> left (64, 0.0f), right (64, 0.0f);
    waveform::renderSamplerNotes (&note, 1, &zone, 1, 64, 48000.0, patch, 1.0f, 1.0f, left.data(), right.data());
    REQUIRE (left[32] == Catch::Approx (0.25f).margin (0.02));

    note.note = 72;
    std::fill (left.begin(), left.end(), 0.0f);
    waveform::renderSamplerNotes (&note, 1, &zone, 1, 64, 48000.0, patch, 1.0f, 1.0f, left.data(), right.data());
    REQUIRE (left[32] == Catch::Approx (0.0f).margin (1.0e-4));
}
