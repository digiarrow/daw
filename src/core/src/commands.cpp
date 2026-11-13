#include "waveform/commands.hpp"
#include "waveform/routing.hpp"

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

SetSessionSlotCommand::SetSessionSlotCommand (std::string trackId, int scene, Clip clip)
    : trackId_ (std::move (trackId)), scene_ (std::clamp (scene, 0, kSessionScenes - 1)), next_ (std::move (clip))
{
    if (auto* midi = std::get_if<MidiClip> (&next_))
    {
        if (midi->id.empty())
            midi->id = makeId();

        if (midi->name.empty())
            midi->name = "Scene";

        if (! (midi->lengthQuarterNotes > 0.0))
            midi->lengthQuarterNotes = 4.0;
    }

    if (auto* audio = std::get_if<AudioClip> (&next_))
    {
        if (audio->id.empty())
            audio->id = makeId();

        if (audio->name.empty())
            audio->name = "Scene";
    }
}

const std::string& SetSessionSlotCommand::clipId() const
{
    return waveform::clipId (next_);
}

void SetSessionSlotCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    hadPrevious_ = false;

    for (const auto& slot : track->session)
    {
        if (slot.scene == scene_)
        {
            previous_ = slot;
            hadPrevious_ = true;
            break;
        }
    }

    track->session.erase (std::remove_if (track->session.begin(), track->session.end(),
                                          [&] (const SessionSlot& slot) { return slot.scene == scene_; }),
                          track->session.end());
    track->session.push_back (SessionSlot { scene_, next_ });
}

void SetSessionSlotCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
    {
        track->session.erase (std::remove_if (track->session.begin(), track->session.end(),
                                              [&] (const SessionSlot& slot) { return slot.scene == scene_; }),
                              track->session.end());

        if (hadPrevious_)
            track->session.push_back (previous_);
    }
}

const char* SetSessionSlotCommand::name() const { return "Session Slot"; }

ClearSessionSlotCommand::ClearSessionSlotCommand (std::string trackId, int scene)
    : trackId_ (std::move (trackId)), scene_ (std::clamp (scene, 0, kSessionScenes - 1))
{
}

void ClearSessionSlotCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = false;

    if (track == nullptr)
        return;

    for (const auto& slot : track->session)
    {
        if (slot.scene == scene_)
        {
            previous_ = slot;
            applied_ = true;
            break;
        }
    }

    if (! applied_)
        return;

    track->session.erase (std::remove_if (track->session.begin(), track->session.end(),
                                          [&] (const SessionSlot& slot) { return slot.scene == scene_; }),
                          track->session.end());
}

void ClearSessionSlotCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        track->session.push_back (previous_);
}

const char* ClearSessionSlotCommand::name() const { return "Clear Scene"; }

namespace
{

MidiClip* findMidiClip (Project& project, const std::string& trackId, const std::string& clip)
{
    auto* track = findTrack (project, trackId);

    if (track == nullptr)
        return nullptr;

    for (auto& candidate : track->clips)
        if (auto* midi = std::get_if<MidiClip> (&candidate); midi != nullptr && midi->id == clip)
            return midi;

    for (auto& slot : track->session)
        if (auto* midi = std::get_if<MidiClip> (&slot.clip); midi != nullptr && midi->id == clip)
            return midi;

    return nullptr;
}

double& clipStart (Clip& clip)
{
    if (auto* audio = std::get_if<AudioClip> (&clip))
        return audio->startQuarterNotes;

    return std::get<MidiClip> (clip).startQuarterNotes;
}

} // namespace

SetMidiNotesCommand::SetMidiNotesCommand (std::string trackId, std::string clipId, std::vector<MidiNote> notes)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId)), next_ (std::move (notes))
{
    for (auto& note : next_)
    {
        note.note = std::clamp (note.note, 0, 127);
        note.velocity = std::clamp (note.velocity, 1, 127);
        note.channel = std::clamp (note.channel, 1, 16);
        note.startQuarterNotes = std::max (0.0, note.startQuarterNotes);

        if (! (note.lengthQuarterNotes > 0.0) || std::isnan (note.lengthQuarterNotes))
            note.lengthQuarterNotes = 0.25;
    }
}

void SetMidiNotesCommand::apply (Project& project)
{
    auto* clip = findMidiClip (project, trackId_, clipId_);
    applied_ = clip != nullptr;

    if (clip == nullptr)
        return;

    previous_ = clip->notes;
    clip->notes = next_;
}

void SetMidiNotesCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* clip = findMidiClip (project, trackId_, clipId_))
        clip->notes = previous_;
}

const char* SetMidiNotesCommand::name() const { return "Edit Notes"; }

MoveClipCommand::MoveClipCommand (std::string trackId, std::string clipId, double startQuarterNotes)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId)), next_ (std::max (0.0, startQuarterNotes))
{
}

void MoveClipCommand::apply (Project& project)
{
    applied_ = false;
    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    for (auto& clip : track->clips)
    {
        if (clipId (clip) != clipId_)
            continue;

        previous_ = clipStart (clip);
        clipStart (clip) = next_;
        applied_ = true;
        return;
    }
}

void MoveClipCommand::revert (Project& project)
{
    if (! applied_)
        return;

    auto* track = findTrack (project, trackId_);

    if (track == nullptr)
        return;

    for (auto& clip : track->clips)
        if (clipId (clip) == clipId_)
            clipStart (clip) = previous_;
}

const char* MoveClipCommand::name() const { return "Move Clip"; }

SetSynthPatchCommand::SetSynthPatchCommand (std::string trackId, SynthPatch patch)
    : trackId_ (std::move (trackId)), next_ (patch)
{
    next_.detuneCents = std::clamp (next_.detuneCents, -50.0, 50.0);
    next_.cutoffHz = std::clamp (next_.cutoffHz, 40.0, 16000.0);
    next_.resonance = std::clamp (next_.resonance, 0.0, 0.9);
    next_.attackSeconds = std::clamp (next_.attackSeconds, 0.0, 4.0);
    next_.decaySeconds = std::clamp (next_.decaySeconds, 0.0, 4.0);
    next_.sustain = std::clamp (next_.sustain, 0.0, 1.0);
    next_.releaseSeconds = std::clamp (next_.releaseSeconds, 0.0, 8.0);
}

void SetSynthPatchCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr && track->type == TrackType::Midi;

    if (! applied_)
        return;

    previous_ = track->synth;
    track->synth = next_;
}

void SetSynthPatchCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        track->synth = previous_;
}

const char* SetSynthPatchCommand::name() const { return "Instrument"; }

AddEffectCommand::AddEffectCommand (std::string trackId, EffectKind kind)
    : trackId_ (std::move (trackId)), slot_ (defaultEffect (kind))
{
}

void AddEffectCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr && track->effects.size() < static_cast<std::size_t> (kMaxInserts);

    if (applied_)
        track->effects.push_back (slot_);
}

void AddEffectCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_); track != nullptr && ! track->effects.empty())
        track->effects.pop_back();
}

const char* AddEffectCommand::name() const { return "Add Effect"; }

SetTrackOutputCommand::SetTrackOutputCommand (std::string trackId, std::string outputId)
    : trackId_ (std::move (trackId)), next_ (outputId.empty() ? "master" : std::move (outputId))
{
}

void SetTrackOutputCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    previous_ = track->outputId.empty() ? "master" : track->outputId;
    track->outputId = next_;
}

void SetTrackOutputCommand::revert (Project& project)
{
    if (applied_)
        if (auto* track = findTrack (project, trackId_))
            track->outputId = previous_;
}

const char* SetTrackOutputCommand::name() const { return "Route Track"; }

SetSendCommand::SetSendCommand (std::string trackId, int index, Send send)
    : trackId_ (std::move (trackId)), index_ (std::clamp (index, 0, kMaxSends - 1)), next_ (std::move (send))
{
}

void SetSendCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    if (static_cast<int> (track->sends.size()) <= index_)
        track->sends.resize (static_cast<std::size_t> (index_ + 1));

    hadPrevious_ = true;
    previous_ = track->sends[static_cast<std::size_t> (index_)];
    track->sends[static_cast<std::size_t> (index_)] = next_;
}

void SetSendCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
        if (static_cast<int> (track->sends.size()) > index_)
            track->sends[static_cast<std::size_t> (index_)] = previous_;
}

const char* SetSendCommand::name() const { return "Set Send"; }

SetGroupCommand::SetGroupCommand (std::string trackId, bool group)
    : trackId_ (std::move (trackId)), next_ (group)
{
}

void SetGroupCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    previous_ = track->group;
    track->group = next_;
}

void SetGroupCommand::revert (Project& project)
{
    if (applied_)
        if (auto* track = findTrack (project, trackId_))
            track->group = previous_;
}

const char* SetGroupCommand::name() const { return "Group Track"; }

SetPluginCommand::SetPluginCommand (std::string trackId, std::string identifier, std::string pluginName, std::string state)
    : trackId_ (std::move (trackId)),
      identifier_ (std::move (identifier)),
      pluginName_ (std::move (pluginName)),
      state_ (std::move (state))
{
}

void SetPluginCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    previousId_ = track->pluginIdentifier;
    previousName_ = track->pluginName;
    previousState_ = track->pluginState;
    track->pluginIdentifier = identifier_;
    track->pluginName = pluginName_;
    track->pluginState = state_;
}

void SetPluginCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
    {
        track->pluginIdentifier = previousId_;
        track->pluginName = previousName_;
        track->pluginState = previousState_;
    }
}

const char* SetPluginCommand::name() const { return "Plug-in"; }

SetInstrumentCommand::SetInstrumentCommand (std::string trackId, InstrumentKind instrument)
    : trackId_ (std::move (trackId)), next_ (instrument)
{
}

void SetInstrumentCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr && track->type == TrackType::Midi;

    if (! applied_)
        return;

    previous_ = track->instrument;
    track->instrument = next_;
}

void SetInstrumentCommand::revert (Project& project)
{
    if (applied_)
        if (auto* track = findTrack (project, trackId_))
            track->instrument = previous_;
}

const char* SetInstrumentCommand::name() const { return "Instrument"; }

SetAutomationCommand::SetAutomationCommand (std::string trackId, std::string parameter, std::vector<AutomationPoint> points)
    : trackId_ (std::move (trackId)), parameter_ (std::move (parameter)), next_ (std::move (points))
{
    std::sort (next_.begin(), next_.end(), [] (const AutomationPoint& left, const AutomationPoint& right) {
        return left.quarterNotes < right.quarterNotes;
    });
}

void SetAutomationCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr;

    if (! applied_)
        return;

    hadLane_ = false;

    for (const auto& lane : track->automation)
    {
        if (lane.parameter == parameter_)
        {
            previous_ = lane.points;
            hadLane_ = true;
            break;
        }
    }

    auto& lanes = track->automation;
    lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&] (const AutomationLane& lane) { return lane.parameter == parameter_; }), lanes.end());

    if (! next_.empty())
        lanes.push_back (AutomationLane { parameter_, next_ });
}

void SetAutomationCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_))
    {
        auto& lanes = track->automation;
        lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&] (const AutomationLane& lane) { return lane.parameter == parameter_; }), lanes.end());

        if (hadLane_)
            lanes.push_back (AutomationLane { parameter_, previous_ });
    }
}

const char* SetAutomationCommand::name() const { return "Automation"; }

namespace
{

AudioClip* findAudioClip (Project& project, const std::string& trackId, const std::string& clipId)
{
    auto* track = findTrack (project, trackId);

    if (track == nullptr)
        return nullptr;

    for (auto& candidate : track->clips)
        if (auto* audio = std::get_if<AudioClip> (&candidate); audio != nullptr && audio->id == clipId)
            return audio;

    return nullptr;
}

} // namespace

SetClipMarkersCommand::SetClipMarkersCommand (std::string trackId, std::string clipId, std::vector<WarpMarker> warp, std::vector<CompSegment> comp)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId)), warp_ (std::move (warp)), comp_ (std::move (comp))
{
}

void SetClipMarkersCommand::apply (Project& project)
{
    auto* clip = findAudioClip (project, trackId_, clipId_);
    applied_ = clip != nullptr;

    if (! applied_)
        return;

    previousWarp_ = clip->warp;
    previousComp_ = clip->comp;
    clip->warp = warp_;
    clip->comp = comp_;
}

void SetClipMarkersCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* clip = findAudioClip (project, trackId_, clipId_))
    {
        clip->warp = previousWarp_;
        clip->comp = previousComp_;
    }
}

const char* SetClipMarkersCommand::name() const { return "Clip Markers"; }

AddAudioTakeCommand::AddAudioTakeCommand (std::string trackId, std::string clipId, ClipTake take)
    : trackId_ (std::move (trackId)), clipId_ (std::move (clipId)), take_ (std::move (take))
{
    if (take_.name.empty())
        take_.name = "Take";
}

void AddAudioTakeCommand::apply (Project& project)
{
    auto* clip = findAudioClip (project, trackId_, clipId_);
    applied_ = clip != nullptr && ! take_.file.empty();

    if (applied_)
        clip->takes.push_back (take_);
}

void AddAudioTakeCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* clip = findAudioClip (project, trackId_, clipId_); clip != nullptr && ! clip->takes.empty())
        clip->takes.pop_back();
}

const char* AddAudioTakeCommand::name() const { return "Add Take"; }

AddSamplerZoneCommand::AddSamplerZoneCommand (std::string trackId, SamplerZone zone)
    : trackId_ (std::move (trackId)), zone_ (std::move (zone))
{
}

void AddSamplerZoneCommand::apply (Project& project)
{
    auto* track = findTrack (project, trackId_);
    applied_ = track != nullptr && track->type == TrackType::Midi;

    if (! applied_)
        return;

    track->instrument = InstrumentKind::Sampler;
    track->samplerZones.push_back (zone_);
}

void AddSamplerZoneCommand::revert (Project& project)
{
    if (! applied_)
        return;

    if (auto* track = findTrack (project, trackId_); track != nullptr && ! track->samplerZones.empty())
        track->samplerZones.pop_back();
}

const char* AddSamplerZoneCommand::name() const { return "Sampler Zone"; }

} // namespace waveform
