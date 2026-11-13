#include "waveform/commands.hpp"
#include "waveform/midi_effects.hpp"
#include "waveform/serialize.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using waveform::MidiEffect;
using waveform::MidiEffectKind;
using waveform::MidiNote;

namespace
{

MidiNote note (int pitch, double start, double length, int velocity = 100)
{
    MidiNote value;
    value.note = pitch;
    value.startQuarterNotes = start;
    value.lengthQuarterNotes = length;
    value.velocity = velocity;
    return value;
}

} // namespace

TEST_CASE ("pitch, velocity, scale, and chord run before the instrument", "[midi-effects]")
{
    const MidiNote input[] = { note (60, 0.0, 1.0, 80), note (64, 0.0, 1.0, 80) };
    MidiNote output[8];

    MidiEffect pitch;
    pitch.kind = MidiEffectKind::Pitch;
    pitch.semitones = 2;
    REQUIRE (waveform::applyMidiEffects (input, 2, &pitch, 1, 4.0, output, 8) == 2);
    REQUIRE (output[0].note == 62);
    REQUIRE (output[1].note == 66);

    MidiEffect velocity;
    velocity.kind = MidiEffectKind::Velocity;
    velocity.velocityAdd = -10;
    REQUIRE (waveform::applyMidiEffects (input, 1, &velocity, 1, 4.0, output, 8) == 1);
    REQUIRE (output[0].velocity == 70);

    MidiEffect scale;
    scale.kind = MidiEffectKind::Scale;
    scale.root = 0;
    REQUIRE (waveform::applyMidiEffects (input + 1, 1, &scale, 1, 4.0, output, 8) == 1);
    REQUIRE (output[0].note == 64);

    const MidiNote offScale[] = { note (61, 0.0, 1.0) };
    REQUIRE (waveform::applyMidiEffects (offScale, 1, &scale, 1, 4.0, output, 8) == 1);
    REQUIRE (output[0].note == 60);

    MidiEffect chord;
    chord.kind = MidiEffectKind::Chord;
    chord.chord = waveform::ChordKind::Major;
    REQUIRE (waveform::applyMidiEffects (input, 1, &chord, 1, 4.0, output, 8) == 3);
    REQUIRE (output[0].note == 60);
    REQUIRE (output[1].note == 64);
    REQUIRE (output[2].note == 67);
}

TEST_CASE ("an arpeggiator walks a chord in sixteenths", "[midi-effects]")
{
    const MidiNote chord[] = { note (60, 0.0, 1.0, 90), note (64, 0.0, 1.0, 100), note (67, 0.0, 1.0, 110) };
    MidiEffect arp;
    arp.kind = MidiEffectKind::Arpeggiator;
    arp.division = 16;
    arp.octaves = 1;
    MidiNote output[16];
    const int count = waveform::applyMidiEffects (chord, 3, &arp, 1, 1.0, output, 16);
    REQUIRE (count == 4);
    REQUIRE (output[0].note == 60);
    REQUIRE (output[1].note == 64);
    REQUIRE (output[2].note == 67);
    REQUIRE (output[3].note == 60);
    REQUIRE (output[0].lengthQuarterNotes == Catch::Approx (0.25));
    REQUIRE (output[1].startQuarterNotes == Catch::Approx (0.25));
    REQUIRE (output[0].velocity == 90);
    REQUIRE (output[2].velocity == 110);
}

TEST_CASE ("MIDI effects are stored on the track in slot order", "[midi-effects]")
{
    auto project = waveform::Project::makeDefault();
    const auto trackId = project.tracks[1].id;
    std::vector<MidiEffect> effects (2);
    effects[0].kind = MidiEffectKind::Pitch;
    effects[0].semitones = -12;
    effects[1].kind = MidiEffectKind::Velocity;
    effects[1].velocityAdd = 5;
    waveform::History history;
    history.execute (project, std::make_unique<waveform::SetMidiEffectsCommand> (trackId, effects));
    REQUIRE (waveform::findTrack (project, trackId)->midiEffects.size() == 2);
    REQUIRE (waveform::findTrack (project, trackId)->midiEffects[0].semitones == -12);

    const auto loaded = waveform::projectFromJson (waveform::projectToJson (project), nullptr);
    REQUIRE (loaded.has_value());
    REQUIRE (waveform::findTrack (*loaded, trackId)->midiEffects[1].velocityAdd == 5);
    REQUIRE (history.undo (project));
    REQUIRE (waveform::findTrack (project, trackId)->midiEffects.empty());
}
