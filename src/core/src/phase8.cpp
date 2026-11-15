#include "waveform/phase8.hpp"

#include "waveform/mix.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace waveform
{
namespace
{

bool containsToken (const char* text, const char* query) noexcept
{
    if (text == nullptr || query == nullptr || query[0] == '\0')
        return false;

    const std::size_t queryLength = std::strlen (query);

    for (const char* cursor = text; *cursor != '\0'; ++cursor)
    {
        std::size_t index = 0;

        while (index < queryLength
               && cursor[index] != '\0'
               && std::tolower (static_cast<unsigned char> (cursor[index])) == std::tolower (static_cast<unsigned char> (query[index])))
            ++index;

        if (index == queryLength)
            return true;
    }

    return false;
}

} // namespace

int assignTrackWaves (const int* dependsOn, int count, int* waves) noexcept
{
    if (dependsOn == nullptr || waves == nullptr || count <= 0)
        return 0;

    for (int index = 0; index < count; ++index)
        waves[index] = 0;

    int highest = 0;

    for (int pass = 0; pass < count; ++pass)
    {
        bool changed = false;

        for (int index = 0; index < count; ++index)
        {
            const int dependency = dependsOn[index];

            if (dependency < 0 || dependency >= count || dependency == index)
                continue;

            const int next = waves[dependency] + 1;

            if (next > waves[index] && next < count)
            {
                waves[index] = next;
                highest = std::max (highest, next);
                changed = true;
            }
        }

        if (! changed)
            break;
    }

    return highest + 1;
}

struct WorkerPool::Impl
{
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable work;
    std::condition_variable finished;
    bool stop = false;
    void (*fn) (void*, int) = nullptr;
    void* context = nullptr;
    int count = 0;
    int epoch = 0;
    std::atomic<int> next { 0 };
    std::atomic<int> remaining { 0 };

    void run()
    {
        int seen = 0;

        for (;;)
        {
            void (*job) (void*, int) = nullptr;
            void* ctx = nullptr;
            int jobs = 0;

            {
                std::unique_lock<std::mutex> lock (mutex);
                work.wait (lock, [&] { return stop || epoch != seen; });

                if (stop)
                    return;

                seen = epoch;
                job = fn;
                ctx = context;
                jobs = count;
            }

            if (job != nullptr)
            {
                for (;;)
                {
                    const int index = next.fetch_add (1, std::memory_order_relaxed);

                    if (index >= jobs)
                        break;

                    job (ctx, index);
                }
            }

            if (remaining.fetch_sub (1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard<std::mutex> lock (mutex);
                finished.notify_one();
            }
        }
    }
};

WorkerPool::WorkerPool (int threads)
    : impl_ (new Impl)
{
    const int count = std::max (0, threads);

    for (int index = 0; index < count; ++index)
        impl_->threads.emplace_back ([this] { impl_->run(); });
}

WorkerPool::~WorkerPool()
{
    if (impl_ == nullptr)
        return;

    {
        std::lock_guard<std::mutex> lock (impl_->mutex);
        impl_->stop = true;
        impl_->epoch += 1;
    }

    impl_->work.notify_all();

    for (auto& thread : impl_->threads)
        if (thread.joinable())
            thread.join();

    delete impl_;
    impl_ = nullptr;
}

int WorkerPool::threadCount() const noexcept
{
    return impl_ == nullptr ? 0 : static_cast<int> (impl_->threads.size());
}

void WorkerPool::parallel (int count, void (*fn) (void*, int), void* context)
{
    if (fn == nullptr || count <= 0)
        return;

    if (impl_ == nullptr || impl_->threads.empty() || count == 1)
    {
        for (int index = 0; index < count; ++index)
            fn (context, index);

        return;
    }

    {
        std::lock_guard<std::mutex> lock (impl_->mutex);
        impl_->fn = fn;
        impl_->context = context;
        impl_->count = count;
        impl_->next.store (0, std::memory_order_relaxed);
        impl_->remaining.store (static_cast<int> (impl_->threads.size()), std::memory_order_relaxed);
        impl_->epoch += 1;
    }

    impl_->work.notify_all();

    for (;;)
    {
        const int index = impl_->next.fetch_add (1, std::memory_order_relaxed);

        if (index >= count)
            break;

        fn (context, index);
    }

    std::unique_lock<std::mutex> lock (impl_->mutex);
    impl_->finished.wait (lock, [&] { return impl_->remaining.load (std::memory_order_acquire) <= 0; });
}

TimeSignature signatureAt (const SignaturePoint* points, int count, double quarterNotes, TimeSignature fallback) noexcept
{
    if (points == nullptr || count <= 0 || std::isnan (quarterNotes))
        return fallback;

    TimeSignature signature = fallback;

    for (int index = 0; index < count; ++index)
        if (points[index].quarterNotes <= quarterNotes && points[index].signature.isValid())
            signature = points[index].signature;

    return signature;
}

void crossfadeAdjacent (AudioClip& earlier, AudioClip& later, double tempoBpm) noexcept
{
    if (! (tempoBpm > 0.0) || ! (earlier.lengthSeconds > 0.0) || ! (later.lengthSeconds > 0.0))
        return;

    const double earlierEnd = earlier.startQuarterNotes + earlier.lengthSeconds * tempoBpm / 60.0;
    const double overlapQuarters = earlierEnd - later.startQuarterNotes;
    double fadeSeconds = overlapQuarters > 0.0 ? overlapQuarters * 60.0 / tempoBpm : 0.0;

    if (fadeSeconds <= 0.0 && std::abs (overlapQuarters) <= 0.02)
        fadeSeconds = std::min (0.02, std::min (earlier.lengthSeconds, later.lengthSeconds) * 0.5);

    if (! (fadeSeconds > 0.0))
        return;

    fadeSeconds = std::min (fadeSeconds, std::min (earlier.lengthSeconds, later.lengthSeconds) * 0.5);
    earlier.fadeOutSeconds = std::max (earlier.fadeOutSeconds, fadeSeconds);
    later.fadeInSeconds = std::max (later.fadeInSeconds, fadeSeconds);
}

void freezeClipSamples (const float* source, int frames, float gain, int fadeInFrames, int fadeOutFrames, float* destination) noexcept
{
    if (source == nullptr || destination == nullptr || frames <= 0)
        return;

    constexpr float kHalfPi = 1.5707963267948966f;

    for (int index = 0; index < frames; ++index)
    {
        float envelope = 1.0f;

        if (fadeInFrames > 0 && index < fadeInFrames)
            envelope *= std::sin (static_cast<float> (index) / static_cast<float> (fadeInFrames) * kHalfPi);

        const int remaining = frames - index;

        if (fadeOutFrames > 0 && remaining < fadeOutFrames)
        {
            const float progress = static_cast<float> (fadeOutFrames - remaining) / static_cast<float> (fadeOutFrames);
            envelope *= std::cos (progress * kHalfPi);
        }

        destination[index] = source[index] * gain * envelope;
    }
}

void overdubNotes (MidiClip& clip, const MidiNote* notes, int count)
{
    if (notes == nullptr || count <= 0)
        return;

    for (int index = 0; index < count; ++index)
    {
        MidiNote note = notes[index];

        if (note.startQuarterNotes < 0.0 || note.lengthQuarterNotes <= 0.0)
            continue;

        if (note.startQuarterNotes >= clip.lengthQuarterNotes)
            continue;

        note.lengthQuarterNotes = std::min (note.lengthQuarterNotes, clip.lengthQuarterNotes - note.startQuarterNotes);
        clip.notes.push_back (note);
    }
}

bool noteInPunch (PunchSpan punch, double startQuarter, double lengthQuarter) noexcept
{
    if (! punch.enabled)
        return true;

    const double end = startQuarter + std::max (0.0, lengthQuarter);
    return end > punch.startQuarter && startQuarter < punch.endQuarter;
}

void applyPunch (std::vector<MidiNote>& notes, PunchSpan punch)
{
    if (! punch.enabled)
        return;

    notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const MidiNote& note)
    {
        return ! noteInPunch (punch, note.startQuarterNotes, note.lengthQuarterNotes);
    }), notes.end());
}

float crossfaderLevel (CrossfaderSide side, float position) noexcept
{
    const float clamped = std::clamp (position, 0.0f, 1.0f);
    const float angle = clamped * 1.5707963267948966f;

    if (side == CrossfaderSide::A)
        return std::cos (angle);

    if (side == CrossfaderSide::B)
        return std::sin (angle);

    return 1.0f;
}

int resampleLinear (const float* input, int inputFrames, double inputRate, float* output, int outCapacity, double outputRate) noexcept
{
    if (input == nullptr || output == nullptr || inputFrames <= 0 || outCapacity <= 0 || ! (inputRate > 0.0) || ! (outputRate > 0.0))
        return 0;

    const double seconds = static_cast<double> (inputFrames) / inputRate;
    int frames = static_cast<int> (std::floor (seconds * outputRate));
    frames = std::clamp (frames, 1, outCapacity);
    const double step = inputRate / outputRate;

    for (int index = 0; index < frames; ++index)
    {
        const double position = static_cast<double> (index) * step;
        const int left = std::min (static_cast<int> (position), inputFrames - 1);
        const int right = std::min (left + 1, inputFrames - 1);
        const float mix = static_cast<float> (position - static_cast<double> (left));
        output[index] = input[left] * (1.0f - mix) + input[right] * mix;
    }

    return frames;
}

int hardwareChannel (int pair, int channelInPair, int channelCount) noexcept
{
    const int channel = pair * 2 + std::clamp (channelInPair, 0, 1);

    if (channel < 0 || channel >= channelCount)
        return channelCount > 0 ? std::min (channelInPair, channelCount - 1) : -1;

    return channel;
}

int noteFromTypingKey (int ascii, int octave) noexcept
{
    const int key = std::tolower (ascii);
    const char* layout = "awsedftgyhujk";
    const int semis[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    const char* found = std::strchr (layout, key);

    if (found == nullptr)
        return -1;

    const int index = static_cast<int> (found - layout);
    const int note = (std::clamp (octave, 0, 8) + 1) * 12 + semis[index];
    return note >= 0 && note <= 127 ? note : -1;
}

int midiClocksInBlock (double bpm, double sampleRate, int frames, double& quarterPhase) noexcept
{
    if (! (bpm > 0.0) || ! (sampleRate > 0.0) || frames <= 0)
        return 0;

    const double quarters = (bpm / 60.0) * (static_cast<double> (frames) / sampleRate);
    const double before = quarterPhase * 24.0;
    quarterPhase += quarters;
    const double after = quarterPhase * 24.0;
    return std::max (0, static_cast<int> (std::floor (after)) - static_cast<int> (std::floor (before)));
}

double bpmFromMidiClocks (int clocks, double seconds) noexcept
{
    if (clocks <= 0 || ! (seconds > 0.0))
        return 0.0;

    const double quarters = static_cast<double> (clocks) / 24.0;
    return Timing::clampBpm (quarters / seconds * 60.0);
}

bool mediaMatches (MediaRecord record, const char* query) noexcept
{
    if (query == nullptr || query[0] == '\0')
        return true;

    return containsToken (record.name, query) || containsToken (record.tags, query);
}

void spectrumBands (const float* samples, int frames, float* bands, int bandCount) noexcept
{
    if (bands == nullptr || bandCount <= 0)
        return;

    for (int band = 0; band < bandCount; ++band)
        bands[band] = 0.0f;

    if (samples == nullptr || frames <= 0)
        return;

    for (int index = 0; index < frames; ++index)
    {
        const float sample = samples[index];
        const int band = std::min (bandCount - 1, static_cast<int> ((static_cast<long long> (index) * bandCount) / frames));
        bands[band] += sample * sample;
    }

    const float normal = 1.0f / static_cast<float> (std::max (1, frames / bandCount));

    for (int band = 0; band < bandCount; ++band)
        bands[band] = std::sqrt (bands[band] * normal);
}

void fillWavetable (float* table, int size, float morph) noexcept
{
    if (table == nullptr || size <= 1)
        return;

    const float mix = std::clamp (morph, 0.0f, 1.0f);

    for (int index = 0; index < size; ++index)
    {
        const double phase = static_cast<double> (index) / static_cast<double> (size);
        const float sine = static_cast<float> (std::sin (phase * 6.283185307179586));
        const float saw = static_cast<float> (2.0 * phase - 1.0);
        table[index] = sine * (1.0f - mix) + saw * mix;
    }
}

float wavetableSample (const float* table, int size, double phase) noexcept
{
    if (table == nullptr || size <= 1)
        return 0.0f;

    const double wrapped = phase - std::floor (phase);
    const double position = wrapped * static_cast<double> (size);
    const int left = static_cast<int> (position) % size;
    const int right = (left + 1) % size;
    const float mix = static_cast<float> (position - std::floor (position));
    return table[left] * (1.0f - mix) + table[right] * mix;
}

} // namespace waveform
