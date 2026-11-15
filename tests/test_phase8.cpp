#include "waveform/effects.hpp"
#include "waveform/phase8.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <vector>

TEST_CASE ("independent tracks run in waves on worker threads", "[parallel]")
{
    const int depends[] = { -1, 0, -1, 1 };
    int waves[4] {};
    REQUIRE (waveform::assignTrackWaves (depends, 4, waves) == 3);
    REQUIRE (waves[0] == 0);
    REQUIRE (waves[2] == 0);
    REQUIRE (waves[1] == 1);
    REQUIRE (waves[3] == 2);

    waveform::WorkerPool pool (2);
    std::atomic<int> seen { 0 };
    int values[8] {};
    struct Job
    {
        int* values;
        std::atomic<int>* seen;
    } job { values, &seen };

    pool.parallel (8, [] (void* raw, int index)
    {
        auto* job = static_cast<Job*> (raw);
        job->values[index] = index + 1;
        job->seen->fetch_add (1);
    }, &job);

    REQUIRE (seen.load() == 8);
    REQUIRE (values[7] == 8);
}

TEST_CASE ("time signatures, crossfades, freeze, overdub, and punch", "[arrangement]")
{
    waveform::SignaturePoint points[] = {
        { 0.0, { 4, 4 } },
        { 8.0, { 3, 4 } }
    };
    const auto later = waveform::signatureAt (points, 2, 8.0, { 4, 4 });
    REQUIRE (later.numerator == 3);
    REQUIRE (waveform::signatureAt (points, 2, 2.0, { 4, 4 }).numerator == 4);

    waveform::AudioClip first;
    first.startQuarterNotes = 0.0;
    first.lengthSeconds = 2.0;
    waveform::AudioClip second;
    second.startQuarterNotes = 4.0;
    second.lengthSeconds = 2.0;
    waveform::crossfadeAdjacent (first, second, 120.0);
    REQUIRE (first.fadeOutSeconds > 0.0);
    REQUIRE (second.fadeInSeconds == Catch::Approx (first.fadeOutSeconds));

    const float source[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float frozen[4] {};
    waveform::freezeClipSamples (source, 4, 0.5f, 0, 0, frozen);
    REQUIRE (frozen[1] == Catch::Approx (0.5f));

    waveform::MidiClip clip;
    clip.lengthQuarterNotes = 4.0;
    waveform::MidiNote extra { 62, 1.0, 1.0, 90, 1, 0.0f };
    waveform::overdubNotes (clip, &extra, 1);
    REQUIRE (clip.notes.size() == 1);

    std::vector<waveform::MidiNote> take { extra, { 64, 6.0, 1.0, 80, 1, 0.0f } };
    waveform::applyPunch (take, { true, 0.0, 4.0 });
    REQUIRE (take.size() == 1);
    REQUIRE (take[0].note == 62);
}

TEST_CASE ("crossfader, resampling, and hardware pairs", "[mixer]")
{
    REQUIRE (waveform::crossfaderLevel (waveform::CrossfaderSide::A, 0.0f) == Catch::Approx (1.0f));
    REQUIRE (waveform::crossfaderLevel (waveform::CrossfaderSide::B, 0.0f) == Catch::Approx (0.0f).margin (1.0e-5));
    REQUIRE (waveform::crossfaderLevel (waveform::CrossfaderSide::A, 1.0f) == Catch::Approx (0.0f).margin (1.0e-5));
    REQUIRE (waveform::crossfaderLevel (waveform::CrossfaderSide::None, 0.7f) == Catch::Approx (1.0f));

    const float input[] = { 0.0f, 1.0f };
    float output[8] {};
    const int frames = waveform::resampleLinear (input, 2, 2.0, output, 8, 4.0);
    REQUIRE (frames == 4);
    REQUIRE (output[0] == Catch::Approx (0.0f).margin (1.0e-4));
    REQUIRE (output[2] > output[0]);

    REQUIRE (waveform::hardwareChannel (1, 0, 4) == 2);
    REQUIRE (waveform::hardwareChannel (1, 1, 2) == 1);
}

TEST_CASE ("new devices, the wavetable, and the spectrum", "[devices]")
{
    REQUIRE (std::string (waveform::effectName (waveform::EffectKind::Glue)) == "glue");
    REQUIRE (waveform::effectFromName ("redux") == waveform::EffectKind::Redux);

    waveform::EffectSlot redux = waveform::defaultEffect (waveform::EffectKind::Redux);
    redux.p0 = 1.0;
    redux.p1 = 1.0;
    waveform::EffectState state;
    float left[] = { 0.2f, 0.8f };
    float right[] = { 0.2f, 0.8f };
    waveform::processEffect (redux, state, left, right, 2, 48000.0);
    REQUIRE (left[0] == Catch::Approx (0.0f).margin (1.0e-4));
    REQUIRE (left[1] == Catch::Approx (1.0f).margin (1.0e-4));

    waveform::EffectSlot gate = waveform::defaultEffect (waveform::EffectKind::Gate);
    gate.p0 = -6.0;
    gate.p1 = 0.001;
    gate.p2 = 0.01;
    waveform::EffectState gateState;
    gateState.env = 1.0f;
    float hot[] = { 0.001f, 0.001f, 0.001f, 0.001f };
    float hotRight[] = { 0.001f, 0.001f, 0.001f, 0.001f };
    waveform::processEffect (gate, gateState, hot, hotRight, 4, 48000.0);
    REQUIRE (std::abs (hot[3]) < 0.001f);

    float table[32];
    waveform::fillWavetable (table, 32, 0.0f);
    REQUIRE (waveform::wavetableSample (table, 32, 0.0) == Catch::Approx (0.0f).margin (1.0e-4));
    REQUIRE (std::abs (waveform::wavetableSample (table, 32, 0.25)) > 0.5f);

    const float noise[] = { 1.0f, 0.0f, -1.0f, 0.0f };
    float bands[4] {};
    waveform::spectrumBands (noise, 4, bands, 4);
    REQUIRE (bands[0] > 0.0f);
}

TEST_CASE ("typing keys, MIDI clock, and browser search", "[midi]")
{
    REQUIRE (waveform::noteFromTypingKey ('a', 4) == 60);
    REQUIRE (waveform::noteFromTypingKey ('w', 4) == 61);
    REQUIRE (waveform::noteFromTypingKey ('q', 4) == -1);

    double phase = 0.0;
    const int clocks = waveform::midiClocksInBlock (120.0, 48000.0, 48000, phase);
    REQUIRE (clocks == 48);
    REQUIRE (waveform::bpmFromMidiClocks (48, 1.0) == Catch::Approx (120.0));

    REQUIRE (waveform::mediaMatches ({ "kick.wav", "drums one-shot" }, "drums"));
    REQUIRE (waveform::mediaMatches ({ "kick.wav", "drums" }, "kick"));
    REQUIRE_FALSE (waveform::mediaMatches ({ "kick.wav", "drums" }, "vocal"));
    REQUIRE (waveform::mediaMatches ({ "kick.wav", nullptr }, ""));
}
