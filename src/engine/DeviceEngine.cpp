#include "DeviceEngine.hpp"

#include "waveform/automation.hpp"
#include "waveform/midi.hpp"
#include "waveform/mix.hpp"
#include "waveform/session.hpp"
#include "waveform/timing.hpp"

#include <limits>

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

DeviceEngine::DeviceEngine()
{
    for (auto& active : sessionActive_)
        active.store (-1, std::memory_order_relaxed);

    for (auto& pending : sessionPending_)
        pending.store (-2, std::memory_order_relaxed);
}

DeviceEngine::~DeviceEngine()
{
    shutdown();
}

juce::String DeviceEngine::initialise()
{
    if (initialised_)
        return {};

    auto error = deviceManager_.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager_.initialiseWithDefaultDevices (0, 2);

    deviceManager_.addAudioCallback (this);
    initialised_ = true;
    return error;
}

void DeviceEngine::shutdown()
{
    if (activeMidi_.isNotEmpty())
    {
        deviceManager_.removeMidiInputDeviceCallback (activeMidi_, this);
        activeMidi_.clear();
    }

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

bool DeviceEngine::seekQuarterNotes (double quarterNotes)
{
    const double rate = currentSampleRate() > 0.0 ? currentSampleRate() : 48000.0;
    const double tempo = playbackInfo().tempoBpm;
    EngineCommand command;
    command.type = EngineCommandType::SeekSamples;
    command.c = Timing::samplesFromQuarterNotes (quarterNotes, rate, tempo);
    return push (command);
}

void DeviceEngine::setMidiInput (const juce::String& deviceIdentifier)
{
    if (activeMidi_.isNotEmpty())
        deviceManager_.removeMidiInputDeviceCallback (activeMidi_, this);

    activeMidi_.clear();

    if (deviceIdentifier.isEmpty())
        return;

    deviceManager_.setMidiInputDeviceEnabled (deviceIdentifier, true);
    deviceManager_.addMidiInputDeviceCallback (deviceIdentifier, this);
    activeMidi_ = deviceIdentifier;
}

void DeviceEngine::setMonitorTrack (int trackIndex) noexcept
{
    monitorTrack_.store (trackIndex, std::memory_order_relaxed);
}

void DeviceEngine::prepareAudioCapture (double maxSeconds)
{
    const int rate = std::max (sampleRate_.load (std::memory_order_relaxed), 48000);
    const int frames = static_cast<int> (std::clamp (maxSeconds, 1.0, 180.0) * rate);

    if (static_cast<int> (recordLeft_.size()) < frames)
    {
        recordLeft_.assign (static_cast<std::size_t> (frames), 0.0f);
        recordRight_.assign (static_cast<std::size_t> (frames), 0.0f);
    }

    recordCapacity_ = static_cast<int> (recordLeft_.size());
}

bool DeviceEngine::launchSession (int trackIndex, int scene, int quantize)
{
    if (trackIndex < 0 || trackIndex >= 64 || scene < -1 || scene >= kSessionScenes)
        return false;

    EngineCommand command;
    command.type = EngineCommandType::LaunchSession;
    command.a = trackIndex;
    command.b = scene;
    command.c = std::clamp (quantize, 0, 2);
    return push (command);
}

int DeviceEngine::sessionScene (int trackIndex) const noexcept
{
    if (trackIndex < 0 || trackIndex >= 64)
        return -1;

    return sessionActive_[static_cast<std::size_t> (trackIndex)].load (std::memory_order_relaxed);
}

int DeviceEngine::sessionPending (int trackIndex) const noexcept
{
    if (trackIndex < 0 || trackIndex >= 64)
        return -2;

    return sessionPending_[static_cast<std::size_t> (trackIndex)].load (std::memory_order_relaxed);
}

int DeviceEngine::controlEpoch() const noexcept
{
    return controlEpoch_.load (std::memory_order_acquire);
}

int DeviceEngine::lastController() const noexcept
{
    return lastController_.load (std::memory_order_relaxed);
}

int DeviceEngine::lastControllerValue() const noexcept
{
    return lastControllerValue_.load (std::memory_order_relaxed);
}

void DeviceEngine::applySession (const EngineCommand& command) noexcept
{
    if (command.type != EngineCommandType::LaunchSession)
        return;

    const int track = command.a;

    if (track < 0 || track >= 64 || command.b < -1 || command.b >= kSessionScenes)
        return;

    auto& voice = session_[static_cast<std::size_t> (track)];
    const int quantize = static_cast<int> (command.c);

    if (quantize == 0 || transport_.state() != TransportState::Playing || ! (transport_.sampleRate() > 0.0))
    {
        voice.pending = -2;
        voice.active = command.b;
        voice.playhead = 0;
        return;
    }

    const auto quant = quantize == 2 ? LaunchQuantize::Bar : LaunchQuantize::Beat;
    const double next = nextLaunchQuarter (transport_.musicalPosition().quarterNotes, quant, transport_.timeSignature());
    const auto sample = Timing::samplesFromQuarterNotes (next, transport_.sampleRate(), transport_.tempoBpm());

    if (sample <= transport_.positionSamples())
    {
        voice.pending = -2;
        voice.active = command.b;
        voice.playhead = 0;
        return;
    }

    voice.pending = command.b;
    voice.pendingSample = sample;
}

void DeviceEngine::fireSession (std::int64_t timelineSample) noexcept
{
    for (auto& voice : session_)
    {
        if (voice.pending != -2 && timelineSample >= voice.pendingSample)
        {
            voice.active = voice.pending;
            voice.playhead = 0;
            voice.pending = -2;
        }
    }
}

std::int64_t DeviceEngine::nextSessionSample (std::int64_t after) const noexcept
{
    auto next = std::numeric_limits<std::int64_t>::max();

    for (const auto& voice : session_)
        if (voice.pending != -2 && voice.pendingSample > after)
            next = std::min (next, voice.pendingSample);

    return next;
}

void DeviceEngine::publishSession() noexcept
{
    for (int index = 0; index < 64; ++index)
    {
        sessionActive_[static_cast<std::size_t> (index)].store (session_[static_cast<std::size_t> (index)].active, std::memory_order_relaxed);
        sessionPending_[static_cast<std::size_t> (index)].store (session_[static_cast<std::size_t> (index)].pending, std::memory_order_relaxed);
    }
}

bool DeviceEngine::startRecording (int trackIndex, int countInBars, bool midi)
{
    if (deviceManager_.getCurrentAudioDevice() == nullptr || trackIndex < 0)
        return false;

    EngineCommand command;
    command.type = EngineCommandType::StartRecord;
    command.a = trackIndex;
    command.b = std::clamp (countInBars, 0, 8);
    command.c = midi ? 1 : 0;
    return push (command);
}

int DeviceEngine::recordEpoch() const noexcept
{
    return recordEpoch_.load (std::memory_order_acquire);
}

MidiTake DeviceEngine::copyMidiTake() const
{
    MidiTake take;
    take.trackIndex = publishedTrack_.load (std::memory_order_acquire);
    take.startQuarterNotes = Transport::quarterNotesFromMicro (publishedStartMicro_.load (std::memory_order_acquire));
    const int count = std::min (publishedMidiCount_.load (std::memory_order_acquire), kMaxRecordedNotes);

    if (publishedMidi_.load (std::memory_order_acquire) == 0 || count <= 0)
        return take;

    for (int index = 0; index < count; ++index)
    {
        const auto& slot = midiSlots_[index];

        if (slot.endMicro <= slot.startMicro)
            continue;

        MidiTakeNote note;
        note.note = slot.note;
        note.velocity = slot.velocity;
        note.channel = slot.channel;
        note.startQuarterNotes = Transport::quarterNotesFromMicro (slot.startMicro);
        note.lengthQuarterNotes = Transport::quarterNotesFromMicro (slot.endMicro - slot.startMicro);
        take.notes.push_back (note);
    }

    take.valid = ! take.notes.empty();
    return take;
}

AudioTake DeviceEngine::copyAudioTake() const
{
    AudioTake take;

    if (publishedMidi_.load (std::memory_order_acquire) != 0)
        return take;

    const int frames = std::min (publishedFrames_.load (std::memory_order_acquire), recordCapacity_);

    if (frames <= 0)
        return take;

    take.valid = true;
    take.trackIndex = publishedTrack_.load (std::memory_order_acquire);
    take.startQuarterNotes = Transport::quarterNotesFromMicro (publishedStartMicro_.load (std::memory_order_acquire));
    take.sampleRate = currentSampleRate() > 0.0 ? currentSampleRate() : 48000.0;
    take.left.assign (recordLeft_.begin(), recordLeft_.begin() + frames);
    take.right.assign (recordRight_.begin(), recordRight_.begin() + frames);
    return take;
}

void DeviceEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isController())
    {
        lastController_.store (message.getControllerNumber(), std::memory_order_relaxed);
        lastControllerValue_.store (message.getControllerValue(), std::memory_order_relaxed);
        controlEpoch_.fetch_add (1, std::memory_order_release);
    }

    midiCollector_.addMessageToQueue (message);
}

void DeviceEngine::applyLiveMessage (const juce::MidiMessage& message, int sampleOffset) noexcept
{
    IncomingMidi event;

    if (message.isNoteOn())
    {
        event.note = static_cast<std::uint8_t> (message.getNoteNumber());
        event.velocity = static_cast<std::uint8_t> (std::max (1, static_cast<int> (message.getVelocity())));
    }
    else if (message.isNoteOff())
    {
        event.note = static_cast<std::uint8_t> (message.getNoteNumber());
        event.velocity = 0;
    }
    else
    {
        return;
    }

    event.channel = static_cast<std::uint8_t> (std::clamp (message.getChannel(), 1, 16));

    event.offset = std::max (0, sampleOffset);
    midiQueue_.push (event);
}

void DeviceEngine::maintenance()
{
    reclaimGraphs();
    plugins_.reclaim();
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
    info.recording = recordingFlag_.load (std::memory_order_relaxed) != 0;
    info.countingIn = countingFlag_.load (std::memory_order_relaxed) != 0;
    info.recordEpoch = recordEpoch_.load (std::memory_order_relaxed);
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
        case EngineCommandType::LaunchSession: applySession (command); break;
        default:                              transport_.apply (command); break;
    }

    publishInfo();
    publishSession();
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
            case EngineCommandType::Stop:
                transport_.apply (command);
                finishRecording();

                for (auto& voice : session_)
                    voice.playhead = 0;

                break;
            case EngineCommandType::LaunchSession:
                applySession (command);
                break;
            case EngineCommandType::StartRecord:
                if (! recording_ && ! countingIn_)
                {
                    recordTrack_ = command.a;
                    recordMidi_ = command.c != 0;
                    const int bars = std::clamp (command.b, 0, 8);
                    const auto samples = countInSamples (bars, transport_.timeSignature(), transport_.sampleRate(), transport_.tempoBpm());

                    if (samples > 0)
                    {
                        countingIn_ = true;
                        countInRemaining_ = static_cast<int> (std::min<std::int64_t> (samples, 60 * 48000));
                        countInElapsed_ = 0;
                    }
                    else
                    {
                        beginRecordingNow();
                    }
                }
                break;
            default:
                transport_.apply (command);
                break;
        }
    }
}

void DeviceEngine::beginRecordingNow() noexcept
{
    recording_ = true;
    countingIn_ = false;
    recordStartQuarter_ = transport_.musicalPosition().quarterNotes;
    recordWrite_ = 0;
    midiCount_ = 0;

    if (transport_.state() != TransportState::Playing)
        transport_.play();

    if (! recordMidi_)
        return;

    const int micro = static_cast<int> (Transport::quarterNotesToMicro (0.0));

    for (int note = 0; note < 128; ++note)
    {
        auto& slot = live_[note];

        if (! slot.down || slot.written || midiCount_ >= kMaxRecordedNotes)
            continue;

        auto& recorded = midiSlots_[midiCount_++];
        recorded.note = note;
        recorded.velocity = slot.velocity;
        recorded.channel = 1;
        recorded.startMicro = micro;
        recorded.endMicro = -1;
        slot.written = true;
        slot.startMicro = micro;
    }
}

void DeviceEngine::closeOpenMidiNotes() noexcept
{
    const int end = static_cast<int> (Transport::quarterNotesToMicro (transport_.musicalPosition().quarterNotes - recordStartQuarter_));

    for (int index = 0; index < midiCount_; ++index)
        if (midiSlots_[index].endMicro < 0)
            midiSlots_[index].endMicro = std::max (midiSlots_[index].startMicro + 1, end);
}

void DeviceEngine::finishRecording() noexcept
{
    if (! recording_ && ! countingIn_)
        return;

    const bool captured = recording_;
    countingIn_ = false;

    if (recording_ && recordMidi_)
        closeOpenMidiNotes();

    recording_ = false;

    if (! captured)
        return;

    publishedFrames_.store (recordWrite_, std::memory_order_relaxed);
    publishedMidiCount_.store (midiCount_, std::memory_order_relaxed);
    publishedStartMicro_.store (static_cast<std::int32_t> (Transport::quarterNotesToMicro (recordStartQuarter_)), std::memory_order_relaxed);
    publishedTrack_.store (recordTrack_, std::memory_order_relaxed);
    publishedMidi_.store (recordMidi_ ? 1 : 0, std::memory_order_relaxed);
    recordEpoch_.fetch_add (1, std::memory_order_release);
}

void DeviceEngine::drainLiveMidi() noexcept
{
    IncomingMidi event;

    while (midiQueue_.pop (event))
    {
        if (event.note > 127)
            continue;

        const double extra = transport_.sampleRate() > 0.0
                                 ? Timing::quarterNotesFromSamples (event.offset, transport_.sampleRate(), transport_.tempoBpm())
                                 : 0.0;
        const int micro = static_cast<int> (Transport::quarterNotesToMicro (transport_.musicalPosition().quarterNotes + extra - recordStartQuarter_));
        auto& slot = live_[event.note];

        if (event.velocity > 0)
        {
            slot.down = true;
            slot.releasing = false;
            slot.written = false;
            slot.velocity = event.velocity;
            slot.age = -event.offset;
            slot.releaseAge = 0;
            slot.startMicro = micro;

            if (recording_ && recordMidi_ && midiCount_ < kMaxRecordedNotes)
            {
                auto& recorded = midiSlots_[midiCount_++];
                recorded.note = event.note;
                recorded.velocity = event.velocity;
                recorded.channel = event.channel == 0 ? 1 : event.channel;
                recorded.startMicro = std::max (0, micro);
                recorded.endMicro = -1;
                slot.written = true;
            }
        }
        else
        {
            slot.down = false;
            slot.releasing = true;
            slot.releaseAge = 0;

            if (recording_ && recordMidi_ && slot.written)
            {
                for (int index = midiCount_ - 1; index >= 0; --index)
                {
                    if (midiSlots_[index].note == event.note && midiSlots_[index].endMicro < 0)
                    {
                        midiSlots_[index].endMicro = std::max (midiSlots_[index].startMicro + 1, micro);
                        break;
                    }
                }
            }
        }
    }
}

int DeviceEngine::collectLiveNotes (SoundingNote* destination, int capacity) const noexcept
{
    if (destination == nullptr || capacity <= 0 || ! (transport_.sampleRate() > 0.0))
        return 0;

    const double rate = transport_.sampleRate();
    int count = 0;

    for (int note = 0; note < 128 && count < capacity; ++note)
    {
        const auto& slot = live_[note];

        if (! slot.down && ! slot.releasing)
            continue;

        destination[count].note = note;
        destination[count].velocity = std::max (1, slot.velocity);
        destination[count].onsetSeconds = -static_cast<double> (slot.age) / rate;
        destination[count].releaseSeconds = slot.down ? 1.0e9 : -static_cast<double> (slot.releaseAge) / rate;
        ++count;
    }

    return count;
}

void DeviceEngine::advanceLiveNotes (int frames) noexcept
{
    const int releaseLimit = static_cast<int> (std::lround (std::max (0.05, 0.2) * std::max (1.0, transport_.sampleRate())));

    for (auto& slot : live_)
    {
        if (slot.down)
            slot.age += frames;
        else if (slot.releasing)
        {
            slot.releaseAge += frames;

            if (slot.releaseAge > releaseLimit)
                slot = {};
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
    recordingFlag_.store (recording_ ? 1 : 0, std::memory_order_relaxed);
    countingFlag_.store (countingIn_ ? 1 : 0, std::memory_order_relaxed);

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
    scratchFrames_ = scratch;
    routeStorage_.assign (static_cast<std::size_t> ((kMaxRouteTracks + kMaxBuses) * 2 * scratch), 0.0f);

    if (effectArena_ == nullptr)
        effectArena_.reset (new EffectState[static_cast<std::size_t> (kMaxRouteTracks * kMaxInserts)] {});

    bindBlockScratch (blockScratch_, routeStorage_.data(), scratch, effectArena_.get());
    rackScratch_.assign (static_cast<std::size_t> (scratch * 4), 0.0f);
    blockScratch_.rackScratchLeft = rackScratch_.data();
    blockScratch_.rackScratchRight = rackScratch_.data() + scratch;
    blockScratch_.rackOutLeft = rackScratch_.data() + scratch * 2;
    blockScratch_.rackOutRight = rackScratch_.data() + scratch * 3;

    if (rackStates_ == nullptr)
        rackStates_.reset (new EffectState[static_cast<std::size_t> (8 * 4 * kMaxInserts)] {});

    blockScratch_.rackStates = rackStates_.get();
    plugins_.prepare (transport_.sampleRate(), scratch);
    midiCollector_.reset (transport_.sampleRate());
    longDelayCapacity_ = std::max (8192, static_cast<int> (transport_.sampleRate() * 4.0));
    longDelay_.assign (static_cast<std::size_t> (16 * 2 * longDelayCapacity_), 0.0f);

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

    plugins_.setBusy (true);
    const RenderGraph* graph = acquireGraph();

    struct Release
    {
        DeviceEngine& engine;
        ~Release()
        {
            engine.releaseGraph();
            engine.plugins_.setBusy (false);
            engine.plugins_.reclaim();
        }
    } release { *this };

    if (numOutputChannels <= 0 || numSamples <= 0 || outputChannelData[0] == nullptr)
    {
        drainCommands();
        publishInfo();
        return;
    }

    drainCommands();
    juce::MidiBuffer midiBlock;
    midiCollector_.removeNextBlockOfMessages (midiBlock, numSamples);

    for (const auto metadata : midiBlock)
        applyLiveMessage (metadata.getMessage(), metadata.samplePosition);

    drainLiveMidi();
    plugins_.beginBlock();

    if (graph != nullptr && effectArena_ != nullptr && longDelayCapacity_ > 1 && ! longDelay_.empty())
    {
        int ordinal = 0;
        const int tracks = std::min (static_cast<int> (graph->tracks.size()), kMaxRouteTracks);

        for (int track = 0; track < tracks; ++track)
        {
            const auto& source = graph->tracks[static_cast<std::size_t> (track)];

            for (int slot = 0; slot < source.effectCount && slot < kMaxInserts; ++slot)
            {
                auto& state = effectArena_[static_cast<std::size_t> (track * kMaxInserts + slot)];
                const auto kind = source.effects[slot].kind;
                const bool longEffect = kind == EffectKind::Delay || kind == EffectKind::Chorus;

                if (longEffect && ordinal < 16)
                {
                    state.externalLeft = longDelay_.data() + static_cast<std::size_t> ((ordinal * 2) * longDelayCapacity_);
                    state.externalRight = longDelay_.data() + static_cast<std::size_t> ((ordinal * 2 + 1) * longDelayCapacity_);
                    state.externalCapacity = longDelayCapacity_;
                    ++ordinal;
                }
                else
                {
                    state.externalLeft = nullptr;
                    state.externalRight = nullptr;
                    state.externalCapacity = 0;
                }
            }
        }
    }
    ExternalInsert external;
    external.process = [] (void* context, int track, float* left, float* right, int frames)
    {
        static_cast<PluginHost*> (context)->processTrack (track, left, right, frames);
    };
    external.context = &plugins_;
    ExternalInsert* externalPtr = plugins_.anyLoaded() ? &external : nullptr;

    const bool directStereo = numOutputChannels >= 2 && outputChannelData[1] != nullptr;
    const int scratchFrames = static_cast<int> (scratchLeft_.size());
    const int monitor = monitorTrack_.load (std::memory_order_relaxed);
    SoundingNote liveNotes[128];
    const int liveCount = collectLiveNotes (liveNotes, 128);
    SynthSchedule schedule;
    schedule.memories = synthMemory_.data();
    schedule.memoryCount = static_cast<int> (synthMemory_.size());
    schedule.liveNotes = nullptr;
    schedule.liveNoteCount = 0;
    schedule.liveTrack = -1;

    auto renderFrames = [&] (int frames, float* left, float* right, int inputOffset)
    {
        const double rate = transport_.sampleRate();
        const double tempo = transport_.tempoBpm();
        const auto signature = transport_.timeSignature();
        int done = 0;

        auto monitorSynth = [&] (int offset, int count)
        {
            if (graph == nullptr || monitor < 0 || monitor >= static_cast<int> (graph->tracks.size()) || liveCount <= 0)
                return;

            const auto& track = graph->tracks[static_cast<std::size_t> (monitor)];

            if (! track.isMidi || track.mute)
                return;

            if (transport_.state() == TransportState::Playing)
                return;

            const PanGains pan = constantPowerPan (track.pan);
            renderTrackInstrument (track, liveNotes, liveCount, count, rate, synthMemory_[static_cast<std::size_t> (monitor)],
                                   track.gain * pan.left * graph->masterGain,
                                   track.gain * pan.right * graph->masterGain,
                                   left + offset, right + offset);
        };

        if (countingIn_)
        {
            const int count = std::min (frames, std::max (0, countInRemaining_));
            renderMetronome (left, right, countInElapsed_, count, rate, tempo, signature, 0.85f);
            countInElapsed_ += count;
            countInRemaining_ -= count;
            done += count;

            if (countInRemaining_ <= 0)
                beginRecordingNow();
        }

        const int remain = frames - done;

        if (remain > 0)
        {
            if (transport_.state() == TransportState::Playing)
            {
                int offset = done;
                transport_.advance (remain, [&] (std::int64_t start, int count)
                {
                    int local = 0;

                    while (local < count)
                    {
                        fireSession (start + local);
                        const auto boundary = nextSessionSample (start + local);
                        int piece = count - local;

                        if (boundary > start + local && boundary - (start + local) < piece)
                            piece = static_cast<int> (boundary - (start + local));

                        if (piece <= 0)
                            break;

                        renderLinearBlock (graph, rate, tempo, signature, start + local, piece,
                                           left + offset + local, right + offset + local,
                                           metronome_, false, nullptr, &structureOverflow_, &schedule,
                                           frames <= scratchFrames_ ? &blockScratch_ : nullptr,
                                           session_.data(), static_cast<int> (session_.size()),
                                           externalPtr);
                        local += piece;
                    }

                    offset += count;
                });
            }
            else
            {
                renderLinearBlock (nullptr, rate, tempo, signature, transport_.positionSamples(), remain,
                                   left + done, right + done,
                                   false, false, nullptr, nullptr, nullptr);
            }

            if (recording_ && ! recordMidi_ && recordWrite_ < recordCapacity_ && inputChannelData != nullptr && numInputChannels > 0)
            {
                const int space = recordCapacity_ - recordWrite_;
                const int count = std::min (remain, space);
                const float* inLeft = inputChannelData[0];
                const float* inRight = numInputChannels > 1 ? inputChannelData[1] : inLeft;

                if (inLeft != nullptr)
                {
                    for (int index = 0; index < count; ++index)
                    {
                        recordLeft_[static_cast<std::size_t> (recordWrite_ + index)] = inLeft[inputOffset + done + index];
                        recordRight_[static_cast<std::size_t> (recordWrite_ + index)] = inRight != nullptr ? inRight[inputOffset + done + index] : inLeft[inputOffset + done + index];
                    }

                    recordWrite_ += count;
                }
            }
        }

        monitorSynth (0, frames);

        if (testToneEnabled_)
            renderTestTone (left, right, frames, rate, 440.0, 0.15f, testTone_);

        float peakLeft = 0.0f;
        float peakRight = 0.0f;

        for (int index = 0; index < frames; ++index)
        {
            left[index] = clampSample (left[index]);
            right[index] = clampSample (right[index]);
            peakLeft = std::max (peakLeft, std::abs (left[index]));
            peakRight = std::max (peakRight, std::abs (right[index]));
        }

        advanceLiveNotes (frames);
        publishSession();
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

        const auto peaks = renderFrames (frames, left, right, rendered);
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
