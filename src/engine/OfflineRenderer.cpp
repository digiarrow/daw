#include "OfflineRenderer.hpp"

#include "RenderGraph.hpp"
#include "waveform/paths.hpp"
#include "waveform/serialize.hpp"
#include "waveform/synth.hpp"
#include "waveform/timing.hpp"
#include "waveform/transport.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace waveform
{
namespace
{

juce::File fileFromUtf8 (const std::string& path)
{
    return juce::File (juce::String::fromUTF8 (path.data(), static_cast<int> (std::min<std::size_t> (path.size(), 1 << 20))));
}

int countAudioClips (const Project& project)
{
    int count = 0;

    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
            if (std::holds_alternative<AudioClip> (clip))
                ++count;

    return count;
}

} // namespace

OfflineRenderResult renderProjectFile (const OfflineRenderRequest& request)
{
    OfflineRenderResult result;

    if (! Timing::isValidSampleRate (request.sampleRate) || ! (request.seconds > 0.0))
    {
        result.exitCode = 2;
        result.message = "sample rate or duration is invalid";
        return result;
    }

    const auto projectFile = fileFromUtf8 (request.projectPath);

    if (! projectFile.existsAsFile())
    {
        result.exitCode = 3;
        result.message = "project file not found";
        return result;
    }

    std::string error;
    const auto text = projectFile.loadFileAsString().toStdString();
    auto project = projectFromJson (text, &error);

    if (! project.has_value())
    {
        result.exitCode = 3;
        result.message = error.empty() ? "could not read project" : error;
        return result;
    }

    ClipLibrary library;
    const auto projectPath = pathFromUtf8 (request.projectPath);
    auto built = buildGraph (*project, &projectPath, library, request.sampleRate);

    if (countAudioClips (*project) > 0 && built.loadedClips == 0 && ! request.testTone)
    {
        result.exitCode = 3;
        result.message = built.warnings.empty() ? "no audio clips could be loaded" : built.warnings.front();
        return result;
    }

    std::vector<SynthVoiceMemory> synthMemory (std::max<std::size_t> (1, built.graph->tracks.size()));
    SynthSchedule schedule;
    schedule.memories = synthMemory.data();
    schedule.memoryCount = static_cast<int> (synthMemory.size());

    Transport transport;
    transport.setSampleRate (request.sampleRate);
    transport.setTempoBpm (project->tempoBpm);
    transport.setTimeSignature (project->timeSignature);
    transport.setLoopQuarterNotes (project->loopStartQuarterNotes, project->loopEndQuarterNotes);
    transport.setLoopEnabled (project->loopEnabled);
    transport.play();

    const auto totalFrames = std::llround (request.seconds * request.sampleRate);
    constexpr int kBlock = 256;
    std::vector<float> left (static_cast<std::size_t> (kBlock));
    std::vector<float> right (static_cast<std::size_t> (kBlock));
    Oscillator tone;
    juce::AudioBuffer<float> buffer (2, kBlock);
    double peak = 0.0;

    auto output = fileFromUtf8 (request.outputPath);
    output.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (output);

    if (fileStream->failedToOpen())
    {
        result.exitCode = 3;
        result.message = "could not open output file";
        return result;
    }

    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (request.sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (16)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = format.createWriterFor (stream, options);

    if (writer == nullptr)
    {
        result.exitCode = 3;
        result.message = "could not create a WAV writer";
        return result;
    }

    std::int64_t rendered = 0;

    while (rendered < totalFrames)
    {
        const int frames = static_cast<int> (std::min<std::int64_t> (kBlock, totalFrames - rendered));
        int offset = 0;

        transport.advance (frames, [&] (std::int64_t start, int count)
        {
            renderLinearBlock (built.graph.get(), request.sampleRate, transport.tempoBpm(),
                               transport.timeSignature(), start, count,
                               left.data() + offset, right.data() + offset,
                               request.metronome, false, nullptr, nullptr, &schedule);
            offset += count;
        });

        if (request.testTone)
            renderTestTone (left.data(), right.data(), frames, request.sampleRate, 440.0, 0.15f, tone);

        for (int index = 0; index < frames; ++index)
        {
            peak = std::max (peak, static_cast<double> (std::abs (left[static_cast<std::size_t> (index)])));
            peak = std::max (peak, static_cast<double> (std::abs (right[static_cast<std::size_t> (index)])));
            left[static_cast<std::size_t> (index)] = std::clamp (left[static_cast<std::size_t> (index)], -1.0f, 1.0f);
            right[static_cast<std::size_t> (index)] = std::clamp (right[static_cast<std::size_t> (index)], -1.0f, 1.0f);
        }

        buffer.copyFrom (0, 0, left.data(), frames);
        buffer.copyFrom (1, 0, right.data(), frames);

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, frames))
        {
            result.exitCode = 3;
            result.message = "failed while writing samples";
            return result;
        }

        rendered += frames;
    }

    writer.reset();
    result.peak = peak;
    result.sampleRate = static_cast<int> (std::lround (request.sampleRate));
    result.frames = totalFrames;

    if (peak < 1.0e-4)
    {
        result.exitCode = 4;
        result.message = "render completed but the output is silent";
        return result;
    }

    result.ok = true;
    result.exitCode = 0;
    result.message = "rendered " + std::to_string (request.seconds) + "s at "
                   + std::to_string (result.sampleRate) + " Hz, peak " + std::to_string (peak);

    if (! built.warnings.empty())
        result.message += " (" + built.warnings.front() + ")";

    return result;
}

bool writeStereoWav (const std::string& utf8Path,
                     double sampleRate,
                     const float* left,
                     const float* right,
                     std::int64_t frames,
                     std::string& error)
{
    if (left == nullptr || frames <= 0 || ! Timing::isValidSampleRate (sampleRate))
    {
        error = "nothing to write";
        return false;
    }

    const auto output = juce::File (juce::String::fromUTF8 (utf8Path.data(), static_cast<int> (std::min<std::size_t> (utf8Path.size(), 1 << 20))));
    output.getParentDirectory().createDirectory();
    output.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (output);

    if (fileStream->failedToOpen())
    {
        error = "could not open " + utf8Path;
        return false;
    }

    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (16)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = format.createWriterFor (stream, options);

    if (writer == nullptr)
    {
        error = "could not create a WAV writer";
        return false;
    }

    constexpr int kBlock = 1024;
    juce::AudioBuffer<float> buffer (2, kBlock);
    std::int64_t rendered = 0;

    while (rendered < frames)
    {
        const int count = static_cast<int> (std::min<std::int64_t> (kBlock, frames - rendered));

        for (int index = 0; index < count; ++index)
        {
            const auto source = static_cast<std::size_t> (rendered + index);
            buffer.setSample (0, index, std::clamp (left[source], -1.0f, 1.0f));
            buffer.setSample (1, index, std::clamp (right != nullptr ? right[source] : left[source], -1.0f, 1.0f));
        }

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, count))
        {
            error = "failed while writing samples";
            return false;
        }

        rendered += count;
    }

    return true;
}

} // namespace waveform
