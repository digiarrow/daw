#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace waveform
{

/// One VST3 insert per track. The audio thread only reads a published binding.
class PluginHost
{
public:
    static constexpr int kTracks = 64;
    static constexpr int kMaxDelay = 16384;

    PluginHost();
    ~PluginHost();

    PluginHost (const PluginHost&) = delete;
    PluginHost& operator= (const PluginHost&) = delete;

    juce::AudioPluginFormatManager& formats() noexcept { return formats_; }
    juce::KnownPluginList& known() noexcept { return known_; }

    /// Scans the platform's default VST3 folders. Message thread only.
    int scan();

    /// Replaces the insert on `track`. Empty error string means success. Message thread only.
    juce::String load (int track, const juce::PluginDescription& description, double sampleRate, int blockSize);
    void clear (int track);
    void clearAll();

    [[nodiscard]] juce::AudioPluginInstance* instance (int track) noexcept;
    [[nodiscard]] juce::String identifier (int track) const;
    [[nodiscard]] int latency (int track) const noexcept;
    [[nodiscard]] int maximumLatency() const noexcept;

    /// Audio thread. Processes the insert, then delays the track so every track matches the slowest plug-in.
    void processTrack (int track, float* left, float* right, int frames);
    [[nodiscard]] bool anyLoaded() const noexcept;
    void beginBlock() noexcept;

    void prepare (double sampleRate, int blockSize);
    void reclaim() noexcept;
    void setBusy (bool busy) noexcept { busy_.store (busy ? 1 : 0, std::memory_order_release); }

    [[nodiscard]] juce::String stateBase64 (int track) const;
    void restoreState (int track, const juce::String& base64);

private:
    struct Binding
    {
        juce::AudioPluginInstance* instance = nullptr;
        int latency = 0;
        juce::String identifier;
    };

    void publish (int track, std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& identifier);

    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList known_;
    std::array<std::atomic<Binding*>, kTracks> bindings_ {};
    std::vector<Binding*> retired_;
    std::atomic<int> busy_ { 0 };
    int maxLatency_ = 0;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;

    std::array<std::vector<float>, kTracks> historyLeft_;
    std::array<std::vector<float>, kTracks> historyRight_;
    std::array<int, kTracks> cursorLeft_ {};
    std::array<int, kTracks> cursorRight_ {};
    std::array<int, kTracks> appliedDelay_ {};
    std::vector<float> tempLeft_;
    std::vector<float> tempRight_;
};

} // namespace waveform
