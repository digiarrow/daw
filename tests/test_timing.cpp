#include "waveform/timing.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using waveform::TimeSignature;
using waveform::Timing;

TEST_CASE ("quarter-note timing is exact at 48 kHz and 120 BPM", "[timing]")
{
    REQUIRE (Timing::samplesPerQuarterNote (48000.0, 120.0) == Catch::Approx (24000.0));
    REQUIRE (Timing::samplesFromQuarterNotes (1.0, 48000.0, 120.0) == 24000);
    REQUIRE (Timing::samplesFromQuarterNotes (4.0, 48000.0, 120.0) == 96000);
    REQUIRE (Timing::quarterNotesFromSamples (24000, 48000.0, 120.0) == Catch::Approx (1.0));
    REQUIRE (Timing::quarterNotesFromSamples (192000, 48000.0, 120.0) == Catch::Approx (8.0));
}

TEST_CASE ("44100 Hz and 120 BPM lands on whole samples", "[timing]")
{
    REQUIRE (Timing::samplesFromQuarterNotes (1.0, 44100.0, 120.0) == 22050);
    REQUIRE (Timing::quarterNotesFromSamples (22050, 44100.0, 120.0) == Catch::Approx (1.0));
}

TEST_CASE ("bars follow the time signature, tempo stays in quarter notes", "[timing]")
{
    const TimeSignature fourFour { 4, 4 };
    REQUIRE (Timing::quarterNotesPerBeat (fourFour) == Catch::Approx (1.0));
    REQUIRE (Timing::quarterNotesPerBar (fourFour) == Catch::Approx (4.0));
    REQUIRE (Timing::ticksPerBeat (fourFour) == 960);

    const auto start = Timing::positionFromSamples (0, 48000.0, 120.0, fourFour);
    REQUIRE (start.bar == 1);
    REQUIRE (start.beat == 1);
    REQUIRE (start.tick == 0);
    REQUIRE (waveform::formatMusical (start) == "001.01.000");

    const auto beatTwo = Timing::positionFromSamples (24000, 48000.0, 120.0, fourFour);
    REQUIRE (beatTwo.bar == 1);
    REQUIRE (beatTwo.beat == 2);
    REQUIRE (beatTwo.tick == 0);
    REQUIRE (waveform::formatMusical (beatTwo) == "001.02.000");

    const auto halfBeat = Timing::positionFromSamples (12000, 48000.0, 120.0, fourFour);
    REQUIRE (halfBeat.beat == 1);
    REQUIRE (halfBeat.tick == 480);

    const auto barTwo = Timing::positionFromSamples (96000, 48000.0, 120.0, fourFour);
    REQUIRE (barTwo.bar == 2);
    REQUIRE (barTwo.beat == 1);
    REQUIRE (barTwo.tick == 0);
    REQUIRE (waveform::formatMusical (barTwo) == "002.01.000");

    const TimeSignature sixEight { 6, 8 };
    REQUIRE (Timing::quarterNotesPerBar (sixEight) == Catch::Approx (3.0));
    REQUIRE (Timing::quarterNotesPerBeat (sixEight) == Catch::Approx (0.5));
    REQUIRE (Timing::ticksPerBeat (sixEight) == 480);

    const auto eighth = Timing::positionFromSamples (12000, 48000.0, 120.0, sixEight);
    REQUIRE (eighth.bar == 1);
    REQUIRE (eighth.beat == 2);
    REQUIRE (eighth.tick == 0);

    const auto nextBar = Timing::positionFromSamples (72000, 48000.0, 120.0, sixEight);
    REQUIRE (nextBar.bar == 2);
    REQUIRE (nextBar.beat == 1);
}

TEST_CASE ("time signatures and tempo are validated", "[timing]")
{
    REQUIRE (TimeSignature { 4, 4 }.isValid());
    REQUIRE (TimeSignature { 6, 8 }.isValid());
    REQUIRE_FALSE (TimeSignature { 4, 3 }.isValid());
    REQUIRE_FALSE (TimeSignature { 0, 4 }.isValid());
    REQUIRE (Timing::clampBpm (10.0) == Catch::Approx (20.0));
    REQUIRE (Timing::clampBpm (1200.0) == Catch::Approx (999.0));
    REQUIRE (Timing::isValidSampleRate (48000.0));
    REQUIRE_FALSE (Timing::isValidSampleRate (1000.0));
}

TEST_CASE ("seconds format", "[timing]")
{
    REQUIRE (waveform::formatSeconds (0, 48000.0) == "0:00.000");
    REQUIRE (waveform::formatSeconds (48000, 48000.0) == "0:01.000");
    REQUIRE (waveform::formatSeconds (48000 * 65, 48000.0) == "1:05.000");
}
