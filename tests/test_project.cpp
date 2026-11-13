#include "waveform/commands.hpp"
#include "waveform/paths.hpp"
#include "waveform/serialize.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

using waveform::AudioClip;
using waveform::History;
using waveform::MidiClip;
using waveform::MidiNote;
using waveform::MixerState;
using waveform::Project;
using waveform::TrackType;

namespace
{

Project sampleProject()
{
    Project project;
    project.name = "Round Trip";
    project.tempoBpm = 96.5;
    project.timeSignature = { 6, 8 };
    project.loopEnabled = true;
    project.loopStartQuarterNotes = 1.0;
    project.loopEndQuarterNotes = 7.5;
    project.master.gainDb = -3.0;

    waveform::Track audio;
    audio.id = "track-audio";
    audio.name = "Room";
    audio.type = TrackType::Audio;
    audio.mixer = MixerState { -6.0, -0.25, false, true };

    AudioClip clip;
    clip.id = "clip-audio";
    clip.name = "Take";
    clip.file = "audio/take.wav";
    clip.startQuarterNotes = 2.0;
    clip.offsetSeconds = 0.5;
    clip.lengthSeconds = 3.25;
    clip.gainDb = -1.5;
    audio.clips.emplace_back (clip);

    waveform::Track midi;
    midi.id = "track-midi";
    midi.name = "Keys";
    midi.type = TrackType::Midi;
    midi.mixer = MixerState { 1.0, 0.5, true, false };

    MidiClip phrase;
    phrase.id = "clip-midi";
    phrase.name = "Phrase";
    phrase.startQuarterNotes = 0.0;
    phrase.lengthQuarterNotes = 4.0;
    phrase.notes.push_back (MidiNote { 60, 0.0, 1.0, 100, 1 });
    phrase.notes.push_back (MidiNote { 67, 1.5, 0.5, 80, 2 });
    midi.clips.emplace_back (phrase);

    project.tracks.push_back (std::move (audio));
    project.tracks.push_back (std::move (midi));
    return project;
}

} // namespace

TEST_CASE ("project json round-trips", "[project]")
{
    const auto project = sampleProject();
    const auto json = waveform::projectToJson (project);
    std::string error;
    const auto loaded = waveform::projectFromJson (json, &error);
    REQUIRE (error.empty());
    REQUIRE (loaded.has_value());

    REQUIRE (loaded->name == "Round Trip");
    REQUIRE (loaded->tempoBpm == Catch::Approx (96.5));
    REQUIRE (loaded->timeSignature.numerator == 6);
    REQUIRE (loaded->timeSignature.denominator == 8);
    REQUIRE (loaded->loopEnabled);
    REQUIRE (loaded->loopStartQuarterNotes == Catch::Approx (1.0));
    REQUIRE (loaded->loopEndQuarterNotes == Catch::Approx (7.5));
    REQUIRE (loaded->master.gainDb == Catch::Approx (-3.0));
    REQUIRE (loaded->tracks.size() == 2);

    const auto* audio = std::get_if<AudioClip> (&loaded->tracks[0].clips[0]);
    REQUIRE (audio != nullptr);
    REQUIRE (audio->file == "audio/take.wav");
    REQUIRE (audio->offsetSeconds == Catch::Approx (0.5));
    REQUIRE (audio->lengthSeconds == Catch::Approx (3.25));
    REQUIRE (loaded->tracks[0].mixer.pan == Catch::Approx (-0.25));
    REQUIRE (loaded->tracks[0].mixer.solo);

    const auto* midi = std::get_if<MidiClip> (&loaded->tracks[1].clips[0]);
    REQUIRE (midi != nullptr);
    REQUIRE (midi->notes.size() == 2);
    REQUIRE (midi->notes[1].note == 67);
    REQUIRE (midi->notes[1].channel == 2);
    REQUIRE (loaded->tracks[1].mixer.mute);

    const auto again = waveform::projectToJson (*loaded);
    REQUIRE (again == json);

    auto withExtra = json;
    withExtra.insert (1, "\"bonus\": true, ");
    const auto ignored = waveform::projectFromJson (withExtra, &error);
    REQUIRE (ignored.has_value());
    REQUIRE (ignored->name == project.name);
}

TEST_CASE ("project json rejects malformed documents", "[project]")
{
    std::string error;
    REQUIRE_FALSE (waveform::projectFromJson ("{", &error).has_value());
    REQUIRE_FALSE (error.empty());

    auto project = sampleProject();
    auto json = waveform::projectToJson (project);
    const auto brokenNote = json.replace (json.find ("\"note\": 67"), std::string ("\"note\": 67").size(), "\"note\": 200");
    REQUIRE_FALSE (waveform::projectFromJson (brokenNote, &error).has_value());
    REQUIRE (error.find ("127") != std::string::npos);

    auto duplicate = project;
    duplicate.tracks[1].id = duplicate.tracks[0].id;
    REQUIRE_FALSE (waveform::projectFromJson (waveform::projectToJson (duplicate), &error).has_value());
    REQUIRE (error.find ("duplicate") != std::string::npos);

    REQUIRE_FALSE (waveform::projectFromJson ("{\"format\":\"other\",\"version\":1}", &error).has_value());
}

TEST_CASE ("undo and redo restore document edits", "[project]")
{
    auto project = Project::makeDefault();
    History history (2);
    const auto originalTempo = project.tempoBpm;

    history.execute (project, std::make_unique<waveform::SetTempoCommand> (140.0));
    REQUIRE (project.tempoBpm == Catch::Approx (140.0));
    REQUIRE (history.undo (project));
    REQUIRE (project.tempoBpm == Catch::Approx (originalTempo));
    REQUIRE (history.redo (project));
    REQUIRE (project.tempoBpm == Catch::Approx (140.0));

    auto added = std::make_unique<waveform::AddTrackCommand> (TrackType::Audio, "Room");
    const auto id = added->trackId();
    history.execute (project, std::move (added));
    REQUIRE (waveform::findTrack (project, id) != nullptr);

    AudioClip clip;
    clip.name = "Take";
    clip.file = "take.wav";
    clip.lengthSeconds = 1.0;
    history.execute (project, std::make_unique<waveform::AddAudioClipCommand> (id, clip));
    REQUIRE (history.canUndo());
    REQUIRE_FALSE (history.canRedo());
    REQUIRE (waveform::findTrack (project, id)->clips.size() == 1);

    REQUIRE (history.undo (project));
    REQUIRE (waveform::findTrack (project, id)->clips.empty());
    REQUIRE (history.undo (project));
    REQUIRE (waveform::findTrack (project, id) == nullptr);

    MixerState mixer;
    mixer.gainDb = -12.0;
    mixer.pan = 0.3;
    mixer.mute = true;
    history.execute (project, std::make_unique<waveform::SetMixerCommand> (project.tracks.front().id, mixer));
    REQUIRE (project.tracks.front().mixer.mute);
    REQUIRE (history.undo (project));
    REQUIRE_FALSE (project.tracks.front().mixer.mute);

    history.execute (project, std::make_unique<waveform::SetTempoCommand> (100.0));
    history.execute (project, std::make_unique<waveform::SetTempoCommand> (110.0));
    history.execute (project, std::make_unique<waveform::SetTempoCommand> (130.0));
    REQUIRE (history.undo (project));
    REQUIRE (project.tempoBpm == Catch::Approx (110.0));
    REQUIRE (history.undo (project));
    REQUIRE_FALSE (history.undo (project));
}

TEST_CASE ("audio paths are stored relative to the project file", "[project]")
{
    const auto directory = std::filesystem::temp_directory_path() / "waveform-studio-path-test";
    std::filesystem::remove_all (directory);
    std::filesystem::create_directories (directory / "audio");
    const auto projectFile = directory / "song.json";
    const auto audioFile = directory / "audio" / "take.wav";
    const auto outside = std::filesystem::temp_directory_path() / "waveform-studio-outside.wav";
    {
        std::ofstream projectStream (projectFile);
        std::ofstream audioStream (audioFile);
        std::ofstream outsideStream (outside);
        projectStream << "{}\n";
        audioStream << "x";
        outsideStream << "y";
    }

    REQUIRE (waveform::portableAudioPath (projectFile, audioFile) == "audio/take.wav");
    REQUIRE (std::filesystem::equivalent (waveform::resolveAudioPath (projectFile, "audio/take.wav"), audioFile));

    const auto storedOutside = waveform::portableAudioPath (projectFile, outside);
    REQUIRE (storedOutside.find ("..") == std::string::npos);
    REQUIRE (std::filesystem::equivalent (waveform::resolveAudioPath (projectFile, storedOutside), outside));

    std::filesystem::remove_all (directory);
    std::filesystem::remove (outside);
}
