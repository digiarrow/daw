#pragma once

#include "PluginHost.hpp"
#include "RenderGraph.hpp"
#include "waveform/queue.hpp"
#include "waveform/session.hpp"
#include "waveform/synth.hpp"
#include "waveform/transport.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
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
    float cpu = 0.0f;
    double latencyMilliseconds = 0.0;
    bool recording = false;
    bool countingIn = false;
    int recordEpoch = 0;
    juce::String deviceName;
};

struct MidiTakeNote
{
    int note = 60;
    int velocity = 100;
    int channel = 1;
    double startQuarterNotes = 0.0;
    double lengthQuarterNotes = 0.25;
};

struct MidiTake
{
    bool valid = false;
    int trackIndex = -1;
    double startQuarterNotes = 0.0;
    std::vector<MidiTakeNote> notes;
};

struct AudioTake
{
    bool valid = false;
    int trackIndex = -1;
    double startQuarterNotes = 0.0;
    double sampleRate = 0.0;
    std::vector<float> left;
    std::vector<float> right;
};

/// Opens the default output through juce::AudioDeviceManager and renders the project.
///
/// Threading:
///   * The message thread calls the public methods, loads files, and publishes graphs.
///   * The audio thread is the only thread that advances Transport or reads a graph's samples.
///   * Commands cross the boundary through SpscQueue. Graphs cross through an atomic pointer
///     with deferred reclamation (see DeviceEngine.cpp). The audio callback does not allocate,
///     lock, log, or touch the file system.
class DeviceEngine : public juce::AudioIODeviceCallback,
                      private juce::MidiInputCallback
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
    bool seekQuarterNotes (double quarterNotes);

    /// `scene` of -1 returns the track to the arrangement. `quantize` is 0 now, 1 next beat, 2 next bar.
    bool launchSession (int trackIndex, int scene, int quantize);
    [[nodiscard]] int sessionScene (int trackIndex) const noexcept;
    [[nodiscard]] int sessionPending (int trackIndex) const noexcept;
    [[nodiscard]] int controlEpoch() const noexcept;
    [[nodiscard]] int lastController() const noexcept;
    [[nodiscard]] int lastControllerValue() const noexcept;

    /// Enables one MIDI input. An empty id disables input. Message thread only.
    void setMidiInput (const juce::String& deviceIdentifier);
    void setMonitorTrack (int trackIndex) noexcept;

    /// Allocates the audio-take buffers before startRecording. Message thread only.
    void prepareAudioCapture (double maxSeconds);
    bool startRecording (int trackIndex, int countInBars, bool midi);

    [[nodiscard]] int recordEpoch() const noexcept;
    [[nodiscard]] MidiTake copyMidiTake() const;
    [[nodiscard]] AudioTake copyAudioTake() const;

    /// Drops graphs the audio thread has finished reading. Call from the message thread.
    void maintenance();

    [[nodiscard]] PlaybackInfo playbackInfo() const;
    [[nodiscard]] std::vector<std::string> warnings() const { return warnings_; }
    [[nodiscard]] int deviceGeneration() const noexcept;
    [[nodiscard]] double currentSampleRate() const noexcept;
    [[nodiscard]] float trackPeak (int trackIndex) const noexcept;

    [[nodiscard]] AudioFileInfo probeFile (const std::string& utf8Path) const;

    PluginHost& plugins() noexcept { return plugins_; }
    ClipLibrary& library() noexcept { return library_; }
    [[nodiscard]] const PluginHost& plugins() const noexcept { return plugins_; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    bool push (EngineCommand command);
    void applyLocal (const EngineCommand& command) noexcept;
    void drainCommands() noexcept;
    void publishInfo() noexcept;
    void beginRecordingNow() noexcept;
    void finishRecording() noexcept;
    void drainLiveMidi() noexcept;
    void applyLiveMessage (const juce::MidiMessage& message, int sampleOffset) noexcept;
    void closeOpenMidiNotes() noexcept;
    int collectLiveNotes (SoundingNote* destination, int capacity) const noexcept;
    void advanceLiveNotes (int frames) noexcept;
    void applySession (const EngineCommand& command) noexcept;
    void fireSession (std::int64_t timelineSample) noexcept;
    [[nodiscard]] std::int64_t nextSessionSample (std::int64_t after) const noexcept;
    void publishSession() noexcept;

    void publishGraph (std::shared_ptr<const RenderGraph> graph);
    const RenderGraph* acquireGraph() noexcept;
    void releaseGraph() noexcept;
    void reclaimGraphs();
    void shutdownGraphs();

    using GraphHolder = std::shared_ptr<const RenderGraph>;

    juce::AudioDeviceManager deviceManager_;
    PluginHost plugins_;
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
    std::vector<float> routeStorage_;
    std::unique_ptr<EffectState[]> effectArena_;
    std::unique_ptr<EffectState[]> rackStates_;
    std::vector<float> rackScratch_;
    BlockScratch blockScratch_ {};
    int scratchFrames_ = 0;

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
    std::atomic<std::uint32_t> cpuBits_ { 0 };
    std::array<std::atomic<std::uint32_t>, 64> trackPeakBits_ {};
    std::atomic<int> latencySamples_ { 0 };
    std::atomic<int> metronomeFlag_ { 0 };
    std::atomic<int> testToneFlag_ { 0 };
    std::atomic<int> deviceGeneration_ { 0 };
    std::atomic<int> structureOverflow_ { 0 };
    std::atomic<int> monitorTrack_ { -1 };
    std::atomic<int> controlEpoch_ { 0 };
    std::atomic<int> lastController_ { -1 };
    std::atomic<int> lastControllerValue_ { 0 };
    std::atomic<int> recordingFlag_ { 0 };
    std::atomic<int> countingFlag_ { 0 };
    std::atomic<int> recordEpoch_ { 0 };
    std::atomic<int> publishedFrames_ { 0 };
    std::atomic<int> publishedMidiCount_ { 0 };
    std::atomic<std::int32_t> publishedStartMicro_ { 0 };
    std::atomic<int> publishedTrack_ { -1 };
    std::atomic<int> publishedMidi_ { 0 };

    struct IncomingMidi
    {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        std::uint8_t channel = 1;
        int offset = 0;
    };

    struct LiveSlot
    {
        bool down = false;
        bool releasing = false;
        bool written = false;
        int velocity = 0;
        int age = 0;
        int releaseAge = 0;
        std::int32_t startMicro = 0;
    };

    struct RecordedMidiSlot
    {
        int note = 0;
        int velocity = 0;
        int channel = 1;
        std::int32_t startMicro = 0;
        std::int32_t endMicro = -1;
    };

    static constexpr int kMaxRecordedNotes = 2048;

    SpscQueue<IncomingMidi, 256> midiQueue_;
    juce::MidiMessageCollector midiCollector_;
    std::vector<float> longDelay_;
    int longDelayCapacity_ = 0;
    juce::String activeMidi_;
    std::array<SynthVoiceMemory, 64> synthMemory_ {};
    std::array<SessionVoice, 64> session_ {};
    std::array<double, 64> lfoPhase_ {};
    std::array<std::atomic<int>, 64> sessionActive_ {};
    std::array<std::atomic<int>, 64> sessionPending_ {};
    LiveSlot live_[128] {};
    RecordedMidiSlot midiSlots_[kMaxRecordedNotes] {};
    int midiCount_ = 0;
    std::vector<float> recordLeft_;
    std::vector<float> recordRight_;
    int recordCapacity_ = 0;
    int recordWrite_ = 0;
    bool recording_ = false;
    bool countingIn_ = false;
    bool recordMidi_ = false;
    int recordTrack_ = -1;
    int countInRemaining_ = 0;
    int countInElapsed_ = 0;
    double recordStartQuarter_ = 0.0;

    // Graph publication. `current_` is exchanged by the message thread and loaded by the audio thread.
    // `inUse_` is 0 outside the callback, 1 while the pointer is being loaded, otherwise the holder address.
    static constexpr std::intptr_t kBusy = 1;
    std::atomic<GraphHolder*> current_ { nullptr };
    std::atomic<std::intptr_t> inUse_ { 0 };
    std::vector<std::unique_ptr<GraphHolder>> retired_;
};

} // namespace waveform
