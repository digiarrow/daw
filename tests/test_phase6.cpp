#include "waveform/automation.hpp"
#include "waveform/drum_rack.hpp"
#include "waveform/effects.hpp"
#include "waveform/media.hpp"
#include "waveform/project.hpp"
#include "waveform/serialize.hpp"
#include "waveform/session.hpp"
#include "waveform/stretch.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE ("automation lanes are sample-accurate and clip envelopes interpolate", "[automation]")
{
    REQUIRE (waveform::isAutomatableParameter ("mixer.gainDb"));
    REQUIRE (waveform::isAutomatableParameter ("effect.0.p0"));
    REQUIRE_FALSE (waveform::isAutomatableParameter ("not.a.parameter"));

    waveform::AutomationLane gain;
    gain.parameter = "mixer.gainDb";
    gain.points = { { 0.0, 0.0 }, { 1.0, 1.0 } };
    float curve[4];
    waveform::writeAutomation (gain, 0.0, 4, 48000.0, 120.0, curve, -1.0);
    REQUIRE (curve[0] == Catch::Approx (0.0).margin (1.0e-4));
    REQUIRE (curve[3] > curve[0]);
    REQUIRE (curve[3] < 1.0f);

    waveform::AutomationLane pan;
    pan.parameter = "mixer.pan";
    pan.points = { { 0.0, -1.0 }, { 2.0, 1.0 } };
    REQUIRE (waveform::automationAt (pan, 1.0, 0.0) == Catch::Approx (0.0));

    waveform::AutomationPoint envelope[] = { { 0.0, 0.0 }, { 1.0, 1.0 } };
    REQUIRE (waveform::clipEnvelopeAt (envelope, 2, 0.5, 0.0) == Catch::Approx (0.5));

    float left[2] = { 1.0f, 1.0f };
    float right[2] = { 1.0f, 1.0f };
    float gains[2] = { 0.25f, 0.5f };
    waveform::applyGainCurve (left, right, gains, 2);
    REQUIRE (left[0] == Catch::Approx (0.25f));
    REQUIRE (right[1] == Catch::Approx (0.5f));
}

TEST_CASE ("the compressor can follow a sidechain", "[effects]")
{
    waveform::EffectSlot slot = waveform::defaultEffect (waveform::EffectKind::Compressor);
    slot.p0 = -6.0;
    slot.p1 = 20.0;
    slot.p2 = 0.001;
    slot.p3 = 0.005;
    waveform::EffectState quiet;
    waveform::EffectState keyed;
    float program[8];
    float side[8];
    std::fill (program, program + 8, 0.05f);
    std::fill (side, side + 8, 1.0f);
    float dryLeft[8];
    float dryRight[8];
    float keyLeft[8];
    float keyRight[8];
    std::copy (program, program + 8, dryLeft);
    std::copy (program, program + 8, dryRight);
    std::copy (program, program + 8, keyLeft);
    std::copy (program, program + 8, keyRight);
    waveform::processEffect (slot, quiet, dryLeft, dryRight, 8, 48000.0);
    waveform::processEffect (slot, keyed, keyLeft, keyRight, 8, 48000.0, side, side);
    REQUIRE (std::abs (keyLeft[7]) < std::abs (dryLeft[7]));
}

TEST_CASE ("a drum rack chokes pads that share a group", "[drums]")
{
    waveform::DrumRack rack;
    rack.count = 2;
    rack.pads[0].note = 36;
    rack.pads[0].chokeGroup = 1;
    rack.pads[0].gain = 1.0f;
    rack.pads[1].note = 38;
    rack.pads[1].chokeGroup = 1;
    rack.pads[1].gain = 1.0f;
    waveform::PadVoice voices[4] {};
    REQUIRE (waveform::strikeDrumRack (voices, 4, rack, 36) == 0);
    REQUIRE (voices[0].playing);
    REQUIRE (waveform::strikeDrumRack (voices, 4, rack, 38) >= 0);
    int playing = 0;
    int playingPad = -1;

    for (const auto& voice : voices)
    {
        if (! voice.playing)
            continue;

        ++playing;
        playingPad = voice.pad;
    }

    REQUIRE (playing == 1);
    REQUIRE (playingPad == 1);

    float left[32] {};
    float right[32] {};
    waveform::renderDrumRack (voices, 4, rack, 32, 48000.0, left, right);
    float energy = 0.0f;

    for (float sample : left)
        energy += std::abs (sample);

    REQUIRE (energy > 0.0f);
}

TEST_CASE ("session clips trigger, toggle, repeat, follow, and record", "[session]")
{
    waveform::LaunchStep step;
    step.launch = waveform::ClipLaunchMode::Toggle;
    step.pressedScene = 1;
    step.activeScene = -1;
    auto started = waveform::stepClipLaunch (step, 8, 0.0, true, false);
    REQUIRE (started.activeScene == 1);
    REQUIRE (started.retrigger);

    step.activeScene = 1;
    step.pressedScene = 1;
    auto stopped = waveform::stepClipLaunch (step, 8, 0.0, true, false);
    REQUIRE (stopped.activeScene == -1);

    step.launch = waveform::ClipLaunchMode::Gate;
    step.gateHeld = true;
    step.activeScene = 2;
    auto released = waveform::stepClipLaunch (step, 8, 0.0, false, true);
    REQUIRE (released.activeScene == -1);

    step.launch = waveform::ClipLaunchMode::Repeat;
    step.activeScene = 0;
    step.pressedScene = 0;
    step.playheadQuarters = 0.0;
    step.repeatQuarters = 1.0;
    step.gateHeld = false;
    auto repeated = waveform::stepClipLaunch (step, 8, 1.0, false, false);
    REQUIRE (repeated.retrigger);
    REQUIRE (repeated.playheadQuarters == Catch::Approx (0.0).margin (1.0e-6));

    step.launch = waveform::ClipLaunchMode::Trigger;
    step.follow = waveform::FollowActionKind::Next;
    step.followAfterQuarters = 1.0;
    step.playheadQuarters = 0.0;
    step.repeatQuarters = 100.0;
    auto followed = waveform::stepClipLaunch (step, 4, 1.0, false, false);
    REQUIRE (followed.activeScene == 1);

    double tempos[8] = { 0.0, 100.0 };
    REQUIRE (waveform::tempoForScene (tempos, 8, 1, 120.0) == Catch::Approx (100.0));
    REQUIRE (waveform::tempoForScene (tempos, 8, 0, 120.0) == Catch::Approx (120.0));

    waveform::Track track;
    track.type = waveform::TrackType::Midi;
    waveform::MidiClip clip;
    clip.id = "take";
    clip.lengthQuarterNotes = 2.0;
    waveform::recordIntoSessionSlot (track, 3, clip);
    REQUIRE (track.session.size() == 1);
    REQUIRE (track.session[0].scene == 3);
    REQUIRE (std::get<waveform::MidiClip> (track.session[0].clip).id == "take");
}

TEST_CASE ("warp modes, tempo detection, and auto-warp markers", "[warp]")
{
    const auto beats = waveform::warpRenderSettings (waveform::WarpMode::Beats, 8000, 8000);
    const auto tones = waveform::warpRenderSettings (waveform::WarpMode::Tones, 8000, 8000);
    const auto complex = waveform::warpRenderSettings (waveform::WarpMode::Complex, 8000, 8000);
    REQUIRE (beats.block < complex.block);
    REQUIRE (tones.block > complex.block);

    constexpr int kRate = 8000;
    constexpr int kFrames = kRate * 2;
    std::vector<float> click (static_cast<std::size_t> (kFrames), 0.0f);
    const int period = kRate / 2;

    for (int frame = 0; frame < kFrames; frame += period)
        click[static_cast<std::size_t> (frame)] = 1.0f;

    const double tempo = waveform::detectTempoBpm (click.data(), kFrames, kRate);
    REQUIRE (tempo == Catch::Approx (120.0).margin (8.0));

    const auto markers = waveform::autoWarpMarkers (2.0, 120.0);
    REQUIRE (markers.size() == 2);
    REQUIRE (markers[0].sourceSeconds == Catch::Approx (0.0));
    REQUIRE (markers[1].sourceSeconds == Catch::Approx (2.0));
    REQUIRE (markers[1].timelineQuarters == Catch::Approx (4.0));
    REQUIRE (waveform::detectTempoBpm (click.data(), 10, kRate) == Catch::Approx (0.0));
}

TEST_CASE ("the browser imports mp3 as well as wav", "[media]")
{
    REQUIRE (waveform::isImportableAudioExtension ("mp3"));
    REQUIRE (waveform::isImportableAudioExtension (".MP3"));
    REQUIRE (waveform::isImportableAudioExtension ("wav"));
    REQUIRE (waveform::isImportableAudioExtension ("flac"));
    REQUIRE_FALSE (waveform::isImportableAudioExtension ("mid"));
    REQUIRE_FALSE (waveform::isImportableAudioExtension ("txt"));
}

TEST_CASE ("clip envelopes, warp mode, drum racks, and scene tempo round-trip", "[project]")
{
    waveform::Project project;
    project.name = "Phase 6";
    project.sceneTempoBpm[2] = 90.0;

    waveform::Track drums;
    drums.id = "drums";
    drums.name = "Drums";
    drums.type = waveform::TrackType::Midi;
    drums.instrument = waveform::InstrumentKind::Drum;
    drums.drumRack.count = 1;
    drums.drumRack.pads[0].note = 36;
    drums.drumRack.pads[0].chokeGroup = 2;
    drums.drumRack.pads[0].gain = 0.5f;
    drums.drumRack.pads[0].chainLength = 1;
    drums.drumRack.pads[0].chain[0].kind = waveform::EffectKind::Saturator;
    drums.drumRack.pads[0].chain[0].p0 = 0.4;

    waveform::Track bass;
    bass.id = "bass";
    bass.name = "Bass";
    bass.type = waveform::TrackType::Audio;
    bass.sidechainTrack = 0;
    waveform::AudioClip clip;
    clip.id = "bass-clip";
    clip.name = "Bass";
    clip.file = "bass.wav";
    clip.lengthSeconds = 2.0;
    clip.warpMode = static_cast<int> (waveform::WarpMode::Beats);
    clip.envelope = { { 0.0, 0.0 }, { 2.0, 1.0 } };
    bass.clips.push_back (clip);

    project.tracks.push_back (std::move (drums));
    project.tracks.push_back (std::move (bass));

    std::string error;
    const auto loaded = waveform::projectFromJson (waveform::projectToJson (project), &error);
    REQUIRE (error.empty());
    REQUIRE (loaded.has_value());
    REQUIRE (loaded->sceneTempoBpm[2] == Catch::Approx (90.0));
    REQUIRE (loaded->tracks[0].drumRack.count == 1);
    REQUIRE (loaded->tracks[0].drumRack.pads[0].chokeGroup == 2);
    REQUIRE (loaded->tracks[0].drumRack.pads[0].chain[0].kind == waveform::EffectKind::Saturator);
    REQUIRE (loaded->tracks[1].sidechainTrack == 0);

    const auto* audio = std::get_if<waveform::AudioClip> (&loaded->tracks[1].clips[0]);
    REQUIRE (audio != nullptr);
    REQUIRE (audio->warpMode == static_cast<int> (waveform::WarpMode::Beats));
    REQUIRE (audio->envelope.size() == 2);
    REQUIRE (audio->envelope[1].value == Catch::Approx (1.0));
}
