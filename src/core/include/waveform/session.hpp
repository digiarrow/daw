#pragma once

#include "waveform/project.hpp"
#include "waveform/synth.hpp"

#include <cstdint>

namespace waveform
{

struct SessionNoteView
{
    int note = 60;
    int velocity = 100;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 0.25;
    float slideSemitones = 0.0f;
};

struct SessionAudioPiece
{
    std::int64_t timelineStartFrame = 0;
    std::int64_t timelineLengthFrames = 0;
    std::int64_t sourceOffsetFrame = 0;
};

/// Looping session clip, split into at most two reads when the loop wraps inside the block.
[[nodiscard]] int sessionAudioPieces (std::int64_t timelineStart,
                                      int frames,
                                      std::int64_t playhead,
                                      std::int64_t loopFrames,
                                      std::int64_t fileOffset,
                                      SessionAudioPiece* pieces) noexcept;

/// Notes sounding in one block of a looping session clip. Times are seconds relative to the block.
[[nodiscard]] int appendSessionNotes (const SessionNoteView* notes,
                                      int noteCount,
                                      double playheadQuarters,
                                      double blockQuarters,
                                      double loopQuarters,
                                      double secondsPerQuarter,
                                      SoundingNote* destination,
                                      int destinationCount,
                                      int capacity) noexcept;

struct SessionVoice
{
    int active = -1;                 ///< Scene index, or -1 to follow the arrangement.
    int pending = -2;                ///< -2 means nothing is waiting.
    std::int64_t pendingSample = 0;  ///< Timeline sample where `pending` becomes active.
    std::int64_t playhead = 0;       ///< Samples into the looping clip.
};

} // namespace waveform
