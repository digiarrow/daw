#include "waveform/automation.hpp"
#include "waveform/commands.hpp"
#include "waveform/drum_rack.hpp"
#include "waveform/effects.hpp"
#include "waveform/session.hpp"
#include "waveform/stretch.hpp"
#include "waveform/synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE ("pan, cutoff, and effect lanes are sample-accurate", "[automation]")
{
    const auto pan = waveform::bindAutomationParameter ("mixer.pan");
    const auto cutoff = waveform::bindAutomationParameter ("synth.cutoffHz");
    const auto drive = waveform::bindAutomationParameter ("effect.1.p2");
    REQUIRE (pan.kind == waveform::AutomationKind::Pan);
    REQUIRE (cutoff.kind == waveform::AutomationKind::Cutoff);
    REQUIRE (drive.kind == waveform::AutomationKind::Effect);
    REQUIRE (drive.effect == 1);
    REQUIRE (drive.parameter == 2);
    REQUIRE (waveform::bindAutomationParameter ("effect.0.p9").kind == waveform::AutomationKind::None);

    float left[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float right[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float pans[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    waveform::applyPanCurve (left, right, pans, 4);
    REQUIRE (left[0] == Catch::Approx (1.0f).margin (1.0e-4));
    REQUIRE (right[0] == Catch::Approx (0.0f).margin (1.0e-4));
    REQUIRE (left[3] == Catch::Approx (0.0f).margin (1.0e-4));
    REQUIRE (right[3] == Catch::Approx (1.0f).margin (1.0e-4));

    waveform::EffectSlot utility = waveform::defaultEffect (waveform::EffectKind::Utility);
    waveform::EffectState state;
    waveform::AutomationLane lane;
    lane.parameter = "effect.0.p0";
    lane.points = { { 0.0, -60.0 }, { 0.0002, 0.0 } };
    float wetLeft[8];
    float wetRight[8];

    for (int index = 0; index < 8; ++index)
    {
        wetLeft[index] = 1.0f;
        wetRight[index] = 1.0f;
    }

    waveform::processAutomatedInserts (&utility, 1, &state, wetLeft, wetRight, 8, 48000.0, nullptr, nullptr, &lane, 1, 0.0, 120.0);
    REQUIRE (std::abs (wetLeft[0]) < std::abs (wetLeft[7]));
    REQUIRE (wetLeft[7] == Catch::Approx (1.0f).margin (0.05f));

    waveform::SynthPatch patch;
    patch.cutoffHz = 8000.0;
    patch.wave = waveform::OscillatorWave::Saw;
    patch.attackSeconds = 0.0;
    patch.decaySeconds = 0.2;
    patch.sustain = 1.0;
    patch.releaseSeconds = 0.05;
    waveform::SoundingNote note;
    note.note = 96;
    note.velocity = 127;
    note.onsetSeconds = 0.0;
    note.releaseSeconds = 0.05;
    waveform::SynthVoiceMemory open;
    waveform::SynthVoiceMemory closed;
    waveform::AutomationLane high;
    high.parameter = "synth.cutoffHz";
    high.points = { { 0.0, 12000.0 }, { 1.0, 12000.0 } };
    waveform::AutomationLane low;
    low.parameter = "synth.cutoffHz";
    low.points = { { 0.0, 80.0 }, { 1.0, 80.0 } };
    float openLeft[256] {};
    float openRight[256] {};
    float closedLeft[256] {};
    float closedRight[256] {};
    waveform::renderSynthNotes (&note, 1, 256, 48000.0, patch, open, 1.0f, 1.0f, openLeft, openRight, &high, 0.0, 120.0);
    waveform::renderSynthNotes (&note, 1, 256, 48000.0, patch, closed, 1.0f, 1.0f, closedLeft, closedRight, &low, 0.0, 120.0);
    double openEnergy = 0.0;
    double closedEnergy = 0.0;

    for (int index = 0; index < 256; ++index)
    {
        openEnergy += std::abs (openLeft[index]);
        closedEnergy += std::abs (closedLeft[index]);
    }

    REQUIRE (openEnergy > closedEnergy);
}

TEST_CASE ("each drum track keeps its own voices for the whole block", "[drums]")
{
    REQUIRE (waveform::drumVoicePool (0) != waveform::drumVoicePool (1));

    waveform::DrumRack rack;
    rack.count = 2;
    rack.pads[0].note = 36;
    rack.pads[0].chokeGroup = 1;
    rack.pads[1].note = 38;
    rack.pads[1].chokeGroup = 1;
    waveform::PadVoice* first = waveform::drumVoicePool (2);
    waveform::PadVoice* second = waveform::drumVoicePool (3);

    for (int index = 0; index < 4; ++index)
    {
        first[index] = {};
        second[index] = {};
    }

    REQUIRE (waveform::strikeDrumRack (first, 4, rack, 36) == 0);
    REQUIRE (waveform::strikeDrumRack (second, 4, rack, 36) == 0);
    REQUIRE (waveform::strikeDrumRack (first, 4, rack, 38) >= 0);
    REQUIRE (second[0].playing);
    REQUIRE (second[0].pad == 0);

    constexpr int kFrames = 3000;
    std::vector<float> left (static_cast<std::size_t> (kFrames), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (kFrames), 0.0f);
    waveform::mixDrumRack (first, 4, rack, kFrames, 48000.0, 1.0f, 1.0f, left.data(), right.data());
    REQUIRE (std::abs (left[2500]) > 0.0f);
}

TEST_CASE ("a tempo edit during a scene is restored when the scene stops", "[session]")
{
    double scenes[8] = { 100.0, 0.0 };
    waveform::SceneTempoHold hold;
    auto playing = waveform::holdSceneTempo (hold, 120.0, 0, scenes, 8, 120.0);
    REQUIRE (playing.playback == Catch::Approx (100.0));
    REQUIRE (playing.hold.restore == Catch::Approx (120.0));

    auto edited = waveform::holdSceneTempo (playing.hold, 140.0, 0, scenes, 8, 120.0);
    REQUIRE (edited.playback == Catch::Approx (100.0));
    REQUIRE (edited.hold.restore == Catch::Approx (140.0));

    auto stopped = waveform::holdSceneTempo (edited.hold, 100.0, -1, scenes, 8, 120.0);
    REQUIRE (stopped.playback == Catch::Approx (140.0));
    REQUIRE (stopped.hold.applied == Catch::Approx (0.0));
}

TEST_CASE ("session recording and auto-warp mode undo", "[commands]")
{
    waveform::Project project;
    waveform::Track track;
    track.id = "midi";
    track.name = "MIDI";
    track.type = waveform::TrackType::Midi;
    waveform::MidiClip existing;
    existing.id = "old";
    existing.name = "Old";
    existing.lengthQuarterNotes = 1.0;
    waveform::SessionSlot slot;
    slot.scene = 1;
    slot.launch = waveform::ClipLaunchMode::Gate;
    slot.clip = existing;
    track.session.push_back (slot);
    project.tracks.push_back (std::move (track));

    waveform::MidiClip take;
    take.id = "take";
    take.name = "Take";
    take.lengthQuarterNotes = 2.0;
    waveform::History history;
    history.execute (project, std::make_unique<waveform::RecordSessionSlotCommand> ("midi", 1, take));
    REQUIRE (std::get<waveform::MidiClip> (project.tracks[0].session[0].clip).id == "take");
    REQUIRE (project.tracks[0].session[0].launch == waveform::ClipLaunchMode::Gate);
    REQUIRE (history.undo (project));
    REQUIRE (std::get<waveform::MidiClip> (project.tracks[0].session[0].clip).id == "old");

    waveform::Track audioTrack;
    audioTrack.id = "audio";
    audioTrack.name = "Audio";
    audioTrack.type = waveform::TrackType::Audio;
    waveform::AudioClip clip;
    clip.id = "clip";
    clip.name = "Clip";
    clip.file = "tone.wav";
    clip.lengthSeconds = 1.0;
    clip.warpMode = static_cast<int> (waveform::WarpMode::Complex);
    audioTrack.clips.push_back (clip);
    project.tracks.push_back (std::move (audioTrack));

    std::vector<waveform::WarpMarker> markers { { 0.0, 0.0 }, { 1.0, 2.0 } };
    history.execute (project, std::make_unique<waveform::SetClipMarkersCommand> ("audio", "clip", markers, std::vector<waveform::CompSegment> {}, static_cast<int> (waveform::WarpMode::Beats)));
    const auto* warped = std::get_if<waveform::AudioClip> (&project.tracks[1].clips[0]);
    REQUIRE (warped != nullptr);
    REQUIRE (warped->warpMode == static_cast<int> (waveform::WarpMode::Beats));
    REQUIRE (warped->warp.size() == 2);
    REQUIRE (history.undo (project));
    warped = std::get_if<waveform::AudioClip> (&project.tracks[1].clips[0]);
    REQUIRE (warped->warpMode == static_cast<int> (waveform::WarpMode::Complex));
    REQUIRE (warped->warp.empty());

    history.execute (project, std::make_unique<waveform::SetClipEnvelopeCommand> ("audio", "clip", std::vector<waveform::AutomationPoint> { { 0.0, 0.0 }, { 1.0, 1.0 } }));
    REQUIRE (std::get<waveform::AudioClip> (project.tracks[1].clips[0]).envelope.size() == 2);
    REQUIRE (history.undo (project));
    REQUIRE (std::get<waveform::AudioClip> (project.tracks[1].clips[0]).envelope.empty());
}
