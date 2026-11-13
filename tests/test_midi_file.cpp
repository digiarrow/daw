#include "waveform/midi_file.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("a MIDI clip round-trips through a standard MIDI file", "[midi-file]")
{
    waveform::MidiClip clip;
    clip.name = "Phrase";
    clip.lengthQuarterNotes = 2.0;
    waveform::MidiNote first;
    first.note = 60;
    first.velocity = 100;
    first.startQuarterNotes = 0.0;
    first.lengthQuarterNotes = 1.0;
    first.channel = 1;
    waveform::MidiNote second;
    second.note = 67;
    second.velocity = 80;
    second.startQuarterNotes = 1.0;
    second.lengthQuarterNotes = 0.5;
    second.channel = 2;
    clip.notes = { first, second };

    const auto bytes = waveform::midiClipToSmf (clip);
    REQUIRE (bytes.size() > 22);
    REQUIRE (bytes[0] == 'M');
    REQUIRE (bytes[1] == 'T');
    REQUIRE (bytes[2] == 'h');
    REQUIRE (bytes[3] == 'd');

    const auto imported = waveform::midiClipFromSmf (bytes.data(), bytes.size());
    REQUIRE (imported.ok);
    REQUIRE (imported.clip.notes.size() == 2);
    REQUIRE (imported.clip.notes[0].note == 60);
    REQUIRE (imported.clip.notes[0].velocity == 100);
    REQUIRE (imported.clip.notes[0].lengthQuarterNotes == Catch::Approx (1.0));
    REQUIRE (imported.clip.notes[1].note == 67);
    REQUIRE (imported.clip.notes[1].channel == 2);
    REQUIRE (imported.clip.notes[1].startQuarterNotes == Catch::Approx (1.0));
    REQUIRE (imported.clip.lengthQuarterNotes == Catch::Approx (1.5));

    const auto rejected = waveform::midiClipFromSmf (nullptr, 0);
    REQUIRE_FALSE (rejected.ok);
    REQUIRE_FALSE (rejected.error.empty());
}
