#include "waveform/commands.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace waveform
{
namespace
{

void eraseClip (Track& track, const std::string& clip)
{
    std::erase_if (track.clips, [&] (const Clip& candidate) { return clipId (candidate) == clip; });
}

} // namespace

History::History (std::size_t limit)
    : limit_ (std::max<std::size_t> (1, limit))
{
}

void History::execute (Project& project, std::unique_ptr<EditCommand> command)
{
    if (command == nullptr)
        return;

    command->apply (project);
    undo_.push_back (std::move (command));
    redo_.clear();
    trim();
}

bool History::undo (Project& project)
{
    if (undo_.empty())
        return false;

    undo_.back()->revert (project);
    redo_.push_back (std::move (undo_.back()));
    undo_.pop_back();
    return true;
}

bool History::redo (Project& project)
{
    if (redo_.empty())
        return false;

    redo_.back()->apply (project);
    undo_.push_back (std::move (redo_.back()));
    redo_.pop_back();
    trim();
    return true;
}

bool History::canUndo() const noexcept { return ! undo_.empty(); }
bool History::canRedo() const noexcept { return ! redo_.empty(); }

const char* History::undoName() const noexcept
{
    return undo_.empty() ? "" : undo_.back()->name();
}

const char* History::redoName() const noexcept
{
    return redo_.empty() ? "" : redo_.back()->name();
}

void History::clear()
{
    undo_.clear();
    redo_.clear();
}

void History::trim()
{
    while (undo_.size() > limit_)
        undo_.erase (undo_.begin());
}

SetTempoCommand::SetTempoCommand (double tempoBpm)
    : next_ (Timing::clampBpm (tempoBpm))
{
}

void SetTempoCommand::apply (Project& project)
{
    previous_ = project.tempoBpm;
    project.tempoBpm = next_;
}

void SetTempoCommand::revert (Project& project) { project.tempoBpm = previous_; }
const char* SetTempoCommand::name() const { return "Set Tempo"; }

SetTimeSignatureCommand::SetTimeSignatureCommand (int numerator, int denominator)
    : next_ { numerator, denominator }
{
}

void SetTimeSignatureCommand::apply (Project& project)
{
    previous_ = project.timeSignature;

    if (next_.isValid())
        project.timeSignature = next_;
}

void SetTimeSignatureCommand::revert (Project& project) { project.timeSignature = previous_; }
const char* SetTimeSignatureCommand::name() const { return "Set Time Signature"; }

SetLoopCommand::SetLoopCommand (bool enabled, double startQuarterNotes, double endQuarterNotes)
    : enabled_ (enabled), start_ (startQuarterNotes), end_ (endQuarterNotes)
{
    if (std::isnan (start_))
        start_ = 0.0;

    if (std::isnan (end_))
        end_ = start_;

    if (end_ < start_)
        std::swap (start_, end_);

    start_ = std::max (0.0, start_);
}

void SetLoopCommand::apply (Project& project)
{
    previousEnabled_ = project.loopEnabled;
    previousStart_ = project.loopStartQuarterNotes;
    previousEnd_ = project.loopEndQuarterNotes;
    project.loopEnabled = enabled_;
    project.loopStartQuarterNotes = start_;
    project.loopEndQuarterNotes = end_;
}

void SetLoopCommand::revert (Project& project)
{
    project.loopEnabled = previousEnabled_;
    project.loopStartQuarterNotes = previousStart_;
    project.loopEndQuarterNotes = previousEnd_;
}

const char* SetLoopCommand::name() const { return "Set Loop"; }

RenameProjectCommand::RenameProjectCommand (std::string name)
    : next_ (std::move (name))
{
    if (next_.empty())
        next_ = "Untitled";
}

void RenameProjectCommand::apply (Project& project)
{
    previous_ = project.name;
    project.name = next_;
}

void RenameProjectCommand::revert (Project& project) { project.name = previous_; }
const char* RenameProjectCommand::name() const { return "Rename Project"; }

SetMasterGainCommand::SetMasterGainCommand (double gainDb)
    : next_ (gainDb)
{
    MixerState mixer;
    mixer.gainDb = next_;
    clampMixer (mixer);
    next_ = mixer.gainDb;
}

void SetMasterGainCommand::apply (Project& project)
{
    previous_ = project.master.gainDb;
    project.master.gainDb = next_;
}

void SetMasterGainCommand::revert (Project& project) { project.master.gainDb = previous_; }
const char* SetMasterGainCommand::name() const { return "Master Gain"; }

AddTrackCommand::AddTrackCommand (TrackType type, std::string name)
    : type_ (type), name_ (std::move (name)), id_ (makeId())
{
    if (name_.empty())
        name_ = type_ == TrackType::Audio ? "Audio" : "MIDI";
}

void AddTrackCommand::apply (Project& project)
{
    if (findTrack (project, id_) != nullptr)
        return;

    Track track;
    track.id = id_;
    track.name = name_;
    track.type = type_;
    project.tracks.push_back (std::move (track));
}

void AddTrackCommand::revert (Project& project)
{
    std::erase_if (project.tracks, [&] (const Track& track) { return track.id == id_; });
}

const char* AddTrackCommand::name() const { return "Add Track"; }

RemoveTrackCommand::RemoveTrackCommand (std::string trackId)
    : id_ (std::move (trackId))
{
}

void RemoveTrackCommand::apply (Project& project)
{
    const auto it = std::find_if (project.tracks.begin(), project.tracks.end(),
                                  [&] (const Track& track) { return track.id == id_; });

    if (it == project.tracks.end())
    {
        applied_ = false;
        return;
    }

    index_ = static_cast<int> (std::distance (project.tracks.begin(), it));
    removed_ = *it;
    project.tracks.erase (it);
    applied_ = true;
}

void RemoveTrackCommand::revert (Project& project)
{
    if (! applied_ || findTrack (project, id_) != nullptr)
        return;

    const auto index = std::clamp (index_, 0, static_cast<int> (project.tracks.size()));
    project.tracks.insert (project.tracks.begin() + index, removed_);
}

const char* RemoveTrackCommand::name() const { return "Remove Track"; }

SetMixerCommand::SetMixerCommand (std::string trackId, MixerState mixer)
    : trackId_ (std::move (trackId)), next_ (mixer)
{
    clampMixer (next_);
}

void SetMixerCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
    {
        applied_ = false;
        return;
    }

    previous_ = track->mixer;
    track->mixer = next_;
    applied_ = true;
}

void SetMixerCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        track->mixer = previous_;
}

const char* SetMixerCommand::name() const { return "Mixer"; }

AddAudioClipCommand::AddAudioClipCommand (std::string trackId, AudioClip clip)
    : trackId_ (std::move (trackId)), clip_ (std::move (clip))
{
    if (clip_.id.empty())
        clip_.id = makeId();

    if (clip_.name.empty())
        clip_.name = "Audio clip";

    if (std::isnan (clip_.startQuarterNotes) || clip_.startQuarterNotes < 0.0)
        clip_.startQuarterNotes = 0.0;

    if (std::isnan (clip_.offsetSeconds) || clip_.offsetSeconds < 0.0)
        clip_.offsetSeconds = 0.0;

    if (! (clip_.lengthSeconds > 0.0) || std::isnan (clip_.lengthSeconds))
        clip_.lengthSeconds = 0.001;

    MixerState gain;
    gain.gainDb = clip_.gainDb;
    clampMixer (gain);
    clip_.gainDb = gain.gainDb;
}

void AddAudioClipCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = false;

    if (track == nullptr || track->type != TrackType::Audio)
        return;

    if (std::any_of (track->clips.begin(), track->clips.end(),
                     [&] (const Clip& clip) { return waveform::clipId (clip) == clip_.id; }))
        return;

    track->clips.emplace_back (clip_);
    applied_ = true;
}

void AddAudioClipCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        eraseClip (*track, clip_.id);
}

const char* AddAudioClipCommand::name() const { return "Add Audio Clip"; }

AddMidiClipCommand::AddMidiClipCommand (std::string trackId, MidiClip clip)
    : trackId_ (std::move (trackId)), clip_ (std::move (clip))
{
    if (clip_.id.empty())
        clip_.id = makeId();

    if (clip_.name.empty())
        clip_.name = "MIDI clip";

    if (std::isnan (clip_.startQuarterNotes) || clip_.startQuarterNotes < 0.0)
        clip_.startQuarterNotes = 0.0;

    if (! (clip_.lengthQuarterNotes > 0.0) || std::isnan (clip_.lengthQuarterNotes))
        clip_.lengthQuarterNotes = 1.0;
}

void AddMidiClipCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = false;

    if (track == nullptr || track->type != TrackType::Midi)
        return;

    if (std::any_of (track->clips.begin(), track->clips.end(),
                     [&] (const Clip& clip) { return waveform::clipId (clip) == clip_.id; }))
        return;

    track->clips.emplace_back (clip_);
    applied_ = true;
}

void AddMidiClipCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        eraseClip (*track, clip_.id);
}

const char* AddMidiClipCommand::name() const { return "Add MIDI Clip"; }

AddMidiNoteCommand::AddMidiNoteCommand (std::string trackId, std::string clipId, MidiNote note)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId)), note_ (note)
{
    note_.note = std::clamp (note_.note, 0, 127);
    note_.velocity = std::clamp (note_.velocity, 1, 127);
    note_.channel = std::clamp (note_.channel, 1, 16);

    if (std::isnan (note_.startQuarterNotes) || note_.startQuarterNotes < 0.0)
        note_.startQuarterNotes = 0.0;

    if (! (note_.lengthQuarterNotes > 0.0) || std::isnan (note_.lengthQuarterNotes))
        note_.lengthQuarterNotes = 0.25;
}

void AddMidiNoteCommand::apply (Project& project)
{
    applied_ = false;
    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    for (auto& clip : track->clips)
    {
        auto* midi = std::get_if<MidiClip> (&clip);

        if (midi == nullptr || midi->id != clipId_)
            continue;

        midi->notes.push_back (note_);
        applied_ = true;
        return;
    }
}

void AddMidiNoteCommand::revert (Project& project)
{
    if (! applied_)
        return;

    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    for (auto& clip : track->clips)
    {
        auto* midi = std::get_if<MidiClip> (&clip);

        if (midi == nullptr || midi->id != clipId_ || midi->notes.empty())
            continue;

        midi->notes.pop_back();
        return;
    }
}

const char* AddMidiNoteCommand::name() const { return "Add MIDI Note"; }

RemoveClipCommand::RemoveClipCommand (std::string trackId, std::string clipId)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId))
{
}

void RemoveClipCommand::apply (Project& project)
{
    applied_ = false;
    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    const auto it = std::find_if (track->clips.begin(), track->clips.end(),
                                  [&] (const Clip& clip) { return clipId (clip) == clipId_; });

    if (it == track->clips.end())
        return;

    index_ = static_cast<int> (std::distance (track->clips.begin(), it));
    removed_ = *it;
    track->clips.erase (it);
    applied_ = true;
}

void RemoveClipCommand::revert (Project& project)
{
    if (! applied_)
        return;

    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    if (std::any_of (track->clips.begin(), track->clips.end(),
                     [&] (const Clip& clip) { return waveform::clipId (clip) == clipId_; }))
        return;

    const auto index = std::clamp (index_, 0, static_cast<int> (track->clips.size()));
    track->clips.insert (track->clips.begin() + index, removed_);
}

const char* RemoveClipCommand::name() const { return "Remove Clip"; }

} // namespace waveform
