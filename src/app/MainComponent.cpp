#include "MainComponent.hpp"

#include "OfflineRenderer.hpp"
#include "waveform/collect.hpp"
#include "StudioLookAndFeel.hpp"
#include "waveform/paths.hpp"
#include "waveform/serialize.hpp"
#include "waveform/timing.hpp"
#include "waveform/version.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

namespace
{

constexpr int kRowHeight = 84;

juce::String quarterText (double value)
{
    return juce::String (value, 2);
}

void silenceFocus (juce::Component& component)
{
    component.setWantsKeyboardFocus (false);
    component.setMouseClickGrabsKeyboardFocus (false);
}

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& processor, std::function<void()> onClose)
        : DocumentWindow (processor.getName(),
                          juce::Colour (waveform::StudioLookAndFeel::kPanel),
                          DocumentWindow::closeButton),
          onClose_ (std::move (onClose))
    {
        juce::AudioProcessorEditor* editor = processor.createEditorAndMakeActive();

        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (processor);

        setContentOwned (editor, true);
        setUsingNativeTitleBar (true);
        setResizable (editor->isResizable(), false);
        centreWithSize (editor->getWidth(), editor->getHeight());
    }

    void closeButtonPressed() override
    {
        if (onClose_)
            onClose_();
    }

private:
    std::function<void()> onClose_;
};

class DeviceSettingsWindow : public juce::DocumentWindow
{
public:
    explicit DeviceSettingsWindow (juce::AudioDeviceManager& devices)
        : DocumentWindow ("Audio Device",
                          juce::Colour (waveform::StudioLookAndFeel::kPanel),
                          DocumentWindow::closeButton)
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 2, 2, false, false, true, false);
        selector->setSize (520, 420);
        setContentOwned (selector, true);
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        centreWithSize (540, 460);
    }

    void closeButtonPressed() override { setVisible (false); }
};

} // namespace

class MainComponent::TrackRow : public juce::Component,
                                private juce::Slider::Listener,
                                public juce::DragAndDropTarget
{
public:
    TrackRow (waveform::Track& track, bool selected)
        : id_ (track.id)
    {
        name_.setText (track.name, juce::dontSendNotification);
        name_.setFont (juce::Font (juce::FontOptions (16.0f)));
        kind_.setText (track.type == waveform::TrackType::Audio ? "Audio" : "MIDI", juce::dontSendNotification);
        kind_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kDim));
        summary_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kDim));
        summary_.setText (summaryFor (track), juce::dontSendNotification);

        gain_.setRange (-48.0, 6.0, 0.1);
        gain_.setSkewFactorFromMidPoint (0.0);
        gain_.setValue (track.mixer.gainDb, juce::dontSendNotification);
        gain_.setTextValueSuffix (" dB");
        gain_.setSliderStyle (juce::Slider::LinearHorizontal);
        gain_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);

        pan_.setRange (-1.0, 1.0, 0.01);
        pan_.setValue (track.mixer.pan, juce::dontSendNotification);
        pan_.setSliderStyle (juce::Slider::LinearHorizontal);
        pan_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);

        mute_.setButtonText ("M");
        solo_.setButtonText ("S");
        remove_.setButtonText ("Remove");
        mute_.setClickingTogglesState (true);
        solo_.setClickingTogglesState (true);
        mute_.setToggleState (track.mixer.mute, juce::dontSendNotification);
        solo_.setToggleState (track.mixer.solo, juce::dontSendNotification);

        juce::Component* children[] = { &name_, &kind_, &summary_, &gain_, &pan_, &mute_, &solo_, &remove_ };

        for (auto* component : children)
            addAndMakeVisible (component);

        silenceFocus (mute_);
        silenceFocus (solo_);
        silenceFocus (remove_);
        gain_.addListener (this);
        pan_.addListener (this);

        mute_.onClick = [this] { if (onImmediate) onImmediate (mixer()); };
        solo_.onClick = [this] { if (onImmediate) onImmediate (mixer()); };
        remove_.onClick = [this] { if (onRemove) onRemove(); };

        selected_ = selected;
    }

    void setSelected (bool selected)
    {
        if (selected_ == selected)
            return;

        selected_ = selected;
        repaint();
    }

    std::function<void()> onSelect;
    std::function<void()> onRemove;
    std::function<void (waveform::MixerState)> onPreview;
    std::function<void (waveform::MixerState, waveform::MixerState)> onCommit;
    std::function<void (waveform::MixerState)> onImmediate;
    std::function<void (const juce::String& path)> onDropFile;

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.isString();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override
    {
        if (onDropFile)
            onDropFile (details.description.toString());
    }

    void paint (juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        graphics.setColour (juce::Colour (selected_ ? 0xff24352e : waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (bounds, 8.0f);
        graphics.setColour (juce::Colour (selected_ ? waveform::StudioLookAndFeel::kCopper : waveform::StudioLookAndFeel::kLine));
        graphics.drawRoundedRectangle (bounds, 8.0f, selected_ ? 1.5f : 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 6);
        auto top = area.removeFromTop (24);
        kind_.setBounds (top.removeFromRight (64));
        name_.setBounds (top);
        summary_.setBounds (area.removeFromTop (18));
        auto controls = area.removeFromTop (28);
        remove_.setBounds (controls.removeFromRight (78).reduced (2, 2));
        solo_.setBounds (controls.removeFromRight (36).reduced (2, 2));
        mute_.setBounds (controls.removeFromRight (36).reduced (2, 2));
        pan_.setBounds (controls.removeFromRight (170).reduced (4, 0));
        gain_.setBounds (controls.reduced (0, 0));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect();
    }

private:
    static juce::String summaryFor (const waveform::Track& track)
    {
        if (track.clips.empty())
            return track.type == waveform::TrackType::Midi ? "No MIDI clips yet. Notes are stored, not played."
                                                           : "Empty. Add an audio file.";

        juce::StringArray names;

        for (const auto& clip : track.clips)
        {
            if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
                names.add (juce::String (audio->name) + "  " + quarterText (audio->lengthSeconds) + "s");
            else if (const auto* midi = std::get_if<waveform::MidiClip> (&clip))
                names.add (juce::String (midi->name) + "  " + juce::String (static_cast<int> (midi->notes.size())) + " notes");
        }

        return names.joinIntoString ("   ·   ");
    }

    waveform::MixerState mixer() const
    {
        waveform::MixerState state;
        state.gainDb = gain_.getValue();
        state.pan = pan_.getValue();
        state.mute = mute_.getToggleState();
        state.solo = solo_.getToggleState();
        return state;
    }

    void sliderDragStarted (juce::Slider*) override
    {
        dragging_ = true;
        dragStart_ = mixer();
    }

    void sliderValueChanged (juce::Slider*) override
    {
        if (! dragging_ || onPreview == nullptr)
            return;

        onPreview (mixer());
    }

    void sliderDragEnded (juce::Slider*) override
    {
        dragging_ = false;

        if (onCommit)
            onCommit (dragStart_, mixer());
    }

    std::string id_;
    juce::Label name_, kind_, summary_;
    juce::Slider gain_, pan_;
    juce::TextButton mute_, solo_, remove_;
    bool selected_ = false;
    bool dragging_ = false;
    waveform::MixerState dragStart_;
};

MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);

    titleLabel_.setFont (juce::Font (juce::FontOptions (20.0f)));
    titleLabel_.setText ("Waveform Studio", juce::dontSendNotification);
    positionLabel_.setFont (juce::Font (juce::FontOptions (28.0f)));
    positionLabel_.setText ("001.01.000", juce::dontSendNotification);
    timeLabel_.setFont (juce::Font (juce::FontOptions (18.0f)));
    timeLabel_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kDim));
    deviceLabel_.setJustificationType (juce::Justification::centredRight);
    deviceLabel_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kDim));
    statusLabel_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kMint));
    hintLabel_.setText ("Space plays or pauses. Audio clips start on the timeline and play at their recorded speed.", juce::dontSendNotification);
    hintLabel_.setColour (juce::Label::textColourId, juce::Colour (waveform::StudioLookAndFeel::kDim));
    hintLabel_.setFont (juce::Font (juce::FontOptions (12.5f)));
    bpmCaption_.setText ("BPM", juce::dontSendNotification);
    masterCaption_.setText ("Master", juce::dontSendNotification);

    bpmSlider_.setRange (40.0, 300.0, 0.1);
    bpmSlider_.setValue (project_.tempoBpm, juce::dontSendNotification);
    bpmSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 68, 22);
    bpmSlider_.addListener (this);

    masterSlider_.setRange (-48.0, 6.0, 0.1);
    masterSlider_.setSkewFactorFromMidPoint (0.0);
    masterSlider_.setValue (0.0, juce::dontSendNotification);
    masterSlider_.setTextValueSuffix (" dB");
    masterSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    masterSlider_.addListener (this);

    for (int numerator = 1; numerator <= 12; ++numerator)
        numeratorBox_.addItem (juce::String (numerator), numerator);

    for (int denominator : { 2, 4, 8, 16 })
        denominatorBox_.addItem (juce::String (denominator), denominator);

    numeratorBox_.setSelectedId (project_.timeSignature.numerator, juce::dontSendNotification);
    denominatorBox_.setSelectedId (project_.timeSignature.denominator, juce::dontSendNotification);
    numeratorBox_.onChange = [this] { commitTimeSignature(); };
    denominatorBox_.onChange = [this] { commitTimeSignature(); };

    loopButton_.setToggleState (project_.loopEnabled, juce::dontSendNotification);
    loopButton_.onClick = [this] { commitLoop(); };
    metronomeButton_.onClick = [this]
    {
        if (! engine_.setMetronome (metronomeButton_.getToggleState()))
            setStatus ("The audio engine is busy. Try the click again.");
    };
    toneButton_.onClick = [this]
    {
        if (! engine_.setTestTone (toneButton_.getToggleState()))
            setStatus ("The audio engine is busy. Try the test tone again.");
    };

    playButton_.onClick = [this] { engine_.play(); };
    pauseButton_.onClick = [this] { engine_.pause(); };
    stopButton_.onClick = [this] { engine_.stop(); };
    recordButton_.onClick = [this] { record(); };
    countInBox_.addItem ("Count-in off", 1);
    countInBox_.addItem ("1 bar", 2);
    countInBox_.addItem ("2 bars", 3);
    countInBox_.setSelectedId (2, juce::dontSendNotification);
    countInBox_.onChange = [this]
    {
        project_.countInBars = std::max (0, countInBox_.getSelectedId() - 1);
    };
    midiInputBox_.onChange = [this]
    {
        if (updatingControls_)
            return;

        const int selected = midiInputBox_.getSelectedId();
        const int index = selected - 2;
        engine_.setMidiInput (index >= 0 && index < static_cast<int> (midiInputIds_.size()) ? midiInputIds_[static_cast<std::size_t> (index)] : juce::String());
    };
    arrangement_.onSelectClip = [this] (const std::string& trackId, const std::string& clipId) { selectClip (trackId, clipId); };
    arrangement_.onArmTrack = [this] (const std::string& trackId) { armTrack (trackId); };
    arrangement_.onMoveClip = [this] (const std::string& trackId, const std::string& clipId, double start) { moveClip (trackId, clipId, start); };
    arrangement_.onCreateMidiClip = [this] (const std::string& trackId, double start) { createMidiClip (trackId, start); };
    arrangement_.onSeek = [this] (double quarter) { engine_.seekQuarterNotes (quarter); };
    arrangement_.onDropFile = [this] (const std::string& path, const std::string& trackId, double start)
    {
        dropAudioFile (juce::File (path), trackId, start);
    };
    browser_.onChooseFolder = [this]
    {
        fileChooser_ = std::make_unique<juce::FileChooser> ("Browser folder", browser_.folder());
        fileChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                   [this] (const juce::FileChooser& chooser)
                                   {
                                       const auto folder = chooser.getResult();

                                       if (folder.isDirectory())
                                           browser_.setFolder (folder);
                                   });
    };
    arrangement_.onLoopEdit = [this] (bool enabled, double start, double end)
    {
        history_.execute (project_, std::make_unique<waveform::SetLoopCommand> (enabled, start, end));
        loopButton_.setToggleState (project_.loopEnabled, juce::dontSendNotification);
        syncEngine();
        refreshHistoryButtons();
    };
    pianoRoll_.onCommit = [this] (std::vector<waveform::MidiNote> notes) { commitNotes (std::move (notes)); };
    launchBox_.addItem ("Launch now", 1);
    launchBox_.addItem ("Launch beat", 2);
    launchBox_.addItem ("Launch bar", 3);
    launchBox_.setSelectedId (3, juce::dontSendNotification);
    arrangementButton_.onClick = [this]
    {
        showSession_ = false;
        arrangement_.setVisible (true);
        session_.setVisible (false);
        resized();
    };
    sessionButton_.onClick = [this]
    {
        showSession_ = true;
        arrangement_.setVisible (false);
        session_.setVisible (true);
        resized();
    };
    session_.onLaunch = [this] (int track, int scene)
    {
        const int quantize = std::max (0, launchBox_.getSelectedId() - 1);
        engine_.launchSession (track, scene, quantize);
        setStatus (quantize == 0 ? "Scene launched." : quantize == 1 ? "Scene waits for the next beat." : "Scene waits for the next bar.");
    };
    session_.onLaunchScene = [this] (int scene)
    {
        const int quantize = std::max (0, launchBox_.getSelectedId() - 1);

        for (int track = 0; track < static_cast<int> (project_.tracks.size()); ++track)
            engine_.launchSession (track, scene, quantize);

        setStatus ("Scene row launched.");
    };
    session_.onReturn = [this] (int track)
    {
        engine_.launchSession (track, -1, 0);
        setStatus ("Track follows the arrangement again.");
    };
    session_.onCreate = [this] (int track, int scene)
    {
        if (track < 0 || track >= static_cast<int> (project_.tracks.size()))
            return;

        const auto& source = project_.tracks[static_cast<std::size_t> (track)];

        if (source.type == waveform::TrackType::Midi)
        {
            waveform::MidiClip clip;
            clip.name = "Scene " + std::to_string (scene + 1);
            clip.lengthQuarterNotes = static_cast<double> (project_.timeSignature.numerator);
            auto command = std::make_unique<waveform::SetSessionSlotCommand> (source.id, scene, clip);
            selectedTrackId_ = source.id;
            selectedClipId_ = command->clipId();
            history_.execute (project_, std::move (command));
        }
        else
        {
            const waveform::AudioClip* existing = nullptr;

            for (const auto& candidate : source.clips)
                if (const auto* audio = std::get_if<waveform::AudioClip> (&candidate))
                    existing = audio;

            if (existing == nullptr)
            {
                setStatus ("Add an audio file on this track before filling the slot.");
                return;
            }

            waveform::AudioClip clip = *existing;
            clip.id.clear();
            clip.name = existing->name;
            auto command = std::make_unique<waveform::SetSessionSlotCommand> (source.id, scene, clip);
            selectedTrackId_ = source.id;
            selectedClipId_ = command->clipId();
            history_.execute (project_, std::move (command));
        }

        syncEngine();
        rebuildTracks();
        refreshHistoryButtons();
        refreshPiano();
        setStatus ("Session slot created. Double-click it to launch.");
    };
    session_.onSelect = [this] (int track, int scene)
    {
        if (track < 0 || track >= static_cast<int> (project_.tracks.size()))
            return;

        selectedTrackId_ = project_.tracks[static_cast<std::size_t> (track)].id;

        for (const auto& slot : project_.tracks[static_cast<std::size_t> (track)].session)
            if (slot.scene == scene)
                selectedClipId_ = waveform::clipId (slot.clip);

        rebuildSelection();
        refreshPiano();
    };
    session_.onClear = [this] (int track, int scene)
    {
        if (track < 0 || track >= static_cast<int> (project_.tracks.size()))
            return;

        history_.execute (project_, std::make_unique<waveform::ClearSessionSlotCommand> (project_.tracks[static_cast<std::size_t> (track)].id, scene));
        engine_.launchSession (track, -1, 0);
        syncEngine();
        refreshHistoryButtons();
        refreshPiano();
    };
    deviceButton_.onClick = [this] { showDeviceSettings(); };
    undoButton_.onClick = [this] { undo(); };
    redoButton_.onClick = [this] { redo(); };
    addFileButton_.onClick = [this] { chooseAudioFile(); };
    addAudioTrackButton_.onClick = [this] { addTrack (waveform::TrackType::Audio); };
    addMidiTrackButton_.onClick = [this] { addTrack (waveform::TrackType::Midi); };
    addDelayButton_.onClick = [this]
    {
        if (selectedTrackId_.empty())
            return;

        history_.execute (project_, std::make_unique<waveform::AddEffectCommand> (selectedTrackId_, waveform::EffectKind::Delay));
        syncEngine();
        refreshHistoryButtons();
        setStatus ("Delay inserted on the selected track.");
    };
    makeGroupButton_.onClick = [this]
    {
        auto command = std::make_unique<waveform::AddTrackCommand> (waveform::TrackType::Audio, "Group");
        const auto id = command->trackId();
        history_.execute (project_, std::move (command));
        history_.execute (project_, std::make_unique<waveform::SetGroupCommand> (id, true));

        if (! selectedTrackId_.empty() && selectedTrackId_ != id)
            history_.execute (project_, std::make_unique<waveform::SetTrackOutputCommand> (selectedTrackId_, id));

        syncEngine();
        rebuildTracks();
        refreshHistoryButtons();
        setStatus ("Group created. The selected track now feeds it.");
    };
    loadDemoButton_.onClick = [this] { loadDemo(); };
    openButton_.onClick = [this] { chooseProjectOpen(); };
    saveButton_.onClick = [this] { chooseProjectSave(); };
    collectButton_.onClick = [this] { collectProject(); };
    exportButton_.onClick = [this] { exportWav(); };
    pluginButton_.onClick = [this] { showPluginMenu(); };

    sampleRateBox_.onChange = [this]
    {
        if (updatingControls_)
            return;

        auto setup = engine_.deviceManager().getAudioDeviceSetup();
        setup.sampleRate = sampleRateBox_.getText().getDoubleValue();
        const auto error = engine_.deviceManager().setAudioDeviceSetup (setup, true);

        if (error.isNotEmpty())
            setStatus (error);
    };
    bufferBox_.onChange = [this]
    {
        if (updatingControls_)
            return;

        auto setup = engine_.deviceManager().getAudioDeviceSetup();
        setup.bufferSize = bufferBox_.getText().getIntValue();
        const auto error = engine_.deviceManager().setAudioDeviceSetup (setup, true);

        if (error.isNotEmpty())
            setStatus (error);
    };

    for (auto* component : std::initializer_list<juce::Component*> {
             &titleLabel_, &deviceLabel_, &positionLabel_, &timeLabel_, &statusLabel_, &hintLabel_,
             &bpmCaption_, &masterCaption_, &playButton_, &pauseButton_, &stopButton_, &recordButton_, &deviceButton_,
             &undoButton_, &redoButton_, &addFileButton_, &addAudioTrackButton_, &addMidiTrackButton_,
             &loadDemoButton_, &openButton_, &saveButton_, &collectButton_, &exportButton_, &addDelayButton_, &makeGroupButton_, &loopButton_, &metronomeButton_, &toneButton_,
             &bpmSlider_, &masterSlider_, &numeratorBox_, &denominatorBox_, &sampleRateBox_, &bufferBox_,
             &countInBox_, &midiInputBox_, &launchBox_, &arrangementButton_, &sessionButton_, &pluginButton_,
             &arrangement_, &session_, &pianoRoll_, &browser_, &trackViewport_ })
    {
        addAndMakeVisible (component);
        silenceFocus (*component);
    }

    bpmSlider_.setWantsKeyboardFocus (true);
    masterSlider_.setWantsKeyboardFocus (true);
    session_.setVisible (false);
    trackViewport_.setViewedComponent (&trackList_, false);
    trackViewport_.setScrollBarsShown (true, false);

#if defined (WAVEFORM_EXAMPLES_DIR)
    browser_.setFolder (juce::File (WAVEFORM_EXAMPLES_DIR));
#endif
    project_ = waveform::Project::makeDefault();
    selectedTrackId_ = project_.tracks.empty() ? std::string {} : project_.tracks.front().id;
    const auto deviceError = engine_.initialise();
    syncEngine();
    rebuildTracks();
    refreshDeviceControls();
    seenDeviceGeneration_ = engine_.deviceGeneration();
    engine_.deviceManager().addChangeListener (this);

    if (deviceError.isNotEmpty())
        setStatus (deviceError);
    else
        setStatus ("Arm a track, then record. Double-click a MIDI lane to add a clip.");

    refreshMidiInputs();
    seenRecordEpoch_ = engine_.recordEpoch();
    startTimerHz (30);
    setSize (1100, 900);
    grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    engine_.deviceManager().removeChangeListener (this);
    engine_.shutdown();
}

void MainComponent::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (waveform::StudioLookAndFeel::kBackground));
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kPanel));
    graphics.fillRoundedRectangle (transportBounds_.toFloat(), 10.0f);
    graphics.fillRoundedRectangle (masterBounds_.toFloat(), 10.0f);
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kLine));
    graphics.drawRoundedRectangle (transportBounds_.toFloat(), 10.0f, 1.0f);
    graphics.drawRoundedRectangle (masterBounds_.toFloat(), 10.0f, 1.0f);

    auto paintMeter = [&graphics] (juce::Rectangle<float> area, float level)
    {
        graphics.setColour (juce::Colour (0xff101614));
        graphics.fillRoundedRectangle (area, 3.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kMint));
        graphics.fillRoundedRectangle (area.withWidth (area.getWidth() * std::clamp (level, 0.0f, 1.0f)), 3.0f);
    };

    paintMeter (meterLeftBounds_, meterLeft_);
    paintMeter (meterRightBounds_, meterRight_);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto header = area.removeFromTop (28);
    titleLabel_.setBounds (header.removeFromLeft (320));
    deviceLabel_.setBounds (header);
    area.removeFromTop (8);

    transportBounds_ = area.removeFromTop (112);
    auto transport = transportBounds_.reduced (12, 10);
    auto buttons = transport.removeFromLeft (340);
    auto buttonRow = buttons.removeFromTop (32);
    playButton_.setBounds (buttonRow.removeFromLeft (78).reduced (0, 2));
    buttonRow.removeFromLeft (6);
    pauseButton_.setBounds (buttonRow.removeFromLeft (78).reduced (0, 2));
    buttonRow.removeFromLeft (6);
    stopButton_.setBounds (buttonRow.removeFromLeft (78).reduced (0, 2));
    buttonRow.removeFromLeft (6);
    recordButton_.setBounds (buttonRow.removeFromLeft (78).reduced (0, 2));
    buttons.removeFromTop (8);
    auto signature = buttons.removeFromTop (28);
    numeratorBox_.setBounds (signature.removeFromLeft (64));
    signature.removeFromLeft (6);
    denominatorBox_.setBounds (signature.removeFromLeft (64));

    auto readout = transport.removeFromLeft (230);
    positionLabel_.setBounds (readout.removeFromTop (40));
    timeLabel_.setBounds (readout.removeFromTop (24));
    auto toggles = readout.removeFromTop (28);
    loopButton_.setBounds (toggles.removeFromLeft (70));
    metronomeButton_.setBounds (toggles.removeFromLeft (70));
    toneButton_.setBounds (toggles);

    bpmCaption_.setBounds (transport.removeFromTop (16));
    bpmSlider_.setBounds (transport.removeFromTop (28));
    hintLabel_.setBounds (transport.removeFromBottom (18));

    area.removeFromTop (8);
    auto deviceRow = area.removeFromTop (30);
    deviceButton_.setBounds (deviceRow.removeFromLeft (120));
    deviceRow.removeFromLeft (8);
    sampleRateBox_.setBounds (deviceRow.removeFromLeft (120));
    deviceRow.removeFromLeft (8);
    bufferBox_.setBounds (deviceRow.removeFromLeft (110));
    deviceRow.removeFromLeft (8);
    undoButton_.setBounds (deviceRow.removeFromLeft (72));
    deviceRow.removeFromLeft (6);
    redoButton_.setBounds (deviceRow.removeFromLeft (72));
    deviceRow.removeFromLeft (8);
    countInBox_.setBounds (deviceRow.removeFromLeft (110));
    deviceRow.removeFromLeft (8);
    midiInputBox_.setBounds (deviceRow.removeFromLeft (180));

    area.removeFromTop (8);
    auto trackHeader = area.removeFromTop (30);
    addFileButton_.setBounds (trackHeader.removeFromLeft (130));
    trackHeader.removeFromLeft (6);
    addAudioTrackButton_.setBounds (trackHeader.removeFromLeft (130));
    trackHeader.removeFromLeft (6);
    addMidiTrackButton_.setBounds (trackHeader.removeFromLeft (130));
    trackHeader.removeFromLeft (6);
    loadDemoButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (6);
    addDelayButton_.setBounds (trackHeader.removeFromLeft (90));
    trackHeader.removeFromLeft (6);
    makeGroupButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (6);
    arrangementButton_.setBounds (trackHeader.removeFromLeft (110));
    trackHeader.removeFromLeft (4);
    sessionButton_.setBounds (trackHeader.removeFromLeft (84));
    trackHeader.removeFromLeft (4);
    launchBox_.setBounds (trackHeader.removeFromLeft (110));
    trackHeader.removeFromLeft (4);
    pluginButton_.setBounds (trackHeader.removeFromLeft (84));
    exportButton_.setBounds (trackHeader.removeFromRight (96));
    trackHeader.removeFromRight (6);
    collectButton_.setBounds (trackHeader.removeFromRight (80));
    trackHeader.removeFromRight (6);
    saveButton_.setBounds (trackHeader.removeFromRight (72));
    trackHeader.removeFromRight (6);
    openButton_.setBounds (trackHeader.removeFromRight (72));

    area.removeFromTop (6);
    statusLabel_.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    masterBounds_ = area.removeFromBottom (52);
    area.removeFromBottom (8);
    pianoRoll_.setBounds (area.removeFromBottom (260));
    area.removeFromBottom (8);
    const int arrangementHeight = std::clamp (22 + static_cast<int> (project_.tracks.size()) * 32, 180, 280);
    auto timeline = area.removeFromTop (arrangementHeight);
    arrangement_.setBounds (timeline);
    session_.setBounds (timeline);
    area.removeFromTop (8);
    browser_.setBounds (area.removeFromLeft (220));
    area.removeFromLeft (8);
    trackViewport_.setBounds (area);

    auto master = masterBounds_.reduced (12, 10);
    auto meters = master.removeFromRight (180);
    meterLeftBounds_ = meters.removeFromTop (14).toFloat().reduced (0.0f, 2.0f);
    meters.removeFromTop (4);
    meterRightBounds_ = meters.removeFromTop (14).toFloat().reduced (0.0f, 2.0f);
    masterCaption_.setBounds (master.removeFromLeft (64));
    masterSlider_.setBounds (master);

    trackList_.setSize (trackViewport_.getWidth() - trackViewport_.getScrollBarThickness(),
                        std::max (trackViewport_.getHeight(), static_cast<int> (rows_.size()) * kRowHeight));

    for (std::size_t index = 0; index < rows_.size(); ++index)
        rows_[index]->setBounds (0, static_cast<int> (index) * kRowHeight, trackList_.getWidth(), kRowHeight - 6);
}

void MainComponent::timerCallback()
{
    refreshTransportReadout();
    engine_.maintenance();

    if (engine_.deviceGeneration() != seenDeviceGeneration_)
    {
        seenDeviceGeneration_ = engine_.deviceGeneration();
        refreshDeviceControls();
        refreshMidiInputs();
        syncEngine();
    }

    if (engine_.recordEpoch() != seenRecordEpoch_)
    {
        seenRecordEpoch_ = engine_.recordEpoch();
        commitTake();
    }

    refreshArrangement();
    refreshPiano();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('z', command, 0))
    {
        undo();
        return true;
    }

    if (key == juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', command, 0))
    {
        redo();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        togglePlay();
        return true;
    }

    return false;
}

void MainComponent::sliderValueChanged (juce::Slider* slider)
{
    if (updatingControls_)
        return;

    if (slider == &bpmSlider_)
    {
        if (draggingTempo_)
        {
            project_.tempoBpm = bpmSlider_.getValue();
            engine_.setTempo (project_.tempoBpm);
        }
        else if (! draggingTempo_)
        {
            commitTempo (bpmSlider_.getValue());
        }
    }
    else if (slider == &masterSlider_)
    {
        if (draggingMaster_)
        {
            project_.master.gainDb = masterSlider_.getValue();
            syncEngine();
        }
        else
        {
            commitMaster (masterSlider_.getValue());
        }
    }
}

void MainComponent::sliderDragStarted (juce::Slider* slider)
{
    if (slider == &bpmSlider_)
    {
        draggingTempo_ = true;
        dragTempo_ = project_.tempoBpm;
    }
    else if (slider == &masterSlider_)
    {
        draggingMaster_ = true;
        dragMasterDb_ = project_.master.gainDb;
    }
}

void MainComponent::sliderDragEnded (juce::Slider* slider)
{
    if (slider == &bpmSlider_ && draggingTempo_)
    {
        const double finalTempo = bpmSlider_.getValue();
        draggingTempo_ = false;
        project_.tempoBpm = dragTempo_;
        commitTempo (finalTempo);
    }
    else if (slider == &masterSlider_ && draggingMaster_)
    {
        const double finalGain = masterSlider_.getValue();
        draggingMaster_ = false;
        project_.master.gainDb = dragMasterDb_;
        commitMaster (finalGain);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshDeviceControls();
    seenDeviceGeneration_ = engine_.deviceGeneration();
    syncEngine();
}

void MainComponent::syncEngine()
{
    const auto path = projectPath();
    engine_.setProject (project_, path.empty() ? nullptr : &path);
    showWarnings();
    titleLabel_.setText ("Waveform Studio   " + juce::String (project_.name), juce::dontSendNotification);
}

void MainComponent::rebuildTracks()
{
    rows_.clear();
    trackList_.removeAllChildren();

    if (waveform::findTrack (project_, selectedTrackId_) == nullptr && ! project_.tracks.empty())
        selectedTrackId_ = project_.tracks.front().id;

    for (auto& track : project_.tracks)
    {
        auto row = std::make_unique<TrackRow> (track, track.id == selectedTrackId_);
        const auto id = track.id;

        row->onSelect = [this, id]
        {
            selectedTrackId_ = id;
            rebuildSelection();
        };
        row->onRemove = [this, id]
        {
            history_.execute (project_, std::make_unique<waveform::RemoveTrackCommand> (id));

            if (selectedTrackId_ == id)
                selectedTrackId_.clear();

            rebuildTracks();
            syncEngine();
            refreshHistoryButtons();
            setStatus ("Removed a track.");
        };
        row->onPreview = [this, id] (waveform::MixerState mixer)
        {
            if (auto* target = waveform::findTrack (project_, id))
                target->mixer = mixer;

            syncEngine();
        };
        row->onCommit = [this, id] (waveform::MixerState before, waveform::MixerState after)
        {
            if (std::abs (before.gainDb - after.gainDb) < 0.05 && std::abs (before.pan - after.pan) < 0.005
                && before.mute == after.mute && before.solo == after.solo)
                return;

            if (auto* target = waveform::findTrack (project_, id))
                target->mixer = before;

            history_.execute (project_, std::make_unique<waveform::SetMixerCommand> (id, after));
            syncEngine();
            refreshHistoryButtons();
        };
        row->onImmediate = [this, id] (waveform::MixerState mixer)
        {
            history_.execute (project_, std::make_unique<waveform::SetMixerCommand> (id, mixer));
            syncEngine();
            refreshHistoryButtons();
        };
        row->onDropFile = [this, id] (const juce::String& path)
        {
            dropAudioFile (juce::File (path), id, 0.0);
        };

        trackList_.addAndMakeVisible (row.get());
        rows_.push_back (std::move (row));
    }

    resized();
    refreshHistoryButtons();
}

void MainComponent::refreshDeviceControls()
{
    const juce::ScopedValueSetter<bool> guard (updatingControls_, true);
    sampleRateBox_.clear (juce::dontSendNotification);
    bufferBox_.clear (juce::dontSendNotification);

    auto* device = engine_.deviceManager().getCurrentAudioDevice();

    if (device == nullptr)
    {
        deviceLabel_.setText ("No audio device", juce::dontSendNotification);
        return;
    }

    for (const auto rate : device->getAvailableSampleRates())
        sampleRateBox_.addItem (juce::String (static_cast<int> (std::lround (rate))), sampleRateBox_.getNumItems() + 1);

    for (const auto buffer : device->getAvailableBufferSizes())
        bufferBox_.addItem (juce::String (buffer), bufferBox_.getNumItems() + 1);

    sampleRateBox_.setText (juce::String (static_cast<int> (std::lround (device->getCurrentSampleRate()))), juce::dontSendNotification);
    bufferBox_.setText (juce::String (device->getCurrentBufferSizeSamples()), juce::dontSendNotification);
    deviceLabel_.setText (device->getTypeName() + "  ·  " + device->getName()
                              + "  ·  " + juce::String (static_cast<int> (std::lround (device->getCurrentSampleRate())))
                              + " Hz  ·  " + juce::String (device->getCurrentBufferSizeSamples()) + " samples",
                          juce::dontSendNotification);
}

void MainComponent::refreshTransportReadout()
{
    const auto info = engine_.playbackInfo();
    const bool rateKnown = info.sampleRate > 0;
    waveform::TimeSignature signature { info.signatureNumerator, info.signatureDenominator };

    if (! signature.isValid())
        signature = project_.timeSignature;

    const auto position = waveform::Timing::positionFromSamples (info.positionSamples,
                                                                rateKnown ? info.sampleRate : 48000.0,
                                                                info.tempoBpm,
                                                                signature);
    positionLabel_.setText (waveform::formatMusical (position), juce::dontSendNotification);
    timeLabel_.setText (rateKnown ? waveform::formatSeconds (info.positionSamples, info.sampleRate) : juce::String ("--:--.---"),
                        juce::dontSendNotification);

    const auto playing = info.state == waveform::TransportState::Playing;
    positionLabel_.setColour (juce::Label::textColourId,
                              juce::Colour (playing ? waveform::StudioLookAndFeel::kMint : waveform::StudioLookAndFeel::kText));
    playButton_.setColour (juce::TextButton::buttonColourId,
                           juce::Colour (playing ? waveform::StudioLookAndFeel::kMint : waveform::StudioLookAndFeel::kPanel));
    playButton_.setColour (juce::TextButton::textColourOffId,
                           juce::Colour (playing ? 0xff10211b : waveform::StudioLookAndFeel::kText));

    meterLeft_ = std::max (info.peakLeft, meterLeft_ * 0.82f);
    meterRight_ = std::max (info.peakRight, meterRight_ * 0.82f);

    if (info.structureOverflow)
        statusLabel_.setText ("This project has more tracks or clips than Phase 0 can mix at once.",
                              juce::dontSendNotification);

    repaint (masterBounds_);
}

void MainComponent::refreshHistoryButtons()
{
    undoButton_.setEnabled (history_.canUndo());
    redoButton_.setEnabled (history_.canRedo());
    undoButton_.setTooltip (history_.canUndo() ? juce::String ("Undo ") + history_.undoName() : juce::String ("Undo"));
    redoButton_.setTooltip (history_.canRedo() ? juce::String ("Redo ") + history_.redoName() : juce::String ("Redo"));
}

void MainComponent::showWarnings()
{
    const auto warnings = engine_.warnings();

    if (! warnings.empty())
        setStatus (warnings.front());
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel_.setText (text, juce::dontSendNotification);
}

void MainComponent::commitTempo (double bpm)
{
    history_.execute (project_, std::make_unique<waveform::SetTempoCommand> (bpm));
    updatingControls_ = true;
    bpmSlider_.setValue (project_.tempoBpm, juce::dontSendNotification);
    updatingControls_ = false;
    engine_.setTempo (project_.tempoBpm);
    refreshHistoryButtons();
}

void MainComponent::commitMaster (double gainDb)
{
    history_.execute (project_, std::make_unique<waveform::SetMasterGainCommand> (gainDb));
    updatingControls_ = true;
    masterSlider_.setValue (project_.master.gainDb, juce::dontSendNotification);
    updatingControls_ = false;
    syncEngine();
    refreshHistoryButtons();
}

void MainComponent::commitTimeSignature()
{
    if (updatingControls_)
        return;

    const int numerator = numeratorBox_.getSelectedId();
    const int denominator = denominatorBox_.getSelectedId();

    if (numerator == project_.timeSignature.numerator && denominator == project_.timeSignature.denominator)
        return;

    history_.execute (project_, std::make_unique<waveform::SetTimeSignatureCommand> (numerator, denominator));
    updatingControls_ = true;
    numeratorBox_.setSelectedId (project_.timeSignature.numerator, juce::dontSendNotification);
    denominatorBox_.setSelectedId (project_.timeSignature.denominator, juce::dontSendNotification);
    updatingControls_ = false;
    syncEngine();
    refreshHistoryButtons();
}

void MainComponent::commitLoop()
{
    history_.execute (project_, std::make_unique<waveform::SetLoopCommand> (loopButton_.getToggleState(),
                                                                           project_.loopStartQuarterNotes,
                                                                           project_.loopEndQuarterNotes));
    syncEngine();
    refreshHistoryButtons();
}

void MainComponent::togglePlay()
{
    if (engine_.playbackInfo().state == waveform::TransportState::Playing)
        engine_.pause();
    else
        engine_.play();
}

void MainComponent::undo()
{
    if (! history_.undo (project_))
        return;

    applyModelToControls();
    rebuildTracks();
    syncEngine();
    syncPlugins();
    setStatus (juce::String ("Undid ") + history_.redoName());
}

void MainComponent::redo()
{
    if (! history_.redo (project_))
        return;

    applyModelToControls();
    rebuildTracks();
    syncEngine();
    syncPlugins();
    setStatus (juce::String ("Redid ") + history_.undoName());
}

void MainComponent::chooseAudioFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser> ("Add an audio file", juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.ogg");
    fileChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file.existsAsFile())
                                       addAudioFile (file);
                               });
}

void MainComponent::chooseProjectOpen()
{
    fileChooser_ = std::make_unique<juce::FileChooser> ("Open a Waveform Studio project", juce::File(), "*.json");
    fileChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file.existsAsFile())
                                       loadProject (file);
                               });
}

void MainComponent::chooseProjectSave()
{
    const auto suggested = projectFile_.existsAsFile()
                               ? projectFile_
                               : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                     .getChildFile (project_.name + ".json");
    fileChooser_ = std::make_unique<juce::FileChooser> ("Save project", suggested, "*.json");
    fileChooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file != juce::File())
                                       saveProject (file);
                               });
}

void MainComponent::addAudioFile (const juce::File& file)
{
    const auto playback = engine_.playbackInfo();
    const double rate = playback.sampleRate > 0 ? playback.sampleRate : 48000.0;
    const double start = waveform::Timing::quarterNotesFromSamples (playback.positionSamples, rate, playback.tempoBpm);
    dropAudioFile (file, {}, start);
}

void MainComponent::dropAudioFile (const juce::File& file, const std::string& trackId, double startQuarterNotes)
{
    const auto path = file.getFullPathName().toStdString();
    const auto info = engine_.probeFile (path);

    if (! info.ok)
    {
        setStatus (juce::String (info.error));
        return;
    }

    std::string destination = trackId;

    if (destination.empty())
        destination = ensureAudioTrack();

    if (const auto* track = waveform::findTrack (project_, destination); track != nullptr && track->type != waveform::TrackType::Audio)
    {
        setStatus ("Drop audio onto an audio track.");
        return;
    }

    waveform::AudioClip clip;
    clip.name = file.getFileNameWithoutExtension().toStdString();
    clip.file = path;
    clip.lengthSeconds = info.seconds;
    clip.offsetSeconds = 0.0;
    clip.startQuarterNotes = std::max (0.0, startQuarterNotes);

    history_.execute (project_, std::make_unique<waveform::AddAudioClipCommand> (destination, std::move (clip)));
    selectedTrackId_ = destination;
    rebuildTracks();
    syncEngine();
    refreshHistoryButtons();
    setStatus ("Added " + file.getFileName() + ".");
}

void MainComponent::saveProject (const juce::File& file)
{
    for (int index = 0; index < static_cast<int> (project_.tracks.size()); ++index)
        if (! project_.tracks[static_cast<std::size_t> (index)].pluginIdentifier.empty())
            project_.tracks[static_cast<std::size_t> (index)].pluginState = engine_.plugins().stateBase64 (index).toStdString();

    auto copy = project_;
    const auto destination = waveform::pathFromUtf8 (file.getFullPathName().toStdString());

    for (auto& track : copy.tracks)
    {
        for (auto& clip : track.clips)
        {
            auto* audio = std::get_if<waveform::AudioClip> (&clip);

            if (audio == nullptr)
                continue;

            const auto location = waveform::pathFromUtf8 (audio->file);

            if (location.is_absolute())
                audio->file = waveform::portableAudioPath (destination, location);
        }
    }

    if (copy.name == "Untitled")
        copy.name = file.getFileNameWithoutExtension().toStdString();

    if (! file.replaceWithText (waveform::projectToJson (copy)))
    {
        setStatus ("Could not write the project file.");
        return;
    }

    if (project_.name == "Untitled")
    {
        history_.execute (project_, std::make_unique<waveform::RenameProjectCommand> (copy.name));
        refreshHistoryButtons();
    }

    projectFile_ = file;
    syncEngine();
    setStatus ("Saved " + file.getFileName());
}

void MainComponent::loadProject (const juce::File& file)
{
    std::string error;
    auto loaded = waveform::projectFromJson (file.loadFileAsString().toStdString(), &error);

    if (! loaded.has_value())
    {
        setStatus (error.empty() ? "That file is not a Waveform Studio project." : juce::String (error));
        return;
    }

    engine_.stop();
    project_ = std::move (*loaded);
    projectFile_ = file;
    history_.clear();
    selectedTrackId_.clear();
    applyModelToControls();
    rebuildTracks();
    syncEngine();
    browser_.setFolder (file.getParentDirectory());
    syncPlugins();
    setStatus ("Opened " + file.getFileName());
}

void MainComponent::loadDemo()
{
#if defined (WAVEFORM_EXAMPLES_DIR)
    const auto demo = juce::File (WAVEFORM_EXAMPLES_DIR).getChildFile ("demo.json");

    if (demo.existsAsFile())
    {
        loadProject (demo);
        return;
    }
#endif

    setStatus ("The demo project was not found next to the source tree.");
}

void MainComponent::addTrack (waveform::TrackType type)
{
    const auto count = std::count_if (project_.tracks.begin(), project_.tracks.end(),
                                      [type] (const waveform::Track& track) { return track.type == type; });
    const auto name = (type == waveform::TrackType::Audio ? "Audio " : "MIDI ") + std::to_string (count + 1);
    auto command = std::make_unique<waveform::AddTrackCommand> (type, name);
    selectedTrackId_ = command->trackId();
    history_.execute (project_, std::move (command));
    rebuildTracks();
    syncEngine();
    refreshHistoryButtons();
    setStatus ("Added " + juce::String (name));
}

void MainComponent::refreshArrangement()
{
    const auto info = engine_.playbackInfo();
    const double rate = info.sampleRate > 0 ? info.sampleRate : 48000.0;
    const double quarter = waveform::Timing::quarterNotesFromSamples (info.positionSamples, rate, info.tempoBpm);
    arrangement_.setState (&project_, quarter, selectedClipId_, armedTrackId_);

    for (int index = 0; index < 64; ++index)
    {
        sessionActive_[static_cast<std::size_t> (index)] = engine_.sessionScene (index);
        sessionPending_[static_cast<std::size_t> (index)] = engine_.sessionPending (index);
    }

    session_.setState (&project_, sessionActive_, sessionPending_, selectedClipId_);
    const std::string monitorId = armedTrackId_.empty() ? selectedTrackId_ : armedTrackId_;
    int monitor = -1;

    for (std::size_t index = 0; index < project_.tracks.size(); ++index)
        if (project_.tracks[index].id == monitorId && project_.tracks[index].type == waveform::TrackType::Midi)
            monitor = static_cast<int> (index);

    engine_.setMonitorTrack (monitor);

    const bool recording = info.recording || info.countingIn;
    recordButton_.setColour (juce::TextButton::buttonColourId,
                             juce::Colour (recording ? waveform::StudioLookAndFeel::kRose : waveform::StudioLookAndFeel::kPanel));
}

void MainComponent::refreshPiano()
{
    const waveform::MidiClip* clip = nullptr;

    if (auto* track = waveform::findTrack (project_, selectedTrackId_))
    {
        for (const auto& candidate : track->clips)
            if (const auto* midi = std::get_if<waveform::MidiClip> (&candidate); midi != nullptr && midi->id == selectedClipId_)
                clip = midi;

        if (clip == nullptr)
            for (const auto& slot : track->session)
                if (const auto* midi = std::get_if<waveform::MidiClip> (&slot.clip); midi != nullptr && midi->id == selectedClipId_)
                    clip = midi;
    }

    pianoRoll_.setClip (clip);
}

void MainComponent::refreshMidiInputs()
{
    const juce::ScopedValueSetter<bool> guard (updatingControls_, true);
    const int previous = midiInputBox_.getSelectedId();
    juce::String previousId;

    if (previous >= 2)
    {
        const int index = previous - 2;

        if (index >= 0 && index < static_cast<int> (midiInputIds_.size()))
            previousId = midiInputIds_[static_cast<std::size_t> (index)];
    }

    midiInputIds_.clear();
    midiInputBox_.clear (juce::dontSendNotification);
    midiInputBox_.addItem ("No MIDI input", 1);
    int itemId = 2;
    int restore = 1;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        midiInputBox_.addItem (device.name, itemId);

        if (device.identifier == previousId)
            restore = itemId;

        ++itemId;
        midiInputIds_.push_back (device.identifier);
    }

    midiInputBox_.setSelectedId (restore, juce::dontSendNotification);
}

void MainComponent::armTrack (const std::string& trackId)
{
    armedTrackId_ = armedTrackId_ == trackId ? std::string {} : trackId;
    selectedTrackId_ = trackId;
    int index = -1;

    for (std::size_t i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == armedTrackId_)
            index = static_cast<int> (i);

    engine_.setMonitorTrack (index >= 0 && project_.tracks[static_cast<std::size_t> (index)].type == waveform::TrackType::Midi ? index : -1);
    refreshArrangement();
}

void MainComponent::selectClip (const std::string& trackId, const std::string& clipId)
{
    selectedTrackId_ = trackId;
    selectedClipId_ = clipId;
    rebuildSelection();
    refreshPiano();
    refreshArrangement();
}

void MainComponent::moveClip (const std::string& trackId, const std::string& clipId, double startQuarterNotes)
{
    history_.execute (project_, std::make_unique<waveform::MoveClipCommand> (trackId, clipId, startQuarterNotes));
    syncEngine();
    refreshHistoryButtons();
    rebuildTracks();
}

void MainComponent::createMidiClip (const std::string& trackId, double startQuarterNotes)
{
    waveform::MidiClip clip;
    clip.name = "Clip";
    clip.startQuarterNotes = startQuarterNotes;
    clip.lengthQuarterNotes = 4.0;
    auto command = std::make_unique<waveform::AddMidiClipCommand> (trackId, std::move (clip));
    selectedTrackId_ = trackId;
    selectedClipId_ = command->clipId();
    history_.execute (project_, std::move (command));
    syncEngine();
    rebuildTracks();
    refreshHistoryButtons();
    refreshPiano();
}

void MainComponent::commitNotes (std::vector<waveform::MidiNote> notes)
{
    if (selectedClipId_.empty())
        return;

    history_.execute (project_, std::make_unique<waveform::SetMidiNotesCommand> (selectedTrackId_, selectedClipId_, std::move (notes)));
    syncEngine();
    refreshHistoryButtons();
    refreshPiano();
}

void MainComponent::record()
{
    if (armedTrackId_.empty())
        armedTrackId_ = selectedTrackId_;

    const auto* track = waveform::findTrack (project_, armedTrackId_);

    if (track == nullptr)
    {
        setStatus ("Arm a track with R, then record.");
        return;
    }

    int index = -1;

    for (std::size_t i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == track->id)
            index = static_cast<int> (i);

    const bool midi = track->type == waveform::TrackType::Midi;

    if (! midi)
        engine_.prepareAudioCapture (180.0);

    engine_.setMonitorTrack (midi ? index : -1);

    if (! engine_.startRecording (index, project_.countInBars, midi))
        setStatus ("Recording needs an open audio device.");
    else
        setStatus (midi ? "Count-in, then play the keyboard." : "Count-in, then the input is recorded.");
}

void MainComponent::commitTake()
{
    const auto midi = engine_.copyMidiTake();

    if (midi.valid && midi.trackIndex >= 0 && midi.trackIndex < static_cast<int> (project_.tracks.size()))
    {
        waveform::MidiClip clip;
        clip.name = "MIDI take";
        clip.startQuarterNotes = std::max (0.0, midi.startQuarterNotes);
        double end = 1.0;

        for (const auto& note : midi.notes)
        {
            waveform::MidiNote stored;
            stored.note = note.note;
            stored.velocity = note.velocity;
            stored.channel = note.channel;
            stored.startQuarterNotes = std::max (0.0, note.startQuarterNotes);
            stored.lengthQuarterNotes = std::max (0.05, note.lengthQuarterNotes);
            end = std::max (end, stored.startQuarterNotes + stored.lengthQuarterNotes);
            clip.notes.push_back (stored);
        }

        clip.lengthQuarterNotes = end + 0.25;
        auto command = std::make_unique<waveform::AddMidiClipCommand> (project_.tracks[static_cast<std::size_t> (midi.trackIndex)].id, std::move (clip));
        selectedTrackId_ = project_.tracks[static_cast<std::size_t> (midi.trackIndex)].id;
        selectedClipId_ = command->clipId();
        history_.execute (project_, std::move (command));
        syncEngine();
        rebuildTracks();
        refreshHistoryButtons();
        setStatus ("Saved the MIDI take.");
        return;
    }

    const auto audio = engine_.copyAudioTake();

    if (! audio.valid || audio.trackIndex < 0 || audio.trackIndex >= static_cast<int> (project_.tracks.size()))
        return;

    const auto directory = projectFile_.existsAsFile()
                               ? projectFile_.getParentDirectory().getChildFile ("Recordings")
                               : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Waveform Studio").getChildFile ("Recordings");
    directory.createDirectory();
    const auto file = directory.getChildFile ("Take " + juce::String (directory.getNumberOfChildFiles (juce::File::findFiles, "*.wav") + 1) + ".wav");
    std::string error;

    if (! waveform::writeStereoWav (file.getFullPathName().toStdString(), audio.sampleRate, audio.left.data(), audio.right.data(),
                                    static_cast<std::int64_t> (audio.left.size()), error))
    {
        setStatus (error.empty() ? "Could not write the audio take." : juce::String (error));
        return;
    }

    waveform::AudioClip clip;
    clip.name = file.getFileNameWithoutExtension().toStdString();
    clip.file = file.getFullPathName().toStdString();
    clip.startQuarterNotes = std::max (0.0, audio.startQuarterNotes);
    clip.lengthSeconds = static_cast<double> (audio.left.size()) / std::max (1.0, audio.sampleRate);
    history_.execute (project_, std::make_unique<waveform::AddAudioClipCommand> (project_.tracks[static_cast<std::size_t> (audio.trackIndex)].id, std::move (clip)));
    syncEngine();
    rebuildTracks();
    refreshHistoryButtons();
    setStatus ("Saved the audio take.");
}

void MainComponent::collectProject()
{
    if (! projectFile_.existsAsFile())
    {
        setStatus ("Save the project before collecting media.");
        return;
    }

    const auto result = waveform::collectMedia (project_, waveform::pathFromUtf8 (projectFile_.getFullPathName().toStdString()));
    saveProject (projectFile_);
    setStatus ("Collected " + juce::String (result.copied) + " file(s) into Media. Missing: " + juce::String (static_cast<int> (result.missing.size())));
}

void MainComponent::exportWav()
{
    if (! projectFile_.existsAsFile())
    {
        setStatus ("Save the project before exporting.");
        return;
    }

    const auto destination = projectFile_.getParentDirectory().getChildFile (projectFile_.getFileNameWithoutExtension() + ".wav");
    waveform::OfflineRenderRequest request;
    request.projectPath = projectFile_.getFullPathName().toStdString();
    request.outputPath = destination.getFullPathName().toStdString();
    request.seconds = 8.0;
    request.sampleRate = engine_.currentSampleRate() > 0.0 ? engine_.currentSampleRate() : 48000.0;
    request.metronome = metronomeButton_.getToggleState();
    const auto rendered = waveform::renderProjectFile (request);
    setStatus (rendered.ok ? "Exported " + destination.getFileName() : juce::String (rendered.message));
}

void MainComponent::syncPlugins()
{
    pluginWindow_.reset();
    const double rate = engine_.currentSampleRate() > 0.0 ? engine_.currentSampleRate() : 48000.0;
    const int block = std::max (64, engine_.playbackInfo().bufferSize);
    const int count = std::min (static_cast<int> (project_.tracks.size()), waveform::PluginHost::kTracks);

    for (int index = 0; index < waveform::PluginHost::kTracks; ++index)
    {
        const auto wanted = index < count ? project_.tracks[static_cast<std::size_t> (index)].pluginIdentifier : std::string {};

        if (wanted == engine_.plugins().identifier (index).toStdString())
            continue;

        if (wanted.empty())
        {
            engine_.plugins().clear (index);
            continue;
        }

        const juce::PluginDescription* description = nullptr;

        for (const auto& candidate : scannedPlugins_)
            if (candidate.createIdentifierString().toStdString() == wanted)
                description = &candidate;

        if (description == nullptr)
        {
            setStatus ("Scan VST3 to restore " + juce::String (project_.tracks[static_cast<std::size_t> (index)].pluginName) + ".");
            continue;
        }

        const auto error = engine_.plugins().load (index, *description, rate, block);

        if (error.isNotEmpty())
            setStatus (error);
        else
            engine_.plugins().restoreState (index, project_.tracks[static_cast<std::size_t> (index)].pluginState);
    }
}

void MainComponent::openPluginEditor()
{
    int index = -1;

    for (std::size_t i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == selectedTrackId_)
            index = static_cast<int> (i);

    auto* instance = index >= 0 ? engine_.plugins().instance (index) : nullptr;

    if (instance == nullptr)
    {
        setStatus ("Load a plug-in on the selected track first.");
        return;
    }

    pluginWindow_ = std::make_unique<PluginWindow> (*instance, [this] { pluginWindow_.reset(); });
    pluginWindow_->setVisible (true);
}

void MainComponent::showPluginMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Scan VST3");
    menu.addItem (2, "Open editor");
    menu.addItem (3, "Remove plug-in");

    if (! scannedPlugins_.empty())
        menu.addSeparator();

    for (int index = 0; index < static_cast<int> (scannedPlugins_.size()); ++index)
        menu.addItem (100 + index, scannedPlugins_[static_cast<std::size_t> (index)].name
                                      + "  (" + scannedPlugins_[static_cast<std::size_t> (index)].manufacturerName + ")");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pluginButton_),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                setStatus ("Scanning VST3...");
                                const int found = engine_.plugins().scan();
                                scannedPlugins_.clear();

                                for (const auto& type : engine_.plugins().known().getTypes())
                                    scannedPlugins_.push_back (type);

                                setStatus ("Found " + juce::String (found) + " VST3 plug-in(s).");
                                syncPlugins();
                                return;
                            }

                            if (result == 2)
                            {
                                openPluginEditor();
                                return;
                            }

                            if (result == 3)
                            {
                                if (selectedTrackId_.empty())
                                    return;

                                pluginWindow_.reset();
                                auto command = std::make_unique<waveform::SetPluginCommand> (selectedTrackId_, std::string(), std::string(), std::string());
                                history_.execute (project_, std::move (command));
                                syncPlugins();
                                refreshHistoryButtons();
                                setStatus ("Plug-in removed.");
                                return;
                            }

                            const int choice = result - 100;

                            if (choice < 0 || choice >= static_cast<int> (scannedPlugins_.size()) || selectedTrackId_.empty())
                                return;

                            const auto& description = scannedPlugins_[static_cast<std::size_t> (choice)];
                            const double rate = engine_.currentSampleRate() > 0.0 ? engine_.currentSampleRate() : 48000.0;
                            const int block = std::max (64, engine_.playbackInfo().bufferSize);
                            int index = -1;

                            for (std::size_t i = 0; i < project_.tracks.size(); ++i)
                                if (project_.tracks[i].id == selectedTrackId_)
                                    index = static_cast<int> (i);

                            if (index < 0)
                                return;

                            pluginWindow_.reset();
                            const auto error = engine_.plugins().load (index, description, rate, block);

                            if (error.isNotEmpty())
                            {
                                setStatus (error);
                                return;
                            }

                            history_.execute (project_, std::make_unique<waveform::SetPluginCommand> (
                                                            selectedTrackId_,
                                                            description.createIdentifierString().toStdString(),
                                                            description.name.toStdString(),
                                                            engine_.plugins().stateBase64 (index).toStdString()));
                            refreshHistoryButtons();
                            const int latency = engine_.plugins().latency (index);
                            setStatus ("Loaded " + description.name + ". Latency " + juce::String (latency) + " samples, compensated on the other tracks.");
                        });
}

void MainComponent::showDeviceSettings()
{
    if (deviceWindow_ == nullptr)
        deviceWindow_ = std::make_unique<DeviceSettingsWindow> (engine_.deviceManager());

    deviceWindow_->setVisible (true);
    deviceWindow_->toFront (true);
}

std::filesystem::path MainComponent::projectPath() const
{
    if (! projectFile_.existsAsFile())
        return {};

    return waveform::pathFromUtf8 (projectFile_.getFullPathName().toStdString());
}

waveform::Track* MainComponent::selectedTrack()
{
    return waveform::findTrack (project_, selectedTrackId_);
}

std::string MainComponent::ensureAudioTrack()
{
    if (auto* selected = selectedTrack())
        if (selected->type == waveform::TrackType::Audio)
            return selected->id;

    for (const auto& track : project_.tracks)
        if (track.type == waveform::TrackType::Audio)
            return track.id;

    auto command = std::make_unique<waveform::AddTrackCommand> (waveform::TrackType::Audio, "Audio 1");
    const auto id = command->trackId();
    history_.execute (project_, std::move (command));
    selectedTrackId_ = id;
    return id;
}

void MainComponent::applyModelToControls()
{
    updatingControls_ = true;
    bpmSlider_.setValue (project_.tempoBpm, juce::dontSendNotification);
    masterSlider_.setValue (project_.master.gainDb, juce::dontSendNotification);
    numeratorBox_.setSelectedId (project_.timeSignature.numerator, juce::dontSendNotification);
    denominatorBox_.setSelectedId (project_.timeSignature.denominator, juce::dontSendNotification);
    loopButton_.setToggleState (project_.loopEnabled, juce::dontSendNotification);
    updatingControls_ = false;
}

void MainComponent::rebuildSelection()
{
    for (std::size_t index = 0; index < rows_.size() && index < project_.tracks.size(); ++index)
        rows_[index]->setSelected (project_.tracks[index].id == selectedTrackId_);
}
