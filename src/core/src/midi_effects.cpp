#include "waveform/midi_effects.hpp"

#include "waveform/midi.hpp"

#include <algorithm>
#include <cmath>

namespace waveform
{
namespace
{

constexpr int kScratch = 256;

int clampNote (int note) noexcept
{
    return std::clamp (note, 0, 127);
}

int clampVelocity (int velocity) noexcept
{
    return std::clamp (velocity, 1, 127);
}

bool scaleContains (ScaleKind scale, int pitchClass) noexcept
{
    const int pc = (pitchClass % 12 + 12) % 12;

    switch (scale)
    {
        case ScaleKind::NaturalMinor:
            return pc == 0 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 10;
        case ScaleKind::Pentatonic:
            return pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9;
        case ScaleKind::Major:
            break;
    }

    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
}

int snapToScale (int note, int root, ScaleKind scale) noexcept
{
    int best = clampNote (note);
    int bestDistance = 128;

    for (int candidate = note - 6; candidate <= note + 6; ++candidate)
    {
        if (candidate < 0 || candidate > 127)
            continue;

        const int pitchClass = (candidate - root) % 12;
        const int wrapped = pitchClass < 0 ? pitchClass + 12 : pitchClass;

        if (! scaleContains (scale, wrapped))
            continue;

        const int distance = std::abs (candidate - note);

        if (distance < bestDistance || (distance == bestDistance && candidate < best))
        {
            best = candidate;
            bestDistance = distance;
        }
    }

    return best;
}

int copyNotes (const MidiNote* input, int inputCount, MidiNote* output, int outputCapacity) noexcept
{
    const int count = std::min (inputCount, outputCapacity);

    for (int index = 0; index < count; ++index)
        output[index] = input[index];

    return count;
}

int applyArpeggiator (const MidiNote* input, int inputCount, const MidiEffect& effect, double clipLength, MidiNote* output, int outputCapacity) noexcept
{
    if (inputCount <= 0 || outputCapacity <= 0)
        return 0;

    const int count = std::min (inputCount, kScratch);
    int order[kScratch];

    for (int index = 0; index < count; ++index)
        order[index] = index;

    std::sort (order, order + count, [&] (int left, int right)
    {
        const auto& a = input[left];
        const auto& b = input[right];

        if (a.startQuarterNotes != b.startQuarterNotes)
            return a.startQuarterNotes < b.startQuarterNotes;

        return a.note < b.note;
    });

    const double step = std::max (0.0625, gridQuarterNotes (effect.division > 0 ? effect.division : 16));
    const int octaves = std::clamp (effect.octaves, 1, 4);
    int produced = 0;

    for (int index = 0; index < count && produced < outputCapacity;)
    {
        int groupEnd = index + 1;

        while (groupEnd < count
               && std::abs (input[order[groupEnd]].startQuarterNotes - input[order[index]].startQuarterNotes) < 1.0e-4)
            ++groupEnd;

        const double start = input[order[index]].startQuarterNotes;
        double groupStop = start;
        int pitches[64];
        int velocities[64];
        int pitchCount = 0;

        for (int member = index; member < groupEnd && pitchCount < 64; ++member)
        {
            const auto& note = input[order[member]];
            groupStop = std::max (groupStop, note.startQuarterNotes + note.lengthQuarterNotes);
            pitches[pitchCount] = note.note;
            velocities[pitchCount] = note.velocity;
            ++pitchCount;
        }

        const double limit = std::max (clipLength, groupStop);
        const double length = std::min (groupStop, limit) - start;

        if (pitchCount > 0 && length > 1.0e-6)
        {
            const int cycle = pitchCount * octaves;
            int stepIndex = 0;

            for (double cursor = 0.0; cursor < length - 1.0e-6 && produced < outputCapacity; cursor += step, ++stepIndex)
            {
                const int tone = stepIndex % cycle;
                const int which = tone % pitchCount;
                const int octave = tone / pitchCount;
                MidiNote note = input[order[index]];
                note.note = clampNote (pitches[which] + 12 * octave);
                note.velocity = clampVelocity (velocities[which]);
                note.startQuarterNotes = start + cursor;
                note.lengthQuarterNotes = std::min (step, length - cursor);
                note.slideSemitones = 0.0f;
                output[produced++] = note;
            }
        }

        index = groupEnd;
    }

    return produced;
}

int applyChord (const MidiNote* input, int inputCount, const MidiEffect& effect, MidiNote* output, int outputCapacity) noexcept
{
    int intervals[3] = { 0, 4, 7 };
    int intervalCount = 3;

    if (effect.chord == ChordKind::Minor)
    {
        intervals[1] = 3;
    }
    else if (effect.chord == ChordKind::Power)
    {
        intervals[1] = 7;
        intervalCount = 2;
    }

    int produced = 0;

    for (int index = 0; index < inputCount && produced < outputCapacity; ++index)
    {
        for (int interval = 0; interval < intervalCount && produced < outputCapacity; ++interval)
        {
            const int pitch = input[index].note + intervals[interval];

            if (pitch > 127)
                continue;

            bool duplicate = false;

            for (int earlier = 0; earlier < produced; ++earlier)
            {
                if (output[earlier].note == pitch
                    && std::abs (output[earlier].startQuarterNotes - input[index].startQuarterNotes) < 1.0e-6)
                    duplicate = true;
            }

            if (duplicate)
                continue;

            MidiNote note = input[index];
            note.note = pitch;
            output[produced++] = note;
        }
    }

    return produced;
}

int applyScale (const MidiNote* input, int inputCount, const MidiEffect& effect, MidiNote* output, int outputCapacity) noexcept
{
    const int count = copyNotes (input, inputCount, output, outputCapacity);

    for (int index = 0; index < count; ++index)
        output[index].note = snapToScale (output[index].note, effect.root, effect.scale);

    return count;
}

int applyPitch (const MidiNote* input, int inputCount, const MidiEffect& effect, MidiNote* output, int outputCapacity) noexcept
{
    const int count = copyNotes (input, inputCount, output, outputCapacity);

    for (int index = 0; index < count; ++index)
        output[index].note = clampNote (output[index].note + effect.semitones);

    return count;
}

int applyVelocity (const MidiNote* input, int inputCount, const MidiEffect& effect, MidiNote* output, int outputCapacity) noexcept
{
    const int count = copyNotes (input, inputCount, output, outputCapacity);

    for (int index = 0; index < count; ++index)
    {
        const double shaped = static_cast<double> (output[index].velocity) * effect.velocityScale
                            + static_cast<double> (effect.velocityAdd);
        output[index].velocity = clampVelocity (static_cast<int> (std::lround (shaped)));
    }

    return count;
}

int applyOne (const MidiNote* input, int inputCount, const MidiEffect& effect, double clipLength, MidiNote* output, int outputCapacity) noexcept
{
    if (! effect.enabled)
        return copyNotes (input, inputCount, output, outputCapacity);

    switch (effect.kind)
    {
        case MidiEffectKind::Arpeggiator:
            return applyArpeggiator (input, inputCount, effect, clipLength, output, outputCapacity);
        case MidiEffectKind::Chord:
            return applyChord (input, inputCount, effect, output, outputCapacity);
        case MidiEffectKind::Scale:
            return applyScale (input, inputCount, effect, output, outputCapacity);
        case MidiEffectKind::Pitch:
            return applyPitch (input, inputCount, effect, output, outputCapacity);
        case MidiEffectKind::Velocity:
            return applyVelocity (input, inputCount, effect, output, outputCapacity);
    }

    return copyNotes (input, inputCount, output, outputCapacity);
}

bool anyEnabled (const MidiEffect* effects, int effectCount) noexcept
{
    for (int index = 0; index < effectCount; ++index)
        if (effects[index].enabled)
            return true;

    return false;
}

} // namespace

const char* midiEffectKindName (MidiEffectKind kind) noexcept
{
    switch (kind)
    {
        case MidiEffectKind::Arpeggiator: return "arpeggiator";
        case MidiEffectKind::Chord: return "chord";
        case MidiEffectKind::Scale: return "scale";
        case MidiEffectKind::Pitch: return "pitch";
        case MidiEffectKind::Velocity: return "velocity";
    }

    return "pitch";
}

bool midiEffectKindFromName (const std::string& name, MidiEffectKind& kind) noexcept
{
    if (name == "arpeggiator") { kind = MidiEffectKind::Arpeggiator; return true; }
    if (name == "chord") { kind = MidiEffectKind::Chord; return true; }
    if (name == "scale") { kind = MidiEffectKind::Scale; return true; }
    if (name == "pitch") { kind = MidiEffectKind::Pitch; return true; }
    if (name == "velocity") { kind = MidiEffectKind::Velocity; return true; }
    return false;
}

const char* scaleKindName (ScaleKind kind) noexcept
{
    switch (kind)
    {
        case ScaleKind::NaturalMinor: return "minor";
        case ScaleKind::Pentatonic: return "pentatonic";
        case ScaleKind::Major: break;
    }

    return "major";
}

bool scaleKindFromName (const std::string& name, ScaleKind& kind) noexcept
{
    if (name == "major") { kind = ScaleKind::Major; return true; }
    if (name == "minor") { kind = ScaleKind::NaturalMinor; return true; }
    if (name == "pentatonic") { kind = ScaleKind::Pentatonic; return true; }
    return false;
}

const char* chordKindName (ChordKind kind) noexcept
{
    switch (kind)
    {
        case ChordKind::Minor: return "minor";
        case ChordKind::Power: return "power";
        case ChordKind::Major: break;
    }

    return "major";
}

bool chordKindFromName (const std::string& name, ChordKind& kind) noexcept
{
    if (name == "major") { kind = ChordKind::Major; return true; }
    if (name == "minor") { kind = ChordKind::Minor; return true; }
    if (name == "power") { kind = ChordKind::Power; return true; }
    return false;
}

int applyMidiEffects (const MidiNote* input,
                      int inputCount,
                      const MidiEffect* effects,
                      int effectCount,
                      double clipLengthQuarters,
                      MidiNote* output,
                      int outputCapacity) noexcept
{
    if (output == nullptr || outputCapacity <= 0)
        return 0;

    if (input == nullptr || inputCount <= 0)
        return 0;

    if (effects == nullptr || effectCount <= 0)
        return copyNotes (input, inputCount, output, outputCapacity);

    MidiNote buffers[2][kScratch];
    int count = copyNotes (input, std::min (inputCount, kScratch), buffers[0], kScratch);
    int source = 0;

    for (int index = 0; index < effectCount; ++index)
    {
        const int destination = 1 - source;
        count = applyOne (buffers[source], count, effects[index], clipLengthQuarters, buffers[destination], kScratch);
        source = destination;
    }

    return copyNotes (buffers[source], count, output, outputCapacity);
}

int shapeHeldNotes (const SoundingNote* held,
                    int heldCount,
                    const MidiEffect* effects,
                    int effectCount,
                    double secondsHeld,
                    double bpm,
                    SoundingNote* output,
                    int outputCapacity) noexcept
{
    if (output == nullptr || outputCapacity <= 0 || held == nullptr || heldCount <= 0)
        return 0;

    if (! anyEnabled (effects, effectCount) || ! (bpm > 0.0))
    {
        const int count = std::min (heldCount, outputCapacity);

        for (int index = 0; index < count; ++index)
            output[index] = held[index];

        return count;
    }

    const double quartersPerSecond = bpm / 60.0;
    const double heldQuarters = std::max (0.0, secondsHeld) * quartersPerSecond;
    MidiNote input[128];
    int inputCount = 0;

    for (int index = 0; index < heldCount && inputCount < 128; ++index)
    {
        MidiNote note;
        note.note = held[index].note;
        note.velocity = clampVelocity (held[index].velocity);
        note.startQuarterNotes = std::max (0.0, (held[index].onsetSeconds + secondsHeld) * quartersPerSecond);
        const bool heldDown = held[index].releaseSeconds > 1.0e6;
        note.lengthQuarterNotes = heldDown ? std::max (4.0, heldQuarters + 4.0)
                                           : std::max (0.01, (held[index].releaseSeconds + secondsHeld) * quartersPerSecond - note.startQuarterNotes);
        note.slideSemitones = held[index].slideSemitones;
        input[inputCount++] = note;
    }

    MidiNote processed[kScratch];
    const int produced = applyMidiEffects (input, inputCount, effects, effectCount,
                                           std::max (heldQuarters + 0.01, 0.25), processed, kScratch);
    int written = 0;

    for (int index = 0; index < produced && written < outputCapacity; ++index)
    {
        const auto& note = processed[index];
        const double end = note.startQuarterNotes + note.lengthQuarterNotes;

        if (note.startQuarterNotes > heldQuarters + 1.0e-4 || end <= heldQuarters - 1.0e-6)
            continue;

        SoundingNote sounding;
        sounding.note = note.note;
        sounding.velocity = note.velocity;
        sounding.slideSemitones = note.slideSemitones;
        const double ageQuarters = std::max (0.0, heldQuarters - note.startQuarterNotes);
        sounding.onsetSeconds = -ageQuarters * 60.0 / bpm;
        sounding.releaseSeconds = (end - heldQuarters) > 1.0e-3 ? 1.0e9 : 0.0;
        output[written++] = sounding;
    }

    return written;
}

} // namespace waveform
