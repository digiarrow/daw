#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace waveform
{

/// Stereo audio resident in memory at a single sample rate.
/// The audio thread reads the vectors and never resizes them.
struct ClipAudio
{
    double sampleRate = 0.0;
    std::vector<float> left;
    std::vector<float> right;

    [[nodiscard]] std::int64_t frames() const noexcept
    {
        return static_cast<std::int64_t> (left.size());
    }
};

struct AudioFileInfo
{
    bool ok = false;
    double seconds = 0.0;
    double sampleRate = 0.0;
    std::int64_t frames = 0;
    std::string error;
};

/// Message-thread file loader. Decodes with JUCE and resamples before publishing a clip.
/// Not safe to call from the audio thread.
class ClipLibrary
{
public:
    ClipLibrary();
    ~ClipLibrary();

    ClipLibrary (const ClipLibrary&) = delete;
    ClipLibrary& operator= (const ClipLibrary&) = delete;

    [[nodiscard]] AudioFileInfo probe (const std::string& utf8Path) const;

    /// Loads or returns a cached buffer at `targetSampleRate`. On failure `error` is set.
    [[nodiscard]] std::shared_ptr<const ClipAudio> load (const std::string& utf8Path,
                                                         double targetSampleRate,
                                                         std::string& error);

    void clear();

private:
    struct Formats;
    struct Entry;

    std::unique_ptr<Formats> formats_;
    std::vector<Entry> entries_;
};

} // namespace waveform
