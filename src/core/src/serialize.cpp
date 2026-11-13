#include "waveform/effects.hpp"
#include "waveform/serialize.hpp"
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

json audioClipToJson (const AudioClip& clip)
{
    return json {
        { "file", clip.file },
        { "gainDb", clip.gainDb },
        { "id", clip.id },
        { "lengthSeconds", clip.lengthSeconds },
        { "name", clip.name },
        { "offsetSeconds", clip.offsetSeconds },
        { "startQuarterNotes", clip.startQuarterNotes },
        { "type", "audio" }
    };
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
            { "startQuarterNotes", note.startQuarterNotes },
            { "velocity", note.velocity }
        });
    }

    return json {
        { "id", clip.id },
        { "lengthQuarterNotes", clip.lengthQuarterNotes },
        { "name", clip.name },
        { "notes", std::move (notes) },
        { "startQuarterNotes", clip.startQuarterNotes },
        { "type", "midi" }
    };
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

    if (track.type == TrackType::Midi && node.contains ("synth"))
        track.synth = synthFromJson (node.at ("synth"));

    if (node.contains ("group") && node.at ("group").is_boolean())
        track.group = node.at ("group").get<bool>();

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
            track.sends.push_back (std::move (send));
        }
    }

    if (node.contains ("instrument") && node.at ("instrument").is_string()
        && node.at ("instrument").get<std::string>() == "sampler")
        track.instrument = InstrumentKind::Sampler;

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

json trackToJson (const Track& track)
{
    json clips = json::array();

    for (const auto& clip : track.clips)
        clips.push_back (clipToJson (clip));

    json trackJson = {
        { "clips", std::move (clips) },
        { "id", track.id },
        { "mixer", mixerToJson (track.mixer) },
        { "name", track.name },
        { "type", track.type == TrackType::Audio ? "audio" : "midi" }
    };

    if (track.type == TrackType::Midi)
        trackJson["synth"] = synthToJson (track.synth);

    trackJson["group"] = track.group;
    trackJson["output"] = track.outputId.empty() ? "master" : track.outputId;
    json sends = json::array();

    for (const auto& send : track.sends)
        sends.push_back (json { { "levelDb", send.levelDb }, { "target", send.targetId } });

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
    trackJson["instrument"] = track.instrument == InstrumentKind::Sampler ? "sampler" : "synth";
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
