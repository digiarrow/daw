#include "ClipLibrary.hpp"

#include "waveform/paths.hpp"
#include "waveform/timing.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace waveform
{
namespace
{

constexpr double kMaxSeconds = 10.0 * 60.0;

juce::File fileFromUtf8 (const std::string& path)
{
    return juce::File (juce::String::fromUTF8 (path.data(), static_cast<int> (std::min<std::size_t> (path.size(), 1 << 20))));
}

std::string cacheKey (const std::string& path, double sampleRate)
{
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time (pathFromUtf8 (path), error);
    const auto ticks = error ? std::filesystem::file_time_type::duration::zero().count()
                             : stamp.time_since_epoch().count();
    return path + "|" + std::to_string (static_cast<int> (std::lround (sampleRate))) + "|" + std::to_string (ticks);
}

void resampleChannel (const std::vector<float>& input, double inputRate, std::vector<float>& output, double outputRate)
{
    if (input.empty() || ! (inputRate > 0.0) || ! (outputRate > 0.0))
    {
        output.clear();
        return;
    }

    if (std::abs (inputRate - outputRate) < 1.0)
    {
        output = input;
        return;
    }

    const double ratio = outputRate / inputRate;
    const auto outputFrames = std::max<std::int64_t> (1, std::llround (static_cast<double> (input.size()) * ratio));
    output.resize (static_cast<std::size_t> (outputFrames));

    if (input.size() == 1 || outputFrames == 1)
    {
        std::fill (output.begin(), output.end(), input.front());
        return;
    }

    const double step = static_cast<double> (input.size() - 1) / static_cast<double> (outputFrames - 1);

    for (std::int64_t index = 0; index < outputFrames; ++index)
    {
        const double position = step * static_cast<double> (index);
        const auto leftIndex = static_cast<std::int64_t> (position);
        const double fraction = position - static_cast<double> (leftIndex);
        const auto rightIndex = std::min<std::int64_t> (leftIndex + 1, static_cast<std::int64_t> (input.size()) - 1);
        const float sample = static_cast<float> ((1.0 - fraction) * input[static_cast<std::size_t> (leftIndex)]
                                                + fraction * input[static_cast<std::size_t> (rightIndex)]);
        output[static_cast<std::size_t> (index)] = sample;
    }
}

} // namespace

struct ClipLibrary::Formats
{
    juce::AudioFormatManager manager;

    Formats() { manager.registerBasicFormats(); }
};

struct ClipLibrary::Entry
{
    std::string key;
    std::shared_ptr<const ClipAudio> audio;
};

ClipLibrary::ClipLibrary()
    : formats_ (std::make_unique<Formats>())
{
}

ClipLibrary::~ClipLibrary() = default;

AudioFileInfo ClipLibrary::probe (const std::string& utf8Path) const
{
    AudioFileInfo info;
    const auto file = fileFromUtf8 (utf8Path);

    if (! file.existsAsFile())
    {
        info.error = "file not found";
        return info;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats_->manager.createReaderFor (file));

    if (reader == nullptr)
    {
        info.error = "unsupported or unreadable audio file";
        return info;
    }

    if (! (reader->sampleRate > 0.0) || reader->lengthInSamples <= 0)
    {
        info.error = "audio file has no samples";
        return info;
    }

    info.ok = true;
    info.sampleRate = reader->sampleRate;
    info.frames = reader->lengthInSamples;
    info.seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    return info;
}

std::shared_ptr<const ClipAudio> ClipLibrary::load (const std::string& utf8Path,
                                                    double targetSampleRate,
                                                    std::string& error)
{
    error.clear();

    if (! Timing::isValidSampleRate (targetSampleRate))
    {
        error = "audio device sample rate is not ready";
        return nullptr;
    }

    const auto key = cacheKey (utf8Path, targetSampleRate);

    for (const auto& entry : entries_)
        if (entry.key == key)
            return entry.audio;

    const auto probed = probe (utf8Path);

    if (! probed.ok)
    {
        error = probed.error;
        return nullptr;
    }

    if (probed.seconds > kMaxSeconds)
    {
        error = "file is longer than 10 minutes; Phase 0 keeps clips in memory";
        return nullptr;
    }

    if (probed.frames > static_cast<std::int64_t> (std::numeric_limits<int>::max()))
    {
        error = "file is too large to load";
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats_->manager.createReaderFor (fileFromUtf8 (utf8Path)));

    if (reader == nullptr)
    {
        error = "could not open audio file";
        return nullptr;
    }

    const auto frames = static_cast<int> (reader->lengthInSamples);
    std::vector<float> left (static_cast<std::size_t> (frames));
    std::vector<float> right (static_cast<std::size_t> (frames));
    float* channels[] = { left.data(), right.data() };
    const int channelsToRead = reader->numChannels >= 2 ? 2 : 1;

    if (! reader->read (channels, channelsToRead, 0, frames))
    {
        error = "failed while reading samples";
        return nullptr;
    }

    if (channelsToRead == 1)
        right = left;

    auto audio = std::make_shared<ClipAudio>();
    audio->sampleRate = targetSampleRate;
    resampleChannel (left, reader->sampleRate, audio->left, targetSampleRate);
    resampleChannel (right, reader->sampleRate, audio->right, targetSampleRate);

    if (audio->right.size() != audio->left.size())
        audio->right.resize (audio->left.size(), 0.0f);

    entries_.push_back (Entry { key, audio });
    return audio;
}

void ClipLibrary::clear()
{
    entries_.clear();
}

} // namespace waveform
