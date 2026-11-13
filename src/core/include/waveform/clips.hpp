#pragma once

#include "waveform/project.hpp"

#include <cstdint>
#include <vector>

namespace waveform
{

/// Seconds of one quarter note at `bpm`.
[[nodiscard]] double secondsPerQuarter (double bpm) noexcept;

/// Equal-power gain at `secondsFromStart` inside a clip of `lengthSeconds`.
[[nodiscard]] float clipFadeGain (double secondsFromStart,
                                  double lengthSeconds,
                                  double fadeInSeconds,
                                  double fadeOutSeconds) noexcept;

/// Peak absolute sample in each column. `right` may be null.
[[nodiscard]] std::vector<float> waveformPeaks (const float* left,
                                                const float* right,
                                                std::int64_t frames,
                                                int columns);

/// Splits `clip` at an absolute timeline quarter. The left piece keeps the original id.
[[nodiscard]] bool splitClip (const Clip& clip,
                              double splitQuarter,
                              double bpm,
                              Clip& left,
                              Clip& right);

/// Moves the clip's start and sets its length in quarter notes. Audio offset follows the start.
[[nodiscard]] bool resizeClip (Clip& clip, double newStartQuarter, double newLengthQuarters, double bpm);

/// Removes the clip. Returns false when the id is missing.
[[nodiscard]] bool deleteClip (Track& track, const std::string& clipId);

} // namespace waveform
