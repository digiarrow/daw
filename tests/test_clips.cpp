#include "waveform/clips.hpp"
#include "waveform/commands.hpp"
#include "waveform/mix.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using waveform::AudioClip;
using waveform::MidiClip;
using waveform::MidiNote;

TEST_CASE ("clips split, resize, and delete", "[clips]")
{
    AudioClip audio;
    audio.id = "audio";
    audio.name = "Take";
    audio.file = "take.wav";
    audio.startQuarterNotes = 0.0;
    audio.offsetSeconds = 0.25;
    audio.lengthSeconds = 2.0;
    audio.fadeInSeconds = 0.1;
    waveform::Clip left;
    waveform::Clip right { AudioClip {} };
    REQUIRE (waveform::splitClip (audio, 1.0, 120.0, left, right));
    const auto& leftAudio = std::get<AudioClip> (left);
    const auto& rightAudio = std::get<AudioClip> (right);
    REQUIRE (leftAudio.id == "audio");
    REQUIRE (leftAudio.lengthSeconds == Catch::Approx (0.5));
    REQUIRE (leftAudio.fadeOutSeconds == Catch::Approx (0.0));
    REQUIRE (rightAudio.startQuarterNotes == Catch::Approx (1.0));
    REQUIRE (rightAudio.offsetSeconds == Catch::Approx (0.75));
    REQUIRE (rightAudio.lengthSeconds == Catch::Approx (1.5));
    REQUIRE (rightAudio.id != leftAudio.id);

    MidiClip midi;
    midi.id = "midi";
    midi.lengthQuarterNotes = 4.0;
    midi.notes.push_back (MidiNote { 60, 0.0, 2.0, 100, 1 });
    midi.notes.push_back (MidiNote { 64, 2.0, 1.0, 90, 1 });
    REQUIRE (waveform::splitClip (midi, 2.0, 120.0, left, right));
    const auto& leftMidi = std::get<MidiClip> (left);
    const auto& rightMidi = std::get<MidiClip> (right);
    REQUIRE (leftMidi.notes.size() == 1);
    REQUIRE (leftMidi.notes[0].lengthQuarterNotes == Catch::Approx (2.0));
    REQUIRE (rightMidi.notes.size() == 1);
    REQUIRE (rightMidi.notes[0].startQuarterNotes == Catch::Approx (0.0));
    REQUIRE (rightMidi.notes[0].note == 64);

    waveform::Clip resized = audio;
    REQUIRE (waveform::resizeClip (resized, 1.0, 2.0, 120.0));
    REQUIRE (std::get<AudioClip> (resized).offsetSeconds == Catch::Approx (0.75));
    REQUIRE (std::get<AudioClip> (resized).lengthSeconds == Catch::Approx (1.0));

    auto project = waveform::Project::makeDefault();
    project.tracks[0].clips.push_back (audio);
    REQUIRE (waveform::deleteClip (project.tracks[0], "audio"));
    REQUIRE (project.tracks[0].clips.empty());
    REQUIRE_FALSE (waveform::deleteClip (project.tracks[0], "audio"));

    waveform::History history;
    project.tracks[0].clips.push_back (audio);
    history.execute (project, std::make_unique<waveform::RemoveClipCommand> (project.tracks[0].id, "audio"));
    REQUIRE (project.tracks[0].clips.empty());
    REQUIRE (history.undo (project));
    REQUIRE (project.tracks[0].clips.size() == 1);
}

TEST_CASE ("fade handles crossfade and peaks describe the waveform", "[clips]")
{
    REQUIRE (waveform::clipFadeGain (0.0, 1.0, 0.5, 0.5) == Catch::Approx (0.0).margin (1.0e-5));
    REQUIRE (waveform::clipFadeGain (0.5, 1.0, 0.5, 0.5) == Catch::Approx (1.0).margin (1.0e-5));
    REQUIRE (waveform::clipFadeGain (1.0, 1.0, 0.5, 0.5) == Catch::Approx (0.0).margin (1.0e-5));

    const float in = waveform::clipFadeGain (0.25, 1.0, 0.5, 0.0);
    const float out = waveform::clipFadeGain (0.75, 1.0, 0.0, 0.5);
    REQUIRE (in * in + out * out == Catch::Approx (1.0).margin (1.0e-4));

    const std::vector<float> samples { 1.0f, 1.0f, 1.0f, 1.0f };
    const std::vector<float> shaped { 0.2f, 0.2f, 0.9f, -0.4f };
    waveform::AudioClipView first;
    first.source.left = samples.data();
    first.source.right = samples.data();
    first.source.frames = 4;
    first.timelineLengthFrames = 4;
    first.fadeOutFrames = 4;
    first.gain = 1.0f;
    waveform::AudioClipView second = first;
    second.fadeOutFrames = 0;
    second.fadeInFrames = 4;
    waveform::AudioClipView pair[] = { first, second };
    waveform::TrackMix track;
    track.clips = pair;
    track.clipCount = 2;
    track.pan = -1.0f;
    waveform::MixView mix;
    mix.tracks = &track;
    mix.trackCount = 1;
    std::vector<float> left (4), right (4);
    waveform::mixBlock (mix, 0, 4, left.data(), right.data());
    REQUIRE (left[0] == Catch::Approx (1.0).margin (1.0e-3));
    REQUIRE (left[2] == Catch::Approx (std::sqrt (2.0)).margin (1.0e-3));
    REQUIRE (right[2] == Catch::Approx (0.0).margin (1.0e-5));

    const auto peaks = waveform::waveformPeaks (shaped.data(), nullptr, 4, 2);
    REQUIRE (peaks.size() == 2);
    REQUIRE (peaks[0] == Catch::Approx (0.2f));
    REQUIRE (peaks[1] == Catch::Approx (0.9f));
}
