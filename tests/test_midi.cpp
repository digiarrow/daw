#include "waveform/commands.hpp"
#include "waveform/midi.hpp"
#include "waveform/synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using waveform::MidiNote;
using waveform::SoundingNote;
using waveform::SynthPatch;
using waveform::SynthVoiceMemory;
using waveform::TimeSignature;

TEST_CASE ("grid snap and quantize", "[midi]")
{
    REQUIRE (waveform::gridQuarterNotes (4) == Catch::Approx (1.0));
    REQUIRE (waveform::gridQuarterNotes (8) == Catch::Approx (0.5));
    REQUIRE (waveform::gridQuarterNotes (16) == Catch::Approx (0.25));
    REQUIRE (waveform::gridQuarterNotes (0) == Catch::Approx (0.0));

    REQUIRE (waveform::snapToGrid (0.30, 0.25, 1.0) == Catch::Approx (0.25));
    REQUIRE (waveform::snapToGrid (0.10, 0.25, 1.0) == Catch::Approx (0.0));
    REQUIRE (waveform::snapToGrid (0.30, 0.25, 0.0) == Catch::Approx (0.30));

    std::vector<MidiNote> notes { MidiNote { 60, 0.30, 0.40, 100, 1 } };
    waveform::quantizeNotes (notes, 0.25, 1.0, false);
    REQUIRE (notes[0].startQuarterNotes == Catch::Approx (0.25));
    REQUIRE (notes[0].lengthQuarterNotes == Catch::Approx (0.40));

    notes[0].startQuarterNotes = 0.30;
    notes[0].lengthQuarterNotes = 0.40;
    waveform::quantizeNotes (notes, 0.25, 1.0, true);
    REQUIRE (notes[0].startQuarterNotes == Catch::Approx (0.25));
    REQUIRE (notes[0].startQuarterNotes + notes[0].lengthQuarterNotes == Catch::Approx (0.75).margin (1.0e-9));
}

TEST_CASE ("count-in length follows the time signature", "[midi]")
{
    REQUIRE (waveform::countInSamples (0, TimeSignature { 4, 4 }, 48000.0, 120.0) == 0);
    REQUIRE (waveform::countInSamples (1, TimeSignature { 4, 4 }, 48000.0, 120.0) == 96000);
    REQUIRE (waveform::countInSamples (2, TimeSignature { 6, 8 }, 48000.0, 120.0) == 144000);
}

TEST_CASE ("editing notes is undoable", "[midi]")
{
    auto project = waveform::Project::makeDefault();
    const auto trackId = project.tracks[1].id;
    waveform::MidiClip clip;
    clip.name = "Phrase";
    clip.lengthQuarterNotes = 4.0;
    clip.notes.push_back (MidiNote { 60, 0.0, 1.0, 100, 1 });
    auto added = std::make_unique<waveform::AddMidiClipCommand> (trackId, clip);
    const auto clipId = added->clipId();
    waveform::History history;
    history.execute (project, std::move (added));

    std::vector<MidiNote> edited { MidiNote { 64, 0.5, 0.5, 80, 1 }, MidiNote { 67, 1.0, 1.0, 90, 1 } };
    history.execute (project, std::make_unique<waveform::SetMidiNotesCommand> (trackId, clipId, edited));
    const auto* midi = std::get_if<waveform::MidiClip> (&waveform::findTrack (project, trackId)->clips.back());
    REQUIRE (midi->notes.size() == 2);
    REQUIRE (midi->notes[0].note == 64);

    REQUIRE (history.undo (project));
    midi = std::get_if<waveform::MidiClip> (&waveform::findTrack (project, trackId)->clips.back());
    REQUIRE (midi->notes.size() == 1);
    REQUIRE (midi->notes[0].note == 60);

    history.execute (project, std::make_unique<waveform::MoveClipCommand> (trackId, clipId, 2.0));
    midi = std::get_if<waveform::MidiClip> (&waveform::findTrack (project, trackId)->clips.back());
    REQUIRE (midi->startQuarterNotes == Catch::Approx (2.0));
    REQUIRE (history.undo (project));
    midi = std::get_if<waveform::MidiClip> (&waveform::findTrack (project, trackId)->clips.back());
    REQUIRE (midi->startQuarterNotes == Catch::Approx (0.0));
}

TEST_CASE ("the synth sounds during a note and falls silent after the release", "[midi]")
{
    SynthPatch patch;
    patch.wave = waveform::OscillatorWave::Sine;
    patch.attackSeconds = 0.005;
    patch.decaySeconds = 0.01;
    patch.sustain = 0.8;
    patch.releaseSeconds = 0.05;
    patch.detuneCents = 0.0;
    patch.cutoffHz = 8000.0;
    patch.resonance = 0.0;

    SoundingNote note;
    note.note = 69;
    note.velocity = 110;
    note.onsetSeconds = 0.0;
    note.releaseSeconds = 0.20;

    constexpr int kFrames = 48000;
    std::vector<float> left (kFrames), right (kFrames);
    SynthVoiceMemory memory;
    waveform::renderSynthNotes (&note, 1, kFrames, 48000.0, patch, memory, 1.0f, 1.0f, left.data(), right.data());

    double during = 0.0;
    double after = 0.0;

    for (int index = 2000; index < 8000; ++index)
        during = std::max (during, static_cast<double> (std::abs (left[static_cast<std::size_t> (index)])));

    for (int index = 20000; index < 30000; ++index)
        after = std::max (after, static_cast<double> (std::abs (left[static_cast<std::size_t> (index)])));

    REQUIRE (during > 0.02);
    REQUIRE (after < 0.001);
}
