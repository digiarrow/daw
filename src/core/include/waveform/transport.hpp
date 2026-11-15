#pragma once

#include "waveform/timing.hpp"

#include <algorithm>
#include <cstdint>

namespace waveform
{

enum class TransportState : std::uint8_t
{
    Stopped = 0,
    Playing = 1,
    Paused = 2
};

/// Messages the message thread may post to the audio thread.
/// Every field is an integer so the command is trivially copyable and lock-free to pass.
enum class EngineCommandType : std::uint8_t
{
    Play,
    Pause,
    Stop,
    SeekSamples,
    SetTempoMilli,      ///< a = BPM * 1000
    SetTimeSignature,   ///< a = numerator, b = denominator
    SetLoop,            ///< c = start micro-quarters, d = end micro-quarters
    SetLoopEnabled,     ///< a = 0 or 1
    SetMetronome,       ///< a = 0 or 1  (handled by the device, ignored by Transport)
    SetTestTone,        ///< a = 0 or 1  (handled by the device, ignored by Transport)
    StartRecord,        ///< a = track index, b = count-in bars, c = 1 for MIDI, 0 for audio
    LaunchSession,      ///< a = track index, b = scene or -1, c = 0 immediate, 1 beat, 2 bar
    ReleaseSession      ///< a = track index. Stops a gate-mode slot.
};

struct EngineCommand
{
    EngineCommandType type = EngineCommandType::Play;
    std::int32_t a = 0;
    std::int32_t b = 0;
    std::int64_t c = 0;
    std::int64_t d = 0;
};

/// Sample clock owned by a single thread (the audio thread, or a test).
///
/// The playhead is an integer sample index at the current sample rate. Tempo changes remap
/// that index so the musical position (quarter notes) stays put. Loop points are stored in
/// quarter notes and converted whenever tempo or sample rate changes.
///
/// This type does not allocate. It is safe to use inside an audio callback after commands
/// have been applied at the start of the block.
class Transport
{
public:
    void setSampleRate (double sampleRate) noexcept;
    void setTempoBpm (double bpm) noexcept;
    void setTimeSignature (TimeSignature signature) noexcept;
    void setLoopEnabled (bool enabled) noexcept;
    void setLoopQuarterNotes (double startQuarterNotes, double endQuarterNotes) noexcept;

    void play() noexcept;
    void pause() noexcept; ///< No effect unless the transport is playing.
    void stop() noexcept;  ///< Stops and returns the playhead to sample 0.
    void seekSamples (std::int64_t sample) noexcept;

    void apply (const EngineCommand& command) noexcept;

    /// While playing, walks `frames` of timeline and invokes `onSegment(startSample, frameCount)`
    /// for each contiguous region. A loop wrap splits the block. The playhead is the position
    /// after the block. Returns false when not playing (the playhead does not move).
    template <typename Fn>
    bool advance (int frames, Fn&& onSegment) noexcept
    {
        if (state_ != TransportState::Playing || frames <= 0)
            return false;

        int remaining = frames;
        int guard = 0;

        while (remaining > 0 && guard++ < frames + 4)
        {
            wrapIfNeeded();

            auto chunk = static_cast<std::int64_t> (remaining);

            if (loopIsActive())
            {
                const auto untilEnd = loopEndSample_ - position_;

                if (untilEnd <= 0)
                {
                    wrapIfNeeded();
                    continue;
                }

                chunk = std::min (chunk, untilEnd);
            }

            if (chunk <= 0)
                break;

            onSegment (position_, static_cast<int> (chunk));
            position_ += chunk;
            remaining -= static_cast<int> (chunk);
        }

        wrapIfNeeded();
        return true;
    }

    [[nodiscard]] TransportState state() const noexcept { return state_; }
    [[nodiscard]] std::int64_t positionSamples() const noexcept { return position_; }
    [[nodiscard]] double tempoBpm() const noexcept { return bpm_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] TimeSignature timeSignature() const noexcept { return signature_; }
    [[nodiscard]] bool loopEnabled() const noexcept { return loopEnabled_; }
    [[nodiscard]] double loopStartQuarterNotes() const noexcept { return loopStartQuarterNotes_; }
    [[nodiscard]] double loopEndQuarterNotes() const noexcept { return loopEndQuarterNotes_; }
    [[nodiscard]] std::int64_t loopStartSample() const noexcept { return loopStartSample_; }
    [[nodiscard]] std::int64_t loopEndSample() const noexcept { return loopEndSample_; }

    [[nodiscard]] MusicalPosition musicalPosition() const noexcept
    {
        return Timing::positionFromSamples (position_, sampleRate_, bpm_, signature_);
    }

    [[nodiscard]] static std::int32_t tempoToMilli (double bpm) noexcept;
    [[nodiscard]] static double tempoFromMilli (std::int32_t milli) noexcept;
    [[nodiscard]] static std::int64_t quarterNotesToMicro (double quarterNotes) noexcept;
    [[nodiscard]] static double quarterNotesFromMicro (std::int64_t micro) noexcept;

private:
    [[nodiscard]] bool loopIsActive() const noexcept;
    void wrapIfNeeded() noexcept;
    void recomputeLoopSamples() noexcept;

    TransportState state_ = TransportState::Stopped;
    double sampleRate_ = 0.0;
    double bpm_ = 120.0;
    TimeSignature signature_ {};
    std::int64_t position_ = 0;
    bool loopEnabled_ = false;
    double loopStartQuarterNotes_ = 0.0;
    double loopEndQuarterNotes_ = 8.0;
    std::int64_t loopStartSample_ = 0;
    std::int64_t loopEndSample_ = 0;
};

} // namespace waveform
