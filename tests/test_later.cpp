#include "waveform/later.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE ("warp markers interpolate source time", "[later]")
{
    std::vector<waveform::WarpMarker> markers { { 0.0, 0.0 }, { 2.0, 4.0 } };
    REQUIRE (waveform::warpedSourceSeconds (2.0, 0.0, 0.5, markers) == Catch::Approx (1.0));
    REQUIRE (waveform::warpedSourceSeconds (1.0, 0.0, 0.5, {}) == Catch::Approx (0.5));
}

TEST_CASE ("comping picks the take that owns the quarter", "[later]")
{
    std::vector<waveform::CompSegment> segments { { 0.0, 2.0, 0 }, { 2.0, 4.0, 1 } };
    REQUIRE (waveform::compTakeAt (segments, 1.0) == 0);
    REQUIRE (waveform::compTakeAt (segments, 3.0) == 1);
}

TEST_CASE ("keys, legato, and MPE pitch ratio", "[later]")
{
    waveform::MusicalKey cMajor { 0, false };
    REQUIRE (waveform::pitchInKey (64, cMajor));
    REQUIRE_FALSE (waveform::pitchInKey (61, cMajor));
    const int constrained = waveform::constrainToKey (61, cMajor);
    REQUIRE ((constrained == 60 || constrained == 62));

    std::vector<waveform::MidiNote> notes { { 60, 0.0, 0.25, 100, 1 }, { 64, 1.0, 0.25, 100, 1 } };
    notes = waveform::makeLegato (notes);
    REQUIRE (notes[0].lengthQuarterNotes == Catch::Approx (1.0));
    REQUIRE (waveform::mpeFrequencyRatio (12.0f) == Catch::Approx (2.0));
}

TEST_CASE ("script, control change, FM, and audio-to-MIDI", "[later]")
{
    waveform::Project project;
    const auto script = waveform::runScript (project, "tempo 100\nadd audio Bass\n");
    REQUIRE (script.ok);
    REQUIRE (project.tempoBpm == Catch::Approx (100.0));
    REQUIRE (project.tracks.size() == 1);

    std::vector<waveform::ControlMapping> map { { 7, "mixer.gainDb" } };
    REQUIRE (waveform::applyControlChange (project, project.tracks[0].id, 7, 127, map));
    REQUIRE (project.tracks[0].mixer.gainDb == Catch::Approx (0.0));

    REQUIRE (std::abs (waveform::fmSample (440.0, 220.0, 1.0, 0.0)) < 1.0e-4f);
    REQUIRE (std::abs (waveform::drumSample (0.0, 180.0f, 0.2f, 0.2f)) > 0.1f);

    std::vector<float> sine (4096);
    for (int index = 0; index < 4096; ++index)
        sine[static_cast<std::size_t> (index)] = std::sin (2.0 * 3.141592653589793 * 440.0 * index / 48000.0);

    const auto detected = waveform::audioToMidi (sine.data(), 4096, 48000.0);
    REQUIRE (detected.size() == 1);
    REQUIRE (detected[0].note == 69);

    float history[4] {};
    float input[8] { 1, 0, 0, 0, 0, 0, 0, 0 };
    float output[8] {};
    int cursor = 0;
    waveform::compensateDelay (input, output, 4, history, 3, cursor);
    waveform::compensateDelay (input + 4, output + 4, 4, history, 3, cursor);
    REQUIRE (output[0] == Catch::Approx (0.0f));
    REQUIRE (output[3] == Catch::Approx (1.0f));
}
