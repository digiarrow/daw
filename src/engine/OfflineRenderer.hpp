#pragma once

#include <cstdint>
#include <string>

namespace waveform
{

struct OfflineRenderRequest
{
    std::string projectPath;
    std::string outputPath;
    double sampleRate = 48000.0;
    double seconds = 4.0;
    bool metronome = false;
    bool testTone = false;
};

struct OfflineRenderResult
{
    bool ok = false;
    int exitCode = 1;
    std::string message;
    double peak = 0.0;
    int sampleRate = 0;
    std::int64_t frames = 0;
};

/// Renders a project to a 16-bit stereo WAV using the same clock and mixer as live playback.
[[nodiscard]] OfflineRenderResult renderProjectFile (const OfflineRenderRequest& request);

} // namespace waveform
