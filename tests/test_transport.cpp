#include "waveform/transport.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using waveform::EngineCommand;
using waveform::EngineCommandType;
using waveform::TimeSignature;
using waveform::Transport;
using waveform::TransportState;

namespace
{

Transport makeClock()
{
    Transport transport;
    transport.setSampleRate (48000.0);
    transport.setTempoBpm (120.0);
    transport.setTimeSignature (TimeSignature { 4, 4 });
    return transport;
}

} // namespace

TEST_CASE ("play, pause, and stop", "[transport]")
{
    auto transport = makeClock();
    REQUIRE (transport.state() == TransportState::Stopped);

    int calls = 0;
    REQUIRE_FALSE (transport.advance (128, [&] (std::int64_t, int) { ++calls; }));
    REQUIRE (calls == 0);
    REQUIRE (transport.positionSamples() == 0);

    transport.play();
    REQUIRE (transport.advance (512, [&] (std::int64_t start, int frames)
    {
        REQUIRE (start == 0);
        REQUIRE (frames == 512);
    }));
    REQUIRE (transport.positionSamples() == 512);

    transport.pause();
    REQUIRE_FALSE (transport.advance (100, [&] (std::int64_t, int) { ++calls; }));
    REQUIRE (transport.positionSamples() == 512);

    transport.play();
    transport.advance (10, [] (std::int64_t, int) {});
    REQUIRE (transport.positionSamples() == 522);

    transport.stop();
    REQUIRE (transport.state() == TransportState::Stopped);
    REQUIRE (transport.positionSamples() == 0);
    transport.pause();
    REQUIRE (transport.state() == TransportState::Stopped);
}

TEST_CASE ("advancing one sample at a time does not drift", "[transport]")
{
    auto transport = makeClock();
    transport.play();

    for (int index = 0; index < 48000; ++index)
    {
        std::int64_t seen = -1;
        transport.advance (1, [&] (std::int64_t start, int frames)
        {
            seen = start;
            REQUIRE (frames == 1);
        });
        REQUIRE (seen == index);
    }

    REQUIRE (transport.positionSamples() == 48000);
    REQUIRE (transport.musicalPosition().quarterNotes == Catch::Approx (2.0));
    REQUIRE (transport.musicalPosition().bar == 1);
    REQUIRE (transport.musicalPosition().beat == 3);
}

TEST_CASE ("loop splits a block on the boundary", "[transport]")
{
    auto transport = makeClock();
    transport.setLoopQuarterNotes (0.0, 1.0);
    transport.setLoopEnabled (true);
    transport.play();

    std::vector<std::pair<std::int64_t, int>> segments;
    transport.advance (24100, [&] (std::int64_t start, int frames)
    {
        segments.emplace_back (start, frames);
    });

    REQUIRE (segments.size() == 2);
    REQUIRE (segments[0] == std::pair<std::int64_t, int> { 0, 24000 });
    REQUIRE (segments[1] == std::pair<std::int64_t, int> { 0, 100 });
    REQUIRE (transport.positionSamples() == 100);
}

TEST_CASE ("playback before the loop start runs up to the loop end", "[transport]")
{
    auto transport = makeClock();
    transport.seekSamples (100);
    transport.setLoopQuarterNotes (0.0, 1.0);
    transport.setLoopEnabled (true);
    transport.play();

    std::int64_t seen = -1;
    transport.advance (50, [&] (std::int64_t start, int frames)
    {
        seen = start;
        REQUIRE (frames == 50);
    });
    REQUIRE (seen == 100);
    REQUIRE (transport.positionSamples() == 150);
}

TEST_CASE ("tempo changes keep the musical position", "[transport]")
{
    auto transport = makeClock();
    transport.seekSamples (24000);
    REQUIRE (transport.musicalPosition().quarterNotes == Catch::Approx (1.0));

    transport.setTempoBpm (60.0);
    REQUIRE (transport.tempoBpm() == Catch::Approx (60.0));
    REQUIRE (transport.positionSamples() == 48000);
    REQUIRE (transport.musicalPosition().quarterNotes == Catch::Approx (1.0));

    transport.setSampleRate (44100.0);
    REQUIRE (transport.musicalPosition().quarterNotes == Catch::Approx (1.0).margin (1.0e-9));
    // Still one quarter note: at 60 BPM that is one second, which is 44100 samples.
    REQUIRE (transport.positionSamples() == 44100);
}

TEST_CASE ("engine commands drive the clock", "[transport]")
{
    auto transport = makeClock();
    EngineCommand play;
    play.type = EngineCommandType::Play;
    transport.apply (play);
    transport.advance (100, [] (std::int64_t, int) {});

    EngineCommand tempo;
    tempo.type = EngineCommandType::SetTempoMilli;
    tempo.a = 90000;
    transport.apply (tempo);
    REQUIRE (transport.tempoBpm() == Catch::Approx (90.0));

    EngineCommand seek;
    seek.type = EngineCommandType::SeekSamples;
    seek.c = 4800;
    transport.apply (seek);
    REQUIRE (transport.positionSamples() == 4800);

    EngineCommand stop;
    stop.type = EngineCommandType::Stop;
    transport.apply (stop);
    REQUIRE (transport.state() == TransportState::Stopped);
    REQUIRE (transport.positionSamples() == 0);
}
