#include "PluginHost.hpp"

#include "waveform/later.hpp"

#include <algorithm>
#include <cstring>

namespace waveform
{

PluginHost::PluginHost()
{
    formats_.addFormat (std::make_unique<juce::VST3PluginFormat>());

    for (auto& binding : bindings_)
        binding.store (nullptr, std::memory_order_relaxed);
}

PluginHost::~PluginHost()
{
    for (int track = 0; track < kTracks; ++track)
        publish (track, nullptr, {});

    busy_.store (0, std::memory_order_release);
    reclaim();
}

int PluginHost::scan()
{
    auto* format = formats_.getFormat (0);

    if (format == nullptr)
        return known_.getNumTypes();

    const auto locations = format->getDefaultLocationsToSearch();
    juce::FileSearchPath narrowed;

    for (int index = 0; index < locations.getNumPaths(); ++index)
    {
        const auto path = locations[index];

        if (path.isDirectory())
            narrowed.add (path);
    }

    if (narrowed.getNumPaths() == 0)
        narrowed = locations;

    juce::PluginDirectoryScanner scanner (known_, *format, narrowed, true, juce::File(), true);
    juce::String name;

    while (scanner.scanNextFile (true, name))
    {
    }

    return known_.getNumTypes();
}

void PluginHost::prepare (double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockSize_ = std::max (64, blockSize);
    tempLeft_.assign (static_cast<std::size_t> (std::max (blockSize_, kMaxDelay)), 0.0f);
    tempRight_.assign (tempLeft_.size(), 0.0f);

    for (int track = 0; track < kTracks; ++track)
    {
        historyLeft_[static_cast<std::size_t> (track)].assign (static_cast<std::size_t> (kMaxDelay), 0.0f);
        historyRight_[static_cast<std::size_t> (track)].assign (static_cast<std::size_t> (kMaxDelay), 0.0f);
        cursorLeft_[static_cast<std::size_t> (track)] = 0;
        cursorRight_[static_cast<std::size_t> (track)] = 0;
        appliedDelay_[static_cast<std::size_t> (track)] = 0;

        if (auto* binding = bindings_[static_cast<std::size_t> (track)].load (std::memory_order_acquire))
            if (binding->instance != nullptr)
                binding->instance->prepareToPlay (sampleRate_, blockSize_);
    }
}

juce::String PluginHost::load (int track, const juce::PluginDescription& description, double sampleRate, int blockSize)
{
    if (track < 0 || track >= kTracks)
        return "Track index is out of range.";

    juce::String error;
    auto instance = formats_.createPluginInstance (description,
                                                   sampleRate > 0.0 ? sampleRate : sampleRate_,
                                                   blockSize > 0 ? blockSize : blockSize_,
                                                   error);

    if (instance == nullptr)
        return error.isNotEmpty() ? error : "The plug-in did not load.";

    instance->enableAllBuses();
    instance->prepareToPlay (sampleRate > 0.0 ? sampleRate : sampleRate_, blockSize > 0 ? blockSize : blockSize_);
    publish (track, std::move (instance), description.createIdentifierString());
    return {};
}

void PluginHost::clear (int track)
{
    if (track >= 0 && track < kTracks)
        publish (track, nullptr, {});
}

void PluginHost::clearAll()
{
    for (int track = 0; track < kTracks; ++track)
        clear (track);
}

void PluginHost::publish (int track, std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& identifier)
{
    auto* binding = new Binding;
    binding->instance = instance.release();
    binding->latency = binding->instance != nullptr ? std::max (0, binding->instance->getLatencySamples()) : 0;
    binding->identifier = identifier;
    auto* previous = bindings_[static_cast<std::size_t> (track)].exchange (binding, std::memory_order_acq_rel);

    if (previous != nullptr)
        retired_.push_back (previous);

    reclaim();
}

juce::AudioPluginInstance* PluginHost::instance (int track) noexcept
{
    if (track < 0 || track >= kTracks)
        return nullptr;

    auto* binding = bindings_[static_cast<std::size_t> (track)].load (std::memory_order_acquire);
    return binding != nullptr ? binding->instance : nullptr;
}

juce::String PluginHost::identifier (int track) const
{
    if (track < 0 || track >= kTracks)
        return {};

    auto* binding = bindings_[static_cast<std::size_t> (track)].load (std::memory_order_acquire);
    return binding != nullptr ? binding->identifier : juce::String();
}

int PluginHost::latency (int track) const noexcept
{
    if (track < 0 || track >= kTracks)
        return 0;

    auto* binding = bindings_[static_cast<std::size_t> (track)].load (std::memory_order_acquire);
    return binding != nullptr ? binding->latency : 0;
}

void PluginHost::beginBlock() noexcept
{
    int maximum = 0;

    for (int track = 0; track < kTracks; ++track)
        maximum = std::max (maximum, latency (track));

    maxLatency_ = std::min (maximum, kMaxDelay);
}

bool PluginHost::anyLoaded() const noexcept
{
    for (const auto& binding : bindings_)
        if (auto* slot = binding.load (std::memory_order_acquire); slot != nullptr && slot->instance != nullptr)
            return true;

    return false;
}

void PluginHost::processTrack (int track, float* left, float* right, int frames)
{
    if (track < 0 || track >= kTracks || left == nullptr || right == nullptr || frames <= 0)
        return;

    int ownLatency = 0;

    if (auto* binding = bindings_[static_cast<std::size_t> (track)].load (std::memory_order_acquire))
    {
        ownLatency = binding->latency;

        if (binding->instance != nullptr)
        {
            const int channels = std::clamp (binding->instance->getTotalNumInputChannels(), 0, 2);

            if (channels > 0)
            {
                float* data[2] = { left, right };
                juce::AudioBuffer<float> buffer (data, channels, frames);
                juce::MidiBuffer midi;
                binding->instance->processBlock (buffer, midi);

                if (channels == 1)
                    std::copy (left, left + frames, right);
            }
        }
    }

    const int delay = std::clamp (maxLatency_ - ownLatency, 0, kMaxDelay);

    if (delay <= 0 || static_cast<int> (tempLeft_.size()) < frames)
        return;

    auto& historyLeft = historyLeft_[static_cast<std::size_t> (track)];
    auto& historyRight = historyRight_[static_cast<std::size_t> (track)];

    if (static_cast<int> (historyLeft.size()) < kMaxDelay)
        return;

    if (appliedDelay_[static_cast<std::size_t> (track)] != delay)
    {
        std::fill (historyLeft.begin(), historyLeft.begin() + kMaxDelay, 0.0f);
        std::fill (historyRight.begin(), historyRight.begin() + kMaxDelay, 0.0f);
        cursorLeft_[static_cast<std::size_t> (track)] = 0;
        cursorRight_[static_cast<std::size_t> (track)] = 0;
        appliedDelay_[static_cast<std::size_t> (track)] = delay;
    }

    compensateDelay (left, tempLeft_.data(), frames, historyLeft.data(), delay, cursorLeft_[static_cast<std::size_t> (track)]);
    compensateDelay (right, tempRight_.data(), frames, historyRight.data(), delay, cursorRight_[static_cast<std::size_t> (track)]);
    std::copy (tempLeft_.begin(), tempLeft_.begin() + frames, left);
    std::copy (tempRight_.begin(), tempRight_.begin() + frames, right);
}

void PluginHost::reclaim() noexcept
{
    if (busy_.load (std::memory_order_acquire) != 0)
        return;

    for (auto* binding : retired_)
    {
        if (binding != nullptr && binding->instance != nullptr)
        {
            binding->instance->releaseResources();
            delete binding->instance;
        }

        delete binding;
    }

    retired_.clear();
}

juce::String PluginHost::stateBase64 (int track) const
{
    auto* plugin = const_cast<PluginHost*> (this)->instance (track);

    if (plugin == nullptr)
        return {};

    juce::MemoryBlock block;
    plugin->getStateInformation (block);
    return block.isEmpty() ? juce::String() : juce::Base64::toBase64 (block.getData(), block.getSize());
}

void PluginHost::restoreState (int track, const juce::String& base64)
{
    auto* plugin = instance (track);

    if (plugin == nullptr || base64.isEmpty())
        return;

    juce::MemoryOutputStream stream;

    if (! juce::Base64::convertFromBase64 (stream, base64))
        return;

    plugin->setStateInformation (stream.getData(), static_cast<int> (stream.getDataSize()));
}

} // namespace waveform
