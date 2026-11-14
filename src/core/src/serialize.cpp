#include "waveform/effects.hpp"
#include "waveform/serialize.hpp"
#include "waveform/midi_effects.hpp"
#include "waveform/version.hpp"

#if defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <nlohmann/json.hpp>
#if defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace waveform
{
namespace
{

using nlohmann::json;

[[noreturn]] void fail (const std::string& message)
{
    throw std::runtime_error (message);
}

const json& requireObject (const json& node, const char* what)
{
    if (! node.is_object())
        fail (std::string (what) + " must be an object");

    return node;
}

double requireNumber (const json& node, const char* key)
{
    if (! node.contains (key) || ! node.at (key).is_number())
        fail (std::string (key) + " must be a number");

    return node.at (key).get<double>();
}

bool requireBool (const json& node, const char* key)
{
    if (! node.contains (key) || ! node.at (key).is_boolean())
        fail (std::string (key) + " must be a boolean");

    return node.at (key).get<bool>();
}

std::string requireString (const json& node, const char* key)
{
    if (! node.contains (key) || ! node.at (key).is_string())
        fail (std::string (key) + " must be a string");

    return node.at (key).get<std::string>();
}

int requireInt (const json& node, const char* key)
{
    if (! node.contains (key) || ! node.at (key).is_number_integer())
        fail (std::string (key) + " must be an integer");

    return node.at (key).get<int>();
}

json mixerToJson (const MixerState& mixer)
{
    return json {
        { "gainDb", mixer.gainDb },
        { "mute", mixer.mute },
        { "pan", mixer.pan },
        { "solo", mixer.solo }
    };
}

SynthPatch synthFromJson (const json& node);

MixerState mixerFromJson (const json& node)
{
    requireObject (node, "mixer");
    MixerState mixer;
    mixer.gainDb = requireNumber (node, "gainDb");
    mixer.pan = requireNumber (node, "pan");
    mixer.mute = requireBool (node, "mute");
    mixer.solo = requireBool (node, "solo");
    clampMixer (mixer);
    return mixer;
}

json rackToJson (const DeviceRack& rack);
json pluginsToJson (const std::vector<PluginInsert>& plugins);
json warpToJson (const std::vector<WarpMarker>& markers);
json takesToJson (const std::vector<ClipTake>& takes);
json compToJson (const std::vector<CompSegment>& segments);

json envelopeToJson (const std::vector<AutomationPoint>& points)
{
    json values = json::array();

    for (const auto& point : points)
        values.push_back (json { { "position", point.quarterNotes }, { "value", point.value } });

    return values;
}

void envelopeFromJson (const json& node, std::vector<AutomationPoint>& points)
{
    if (! node.contains ("envelope") || ! node.at ("envelope").is_array())
        return;

    for (const auto& point : node.at ("envelope"))
        points.push_back (AutomationPoint { requireNumber (point, "position"), requireNumber (point, "value") });
}

json audioClipToJson (const AudioClip& clip)
{
    json value = {
        { "fadeIn", clip.fadeInSeconds },
        { "fadeOut", clip.fadeOutSeconds },
        { "file", clip.file },
        { "gainDb", clip.gainDb },
        { "id", clip.id },
        { "lengthSeconds", clip.lengthSeconds },
        { "name", clip.name },
        { "offsetSeconds", clip.offsetSeconds },
        { "comp", compToJson (clip.comp) },
        { "startQuarterNotes", clip.startQuarterNotes },
        { "takes", takesToJson (clip.takes) },
        { "type", "audio" },
        { "warp", warpToJson (clip.warp) }
    };

    if (! clip.envelope.empty())
        value["envelope"] = envelopeToJson (clip.envelope);

    if (clip.warpMode != 2)
        value["warpMode"] = clip.warpMode;

    return value;
}

json takesToJson (const std::vector<ClipTake>& takes)
{
    json values = json::array();

    for (const auto& take : takes)
        values.push_back (json { { "file", take.file }, { "name", take.name } });

    return values;
}

json warpToJson (const std::vector<WarpMarker>& markers)
{
    json values = json::array();

    for (const auto& marker : markers)
        values.push_back (json { { "source", marker.sourceSeconds }, { "quarter", marker.timelineQuarters } });

    return values;
}

json compToJson (const std::vector<CompSegment>& segments)
{
    json values = json::array();

    for (const auto& segment : segments)
        values.push_back (json { { "end", segment.endQuarter }, { "start", segment.startQuarter }, { "take", segment.takeIndex } });

    return values;
}

json midiClipToJson (const MidiClip& clip)
{
    json notes = json::array();

    for (const auto& note : clip.notes)
    {
        notes.push_back (json {
            { "channel", note.channel },
            { "lengthQuarterNotes", note.lengthQuarterNotes },
            { "note", note.note },
            { "slide", note.slideSemitones },
            { "startQuarterNotes", note.startQuarterNotes },
            { "velocity", note.velocity }
        });
    }

    json value = {
        { "id", clip.id },
        { "lengthQuarterNotes", clip.lengthQuarterNotes },
        { "name", clip.name },
        { "notes", std::move (notes) },
        { "startQuarterNotes", clip.startQuarterNotes },
        { "type", "midi" }
    };

    if (! clip.envelope.empty())
        value["envelope"] = envelopeToJson (clip.envelope);

    return value;
}

json clipToJson (const Clip& clip)
{
    if (const auto* audio = std::get_if<AudioClip> (&clip))
        return audioClipToJson (*audio);

    return midiClipToJson (std::get<MidiClip> (clip));
}

void rememberId (std::unordered_set<std::string>& ids, const std::string& id, const char* what)
{
    if (id.empty())
        fail (std::string (what) + " is missing an id");

    if (! ids.insert (id).second)
        fail ("duplicate id '" + id + "'");
}

AudioClip audioClipFromJson (const json& node, std::unordered_set<std::string>& ids)
{
    AudioClip clip;
    clip.id = requireString (node, "id");
    rememberId (ids, clip.id, "audio clip");
    clip.name = requireString (node, "name");
    clip.file = requireString (node, "file");
    clip.startQuarterNotes = requireNumber (node, "startQuarterNotes");
    clip.offsetSeconds = requireNumber (node, "offsetSeconds");
    clip.lengthSeconds = requireNumber (node, "lengthSeconds");
    clip.gainDb = requireNumber (node, "gainDb");
    clip.fadeInSeconds = node.contains ("fadeIn") ? std::max (0.0, requireNumber (node, "fadeIn")) : 0.0;
    clip.fadeOutSeconds = node.contains ("fadeOut") ? std::max (0.0, requireNumber (node, "fadeOut")) : 0.0;

    if (clip.file.empty())
        fail ("audio clip '" + clip.id + "' has an empty file path");

    if (std::isnan (clip.startQuarterNotes) || clip.startQuarterNotes < 0.0)
        fail ("audio clip '" + clip.id + "' has a negative start");

    if (std::isnan (clip.offsetSeconds) || clip.offsetSeconds < 0.0)
        fail ("audio clip '" + clip.id + "' has a negative offset");

    if (! (clip.lengthSeconds > 0.0) || std::isnan (clip.lengthSeconds))
        fail ("audio clip '" + clip.id + "' must have a positive length");

    MixerState gain;
    gain.gainDb = clip.gainDb;
    clampMixer (gain);
    clip.gainDb = gain.gainDb;

    if (node.contains ("takes") && node.at ("takes").is_array())
    {
        for (const auto& takeNode : node.at ("takes"))
        {
            ClipTake take;
            take.name = requireString (takeNode, "name");
            take.file = requireString (takeNode, "file");
            clip.takes.push_back (std::move (take));
        }
    }

    if (node.contains ("warp") && node.at ("warp").is_array())
        for (const auto& marker : node.at ("warp"))
            clip.warp.push_back (WarpMarker { requireNumber (marker, "source"), requireNumber (marker, "quarter") });

    if (node.contains ("comp") && node.at ("comp").is_array())
        for (const auto& segment : node.at ("comp"))
            clip.comp.push_back (CompSegment { requireNumber (segment, "start"), requireNumber (segment, "end"), requireInt (segment, "take") });

    envelopeFromJson (node, clip.envelope);

    if (node.contains ("warpMode"))
        clip.warpMode = std::clamp (requireInt (node, "warpMode"), 0, 2);

    return clip;
}

MidiNote noteFromJson (const json& node, const std::string& clipId)
{
    requireObject (node, "note");
    MidiNote note;
    note.note = requireInt (node, "note");
    note.startQuarterNotes = requireNumber (node, "startQuarterNotes");
    note.lengthQuarterNotes = requireNumber (node, "lengthQuarterNotes");
    note.velocity = requireInt (node, "velocity");
    note.channel = requireInt (node, "channel");

    if (note.note < 0 || note.note > 127)
        fail ("MIDI clip '" + clipId + "' has a note outside 0..127");

    if (note.velocity < 1 || note.velocity > 127)
        fail ("MIDI clip '" + clipId + "' has a velocity outside 1..127");

    if (note.channel < 1 || note.channel > 16)
        fail ("MIDI clip '" + clipId + "' has a channel outside 1..16");

    if (node.contains ("slide"))
        note.slideSemitones = static_cast<float> (requireNumber (node, "slide"));

    if (std::isnan (note.startQuarterNotes) || note.startQuarterNotes < 0.0)
        fail ("MIDI clip '" + clipId + "' has a negative note start");

    if (! (note.lengthQuarterNotes > 0.0) || std::isnan (note.lengthQuarterNotes))
        fail ("MIDI clip '" + clipId + "' has a note with a non-positive length");

    return note;
}

MidiClip midiClipFromJson (const json& node, std::unordered_set<std::string>& ids)
{
    MidiClip clip;
    clip.id = requireString (node, "id");
    rememberId (ids, clip.id, "MIDI clip");
    clip.name = requireString (node, "name");
    clip.startQuarterNotes = requireNumber (node, "startQuarterNotes");
    clip.lengthQuarterNotes = requireNumber (node, "lengthQuarterNotes");

    if (std::isnan (clip.startQuarterNotes) || clip.startQuarterNotes < 0.0)
        fail ("MIDI clip '" + clip.id + "' has a negative start");

    if (! (clip.lengthQuarterNotes > 0.0) || std::isnan (clip.lengthQuarterNotes))
        fail ("MIDI clip '" + clip.id + "' must have a positive length");

    if (! node.contains ("notes") || ! node.at ("notes").is_array())
        fail ("MIDI clip '" + clip.id + "' is missing notes");

    for (const auto& note : node.at ("notes"))
        clip.notes.push_back (noteFromJson (note, clip.id));

    envelopeFromJson (node, clip.envelope);
    return clip;
}

Clip clipFromJson (const json& node, TrackType trackType, std::unordered_set<std::string>& ids)
{
    requireObject (node, "clip");
    const auto type = requireString (node, "type");

    if (type == "audio")
    {
        if (trackType != TrackType::Audio)
            fail ("audio clip on a MIDI track");

        return audioClipFromJson (node, ids);
    }

    if (type == "midi")
    {
        if (trackType != TrackType::Midi)
            fail ("MIDI clip on an audio track");

        return midiClipFromJson (node, ids);
    }

    fail ("unknown clip type '" + type + "'");
}

Track trackFromJson (const json& node, std::unordered_set<std::string>& ids)
{
    requireObject (node, "track");
    Track track;
    track.id = requireString (node, "id");
    rememberId (ids, track.id, "track");
    track.name = requireString (node, "name");
    const auto type = requireString (node, "type");

    if (type == "audio")
        track.type = TrackType::Audio;
    else if (type == "midi")
        track.type = TrackType::Midi;
    else
        fail ("unknown track type '" + type + "'");

    if (! node.contains ("mixer"))
        fail ("track '" + track.id + "' is missing mixer");

    track.mixer = mixerFromJson (node.at ("mixer"));

    if (! node.contains ("clips") || ! node.at ("clips").is_array())
        fail ("track '" + track.id + "' is missing clips");

    for (const auto& clip : node.at ("clips"))
        track.clips.push_back (clipFromJson (clip, track.type, ids));

    if (node.contains ("session") && node.at ("session").is_array())
    {
        for (const auto& slotNode : node.at ("session"))
        {
            requireObject (slotNode, "session slot");
            SessionSlot slot;
            slot.scene = std::clamp (requireInt (slotNode, "scene"), 0, kSessionScenes - 1);

            if (! slotNode.contains ("clip"))
                fail ("session slot is missing a clip");

            slot.clip = clipFromJson (slotNode.at ("clip"), track.type, ids);

            if (slotNode.contains ("launch"))
                slot.launch = static_cast<ClipLaunchMode> (std::clamp (requireInt (slotNode, "launch"), 0, 3));

            if (slotNode.contains ("follow"))
                slot.follow = static_cast<FollowActionKind> (std::clamp (requireInt (slotNode, "follow"), 0, 5));

            if (slotNode.contains ("followAfter"))
                slot.followAfterQuarters = requireNumber (slotNode, "followAfter");

            if (slotNode.contains ("repeat"))
                slot.repeatQuarters = requireNumber (slotNode, "repeat");

            track.session.push_back (std::move (slot));
        }
    }

    if (node.contains ("plugins") && node.at ("plugins").is_array())
    {
        for (const auto& pluginNode : node.at ("plugins"))
        {
            PluginInsert plugin;
            plugin.identifier = requireString (pluginNode, "identifier");
            plugin.name = requireString (pluginNode, "name");
            plugin.state = pluginNode.contains ("state") && pluginNode.at ("state").is_string() ? pluginNode.at ("state").get<std::string>() : std::string {};
            track.plugins.push_back (std::move (plugin));
        }
    }

    if (node.contains ("midiEffects") && node.at ("midiEffects").is_array())
    {
        for (const auto& effectNode : node.at ("midiEffects"))
        {
            MidiEffect effect;
            MidiEffectKind kind = MidiEffectKind::Pitch;

            if (! midiEffectKindFromName (requireString (effectNode, "kind"), kind))
                fail ("unknown MIDI effect");

            effect.kind = kind;
            effect.enabled = ! effectNode.contains ("enabled") || (effectNode.at ("enabled").is_boolean() && effectNode.at ("enabled").get<bool>());
            effect.division = effectNode.contains ("division") ? requireInt (effectNode, "division") : 16;
            effect.octaves = effectNode.contains ("octaves") ? requireInt (effectNode, "octaves") : 1;
            effect.root = effectNode.contains ("root") ? requireInt (effectNode, "root") : 0;
            effect.semitones = effectNode.contains ("semitones") ? requireInt (effectNode, "semitones") : 0;
            effect.velocityAdd = effectNode.contains ("velocityAdd") ? requireInt (effectNode, "velocityAdd") : 0;
            effect.velocityScale = effectNode.contains ("velocityScale") ? requireNumber (effectNode, "velocityScale") : 1.0;

            if (effectNode.contains ("scale") && effectNode.at ("scale").is_string())
            {
                ScaleKind scale = ScaleKind::Major;

                if (scaleKindFromName (effectNode.at ("scale").get<std::string>(), scale))
                    effect.scale = scale;
            }

            if (effectNode.contains ("chord") && effectNode.at ("chord").is_string())
            {
                ChordKind chord = ChordKind::Major;

                if (chordKindFromName (effectNode.at ("chord").get<std::string>(), chord))
                    effect.chord = chord;
            }

            track.midiEffects.push_back (effect);
        }
    }

    if (node.contains ("plugin") && node.at ("plugin").is_string())
        track.pluginIdentifier = node.at ("plugin").get<std::string>();

    if (node.contains ("pluginName") && node.at ("pluginName").is_string())
        track.pluginName = node.at ("pluginName").get<std::string>();

    if (node.contains ("pluginState") && node.at ("pluginState").is_string())
        track.pluginState = node.at ("pluginState").get<std::string>();

    if (track.plugins.empty() && ! track.pluginIdentifier.empty())
        track.plugins.push_back (PluginInsert { track.pluginIdentifier, track.pluginName, track.pluginState });

    if (track.type == TrackType::Midi && node.contains ("synth"))
        track.synth = synthFromJson (node.at ("synth"));

    if (node.contains ("group") && node.at ("group").is_boolean())
        track.group = node.at ("group").get<bool>();

    if (node.contains ("returns") && node.at ("returns").is_boolean())
        track.returns = node.at ("returns").get<bool>();

    if (node.contains ("output") && node.at ("output").is_string())
        track.outputId = node.at ("output").get<std::string>();

    if (track.outputId.empty())
        track.outputId = "master";

    if (node.contains ("sends") && node.at ("sends").is_array())
    {
        for (const auto& sendNode : node.at ("sends"))
        {
            Send send;
            send.targetId = requireString (sendNode, "target");
            send.levelDb = requireNumber (sendNode, "levelDb");
            send.preFader = sendNode.contains ("pre") && sendNode.at ("pre").is_boolean() && sendNode.at ("pre").get<bool>();
            track.sends.push_back (std::move (send));
        }
    }

    if (node.contains ("instrument") && node.at ("instrument").is_string())
    {
        const auto name = node.at ("instrument").get<std::string>();

        if (name == "sampler")
            track.instrument = InstrumentKind::Sampler;
        else if (name == "fm")
            track.instrument = InstrumentKind::Fm;
        else if (name == "drum")
            track.instrument = InstrumentKind::Drum;
    }

    if (node.contains ("zones") && node.at ("zones").is_array())
    {
        for (const auto& zoneNode : node.at ("zones"))
        {
            SamplerZone zone;
            zone.file = requireString (zoneNode, "file");
            zone.rootNote = std::clamp (requireInt (zoneNode, "root"), 0, 127);
            zone.lowNote = std::clamp (requireInt (zoneNode, "low"), 0, 127);
            zone.highNote = std::clamp (requireInt (zoneNode, "high"), 0, 127);
            track.samplerZones.push_back (std::move (zone));
        }
    }

    if (node.contains ("automation") && node.at ("automation").is_array())
    {
        for (const auto& laneNode : node.at ("automation"))
        {
            AutomationLane lane;
            lane.parameter = requireString (laneNode, "parameter");

            if (laneNode.contains ("points") && laneNode.at ("points").is_array())
                for (const auto& pointNode : laneNode.at ("points"))
                    lane.points.push_back (AutomationPoint { requireNumber (pointNode, "quarterNotes"), requireNumber (pointNode, "value") });

            track.automation.push_back (std::move (lane));
        }
    }

    if (node.contains ("sidechain"))
        track.sidechainTrack = requireInt (node, "sidechain");

    if (node.contains ("drumRack") && node.at ("drumRack").is_array())
    {
        track.drumRack.count = 0;

        for (const auto& padNode : node.at ("drumRack"))
        {
            if (track.drumRack.count >= kMaxDrumPads)
                break;

            auto& pad = track.drumRack.pads[track.drumRack.count];
            pad.note = std::clamp (requireInt (padNode, "note"), 0, 127);
            pad.chokeGroup = std::max (0, requireInt (padNode, "choke"));
            pad.gain = static_cast<float> (requireNumber (padNode, "gain"));
            pad.tuneSemitones = static_cast<float> (requireNumber (padNode, "tune"));
            pad.chainLength = 0;

            if (padNode.contains ("chain") && padNode.at ("chain").is_array())
            {
                for (const auto& effectNode : padNode.at ("chain"))
                {
                    if (pad.chainLength >= 2)
                        break;

                    auto& effect = pad.chain[pad.chainLength];
                    effect.kind = effectFromName (requireString (effectNode, "kind").c_str());
                    effect.bypass = requireBool (effectNode, "bypass");
                    effect.p0 = requireNumber (effectNode, "p0");
                    effect.p1 = requireNumber (effectNode, "p1");
                    effect.p2 = requireNumber (effectNode, "p2");
                    effect.p3 = requireNumber (effectNode, "p3");
                    effect.p4 = requireNumber (effectNode, "p4");
                    effect.p5 = requireNumber (effectNode, "p5");
                    ++pad.chainLength;
                }
            }

            ++track.drumRack.count;
        }
    }

    if (node.contains ("effects") && node.at ("effects").is_array())
    {
        for (const auto& effectNode : node.at ("effects"))
        {
            EffectSlot effect;
            effect.kind = effectFromName (requireString (effectNode, "kind").c_str());
            effect.bypass = requireBool (effectNode, "bypass");
            effect.p0 = requireNumber (effectNode, "p0");
            effect.p1 = requireNumber (effectNode, "p1");
            effect.p2 = requireNumber (effectNode, "p2");
            effect.p3 = requireNumber (effectNode, "p3");
            effect.p4 = requireNumber (effectNode, "p4");
            effect.p5 = requireNumber (effectNode, "p5");
            track.effects.push_back (effect);
        }
    }

    if (node.contains ("modulator") && node.at ("modulator").is_object())
    {
        const auto& modulator = node.at ("modulator");
        track.modulator.enabled = requireBool (modulator, "enabled");
        track.modulator.rateHz = requireNumber (modulator, "rateHz");
        track.modulator.depth = requireNumber (modulator, "depth");
        track.modulator.target = requireString (modulator, "target");
    }

    if (node.contains ("rack") && node.at ("rack").is_object())
    {
        const auto& rackNode = node.at ("rack");

        if (rackNode.contains ("chains") && rackNode.at ("chains").is_array())
        {
            for (const auto& chainNode : rackNode.at ("chains"))
            {
                DeviceChain chain;
                chain.name = requireString (chainNode, "name");
                chain.gain = static_cast<float> (requireNumber (chainNode, "gain"));

                if (chainNode.contains ("effects") && chainNode.at ("effects").is_array())
                {
                    for (const auto& effectNode : chainNode.at ("effects"))
                    {
                        EffectSlot effect;
                        effect.kind = effectFromName (requireString (effectNode, "kind").c_str());
                        effect.bypass = requireBool (effectNode, "bypass");
                        effect.p0 = requireNumber (effectNode, "p0");
                        effect.p1 = requireNumber (effectNode, "p1");
                        effect.p2 = requireNumber (effectNode, "p2");
                        effect.p3 = requireNumber (effectNode, "p3");
                        effect.p4 = requireNumber (effectNode, "p4");
                        effect.p5 = requireNumber (effectNode, "p5");
                        chain.effects.push_back (effect);
                    }
                }

                track.rack.chains.push_back (std::move (chain));
            }
        }

        if (rackNode.contains ("macros") && rackNode.at ("macros").is_array())
        {
            for (const auto& macroNode : rackNode.at ("macros"))
            {
                DeviceMacro macro;
                macro.name = requireString (macroNode, "name");
                macro.value = requireNumber (macroNode, "value");

                if (macroNode.contains ("assignments") && macroNode.at ("assignments").is_array())
                {
                    for (const auto& assignmentNode : macroNode.at ("assignments"))
                    {
                        MacroAssignment assignment;
                        assignment.chain = requireInt (assignmentNode, "chain");
                        assignment.effect = requireInt (assignmentNode, "effect");
                        assignment.parameter = requireInt (assignmentNode, "parameter");
                        assignment.minimum = requireNumber (assignmentNode, "minimum");
                        assignment.maximum = requireNumber (assignmentNode, "maximum");
                        macro.assignments.push_back (assignment);
                    }
                }

                track.rack.macros.push_back (std::move (macro));
            }
        }
    }

    return track;
}

const char* waveName (OscillatorWave wave)
{
    switch (wave)
    {
        case OscillatorWave::Sine: return "sine";
        case OscillatorWave::Square: return "square";
        case OscillatorWave::Triangle: return "triangle";
        case OscillatorWave::Saw: break;
    }

    return "saw";
}

OscillatorWave waveFromName (const std::string& name)
{
    if (name == "sine")
        return OscillatorWave::Sine;

    if (name == "square")
        return OscillatorWave::Square;

    if (name == "triangle")
        return OscillatorWave::Triangle;

    return OscillatorWave::Saw;
}

json synthToJson (const SynthPatch& patch)
{
    return json {
        { "attack", patch.attackSeconds },
        { "cutoffHz", patch.cutoffHz },
        { "decay", patch.decaySeconds },
        { "detuneCents", patch.detuneCents },
        { "release", patch.releaseSeconds },
        { "resonance", patch.resonance },
        { "sustain", patch.sustain },
        { "wave", waveName (patch.wave) }
    };
}

SynthPatch synthFromJson (const json& node)
{
    requireObject (node, "synth");
    SynthPatch patch;
    patch.wave = waveFromName (requireString (node, "wave"));
    patch.detuneCents = requireNumber (node, "detuneCents");
    patch.cutoffHz = requireNumber (node, "cutoffHz");
    patch.resonance = requireNumber (node, "resonance");
    patch.attackSeconds = requireNumber (node, "attack");
    patch.decaySeconds = requireNumber (node, "decay");
    patch.sustain = requireNumber (node, "sustain");
    patch.releaseSeconds = requireNumber (node, "release");
    return patch;
}

json effectSlotToJson (const EffectSlot& effect)
{
    return json {
        { "bypass", effect.bypass },
        { "kind", effectName (effect.kind) },
        { "p0", effect.p0 },
        { "p1", effect.p1 },
        { "p2", effect.p2 },
        { "p3", effect.p3 },
        { "p4", effect.p4 },
        { "p5", effect.p5 }
    };
}

json midiEffectsToJson (const std::vector<MidiEffect>& effects)
{
    json values = json::array();

    for (const auto& effect : effects)
    {
        values.push_back (json {
            { "chord", chordKindName (effect.chord) },
            { "division", effect.division },
            { "enabled", effect.enabled },
            { "kind", midiEffectKindName (effect.kind) },
            { "octaves", effect.octaves },
            { "root", effect.root },
            { "scale", scaleKindName (effect.scale) },
            { "semitones", effect.semitones },
            { "velocityAdd", effect.velocityAdd },
            { "velocityScale", effect.velocityScale }
        });
    }

    return values;
}

json pluginsToJson (const std::vector<PluginInsert>& plugins)
{
    json values = json::array();

    for (const auto& plugin : plugins)
        values.push_back (json { { "identifier", plugin.identifier }, { "name", plugin.name }, { "state", plugin.state } });

    return values;
}

json rackToJson (const DeviceRack& rack)
{
    json chains = json::array();

    for (const auto& chain : rack.chains)
    {
        json effects = json::array();

        for (const auto& effect : chain.effects)
            effects.push_back (effectSlotToJson (effect));

        chains.push_back (json { { "effects", std::move (effects) }, { "gain", chain.gain }, { "name", chain.name } });
    }

    json macros = json::array();

    for (const auto& macro : rack.macros)
    {
        json assignments = json::array();

        for (const auto& assignment : macro.assignments)
        {
            assignments.push_back (json {
                { "chain", assignment.chain },
                { "effect", assignment.effect },
                { "maximum", assignment.maximum },
                { "minimum", assignment.minimum },
                { "parameter", assignment.parameter }
            });
        }

        macros.push_back (json { { "assignments", std::move (assignments) }, { "name", macro.name }, { "value", macro.value } });
    }

    return json { { "chains", std::move (chains) }, { "macros", std::move (macros) } };
}

json trackToJson (const Track& track)
{
    json clips = json::array();

    for (const auto& clip : track.clips)
        clips.push_back (clipToJson (clip));

    json session = json::array();

    for (const auto& slot : track.session)
    {
        json slotJson = { { "clip", clipToJson (slot.clip) }, { "scene", slot.scene } };

        if (slot.launch != ClipLaunchMode::Trigger)
            slotJson["launch"] = static_cast<int> (slot.launch);

        if (slot.follow != FollowActionKind::None)
            slotJson["follow"] = static_cast<int> (slot.follow);

        if (slot.followAfterQuarters != 4.0)
            slotJson["followAfter"] = slot.followAfterQuarters;

        if (slot.repeatQuarters != 1.0)
            slotJson["repeat"] = slot.repeatQuarters;

        session.push_back (std::move (slotJson));
    }

    json trackJson = {
        { "clips", std::move (clips) },
        { "id", track.id },
        { "mixer", mixerToJson (track.mixer) },
        { "name", track.name },
        { "plugin", track.plugins.empty() ? track.pluginIdentifier : track.plugins.front().identifier },
        { "pluginName", track.plugins.empty() ? track.pluginName : track.plugins.front().name },
        { "pluginState", track.plugins.empty() ? track.pluginState : track.plugins.front().state },
        { "midiEffects", midiEffectsToJson (track.midiEffects) },
        { "plugins", pluginsToJson (track.plugins) },
        { "session", std::move (session) },
        { "type", track.type == TrackType::Audio ? "audio" : "midi" }
    };

    if (track.type == TrackType::Midi)
        trackJson["synth"] = synthToJson (track.synth);

    trackJson["group"] = track.group;
    trackJson["returns"] = track.returns;
    trackJson["output"] = track.outputId.empty() ? "master" : track.outputId;
    json sends = json::array();

    for (const auto& send : track.sends)
        sends.push_back (json { { "levelDb", send.levelDb }, { "pre", send.preFader }, { "target", send.targetId } });

    trackJson["sends"] = std::move (sends);
    json effects = json::array();

    for (const auto& effect : track.effects)
    {
        effects.push_back (json {
            { "bypass", effect.bypass },
            { "kind", effectName (effect.kind) },
            { "p0", effect.p0 },
            { "p1", effect.p1 },
            { "p2", effect.p2 },
            { "p3", effect.p3 },
            { "p4", effect.p4 },
            { "p5", effect.p5 }
        });
    }

    trackJson["effects"] = std::move (effects);
    trackJson["modulator"] = json {
        { "depth", track.modulator.depth },
        { "enabled", track.modulator.enabled },
        { "rateHz", track.modulator.rateHz },
        { "target", track.modulator.target }
    };
    trackJson["rack"] = rackToJson (track.rack);
    const char* instrument = "synth";

    if (track.instrument == InstrumentKind::Sampler)
        instrument = "sampler";
    else if (track.instrument == InstrumentKind::Fm)
        instrument = "fm";
    else if (track.instrument == InstrumentKind::Drum)
        instrument = "drum";

    trackJson["instrument"] = instrument;
    json zones = json::array();

    for (const auto& zone : track.samplerZones)
    {
        zones.push_back (json {
            { "file", zone.file },
            { "high", zone.highNote },
            { "low", zone.lowNote },
            { "root", zone.rootNote }
        });
    }

    trackJson["zones"] = std::move (zones);
    json lanes = json::array();

    for (const auto& lane : track.automation)
    {
        json points = json::array();

        for (const auto& point : lane.points)
            points.push_back (json { { "quarterNotes", point.quarterNotes }, { "value", point.value } });

        lanes.push_back (json { { "parameter", lane.parameter }, { "points", std::move (points) } });
    }

    trackJson["automation"] = std::move (lanes);

    if (track.sidechainTrack >= 0)
        trackJson["sidechain"] = track.sidechainTrack;

    if (track.drumRack.count > 0)
    {
        json pads = json::array();

        for (int index = 0; index < track.drumRack.count && index < kMaxDrumPads; ++index)
        {
            const auto& pad = track.drumRack.pads[index];
            json chain = json::array();

            for (int slot = 0; slot < pad.chainLength && slot < 2; ++slot)
            {
                const auto& effect = pad.chain[slot];
                chain.push_back (json {
                    { "bypass", effect.bypass },
                    { "kind", effectName (effect.kind) },
                    { "p0", effect.p0 },
                    { "p1", effect.p1 },
                    { "p2", effect.p2 },
                    { "p3", effect.p3 },
                    { "p4", effect.p4 },
                    { "p5", effect.p5 }
                });
            }

            pads.push_back (json {
                { "chain", std::move (chain) },
                { "choke", pad.chokeGroup },
                { "gain", pad.gain },
                { "note", pad.note },
                { "tune", pad.tuneSemitones }
            });
        }

        trackJson["drumRack"] = std::move (pads);
    }

    return trackJson;
}

} // namespace

std::string projectToJson (const Project& project)
{
    json tracks = json::array();

    for (const auto& track : project.tracks)
        tracks.push_back (trackToJson (track));

    json document = {
        { "format", kProjectFormatId },
        { "loop", json {
            { "enabled", project.loopEnabled },
            { "endQuarterNotes", project.loopEndQuarterNotes },
            { "startQuarterNotes", project.loopStartQuarterNotes }
        } },
        { "master", json { { "gainDb", project.master.gainDb } } },
        { "name", project.name },
        { "tempo", project.tempoBpm },
        { "timeSignature", json {
            { "denominator", project.timeSignature.denominator },
            { "numerator", project.timeSignature.numerator }
        } },
        { "tracks", std::move (tracks) },
        { "version", kProjectFormatVersion },
        { "countInBars", project.countInBars }
    };

    bool customSceneTempo = false;

    for (double tempo : project.sceneTempoBpm)
        customSceneTempo = customSceneTempo || tempo > 0.0;

    if (customSceneTempo)
    {
        json tempos = json::array();

        for (double tempo : project.sceneTempoBpm)
            tempos.push_back (tempo);

        document["sceneTempo"] = std::move (tempos);
    }

    return document.dump (2) + '\n';
}

std::optional<Project> projectFromJson (std::string_view text, std::string* error)
{
    try
    {
        const json document = json::parse (text);
        requireObject (document, "project");

        if (requireString (document, "format") != kProjectFormatId)
            fail ("format must be '" + std::string (kProjectFormatId) + "'");

        if (requireInt (document, "version") != kProjectFormatVersion)
            fail ("unsupported project version");

        Project project;
        project.name = requireString (document, "name");

        if (project.name.empty())
            project.name = "Untitled";

        project.tempoBpm = Timing::clampBpm (requireNumber (document, "tempo"));

        if (! document.contains ("timeSignature"))
            fail ("missing timeSignature");

        const auto& signature = requireObject (document.at ("timeSignature"), "timeSignature");
        project.timeSignature.numerator = requireInt (signature, "numerator");
        project.timeSignature.denominator = requireInt (signature, "denominator");

        if (! project.timeSignature.isValid())
            fail ("time signature is not valid");

        if (! document.contains ("loop"))
            fail ("missing loop");

        const auto& loop = requireObject (document.at ("loop"), "loop");
        project.loopEnabled = requireBool (loop, "enabled");
        project.loopStartQuarterNotes = requireNumber (loop, "startQuarterNotes");
        project.loopEndQuarterNotes = requireNumber (loop, "endQuarterNotes");

        if (std::isnan (project.loopStartQuarterNotes) || std::isnan (project.loopEndQuarterNotes)
            || project.loopStartQuarterNotes < 0.0 || project.loopEndQuarterNotes <= project.loopStartQuarterNotes)
            fail ("loop region must be a positive span of quarter notes");

        if (! document.contains ("master"))
            fail ("missing master");

        project.master.gainDb = requireNumber (requireObject (document.at ("master"), "master"), "gainDb");
        clampMixer (project.master);

        if (document.contains ("countInBars"))
            project.countInBars = std::clamp (requireInt (document, "countInBars"), 0, 8);

        if (document.contains ("sceneTempo") && document.at ("sceneTempo").is_array())
        {
            int index = 0;

            for (const auto& tempo : document.at ("sceneTempo"))
            {
                if (index >= kSessionScenes)
                    break;

                if (! tempo.is_number())
                    fail ("scene tempo must be a number");

                project.sceneTempoBpm[index] = tempo.get<double>() > 0.0 ? Timing::clampBpm (tempo.get<double>()) : 0.0;
                ++index;
            }
        }

        if (! document.contains ("tracks") || ! document.at ("tracks").is_array())
            fail ("tracks must be an array");

        std::unordered_set<std::string> ids;

        for (const auto& track : document.at ("tracks"))
            project.tracks.push_back (trackFromJson (track, ids));

        return project;
    }
    catch (const std::exception& exception)
    {
        if (error != nullptr)
            *error = exception.what();

        return std::nullopt;
    }
}

} // namespace waveform
