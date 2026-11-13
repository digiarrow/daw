#include "waveform/session.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("a session clip loops inside one block", "[session]")
{
    waveform::SessionAudioPiece pieces[2];
    const int count = waveform::sessionAudioPieces (1000, 20, 90, 100, 5, pieces);
    REQUIRE (count == 2);
    REQUIRE (pieces[0].timelineStartFrame == 910);
    REQUIRE (pieces[0].sourceOffsetFrame == 5);
    REQUIRE (pieces[1].timelineStartFrame == 1010);
    REQUIRE (pieces[1].timelineLengthFrames == 10);
}

TEST_CASE ("session notes wrap across the loop end", "[session]")
{
    waveform::SessionNoteView notes[] = { { 60, 100, 0.0, 0.5 }, { 64, 90, 3.5, 0.5 } };
    waveform::SoundingNote sounding[4];
    const int count = waveform::appendSessionNotes (notes, 2, 3.75, 0.5, 4.0, 0.5, sounding, 0, 4);
    REQUIRE (count == 2);
    REQUIRE (sounding[0].note == 64);
    REQUIRE (sounding[0].onsetSeconds == Catch::Approx (-0.125));
    REQUIRE (sounding[1].note == 60);
    REQUIRE (sounding[1].onsetSeconds == Catch::Approx (0.125));
}
