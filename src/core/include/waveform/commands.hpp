#pragma once

#include "waveform/project.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace waveform
{

/// A reversible edit of the document model. Commands run on the message thread only.
class EditCommand
{
public:
    virtual ~EditCommand() = default;
    virtual void apply (Project& project) = 0;
    virtual void revert (Project& project) = 0;
    [[nodiscard]] virtual const char* name() const = 0;
};

class History
{
public:
    explicit History (std::size_t limit = 100);

    void execute (Project& project, std::unique_ptr<EditCommand> command);
    bool undo (Project& project);
    bool redo (Project& project);

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] const char* undoName() const noexcept;
    [[nodiscard]] const char* redoName() const noexcept;
    void clear();

private:
    void trim();

    std::size_t limit_;
    std::vector<std::unique_ptr<EditCommand>> undo_;
    std::vector<std::unique_ptr<EditCommand>> redo_;
};

class SetTempoCommand : public EditCommand
{
public:
    explicit SetTempoCommand (double tempoBpm);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    double next_;
    double previous_ = 120.0;
};

class SetTimeSignatureCommand : public EditCommand
{
public:
    SetTimeSignatureCommand (int numerator, int denominator);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    TimeSignature next_;
    TimeSignature previous_ {};
};

class SetLoopCommand : public EditCommand
{
public:
    SetLoopCommand (bool enabled, double startQuarterNotes, double endQuarterNotes);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    bool enabled_;
    double start_;
    double end_;
    bool previousEnabled_ = false;
    double previousStart_ = 0.0;
    double previousEnd_ = 8.0;
};

class RenameProjectCommand : public EditCommand
{
public:
    explicit RenameProjectCommand (std::string name);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string next_;
    std::string previous_;
};

class SetMasterGainCommand : public EditCommand
{
public:
    explicit SetMasterGainCommand (double gainDb);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    double next_;
    double previous_ = 0.0;
};

class AddTrackCommand : public EditCommand
{
public:
    AddTrackCommand (TrackType type, std::string name);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;
    [[nodiscard]] const std::string& trackId() const noexcept { return id_; }

private:
    TrackType type_;
    std::string name_;
    std::string id_;
};

class RemoveTrackCommand : public EditCommand
{
public:
    explicit RemoveTrackCommand (std::string trackId);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string id_;
    int index_ = 0;
    Track removed_;
    bool applied_ = false;
};

class SetMixerCommand : public EditCommand
{
public:
    SetMixerCommand (std::string trackId, MixerState mixer);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    MixerState next_;
    MixerState previous_ {};
    bool applied_ = false;
};

class AddAudioClipCommand : public EditCommand
{
public:
    AddAudioClipCommand (std::string trackId, AudioClip clip);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;
    [[nodiscard]] const std::string& clipId() const noexcept { return clip_.id; }

private:
    std::string trackId_;
    AudioClip clip_;
    bool applied_ = false;
};

class AddMidiClipCommand : public EditCommand
{
public:
    AddMidiClipCommand (std::string trackId, MidiClip clip);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;
    [[nodiscard]] const std::string& clipId() const noexcept { return clip_.id; }

private:
    std::string trackId_;
    MidiClip clip_;
    bool applied_ = false;
};

class AddMidiNoteCommand : public EditCommand
{
public:
    AddMidiNoteCommand (std::string trackId, std::string clipId, MidiNote note);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string clipId_;
    MidiNote note_;
    bool applied_ = false;
};

class SetMidiNotesCommand : public EditCommand
{
public:
    SetMidiNotesCommand (std::string trackId, std::string clipId, std::vector<MidiNote> notes);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string clipId_;
    std::vector<MidiNote> next_;
    std::vector<MidiNote> previous_;
    bool applied_ = false;
};

class MoveClipCommand : public EditCommand
{
public:
    MoveClipCommand (std::string trackId, std::string clipId, double startQuarterNotes);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string clipId_;
    double next_ = 0.0;
    double previous_ = 0.0;
    bool applied_ = false;
};

class AddEffectCommand : public EditCommand
{
public:
    AddEffectCommand (std::string trackId, EffectKind kind);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    EffectSlot slot_ {};
    bool applied_ = false;
};

class SetTrackOutputCommand : public EditCommand
{
public:
    SetTrackOutputCommand (std::string trackId, std::string outputId);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string next_;
    std::string previous_ = "master";
    bool applied_ = false;
};

class SetSendCommand : public EditCommand
{
public:
    SetSendCommand (std::string trackId, int index, Send send);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    int index_ = 0;
    Send next_ {};
    Send previous_ {};
    bool hadPrevious_ = false;
    bool applied_ = false;
};

class SetGroupCommand : public EditCommand
{
public:
    explicit SetGroupCommand (std::string trackId, bool group);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    bool next_ = false;
    bool previous_ = false;
    bool applied_ = false;
};

class SetSynthPatchCommand : public EditCommand
{
public:
    SetSynthPatchCommand (std::string trackId, SynthPatch patch);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    SynthPatch next_ {};
    SynthPatch previous_ {};
    bool applied_ = false;
};

class SetSessionSlotCommand : public EditCommand
{
public:
    SetSessionSlotCommand (std::string trackId, int scene, Clip clip);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;
    [[nodiscard]] const std::string& clipId() const;

private:
    std::string trackId_;
    int scene_ = 0;
    Clip next_;
    SessionSlot previous_ {};
    bool hadPrevious_ = false;
    bool applied_ = false;
};

class ClearSessionSlotCommand : public EditCommand
{
public:
    ClearSessionSlotCommand (std::string trackId, int scene);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    int scene_ = 0;
    SessionSlot previous_ {};
    bool applied_ = false;
};

class SetInstrumentCommand : public EditCommand
{
public:
    SetInstrumentCommand (std::string trackId, InstrumentKind instrument);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    InstrumentKind next_ = InstrumentKind::Synth;
    InstrumentKind previous_ = InstrumentKind::Synth;
    bool applied_ = false;
};

class AddSamplerZoneCommand : public EditCommand
{
public:
    AddSamplerZoneCommand (std::string trackId, SamplerZone zone);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    SamplerZone zone_;
    bool applied_ = false;
};

class SetPluginCommand : public EditCommand
{
public:
    SetPluginCommand (std::string trackId, std::string identifier, std::string pluginName, std::string state);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string identifier_;
    std::string pluginName_;
    std::string state_;
    std::string previousId_;
    std::string previousName_;
    std::string previousState_;
    bool applied_ = false;
};

class RemoveClipCommand : public EditCommand
{
public:
    RemoveClipCommand (std::string trackId, std::string clipId);
    void apply (Project& project) override;
    void revert (Project& project) override;
    [[nodiscard]] const char* name() const override;

private:
    std::string trackId_;
    std::string clipId_;
    int index_ = 0;
    Clip removed_ { AudioClip {} };
    bool applied_ = false;
};

} // namespace waveform
