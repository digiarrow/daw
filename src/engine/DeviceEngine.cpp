#include "DeviceEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace waveform
{
namespace
{

void storeFloat (std::atomic<std::uint32_t>& slot, float value) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy (&bits, &value, sizeof (bits));
    slot.store (bits, std::memory_order_relaxed);
}

float loadFloat (const std::atomic<std::uint32_t>& slot) noexcept
{
    const auto bits = slot.load (std::memory_order_relaxed);
    float value = 0.0f;
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

float clampSample (float sample) noexcept
{
    return std::clamp (sample, -1.0f, 1.0f);
}

} // namespace

DeviceEngine::DeviceEngine() = default;

DeviceEngine::~DeviceEngine()
{
    shutdown();
}

juce::String DeviceEngine::initialise()
{
    if (initialised_)
        return {};

    const auto error = deviceManager_.initialiseWithDefaultDevices (0, 2);
    deviceManager_.addAudioCallback (this);
    initialised_ = true;
    return error;
}

void DeviceEngine::shutdown()
{
    if (! initialised_)
    {
        shutdownGraphs();
        return;
    }

    deviceManager_.removeAudioCallback (this);
    deviceManager_.closeAudioDevice();
    initialised_ = false;
    shutdownGraphs();
}

void DeviceEngine::setProject (const Project& project, const std::filesystem::path* projectFile)
{
    const double rate = currentSampleRate() > 0.0 ? currentSampleRate() : 48000.0;
    auto built = buildGraph (project, projectFile, library_, rate);
    warnings_ = std::move (built.warnings);
    publishGraph (std::move (built.graph));
    setTempo (project.tempoBpm);
    setTimeSignature (project.timeSignature.numerator, project.timeSignature.denominator);
    setLoop (project.loopEnabled, project.loopStartQuarterNotes, project.loopEndQuarterNotes);
}

bool DeviceEngine::play()
{
    return push (EngineCommand { EngineCommandType::Play });
}

bool DeviceEngine::pause()
{
    return push (EngineCommand { EngineCommandType::Pause });
}

bool DeviceEngine::stop()
{
    return push (EngineCommand { EngineCommandType::Stop });
}

bool DeviceEngine::setTempo (double bpm)
{
    EngineCommand command;
    command.type = EngineCommandType::SetTempoMilli;
    command.a = Transport::tempoToMilli (bpm);
    return push (command);
}

bool DeviceEngine::setTimeSignature (int numerator, int denominator)
{
    EngineCommand command;
    command.type = EngineCommandType::SetTimeSignature;
    command.a = numerator;
    command.b = denominator;
    return push (command);
}

bool DeviceEngine::setLoop (bool enabled, double startQuarterNotes, double endQuarterNotes)
{
    EngineCommand region;
    region.type = EngineCommandType::SetLoop;
    region.c = Transport::quarterNotesToMicro (startQuarterNotes);
    region.d = Transport::quarterNotesToMicro (endQuarterNotes);

    EngineCommand flag;
    flag.type = EngineCommandType::SetLoopEnabled;
    flag.a = enabled ? 1 : 0;

    return push (region) && push (flag);
}

bool DeviceEngine::setMetronome (bool enabled)
{
    EngineCommand command;
    command.type = EngineCommandType::SetMetronome;
    command.a = enabled ? 1 : 0;
    return push (command);
}

bool DeviceEngine::setTestTone (bool enabled)
{
    EngineCommand command;
    command.type = EngineCommandType::SetTestTone;
    command.a = enabled ? 1 : 0;
    return push (command);
}

void DeviceEngine::maintenance()
{
    reclaimGraphs();
}

PlaybackInfo DeviceEngine::playbackInfo() const
{
    PlaybackInfo info;
    info.positionSamples = positionSamples_.load (std::memory_order_relaxed);
    info.sampleRate = sampleRate_.load (std::memory_order_relaxed);
    info.bufferSize = bufferSize_.load (std::memory_order_relaxed);
    info.tempoBpm = Transport::tempoFromMilli (tempoMilli_.load (std::memory_order_relaxed));
    info.signatureNumerator = signatureNumerator_.load (std::memory_order_relaxed);
    info.signatureDenominator = signatureDenominator_.load (std::memory_order_relaxed);
    info.state = static_cast<TransportState> (state_.load (std::memory_order_relaxed));
    info.peakLeft = loadFloat (peakLeftBits_);
    info.peakRight = loadFloat (peakRightBits_);
    info.metronome = metronomeFlag_.load (std::memory_order_relaxed) != 0;
    info.testTone = testToneFlag_.load (std::memory_order_relaxed) != 0;
    info.deviceGeneration = deviceGeneration_.load (std::memory_order_relaxed);
    info.structureOverflow = structureOverflow_.load (std::memory_order_relaxed) != 0;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
        info.deviceName = device->getName();

    return info;
}

int DeviceEngine::deviceGeneration() const noexcept
{
    return deviceGeneration_.load (std::memory_order_relaxed);
}

double DeviceEngine::currentSampleRate() const noexcept
{
    return static_cast<double> (sampleRate_.load (std::memory_order_relaxed));
}

AudioFileInfo DeviceEngine::probeFile (const std::string& utf8Path) const
{
    return library_.probe (utf8Path);
}

bool DeviceEngine::push (EngineCommand command)
{
    // With no device open there is no callback to drain the queue, so apply in place.
    // Public methods run on the message thread, which is also where the device is opened.
    if (deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        applyLocal (command);
        return true;
    }

    return commands_.push (command);
}

void DeviceEngine::applyLocal (const EngineCommand& command) noexcept
{
    switch (command.type)
    {
        case EngineCommandType::SetMetronome: metronome_ = command.a != 0; break;
        case EngineCommandType::SetTestTone:  testToneEnabled_ = command.a != 0; break;
        default:                              transport_.apply (command); break;
    }

    publishInfo();
}

void DeviceEngine::drainCommands() noexcept
{
    EngineCommand command;

    while (commands_.pop (command))
    {
        switch (command.type)
        {
            case EngineCommandType::SetMetronome:
                metronome_ = command.a != 0;
                break;
            case EngineCommandType::SetTestTone:
                testToneEnabled_ = command.a != 0;
                break;
            default:
                transport_.apply (command);
                break;
        }
    }
}

void DeviceEngine::publishInfo() noexcept
{
    positionSamples_.store (transport_.positionSamples(), std::memory_order_relaxed);
    tempoMilli_.store (Transport::tempoToMilli (transport_.tempoBpm()), std::memory_order_relaxed);
    signatureNumerator_.store (transport_.timeSignature().numerator, std::memory_order_relaxed);
    signatureDenominator_.store (transport_.timeSignature().denominator, std::memory_order_relaxed);
    state_.store (static_cast<std::int32_t> (transport_.state()), std::memory_order_relaxed);
    metronomeFlag_.store (metronome_ ? 1 : 0, std::memory_order_relaxed);
    testToneFlag_.store (testToneEnabled_ ? 1 : 0, std::memory_order_relaxed);

    if (transport_.sampleRate() > 0.0)
        sampleRate_.store (static_cast<int> (std::lround (transport_.sampleRate())), std::memory_order_relaxed);
}

void DeviceEngine::publishGraph (std::shared_ptr<const RenderGraph> graph)
{
    auto holder = std::make_unique<GraphHolder> (std::move (graph));
    auto* raw = holder.release();
    auto* previous = current_.exchange (raw, std::memory_order_acq_rel);

    if (previous != nullptr)
        retired_.emplace_back (previous);

    reclaimGraphs();
}

const RenderGraph* DeviceEngine::acquireGraph() noexcept
{
    // Mark the critical section before loading so the message thread cannot
    // delete a holder that this callback is about to use. JUCE calls this on one thread.
    inUse_.store (kBusy, std::memory_order_release);
    auto* holder = current_.load (std::memory_order_acquire);
    inUse_.store (reinterpret_cast<std::intptr_t> (holder), std::memory_order_release);

    if (holder == nullptr || *holder == nullptr)
        return nullptr;

    return holder->get();
}

void DeviceEngine::releaseGraph() noexcept
{
    inUse_.store (0, std::memory_order_release);
}

void DeviceEngine::reclaimGraphs()
{
    const auto inUse = inUse_.load (std::memory_order_acquire);
    auto* current = current_.load (std::memory_order_acquire);
    std::vector<std::unique_ptr<GraphHolder>> keep;
    keep.reserve (retired_.size());

    for (auto& holder : retired_)
    {
        const auto address = reinterpret_cast<std::intptr_t> (holder.get());
        const bool stillVisible = holder.get() == current || inUse == kBusy || inUse == address;

        if (stillVisible)
            keep.push_back (std::move (holder));
    }

    retired_ = std::move (keep);
}

void DeviceEngine::shutdownGraphs()
{
    auto* current = current_.exchange (nullptr, std::memory_order_acq_rel);

    if (current != nullptr)
        retired_.emplace_back (current);

    retired_.clear();
}

void DeviceEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
    transport_.setSampleRate (rate > 0.0 ? rate : 48000.0);

    const int scratch = std::max (block, 4096);
    scratchLeft_.assign (static_cast<std::size_t> (scratch), 0.0f);
    scratchRight_.assign (static_cast<std::size_t> (scratch), 0.0f);

    sampleRate_.store (static_cast<int> (std::lround (transport_.sampleRate())), std::memory_order_relaxed);
    bufferSize_.store (block, std::memory_order_relaxed);
    deviceGeneration_.fetch_add (1, std::memory_order_release);
}

void DeviceEngine::audioDeviceStopped()
{
    deviceGeneration_.fetch_add (1, std::memory_order_release);
}

void DeviceEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (inputChannelData, numInputChannels);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    const RenderGraph* graph = acquireGraph();

    struct Release
    {
        DeviceEngine& engine;
        ~Release() { engine.releaseGraph(); }
    } release { *this };

    if (numOutputChannels <= 0 || numSamples <= 0 || outputChannelData[0] == nullptr)
    {
        drainCommands();
        publishInfo();
        return;
    }

    drainCommands();

    const bool directStereo = numOutputChannels >= 2 && outputChannelData[1] != nullptr;
    const int scratchFrames = static_cast<int> (scratchLeft_.size());

    auto renderFrames = [&] (int frames, float* left, float* right)
    {
        const double rate = transport_.sampleRate();
        const double tempo = transport_.tempoBpm();
        const auto signature = transport_.timeSignature();

        if (transport_.state() == TransportState::Playing)
        {
            int offset = 0;
            transport_.advance (frames, [&] (std::int64_t start, int count)
            {
                renderLinearBlock (graph, rate, tempo, signature, start, count,
                                   left + offset, right + offset,
                                   metronome_, false, nullptr, &structureOverflow_);
                offset += count;
            });

            if (testToneEnabled_)
                renderTestTone (left, right, frames, rate, 440.0, 0.15f, testTone_);
        }
        else
        {
            renderLinearBlock (nullptr, rate, tempo, signature, transport_.positionSamples(), frames,
                               left, right, false, testToneEnabled_, &testTone_, nullptr);
        }

        float peakLeft = 0.0f;
        float peakRight = 0.0f;

        for (int index = 0; index < frames; ++index)
        {
            left[index] = clampSample (left[index]);
            right[index] = clampSample (right[index]);
            peakLeft = std::max (peakLeft, std::abs (left[index]));
            peakRight = std::max (peakRight, std::abs (right[index]));
        }

        return std::pair<float, float> (peakLeft, peakRight);
    };

    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    int rendered = 0;

    while (rendered < numSamples)
    {
        const int frames = directStereo ? (numSamples - rendered)
                                        : std::min (numSamples - rendered, std::max (scratchFrames, 0));

        if (frames <= 0)
            break;

        float* left = nullptr;
        float* right = nullptr;

        if (directStereo)
        {
            left = outputChannelData[0] + rendered;
            right = outputChannelData[1] + rendered;
        }
        else
        {
            if (scratchFrames <= 0)
                break;

            left = scratchLeft_.data();
            right = scratchRight_.data();
        }

        const auto peaks = renderFrames (frames, left, right);
        peakLeft = std::max (peakLeft, peaks.first);
        peakRight = std::max (peakRight, peaks.second);

        if (! directStereo)
        {
            auto* destination = outputChannelData[0] + rendered;

            for (int index = 0; index < frames; ++index)
                destination[index] = 0.5f * (left[index] + right[index]);
        }

        rendered += frames;

        if (directStereo)
            break;
    }

    storeFloat (peakLeftBits_, peakLeft);
    storeFloat (peakRightBits_, peakRight);
    publishInfo();
}

} // namespace waveform
