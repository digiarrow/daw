#include "waveform/transport.hpp"

#include <cmath>

namespace waveform
{

bool Transport::loopIsActive() const noexcept
{
    return loopEnabled_ && loopEndSample_ > loopStartSample_;
}

void Transport::recomputeLoopSamples() noexcept
{
    loopStartSample_ = Timing::samplesFromQuarterNotes (loopStartQuarterNotes_, sampleRate_, bpm_);
    loopEndSample_ = Timing::samplesFromQuarterNotes (loopEndQuarterNotes_, sampleRate_, bpm_);
}

void Transport::wrapIfNeeded() noexcept
{
    if (! loopIsActive() || position_ < loopEndSample_)
        return;

    const auto length = loopEndSample_ - loopStartSample_;

    if (length <= 0)
        return;

    auto relative = position_ - loopStartSample_;
    relative %= length;

    if (relative < 0)
        relative += length;

    position_ = loopStartSample_ + relative;
}

void Transport::setSampleRate (double sampleRate) noexcept
{
    if (! (sampleRate > 0.0) || std::isnan (sampleRate))
        return;

    double quarterNotes = 0.0;

    if (sampleRate_ > 0.0)
        quarterNotes = Timing::quarterNotesFromSamples (position_, sampleRate_, bpm_);

    sampleRate_ = sampleRate;
    position_ = Timing::samplesFromQuarterNotes (quarterNotes, sampleRate_, bpm_);
    recomputeLoopSamples();
}

void Transport::setTempoBpm (double bpm) noexcept
{
    const double next = Timing::clampBpm (bpm);

    if (sampleRate_ > 0.0 && bpm_ > 0.0)
    {
        const double quarterNotes = Timing::quarterNotesFromSamples (position_, sampleRate_, bpm_);
        bpm_ = next;
        position_ = Timing::samplesFromQuarterNotes (quarterNotes, sampleRate_, bpm_);
    }
    else
    {
        bpm_ = next;
    }

    recomputeLoopSamples();
}

void Transport::setTimeSignature (TimeSignature signature) noexcept
{
    if (! signature.isValid())
        return;

    signature_ = signature;
}

void Transport::setLoopEnabled (bool enabled) noexcept
{
    loopEnabled_ = enabled;
    wrapIfNeeded();
}

void Transport::setLoopQuarterNotes (double startQuarterNotes, double endQuarterNotes) noexcept
{
    if (std::isnan (startQuarterNotes) || std::isnan (endQuarterNotes))
        return;

    if (endQuarterNotes < startQuarterNotes)
        std::swap (startQuarterNotes, endQuarterNotes);

    loopStartQuarterNotes_ = std::max (0.0, startQuarterNotes);
    loopEndQuarterNotes_ = std::max (loopStartQuarterNotes_, endQuarterNotes);
    recomputeLoopSamples();
    wrapIfNeeded();
}

void Transport::play() noexcept
{
    if (state_ != TransportState::Playing)
        state_ = TransportState::Playing;
}

void Transport::pause() noexcept
{
    if (state_ == TransportState::Playing)
        state_ = TransportState::Paused;
}

void Transport::stop() noexcept
{
    state_ = TransportState::Stopped;
    position_ = 0;
}

void Transport::seekSamples (std::int64_t sample) noexcept
{
    position_ = std::max<std::int64_t> (0, sample);
    wrapIfNeeded();
}

void Transport::apply (const EngineCommand& command) noexcept
{
    switch (command.type)
    {
        case EngineCommandType::Play:           play(); break;
        case EngineCommandType::Pause:          pause(); break;
        case EngineCommandType::Stop:           stop(); break;
        case EngineCommandType::SeekSamples:    seekSamples (command.c); break;
        case EngineCommandType::SetTempoMilli:  setTempoBpm (tempoFromMilli (command.a)); break;
        case EngineCommandType::SetTimeSignature:
            setTimeSignature (TimeSignature { command.a, command.b });
            break;
        case EngineCommandType::SetLoop:
            setLoopQuarterNotes (quarterNotesFromMicro (command.c), quarterNotesFromMicro (command.d));
            break;
        case EngineCommandType::SetLoopEnabled: setLoopEnabled (command.a != 0); break;
        case EngineCommandType::SetMetronome:
        case EngineCommandType::SetTestTone:
        case EngineCommandType::StartRecord:
        case EngineCommandType::LaunchSession:
            break;
    }
}

std::int32_t Transport::tempoToMilli (double bpm) noexcept
{
    return static_cast<std::int32_t> (std::lround (Timing::clampBpm (bpm) * 1000.0));
}

double Transport::tempoFromMilli (std::int32_t milli) noexcept
{
    return Timing::clampBpm (static_cast<double> (milli) / 1000.0);
}

std::int64_t Transport::quarterNotesToMicro (double quarterNotes) noexcept
{
    if (std::isnan (quarterNotes))
        return 0;

    return static_cast<std::int64_t> (std::llround (quarterNotes * 1000000.0));
}

double Transport::quarterNotesFromMicro (std::int64_t micro) noexcept
{
    return static_cast<double> (micro) / 1000000.0;
}

} // namespace waveform
