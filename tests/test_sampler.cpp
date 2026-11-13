#include "waveform/automation.hpp"
#include "waveform/collect.hpp"
#include "waveform/paths.hpp"
#include "waveform/sampler.hpp"

#include <fstream>

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

TEST_CASE ("collect copies media next to the project and rewrites the path", "[collect]")
{
    const auto root = std::filesystem::temp_directory_path() / "waveform-collect-test";
    std::filesystem::remove_all (root);
    std::filesystem::create_directories (root / "elsewhere");
    const auto source = root / "elsewhere" / "tone.wav";
    const auto projectFile = root / "song.json";
    {
        std::ofstream stream (source, std::ios::binary);
        stream << "RIFF";
        std::ofstream projectStream (projectFile);
        projectStream << "{}";
    }

    waveform::Project project;
    waveform::Track track;
    track.type = waveform::TrackType::Audio;
    waveform::AudioClip clip;
    clip.file = waveform::pathToUtf8 (source);
    clip.lengthSeconds = 1.0;
    track.clips.emplace_back (clip);
    project.tracks.push_back (std::move (track));

    const auto result = waveform::collectMedia (project, projectFile);
    REQUIRE (result.copied == 1);
    REQUIRE (result.missing.empty());
    const auto stored = std::get<waveform::AudioClip> (project.tracks[0].clips[0]).file;
    REQUIRE (stored == "Media/tone.wav");
    REQUIRE (std::filesystem::exists (root / "Media" / "tone.wav"));
    std::filesystem::remove_all (root);
}
