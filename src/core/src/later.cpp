#include "waveform/later.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace waveform
{
namespace
{

int pitchClass (int note) noexcept
{
    int value = note % 12;
    if (value < 0)
        value += 12;
    return value;
}

bool isMinorDegree (int degree) noexcept
{
    switch (degree)
    {
        case 0:
        case 2:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

bool isMajorDegree (int degree) noexcept
{
    switch (degree)
    {
        case 0:
        case 2:
        case 4:
        case 5:
        case 7:
        case 9:
        case 11:
            return true;
        default:
            return false;
    }
}

} // namespace

double warpedSourceSeconds (double timelineQuarter,
                            double clipStartQuarter,
                            double secondsPerQuarter,
                            const std::vector<WarpMarker>& markers) noexcept
{
    const double local = timelineQuarter - clipStartQuarter;

    if (markers.size() < 2)
        return std::max (0.0, local * secondsPerQuarter);

    if (local <= markers.front().timelineQuarters)
        return markers.front().sourceSeconds;

    for (std::size_t index = 1; index < markers.size(); ++index)
    {
        const auto& right = markers[index];
        const auto& left = markers[index - 1];

        if (local > right.timelineQuarters)
            continue;

        const double span = right.timelineQuarters - left.timelineQuarters;

        if (! (span > 0.0))
            return right.sourceSeconds;

        const double mix = (local - left.timelineQuarters) / span;
        return left.sourceSeconds + (right.sourceSeconds - left.sourceSeconds) * mix;
    }

    return markers.back().sourceSeconds;
}

int compTakeAt (const std::vector<CompSegment>& segments, double quarter) noexcept
{
    for (const auto& segment : segments)
        if (quarter >= segment.startQuarter && quarter < segment.endQuarter)
            return segment.takeIndex;

    return 0;
}

void mixRack (const RackChain* chains,
              int chainCount,
              EffectState* states,
              int statesPerChain,
              const float* dryLeft,
              const float* dryRight,
              float* left,
              float* right,
              int frames,
              double sampleRate) noexcept
{
    if (chains == nullptr || dryLeft == nullptr || dryRight == nullptr || left == nullptr || right == nullptr || frames <= 0)
        return;

    std::fill (left, left + frames, 0.0f);
    std::fill (right, right + frames, 0.0f);
    std::vector<float> chainLeft (static_cast<std::size_t> (frames));
    std::vector<float> chainRight (static_cast<std::size_t> (frames));

    for (int chain = 0; chain < chainCount; ++chain)
    {
        std::copy (dryLeft, dryLeft + frames, chainLeft.begin());
        std::copy (dryRight, dryRight + frames, chainRight.begin());
        EffectState* chainState = states != nullptr ? states + chain * statesPerChain : nullptr;

        if (chainState != nullptr)
            processInserts (chains[chain].effects.data(), static_cast<int> (chains[chain].effects.size()),
                            chainState, chainLeft.data(), chainRight.data(), frames, sampleRate);

        for (int frame = 0; frame < frames; ++frame)
        {
            left[frame] += chainLeft[static_cast<std::size_t> (frame)] * chains[chain].gain;
            right[frame] += chainRight[static_cast<std::size_t> (frame)] * chains[chain].gain;
        }
    }
}

bool pitchInKey (int note, MusicalKey key) noexcept
{
    const int degree = (pitchClass (note) - pitchClass (key.root) + 12) % 12;
    return key.minor ? isMinorDegree (degree) : isMajorDegree (degree);
}

int constrainToKey (int note, MusicalKey key) noexcept
{
    if (pitchInKey (note, key))
        return note;

    for (int delta = 1; delta < 12; ++delta)
    {
        if (pitchInKey (note - delta, key))
            return note - delta;

        if (pitchInKey (note + delta, key))
            return note + delta;
    }

    return note;
}

std::vector<MidiNote> reverseNotes (std::vector<MidiNote> notes, double clipLengthQuarters)
{
    for (auto& note : notes)
        note.startQuarterNotes = std::max (0.0, clipLengthQuarters - (note.startQuarterNotes + note.lengthQuarterNotes));

    return notes;
}

std::vector<MidiNote> makeLegato (std::vector<MidiNote> notes)
{
    std::sort (notes.begin(), notes.end(), [] (const MidiNote& left, const MidiNote& right) {
        return left.startQuarterNotes < right.startQuarterNotes;
    });

    for (std::size_t index = 0; index + 1 < notes.size(); ++index)
    {
        const double gap = notes[index + 1].startQuarterNotes - notes[index].startQuarterNotes;

        if (gap > 0.0)
            notes[index].lengthQuarterNotes = gap;
    }

    return notes;
}

std::vector<MidiNote> humanizeNotes (std::vector<MidiNote> notes, double timingAmount, int velocityAmount, unsigned seed)
{
    unsigned state = seed == 0 ? 1u : seed;

    for (auto& note : notes)
    {
        state = state * 1664525u + 1013904223u;
        const double timing = (static_cast<double> (state % 2000) / 1000.0 - 1.0) * timingAmount;
        state = state * 1664525u + 1013904223u;
        const int velocity = static_cast<int> (state % static_cast<unsigned> (std::max (1, velocityAmount * 2 + 1))) - velocityAmount;
        note.startQuarterNotes = std::max (0.0, note.startQuarterNotes + timing);
        note.velocity = std::clamp (note.velocity + velocity, 1, 127);
    }

    return notes;
}

double mpeFrequencyRatio (float pitchBendSemitones) noexcept
{
    return std::pow (2.0, static_cast<double> (pitchBendSemitones) / 12.0);
}

float lfoValue (Lfo& lfo, int frames, double sampleRate) noexcept
{
    if (! (sampleRate > 0.0) || frames < 0)
        return 0.0f;

    lfo.phase += lfo.rateHz * static_cast<double> (frames) / sampleRate;
    lfo.phase -= std::floor (lfo.phase);
    return static_cast<float> (std::sin (lfo.phase * 6.283185307179586) * lfo.depth);
}

namespace
{

float voiceEnvelope (double seconds, double sinceRelease, const SynthPatch& patch) noexcept
{
    const double attack = std::max (1.0e-4, patch.attackSeconds);
    const float sustain = std::clamp (static_cast<float> (patch.sustain), 0.0f, 1.0f);
    const float held = seconds < attack ? static_cast<float> (seconds / attack) : sustain;

    if (sinceRelease >= 0.0)
        return std::max (0.0f, held * (1.0f - static_cast<float> (sinceRelease / std::max (1.0e-4, patch.releaseSeconds))));

    return held;
}

void renderVoices (const SoundingNote* notes, int noteCount, int frames, double sampleRate, const SynthPatch& patch, float leftGain, float rightGain, float* left, float* right, bool drum) noexcept
{
    if (notes == nullptr || left == nullptr || right == nullptr || frames <= 0 || ! (sampleRate > 0.0))
        return;

    for (int index = 0; index < noteCount; ++index)
    {
        const auto& note = notes[index];
        const float velocity = static_cast<float> (std::clamp (note.velocity, 1, 127)) / 127.0f;
        const double frequency = 440.0 * std::pow (2.0, (static_cast<double> (note.note) - 69.0) / 12.0);
        const int first = std::clamp (static_cast<int> (std::floor (note.onsetSeconds * sampleRate)), 0, frames);
        const int last = std::clamp (static_cast<int> (std::ceil ((note.releaseSeconds + patch.releaseSeconds) * sampleRate)), 0, frames);

        for (int frame = first; frame < last; ++frame)
        {
            const double time = static_cast<double> (frame) / sampleRate;
            const float env = voiceEnvelope (time - note.onsetSeconds, time - note.releaseSeconds, patch);

            if (env <= 0.0f)
                continue;

            const float sample = drum
                                   ? drumSample (std::max (0.0, time - note.onsetSeconds), static_cast<float> (frequency * 0.25), 0.35f, static_cast<float> (patch.decaySeconds + 0.05))
                                   : fmSample (frequency, frequency * 2.0, 1.4, std::max (0.0, time - note.onsetSeconds));
            left[frame] += sample * env * velocity * leftGain;
            right[frame] += sample * env * velocity * rightGain;
        }
    }
}

} // namespace

float fmSample (double carrierHz, double modulatorHz, double index, double timeSeconds) noexcept
{
    const double modulation = std::sin (2.0 * 3.141592653589793 * modulatorHz * timeSeconds) * index;
    return static_cast<float> (std::sin (2.0 * 3.141592653589793 * carrierHz * timeSeconds + modulation));
}

void renderFmNotes (const SoundingNote* notes, int noteCount, int frames, double sampleRate, const SynthPatch& patch, float leftGain, float rightGain, float* left, float* right) noexcept
{
    renderVoices (notes, noteCount, frames, sampleRate, patch, leftGain, rightGain, left, right, false);
}

void renderDrumNotes (const SoundingNote* notes, int noteCount, int frames, double sampleRate, const SynthPatch& patch, float leftGain, float rightGain, float* left, float* right) noexcept
{
    renderVoices (notes, noteCount, frames, sampleRate, patch, leftGain, rightGain, left, right, true);
}

float drumSample (double timeSeconds, float toneHz, float noiseMix, float decaySeconds) noexcept
{
    if (timeSeconds < 0.0)
        return 0.0f;

    const float env = std::exp (static_cast<float> (-timeSeconds / std::max (0.01, static_cast<double> (decaySeconds))));
    const float tone = std::sin (2.0f * 3.14159265f * toneHz * static_cast<float> (timeSeconds));
    const float noise = std::sin (static_cast<float> (timeSeconds) * 12345.0f) * 2.0f - 1.0f;
    return (tone * (1.0f - noiseMix) + noise * noiseMix) * env;
}

ScriptResult runScript (Project& project, const std::string& text)
{
    std::istringstream lines (text);
    std::string line;
    int applied = 0;

    while (std::getline (lines, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream words (line);
        std::string verb;
        words >> verb;

        if (verb == "tempo")
        {
            double bpm = 120.0;
            words >> bpm;
            project.tempoBpm = Timing::clampBpm (bpm);
            ++applied;
        }
        else if (verb == "add")
        {
            std::string kind;
            std::string name;
            words >> kind;
            std::getline (words, name);
            if (! name.empty() && name.front() == ' ')
                name.erase (name.begin());

            Track track;
            track.id = makeId();
            track.name = name.empty() ? "Track" : name;
            track.type = kind == "midi" ? TrackType::Midi : TrackType::Audio;
            project.tracks.push_back (std::move (track));
            ++applied;
        }
        else
        {
            return ScriptResult { false, "unknown command '" + verb + "'" };
        }
    }

    return ScriptResult { true, "applied " + std::to_string (applied) };
}

bool applyControlChange (Project& project, const std::string& trackId, int cc, int value, const std::vector<ControlMapping>& map)
{
    const ControlMapping* found = nullptr;

    for (const auto& mapping : map)
        if (mapping.cc == cc)
            found = &mapping;

    if (found == nullptr || found->parameter != "mixer.gainDb")
        return false;

    auto* track = findTrack (project, trackId);

    if (track == nullptr)
        return false;

    track->mixer.gainDb = -48.0 + (48.0 * std::clamp (value, 0, 127) / 127.0);
    return true;
}

std::vector<DetectedNote> audioToMidi (const float* samples, int frames, double sampleRate) noexcept
{
    std::vector<DetectedNote> notes;

    if (samples == nullptr || frames < 1024 || ! (sampleRate > 0.0))
        return notes;

    double energy = 0.0;

    for (int index = 0; index < frames; ++index)
        energy += static_cast<double> (samples[index]) * samples[index];

    if (energy / frames < 1.0e-6)
        return notes;

    const int minLag = static_cast<int> (sampleRate / 1000.0);
    const int maxLag = std::min (frames / 2, static_cast<int> (sampleRate / 50.0));
    int bestLag = minLag;
    double best = 0.0;

    for (int lag = std::max (2, minLag); lag < maxLag; ++lag)
    {
        double sum = 0.0;

        for (int index = 0; index < frames - lag; index += 4)
            sum += static_cast<double> (samples[index]) * samples[index + lag];

        if (sum > best)
        {
            best = sum;
            bestLag = lag;
        }
    }

    if (! (best > 0.0) || bestLag <= 0)
        return notes;

    const double frequency = sampleRate / static_cast<double> (bestLag);
    const int midi = std::clamp (static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (frequency / 440.0))), 0, 127);
    notes.push_back (DetectedNote { midi, 0.0, static_cast<double> (frames) / sampleRate });
    return notes;
}

void compensateDelay (const float* input, float* output, int frames, float* history, int delayFrames, int& cursor) noexcept
{
    if (input == nullptr || output == nullptr || history == nullptr || frames <= 0 || delayFrames <= 0)
    {
        if (input != nullptr && output != nullptr && frames > 0)
            std::copy (input, input + frames, output);

        return;
    }

    if (cursor < 0 || cursor >= delayFrames)
        cursor = 0;

    for (int index = 0; index < frames; ++index)
    {
        output[index] = history[cursor];
        history[cursor] = input[index];
        cursor = (cursor + 1) % delayFrames;
    }
}

} // namespace waveform
