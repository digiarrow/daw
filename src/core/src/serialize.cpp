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

    return track;
}

json trackToJson (const Track& track)
{
    json clips = json::array();

    for (const auto& clip : track.clips)
        clips.push_back (clipToJson (clip));

    return json {
        { "clips", std::move (clips) },
        { "id", track.id },
        { "mixer", mixerToJson (track.mixer) },
        { "name", track.name },
        { "type", track.type == TrackType::Audio ? "audio" : "midi" }
    };
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
        { "version", kProjectFormatVersion }
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
