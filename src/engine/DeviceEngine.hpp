#pragma once

#include "RenderGraph.hpp"
#include "waveform/queue.hpp"
#include "waveform/transport.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace waveform
{

struct PlaybackInfo
{
    std::int64_t positionSamples = 0;
    int sampleRate = 0;
    int bufferSize = 0;
    double tempoBpm = 120.0;
    int signatureNumerator = 4;
    int signatureDenominator = 4;
    TransportState state = TransportState::Stopped;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    bool metronome = false;
    bool testTone = false;
    int deviceGeneration = 0;
    bool structureOverflow = false;
    juce::String deviceName;
};

/// Opens the default output through juce::AudioDeviceManager and renders the project.
///
/// Threading:
///   * The message thread calls the public methods, loads files, and publishes graphs.
///   * The audio thread is the only thread that advances Transport or reads a graph's samples.
///   * Commands cross the boundary through SpscQueue. Graphs cross through an atomic pointer
///     with deferred reclamation (see DeviceEngine.cpp). The audio callback does not allocate,
///     lock, log, or touch the file system.
class DeviceEngine : public juce::AudioIODeviceCallback
{
public:
    DeviceEngine();
    ~DeviceEngine() override;

    DeviceEngine (const DeviceEngine&) = delete;
    DeviceEngine& operator= (const DeviceEngine&) = delete;

    juce::AudioDeviceManager& deviceManager() noexcept { return deviceManager_; }

    /// Opens the default output device (CoreAudio, WASAPI, or ALSA depending on the host).
    /// The returned string is empty on success.
    juce::String initialise();
    void shutdown();

    /// Rebuilds the render graph from the document. Safe to call often; audio data is cached.
    /// `projectFile` resolves relative clip paths and may be null.
    void setProject (const Project& project, const std::filesystem::path* projectFile);

    bool play();
    bool pause();
    bool stop();
    bool setTempo (double bpm);
    bool setTimeSignature (int numerator, int denominator);
    bool setLoop (bool enabled, double startQuarterNotes, double endQuarterNotes);
    bool setMetronome (bool enabled);
    bool setTestTone (bool enabled);

    /// Drops graphs the audio thread has finished reading. Call from the message thread.
    void maintenance();

    [[nodiscard]] PlaybackInfo playbackInfo() const;
    [[nodiscard]] std::vector<std::string> warnings() const { return warnings_; }
    [[nodiscard]] int deviceGeneration() const noexcept;
    [[nodiscard]] double currentSampleRate() const noexcept;

    [[nodiscard]] AudioFileInfo probeFile (const std::string& utf8Path) const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    bool push (EngineCommand command);
    void applyLocal (const EngineCommand& command) noexcept;
    void drainCommands() noexcept;
    void publishInfo() noexcept;

    void publishGraph (std::shared_ptr<const RenderGraph> graph);
    const RenderGraph* acquireGraph() noexcept;
    void releaseGraph() noexcept;
    void reclaimGraphs();
    void shutdownGraphs();

    using GraphHolder = std::shared_ptr<const RenderGraph>;

    juce::AudioDeviceManager deviceManager_;
    ClipLibrary library_;
    SpscQueue<EngineCommand, 128> commands_;
    std::vector<std::string> warnings_;
    bool initialised_ = false;

    // --- audio thread state. Not touched from the message thread. ---
    Transport transport_;
    Oscillator testTone_;
    bool metronome_ = false;
    bool testToneEnabled_ = false;
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;

    // --- published to the message thread ---
    std::atomic<std::int64_t> positionSamples_ { 0 };
    std::atomic<int> sampleRate_ { 0 };
    std::atomic<int> bufferSize_ { 0 };
    std::atomic<std::int32_t> tempoMilli_ { 120000 };
    std::atomic<int> signatureNumerator_ { 4 };
    std::atomic<int> signatureDenominator_ { 4 };
    std::atomic<std::int32_t> state_ { static_cast<std::int32_t> (TransportState::Stopped) };
    std::atomic<std::uint32_t> peakLeftBits_ { 0 };
    std::atomic<std::uint32_t> peakRightBits_ { 0 };
    std::atomic<int> metronomeFlag_ { 0 };
    std::atomic<int> testToneFlag_ { 0 };
    std::atomic<int> deviceGeneration_ { 0 };
    std::atomic<int> structureOverflow_ { 0 };

    // Graph publication. `current_` is exchanged by the message thread and loaded by the audio thread.
    // `inUse_` is 0 outside the callback, 1 while the pointer is being loaded, otherwise the holder address.
    static constexpr std::intptr_t kBusy = 1;
    std::atomic<GraphHolder*> current_ { nullptr };
    std::atomic<std::intptr_t> inUse_ { 0 };
    std::vector<std::unique_ptr<GraphHolder>> retired_;
};

} // namespace waveform
