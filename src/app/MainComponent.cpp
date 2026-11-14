#include "MainComponent.hpp"

#include "ClipLibrary.hpp"
#include "OfflineRenderer.hpp"
#include "waveform/clips.hpp"
#include "waveform/collect.hpp"
#include "waveform/later.hpp"
#include "waveform/midi_file.hpp"
#include "waveform/plugins.hpp"
#include "StudioLookAndFeel.hpp"
#include "waveform/paths.hpp"
#include "waveform/serialize.hpp"
#include "waveform/session.hpp"
#include "waveform/stretch.hpp"
#include "waveform/timing.hpp"
#include "waveform/version.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

namespace
{

constexpr int kRowHeight = 118;

juce::String quarterText (double value)
{
    return juce::String (value, 2);
}

juce::File studioStateDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WaveformStudio");
    directory.createDirectory();
    return directory;
}

void silenceFocus (juce::Component& component)
{
    component.setWantsKeyboardFocus (false);
    component.setMouseClickGrabsKeyboardFocus (false);
}

class ScriptContent : public juce::Component
{
public:
    ScriptContent()
    {
        editor_.setMultiLine (true, true);
        editor_.setReturnKeyStartsNewLine (true);
        editor_.setText ("tempo 124\nadd audio Drums\nadd midi Lead\n");
        run_.setButtonText ("Run");
        addAndMakeVisible (editor_);
        addAndMakeVisible (run_);
        addAndMakeVisible (result_);
        run_.onClick = [this]
        {
            if (onRun)
                onRun (editor_.getText().toStdString());
        };
    }

    void setResult (const juce::String& text) { result_.setText (text, juce::dontSendNotification); }

    std::function<void (const std::string&)> onRun;

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        result_.setBounds (area.removeFromBottom (24));
        run_.setBounds (area.removeFromBottom (28).removeFromLeft (80));
        area.removeFromBottom (6);
        editor_.setBounds (area);
    }

private:
    juce::TextEditor editor_;
    juce::TextButton run_;
    juce::Label result_;
};

class MapContent : public juce::Component
{
public:
    MapContent()
    {
        learn_.setButtonText ("Learn");
        clear_.setButtonText ("Clear");
        addAndMakeVisible (learn_);
        addAndMakeVisible (clear_);
        addAndMakeVisible (body_);
        learn_.onClick = [this] { if (onLearn) onLearn(); };
        clear_.onClick = [this] { if (onClear) onClear(); };
    }

    void setBody (const juce::String& text) { body_.setText (text, juce::dontSendNotification); }

    std::function<void()> onLearn;
    std::function<void()> onClear;

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto row = area.removeFromTop (28);
        learn_.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        clear_.setBounds (row.removeFromLeft (80));
        area.removeFromTop (8);
        body_.setBounds (area);
    }

private:
    juce::TextButton learn_;
    juce::TextButton clear_;
    juce::Label body_;
};

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
    TrackRow (waveform::Track& track, bool selected, const waveform::Project& project)
        : id_ (track.id)
    {
        name_.setText (track.name, juce::dontSendNotification);
        name_.setFont (juce::Font (juce::FontOptions (16.0f)));
        kind_.setText (track.returns ? "Return" : track.type == waveform::TrackType::Audio ? "Audio" : "MIDI", juce::dontSendNotification);
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

        sendLevel_.setRange (-60.0, 6.0, 0.1);
        sendLevel_.setSkewFactorFromMidPoint (-12.0);
        sendLevel_.setSliderStyle (juce::Slider::LinearHorizontal);
        sendLevel_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        sendLevel_.setValue (track.sends.empty() ? -60.0 : track.sends.front().levelDb, juce::dontSendNotification);
        sendPre_.setButtonText ("Pre");
        sendPre_.setClickingTogglesState (true);
        sendPre_.setToggleState (! track.sends.empty() && track.sends.front().preFader, juce::dontSendNotification);
        sendTarget_.addItem ("No return", 1);
        int item = 2;
        returnIds_.clear();

        for (const auto& candidate : project.tracks)
            if (candidate.returns)
            {
                returnIds_.push_back (candidate.id);
                sendTarget_.addItem (candidate.name, item++);
            }

        int selectedTarget = 1;

        if (! track.sends.empty())
        {
            item = 2;

            for (const auto& candidate : project.tracks)
            {
                if (! candidate.returns)
                    continue;

                if (candidate.id == track.sends.front().targetId)
                    selectedTarget = item;

                ++item;
            }
        }

        sendTarget_.setSelectedId (selectedTarget, juce::dontSendNotification);
        const bool showSend = ! track.returns;
        sendLevel_.setVisible (showSend);
        sendPre_.setVisible (showSend);
        sendTarget_.setVisible (showSend);
        sendLevel_.onDragEnd = [this] { publishSend(); };
        sendPre_.onClick = [this] { publishSend(); };
        sendTarget_.onChange = [this] { publishSend(); };

        juce::Component* children[] = { &name_, &kind_, &summary_, &gain_, &pan_, &mute_, &solo_, &remove_,
                                        &sendLevel_, &sendPre_, &sendTarget_ };

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
    std::function<void (waveform::Send)> onSend;
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
        auto send = area.removeFromTop (26);
        sendTarget_.setBounds (send.removeFromRight (140).reduced (2, 2));
        sendPre_.setBounds (send.removeFromRight (52).reduced (2, 2));
        sendLevel_.setBounds (send.reduced (0, 2));
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

    void publishSend()
    {
        if (onSend == nullptr)
            return;

        waveform::Send send;
        send.levelDb = sendLevel_.getValue();
        send.preFader = sendPre_.getToggleState();
        send.targetId = "none";
        const int selected = sendTarget_.getSelectedId();

        if (selected >= 2 && selected - 2 < static_cast<int> (returnIds_.size()))
            send.targetId = returnIds_[static_cast<std::size_t> (selected - 2)];

        onSend (send);
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
    juce::TextButton mute_, solo_, remove_, sendPre_;
    juce::Slider sendLevel_;
    juce::ComboBox sendTarget_;
    std::vector<std::string> returnIds_;
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
    hintLabel_.setText ("Space plays. Delete removes a clip, X splits it. Drag clip edges to trim, top corners to fade. Cmd/Ctrl+S saves.", juce::dontSendNotification);
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
    arrangement_.onResizeClip = [this] (const std::string& trackId, const std::string& clipId, double start, double length) { resizeClip (trackId, clipId, start, length); };
    arrangement_.onFadeClip = [this] (const std::string& trackId, const std::string& clipId, double fadeIn, double fadeOut) { fadeClip (trackId, clipId, fadeIn, fadeOut); };
    arrangement_.setAutomationParameter (automationParameter_);
    browser_.onPreview = [this] (const juce::String& path)
    {
        std::string error;
        const double rate = engine_.playbackInfo().sampleRate > 0 ? engine_.playbackInfo().sampleRate : 48000.0;
        const auto audio = engine_.library().load (path.toStdString(), rate, error);

        if (audio == nullptr)
        {
            setStatus (error.empty() ? "Cannot preview that file." : juce::String (error));
            return;
        }

        const int frames = static_cast<int> (std::min<std::int64_t> (audio->frames(), static_cast<std::int64_t> (rate * 8.0)));
        engine_.playPreview (std::vector<float> (audio->left.begin(), audio->left.begin() + frames),
                             std::vector<float> (audio->right.begin(), audio->right.begin() + frames));
        setStatus ("Previewing " + juce::File (path).getFileName() + ".");
    };
    midiEffectBar_.onChange = [this] (std::vector<waveform::MidiEffect> effects) { setMidiEffects (std::move (effects)); };
    midiEffectBar_.importButton().onClick = [this] { importMidi(); };
    midiEffectBar_.exportButton().onClick = [this] { exportMidi(); };
    arrangement_.onCreateMidiClip = [this] (const std::string& trackId, double start) { createMidiClip (trackId, start); };
    arrangement_.onSeek = [this] (double quarter) { engine_.seekQuarterNotes (quarter); };
    arrangement_.onMarkers = [this] (const std::string& trackId, const std::string& clipId, std::vector<waveform::WarpMarker> warp, std::vector<waveform::CompSegment> comp)
    {
        history_.execute (project_, std::make_unique<waveform::SetClipMarkersCommand> (trackId, clipId, std::move (warp), std::move (comp)));
        syncEngine();
        refreshHistoryButtons();
        setStatus ("Ctrl-click adds a warp marker. Alt-click adds a one-beat comp segment.");
    };
    arrangement_.onAutomation = [this] (const std::string& trackId, std::vector<waveform::AutomationPoint> points)
    {
        history_.execute (project_, std::make_unique<waveform::SetAutomationCommand> (trackId, automationParameter_, std::move (points)));
        syncEngine();
        refreshHistoryButtons();
    };
    automationButton_.onClick = [this]
    {
        showAutomation_ = ! showAutomation_;
        automationButton_.setToggleState (showAutomation_, juce::dontSendNotification);
        refreshArrangement();
    };
    automationButton_.setClickingTogglesState (true);
    automationButton_.setToggleState (true, juce::dontSendNotification);
    arrangement_.onAddTake = [this] (const std::string& path, const std::string& trackId, const std::string& clipId)
    {
        waveform::ClipTake take;
        take.name = juce::File (path).getFileNameWithoutExtension().toStdString();
        take.file = path;
        history_.execute (project_, std::make_unique<waveform::AddAudioTakeCommand> (trackId, clipId, std::move (take)));
        syncEngine();
        refreshHistoryButtons();
        setStatus ("Take added. Alt-click the clip to comp it in.");
    };
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
    instrumentBox_.addItem ("Synth", 1);
    instrumentBox_.addItem ("Sampler", 2);
    instrumentBox_.addItem ("FM", 3);
    instrumentBox_.addItem ("Drum", 4);
    instrumentBox_.setSelectedId (1, juce::dontSendNotification);
    instrumentBox_.onChange = [this]
    {
        if (updatingControls_)
            return;

        auto* track = selectedTrack();

        if (track == nullptr || track->type != waveform::TrackType::Midi)
            return;

        const waveform::InstrumentKind kinds[] = {
            waveform::InstrumentKind::Synth,
            waveform::InstrumentKind::Sampler,
            waveform::InstrumentKind::Fm,
            waveform::InstrumentKind::Drum
        };
        const int index = instrumentBox_.getSelectedId() - 1;

        if (index < 0 || index > 3)
            return;

        history_.execute (project_, std::make_unique<waveform::SetInstrumentCommand> (track->id, kinds[index]));
        syncEngine();
        refreshHistoryButtons();
        setStatus ("Instrument is " + instrumentBox_.getText() + ".");
    };
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
        int mode = 0;
        int follow = 0;
        int after = 400;
        int repeat = 100;

        if (track >= 0 && track < static_cast<int> (project_.tracks.size()))
        {
            for (const auto& slot : project_.tracks[static_cast<std::size_t> (track)].session)
            {
                if (slot.scene != scene)
                    continue;

                mode = static_cast<int> (slot.launch);
                follow = static_cast<int> (slot.follow);
                after = static_cast<int> (std::lround (slot.followAfterQuarters * 100.0));
                repeat = static_cast<int> (std::lround (slot.repeatQuarters * 100.0));
            }
        }

        engine_.launchSession (track, scene, quantize, mode, follow, after, repeat);
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

        selectedScene_ = scene;
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
    addEffectButton_.onClick = [this]
    {
        if (selectedTrackId_.empty())
        {
            setStatus ("Select a track before adding an effect.");
            return;
        }

        juce::PopupMenu menu;
        menu.addItem (1, "EQ");
        menu.addItem (2, "Compressor");
        menu.addItem (3, "Delay");
        menu.addItem (4, "Reverb");
        menu.addItem (5, "Limiter");
        menu.addItem (6, "Utility");
        menu.addItem (7, "Chorus");
        menu.addItem (8, "Saturator");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addEffectButton_), [this] (int result)
        {
            waveform::EffectKind kind = waveform::EffectKind::Eq;

            switch (result)
            {
                case 2: kind = waveform::EffectKind::Compressor; break;
                case 3: kind = waveform::EffectKind::Delay; break;
                case 4: kind = waveform::EffectKind::Reverb; break;
                case 5: kind = waveform::EffectKind::Limiter; break;
                case 6: kind = waveform::EffectKind::Utility; break;
                case 7: kind = waveform::EffectKind::Chorus; break;
                case 8: kind = waveform::EffectKind::Saturator; break;
                case 1: break;
                default: return;
            }

            history_.execute (project_, std::make_unique<waveform::AddEffectCommand> (selectedTrackId_, kind));
            syncEngine();
            refreshHistoryButtons();
            rebuildSelection();
            setStatus ("Effect added.");
        });
    };
    deviceStrip_.onChange = [this] (int index, waveform::EffectSlot slot)
    {
        history_.execute (project_, std::make_unique<waveform::SetEffectCommand> (selectedTrackId_, index, slot));
        syncEngine();
        refreshHistoryButtons();
    };
    deviceStrip_.onRemove = [this] (int index)
    {
        history_.execute (project_, std::make_unique<waveform::RemoveEffectCommand> (selectedTrackId_, index));
        syncEngine();
        refreshHistoryButtons();
        rebuildSelection();
    };
    deviceStrip_.onMove = [this] (int index, int direction)
    {
        history_.execute (project_, std::make_unique<waveform::MoveEffectCommand> (selectedTrackId_, index, direction));
        syncEngine();
        refreshHistoryButtons();
        rebuildSelection();
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
    mixerButton_.onClick = [this]
    {
        showMixer_ = ! showMixer_;
        mixerButton_.setToggleState (showMixer_, juce::dontSendNotification);
        mixer_.setVisible (showMixer_);
        trackViewport_.setVisible (! showMixer_);
        resized();
    };
    mixerButton_.setClickingTogglesState (true);
    mixer_.onMixer = [this] (const std::string& id, waveform::MixerState state)
    {
        if (auto* track = waveform::findTrack (project_, id))
            state.pan = track->mixer.pan;

        history_.execute (project_, std::make_unique<waveform::SetMixerCommand> (id, state));
        syncEngine();
        refreshHistoryButtons();
        rebuildTracks();
    };
    mixer_.onSend = [this] (const std::string& id, waveform::Send send)
    {
        history_.execute (project_, std::make_unique<waveform::SetSendCommand> (id, 0, send));
        syncEngine();
        refreshHistoryButtons();
    };
    mixer_.setVisible (false);
    addReturnButton_.onClick = [this]
    {
        const auto count = std::count_if (project_.tracks.begin(), project_.tracks.end(),
                                          [] (const waveform::Track& track) { return track.returns; });
        auto command = std::make_unique<waveform::AddTrackCommand> (waveform::TrackType::Audio, "Return " + std::string (1, static_cast<char> ('A' + std::min<int> (count, 25))));
        const auto id = command->trackId();
        history_.execute (project_, std::move (command));
        history_.execute (project_, std::make_unique<waveform::SetReturnCommand> (id, true));
        selectedTrackId_ = id;
        syncEngine();
        rebuildTracks();
        refreshHistoryButtons();
        setStatus ("Return added. Set a send on another track to feed it.");
    };
    loadDemoButton_.onClick = [this] { loadDemo(); };
    openButton_.onClick = [this] { chooseProjectOpen(); };
    recentButton_.onClick = [this] { showRecent(); };
    saveButton_.onClick = [this] { chooseProjectSave(); };
    collectButton_.onClick = [this] { collectProject(); };
    exportButton_.onClick = [this] { exportWav(); };
    pluginButton_.onClick = [this] { showPluginMenu(); };
    scriptButton_.onClick = [this] { showScriptConsole(); };
    rackButton_.onClick = [this] { showRackEditor(); };
    lfoButton_.onClick = [this] { showLfo(); };
    audioToMidiButton_.onClick = [this] { convertAudioToMidi(); };
    midiMapButton_.onClick = [this] { showMidiMap(); };

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
             &loadDemoButton_, &openButton_, &recentButton_, &saveButton_, &collectButton_, &exportButton_, &addEffectButton_, &makeGroupButton_, &addReturnButton_, &mixerButton_, &loopButton_, &metronomeButton_, &toneButton_,
             &bpmSlider_, &masterSlider_, &numeratorBox_, &denominatorBox_, &sampleRateBox_, &bufferBox_,
             &countInBox_, &midiInputBox_, &launchBox_, &instrumentBox_, &arrangementButton_, &sessionButton_, &pluginButton_, &automationButton_, &scriptButton_, &midiMapButton_, &rackButton_, &lfoButton_, &audioToMidiButton_,
             &arrangement_, &session_, &pianoRoll_, &browser_, &deviceStrip_, &mixer_, &midiEffectBar_, &trackViewport_ })
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
    restoreAutosave();

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
    addEffectButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (6);
    makeGroupButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (4);
    addReturnButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (4);
    mixerButton_.setBounds (trackHeader.removeFromLeft (72));
    trackHeader.removeFromLeft (6);
    arrangementButton_.setBounds (trackHeader.removeFromLeft (110));
    trackHeader.removeFromLeft (4);
    sessionButton_.setBounds (trackHeader.removeFromLeft (84));
    trackHeader.removeFromLeft (4);
    launchBox_.setBounds (trackHeader.removeFromLeft (110));
    trackHeader.removeFromLeft (4);
    pluginButton_.setBounds (trackHeader.removeFromLeft (84));
    trackHeader.removeFromLeft (4);
    instrumentBox_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (4);
    automationButton_.setBounds (trackHeader.removeFromLeft (100));
    trackHeader.removeFromLeft (4);
    rackButton_.setBounds (trackHeader.removeFromLeft (64));
    trackHeader.removeFromLeft (4);
    lfoButton_.setBounds (trackHeader.removeFromLeft (48));
    trackHeader.removeFromLeft (4);
    audioToMidiButton_.setBounds (trackHeader.removeFromLeft (110));
    exportButton_.setBounds (trackHeader.removeFromRight (96));
    trackHeader.removeFromRight (6);
    collectButton_.setBounds (trackHeader.removeFromRight (80));
    trackHeader.removeFromRight (6);
    saveButton_.setBounds (trackHeader.removeFromRight (72));
    trackHeader.removeFromRight (6);
    openButton_.setBounds (trackHeader.removeFromRight (72));
    trackHeader.removeFromRight (4);
    recentButton_.setBounds (trackHeader.removeFromRight (72));

    area.removeFromTop (6);
    midiEffectBar_.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    statusLabel_.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    masterBounds_ = area.removeFromBottom (52);
    area.removeFromBottom (8);
    pianoRoll_.setBounds (area.removeFromBottom (220));
    area.removeFromBottom (6);
    deviceStrip_.setBounds (area.removeFromBottom (110));
    area.removeFromBottom (6);
    const int arrangementHeight = std::clamp (22 + static_cast<int> (project_.tracks.size()) * 32, 180, 280);
    auto timeline = area.removeFromTop (arrangementHeight);
    arrangement_.setBounds (timeline);
    session_.setBounds (timeline);
    area.removeFromTop (8);
    browser_.setBounds (area.removeFromLeft (220));
    area.removeFromLeft (8);
    trackViewport_.setBounds (area);
    mixer_.setBounds (area);

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

    std::vector<float> peaks;
    peaks.reserve (project_.tracks.size());

    for (int index = 0; index < static_cast<int> (project_.tracks.size()); ++index)
        peaks.push_back (engine_.trackPeak (index));

    mixer_.setPeaks (peaks);

    if (++autosaveTicks_ >= 30 * 30)
    {
        autosaveTicks_ = 0;
        autosave();
    }

    if (engine_.deviceGeneration() != seenDeviceGeneration_)
    {
        seenDeviceGeneration_ = engine_.deviceGeneration();
        refreshDeviceControls();
        refreshMidiInputs();
        syncEngine();
    }

    if (engine_.controlEpoch() != seenControlEpoch_)
    {
        seenControlEpoch_ = engine_.controlEpoch();
        applyLearnedControl();
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

    if (key == juce::KeyPress ('s', command, 0))
    {
        if (projectFile_.existsAsFile())
            saveProject (projectFile_);
        else
            chooseProjectSave();

        return true;
    }

    if (key == juce::KeyPress ('o', command, 0))
    {
        chooseProjectOpen();
        return true;
    }

    if (key == juce::KeyPress ('e', command, 0))
    {
        exportWav();
        return true;
    }

    if (key == juce::KeyPress ('r', 0, 0))
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        record();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        deleteSelectedClip();
        return true;
    }

    if (key == juce::KeyPress ('w', 0, 0))
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        auto* track = selectedTrack();
        const waveform::AudioClip* audio = nullptr;

        if (track != nullptr)
            for (const auto& clip : track->clips)
                if (const auto* candidate = std::get_if<waveform::AudioClip> (&clip); candidate != nullptr && candidate->id == selectedClipId_)
                    audio = candidate;

        if (audio == nullptr)
        {
            setStatus ("Select an audio clip to auto-warp.");
            return true;
        }

        std::string error;
        const double rate = engine_.playbackInfo().sampleRate > 0 ? engine_.playbackInfo().sampleRate : 48000.0;
        const auto loaded = engine_.library().load (audio->file, rate, error);

        if (loaded == nullptr || loaded->left.empty())
        {
            setStatus (error.empty() ? "Cannot read the clip for auto-warp." : juce::String (error));
            return true;
        }

        const double tempo = waveform::detectTempoBpm (loaded->left.data(), loaded->frames(), loaded->sampleRate);
        const double seconds = loaded->sampleRate > 0.0 ? static_cast<double> (loaded->frames()) / loaded->sampleRate : 0.0;
        auto markers = waveform::autoWarpMarkers (seconds, tempo > 0.0 ? tempo : project_.tempoBpm);
        history_.execute (project_, std::make_unique<waveform::SetClipMarkersCommand> (track->id, audio->id, std::move (markers), audio->comp));

        if (auto* edited = waveform::findTrack (project_, track->id))
            for (auto& clip : edited->clips)
                if (auto* concrete = std::get_if<waveform::AudioClip> (&clip); concrete != nullptr && concrete->id == selectedClipId_)
                    concrete->warpMode = static_cast<int> (waveform::WarpMode::Beats);

        syncEngine();
        refreshHistoryButtons();
        setStatus ("Auto-warp estimated " + juce::String (tempo, 1) + " BPM and set beats mode.");
        return true;
    }

    if (key == juce::KeyPress ('v', 0, 0))
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        if (automationParameter_ == "mixer.gainDb")
            automationParameter_ = "mixer.pan";
        else if (automationParameter_ == "mixer.pan")
            automationParameter_ = "synth.cutoffHz";
        else if (automationParameter_ == "synth.cutoffHz")
            automationParameter_ = "effect.0.p0";
        else
            automationParameter_ = "mixer.gainDb";

        arrangement_.setAutomationParameter (automationParameter_);
        setStatus ("Automation lane: " + juce::String (automationParameter_));
        return true;
    }

    if (key == juce::KeyPress ('x', 0, 0))
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
                return false;

        splitSelectedClip();
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
        auto row = std::make_unique<TrackRow> (track, track.id == selectedTrackId_, project_);
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
        row->onSend = [this, id] (waveform::Send send)
        {
            history_.execute (project_, std::make_unique<waveform::SetSendCommand> (id, 0, send));
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

    mixer_.setProject (project_);

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
    timeLabel_.setText ((rateKnown ? waveform::formatSeconds (info.positionSamples, info.sampleRate) : juce::String ("--:--.---"))
                            + "   CPU " + juce::String (juce::roundToInt (info.cpu * 100.0f)) + "%"
                            + "   " + juce::String (info.latencyMilliseconds, 1) + " ms",
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
    rebuildSelection();
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
    rebuildSelection();
    syncEngine();
    syncPlugins();
    setStatus (juce::String ("Redid ") + history_.undoName());
}

void MainComponent::chooseAudioFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser> ("Add an audio file", juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");
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

    if (const auto* track = waveform::findTrack (project_, destination); track != nullptr && track->type == waveform::TrackType::Midi)
    {
        waveform::SamplerZone zone;
        zone.file = path;
        zone.rootNote = 60;
        zone.lowNote = 0;
        zone.highNote = 127;
        history_.execute (project_, std::make_unique<waveform::AddSamplerZoneCommand> (destination, std::move (zone)));
        selectedTrackId_ = destination;
        rebuildTracks();
        syncEngine();
        refreshHistoryButtons();
        setStatus ("Sampler zone added. The track now plays this file from MIDI.");
        return;
    }

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

void MainComponent::rememberProject (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    auto lines = juce::StringArray::fromLines (studioStateDirectory().getChildFile ("recent.txt").loadFileAsString());
    lines.removeString (file.getFullPathName());
    lines.insert (0, file.getFullPathName());

    while (lines.size() > 8)
        lines.remove (lines.size() - 1);

    studioStateDirectory().getChildFile ("recent.txt").replaceWithText (lines.joinIntoString ("\n"));
}

void MainComponent::showRecent()
{
    juce::PopupMenu menu;
    const auto lines = juce::StringArray::fromLines (studioStateDirectory().getChildFile ("recent.txt").loadFileAsString());
    int id = 1;

    for (const auto& line : lines)
        if (line.isNotEmpty())
            menu.addItem (id++, line);

    if (menu.getNumItems() == 0)
    {
        setStatus ("No recent projects yet.");
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recentButton_), [this, lines] (int result)
    {
        if (result <= 0 || result > lines.size())
            return;

        const auto file = juce::File (lines[result - 1]);

        if (file.existsAsFile())
            loadProject (file);
        else
            setStatus ("That project is no longer there.");
    });
}

void MainComponent::autosave()
{
    const auto destination = projectFile_.existsAsFile()
                                 ? projectFile_.getSiblingFile (projectFile_.getFileNameWithoutExtension() + ".autosave.json")
                                 : studioStateDirectory().getChildFile ("Untitled.autosave.json");
    destination.replaceWithText (juce::String (waveform::projectToJson (project_)));
}

void MainComponent::restoreAutosave()
{
    const auto untitled = studioStateDirectory().getChildFile ("Untitled.autosave.json");

    if (! untitled.existsAsFile())
        return;

    std::string error;
    auto loaded = waveform::projectFromJson (untitled.loadFileAsString().toStdString(), &error);

    if (! loaded.has_value())
        return;

    project_ = std::move (*loaded);
    applyModelToControls();
    rebuildTracks();
    syncEngine();
    setStatus ("Restored the autosave.");
}

void MainComponent::saveProject (const juce::File& file)
{
    for (int index = 0; index < static_cast<int> (project_.tracks.size()); ++index)
        for (int slot = 0; slot < static_cast<int> (project_.tracks[static_cast<std::size_t> (index)].plugins.size()); ++slot)
            project_.tracks[static_cast<std::size_t> (index)].plugins[static_cast<std::size_t> (slot)].state = engine_.plugins().stateBase64 (index, slot).toStdString();

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
    rememberProject (file);
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
    rememberProject (file);
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
    arrangement_.setState (&project_, quarter, selectedClipId_, armedTrackId_, selectedTrackId_, showAutomation_);

    std::string signature;

    for (const auto& track : project_.tracks)
        for (const auto& clip : track.clips)
            if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
                signature += audio->id + '\n' + audio->file + '\n';

    if (signature != peakSignature_)
    {
        peakSignature_ = signature;
        clipPeaks_.clear();

        for (const auto& track : project_.tracks)
        {
            for (const auto& clip : track.clips)
            {
                const auto* audio = std::get_if<waveform::AudioClip> (&clip);

                if (audio == nullptr || audio->file.empty())
                    continue;

                std::string error;
                const auto loaded = engine_.library().load (audio->file, rate, error);

                if (loaded == nullptr || loaded->left.empty())
                    continue;

                clipPeaks_[audio->id] = waveform::waveformPeaks (loaded->left.data(),
                                                                loaded->right.empty() ? nullptr : loaded->right.data(),
                                                                loaded->frames(),
                                                                48);
            }
        }

        arrangement_.setClipPeaks (clipPeaks_);
    }

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

void MainComponent::resizeClip (const std::string& trackId, const std::string& clipId, double startQuarterNotes, double lengthQuarterNotes)
{
    history_.execute (project_, std::make_unique<waveform::ResizeClipCommand> (trackId, clipId, startQuarterNotes, lengthQuarterNotes, project_.tempoBpm));
    syncEngine();
    refreshHistoryButtons();
    refreshArrangement();
}

void MainComponent::fadeClip (const std::string& trackId, const std::string& clipId, double fadeInSeconds, double fadeOutSeconds)
{
    history_.execute (project_, std::make_unique<waveform::SetClipFadesCommand> (trackId, clipId, fadeInSeconds, fadeOutSeconds));
    syncEngine();
    refreshHistoryButtons();
    refreshArrangement();
}

void MainComponent::deleteSelectedClip()
{
    if (selectedTrackId_.empty() || selectedClipId_.empty())
        return;

    history_.execute (project_, std::make_unique<waveform::RemoveClipCommand> (selectedTrackId_, selectedClipId_));
    selectedClipId_.clear();
    syncEngine();
    refreshHistoryButtons();
    rebuildTracks();
    refreshPiano();
    setStatus ("Clip deleted.");
}

void MainComponent::splitSelectedClip()
{
    if (selectedTrackId_.empty() || selectedClipId_.empty())
        return;

    const auto info = engine_.playbackInfo();
    const double rate = info.sampleRate > 0 ? info.sampleRate : 48000.0;
    const double quarter = waveform::Timing::quarterNotesFromSamples (info.positionSamples, rate, project_.tempoBpm);
    history_.execute (project_, std::make_unique<waveform::SplitClipCommand> (selectedTrackId_, selectedClipId_, quarter, project_.tempoBpm));
    syncEngine();
    refreshHistoryButtons();
    refreshArrangement();
    setStatus ("Split the clip at the playhead.");
}

void MainComponent::setMidiEffects (std::vector<waveform::MidiEffect> effects)
{
    if (selectedTrackId_.empty())
        return;

    history_.execute (project_, std::make_unique<waveform::SetMidiEffectsCommand> (selectedTrackId_, std::move (effects)));
    syncEngine();
    refreshHistoryButtons();
}

void MainComponent::importMidi()
{
    auto* track = selectedTrack();

    if (track == nullptr || track->type != waveform::TrackType::Midi)
    {
        setStatus ("Select a MIDI track before importing.");
        return;
    }

    fileChooser_ = std::make_unique<juce::FileChooser> ("Import a MIDI file", juce::File(), "*.mid;*.midi");
    fileChooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (! file.existsAsFile())
                                       return;

                                   juce::MemoryBlock bytes;

                                   if (! file.loadFileAsData (bytes))
                                   {
                                       setStatus ("Could not read the MIDI file.");
                                       return;
                                   }

                                   const auto imported = waveform::midiClipFromSmf (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize());

                                   if (! imported.ok)
                                   {
                                       setStatus (imported.error);
                                       return;
                                   }

                                   auto* destination = selectedTrack();

                                   if (destination == nullptr || destination->type != waveform::TrackType::Midi)
                                       return;

                                   waveform::MidiClip clip = imported.clip;
                                   clip.name = file.getFileNameWithoutExtension().toStdString();
                                   auto command = std::make_unique<waveform::AddMidiClipCommand> (destination->id, std::move (clip));
                                   selectedClipId_ = command->clipId();
                                   history_.execute (project_, std::move (command));
                                   syncEngine();
                                   rebuildTracks();
                                   refreshHistoryButtons();
                                   refreshPiano();
                                   setStatus ("Imported " + file.getFileName() + ".");
                               });
}

void MainComponent::exportMidi()
{
    const auto* track = selectedTrack();
    const waveform::MidiClip* midi = nullptr;

    if (track != nullptr)
        for (const auto& clip : track->clips)
            if (const auto* candidate = std::get_if<waveform::MidiClip> (&clip); candidate != nullptr && candidate->id == selectedClipId_)
                midi = candidate;

    if (midi == nullptr)
    {
        setStatus ("Select a MIDI clip to export.");
        return;
    }

    const auto bytes = waveform::midiClipToSmf (*midi);
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (midi->name + ".mid");
    fileChooser_ = std::make_unique<juce::FileChooser> ("Export MIDI", suggested, "*.mid");
    fileChooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this, bytes] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file == juce::File())
                                       return;

                                   if (! file.replaceWithData (bytes.data(), bytes.size()))
                                       setStatus ("Could not write the MIDI file.");
                                   else
                                       setStatus ("Exported " + file.getFileName() + ".");
                               });
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

        if (session_.isVisible() && selectedScene_ >= 0)
        {
            auto& destination = project_.tracks[static_cast<std::size_t> (midi.trackIndex)];
            waveform::recordIntoSessionSlot (destination, selectedScene_, clip);

            for (const auto& slot : destination.session)
                if (slot.scene == selectedScene_)
                    selectedClipId_ = waveform::clipId (slot.clip);
            syncEngine();
            rebuildTracks();
            refreshHistoryButtons();
            refreshPiano();
            setStatus ("Recorded into the session slot.");
            return;
        }

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

    auto* dialog = new juce::Component();
    auto* range = new juce::ComboBox();
    auto* rate = new juce::ComboBox();
    auto* depth = new juce::ComboBox();
    auto* stems = new juce::ToggleButton ("Separate stem for each track");
    auto* go = new juce::TextButton ("Export");
    range->addItem ("Whole song", 1);
    range->addItem ("Loop", 2);
    range->setSelectedId (1);
    rate->addItem ("44100", 1);
    rate->addItem ("48000", 2);
    rate->addItem ("96000", 3);
    rate->setSelectedId (2);
    depth->addItem ("16-bit", 1);
    depth->addItem ("24-bit", 2);
    depth->addItem ("32-bit float", 3);
    depth->setSelectedId (1);
    dialog->addAndMakeVisible (range);
    dialog->addAndMakeVisible (rate);
    dialog->addAndMakeVisible (depth);
    dialog->addAndMakeVisible (stems);
    dialog->addAndMakeVisible (go);
    dialog->setSize (360, 180);
    range->setBounds (16, 16, 328, 28);
    rate->setBounds (16, 52, 156, 28);
    depth->setBounds (184, 52, 160, 28);
    stems->setBounds (16, 88, 328, 28);
    go->setBounds (16, 128, 120, 28);

    auto* window = new juce::DocumentWindow ("Export WAV", juce::Colour (waveform::StudioLookAndFeel::kPanel), juce::DocumentWindow::closeButton);
    window->setContentOwned (dialog, true);
    window->setUsingNativeTitleBar (true);
    window->centreWithSize (380, 220);
    window->setVisible (true);
    go->onClick = [this, window, range, rate, depth, stems]
    {
        saveProject (projectFile_);
        const double tempo = std::max (1.0, project_.tempoBpm);
        double startQuarter = 0.0;
        double endQuarter = 8.0;

        for (const auto& track : project_.tracks)
            for (const auto& clip : track.clips)
            {
                if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
                {
                    double end = audio->startQuarterNotes + audio->lengthSeconds * tempo / 60.0;

                    for (const auto& marker : audio->warp)
                        end = std::max (end, audio->startQuarterNotes + marker.timelineQuarters);

                    endQuarter = std::max (endQuarter, end);
                }
                else if (const auto* midi = std::get_if<waveform::MidiClip> (&clip))
                    endQuarter = std::max (endQuarter, midi->startQuarterNotes + midi->lengthQuarterNotes);
            }

        if (range->getSelectedId() == 2)
        {
            startQuarter = project_.loopStartQuarterNotes;
            endQuarter = std::max (startQuarter + 0.25, project_.loopEndQuarterNotes);
        }

        waveform::OfflineRenderRequest request;
        request.projectPath = projectFile_.getFullPathName().toStdString();
        request.outputPath = projectFile_.getSiblingFile (projectFile_.getFileNameWithoutExtension() + ".wav").getFullPathName().toStdString();
        request.startSeconds = startQuarter * 60.0 / tempo;
        request.seconds = std::max (0.25, (endQuarter - startQuarter) * 60.0 / tempo);
        request.sampleRate = rate->getText().getDoubleValue();
        request.bitsPerSample = depth->getSelectedId() == 2 ? 24 : 16;
        request.floatSamples = depth->getSelectedId() == 3;
        request.stems = stems->getToggleState();
        request.plugins = &engine_.plugins();
        const auto rendered = waveform::renderProjectFile (request);
        const double deviceRate = engine_.currentSampleRate() > 0.0 ? engine_.currentSampleRate() : 48000.0;
        engine_.plugins().prepare (deviceRate, std::max (64, engine_.playbackInfo().bufferSize));
        setStatus (rendered.ok ? juce::String (rendered.message) : juce::String (rendered.message));
        juce::MessageManager::callAsync ([this] { exportWindow_.reset(); });
    };
    exportWindow_.reset (window);
}

void MainComponent::syncPlugins()
{
    pluginWindow_.reset();
    const double rate = engine_.currentSampleRate() > 0.0 ? engine_.currentSampleRate() : 48000.0;
    const int block = std::max (64, engine_.playbackInfo().bufferSize);
    const int count = std::min (static_cast<int> (project_.tracks.size()), waveform::PluginHost::kTracks);
    const std::vector<waveform::PluginInsert> noPlugins;

    for (int index = 0; index < waveform::PluginHost::kTracks; ++index)
    {
        const auto& inserts = index < count ? project_.tracks[static_cast<std::size_t> (index)].plugins : noPlugins;

        for (int slot = 0; slot < waveform::PluginHost::kSlots; ++slot)
        {
            const auto wanted = slot < static_cast<int> (inserts.size()) ? inserts[static_cast<std::size_t> (slot)].identifier : std::string {};

            if (wanted == engine_.plugins().identifier (index, slot).toStdString())
                continue;

            if (wanted.empty())
            {
                engine_.plugins().clear (index, slot);
                continue;
            }

            const juce::PluginDescription* description = nullptr;

            for (const auto& candidate : scannedPlugins_)
                if (candidate.createIdentifierString().toStdString() == wanted)
                    description = &candidate;

            if (description == nullptr)
            {
                setStatus ("Scan VST3 to restore " + juce::String (inserts[static_cast<std::size_t> (slot)].name) + ".");
                continue;
            }

            const auto error = engine_.plugins().load (index, slot, *description, rate, block);

            if (error.isNotEmpty())
                setStatus (error);
            else
                engine_.plugins().restoreState (index, slot, inserts[static_cast<std::size_t> (slot)].state);
        }
    }
}

void MainComponent::openPluginEditor (int slot)
{
    int index = -1;

    for (std::size_t i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == selectedTrackId_)
            index = static_cast<int> (i);

    auto* instance = index >= 0 ? engine_.plugins().instance (index, slot) : nullptr;

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
    juce::String scanLabel = "Scan ";
    const auto formats = waveform::hostedPluginFormats();

    for (std::size_t index = 0; index < formats.size(); ++index)
    {
        if (index > 0)
            scanLabel += " and ";

        scanLabel += waveform::hostedPluginFormatName (formats[index]);
    }

    menu.addItem (1, scanLabel);
    juce::PopupMenu openMenu;
    juce::PopupMenu removeMenu;
    juce::PopupMenu moveMenu;
    const waveform::Track* selected = selectedTrack();

    if (selected != nullptr)
    {
        for (int slot = 0; slot < static_cast<int> (selected->plugins.size()); ++slot)
        {
            const auto label = juce::String (slot + 1) + "  " + selected->plugins[static_cast<std::size_t> (slot)].name;
            openMenu.addItem (200 + slot, label);
            removeMenu.addItem (300 + slot, label);
            moveMenu.addItem (400 + slot, label);
        }
    }

    menu.addSubMenu ("Open editor", openMenu);
    menu.addSubMenu ("Remove", removeMenu);
    menu.addSubMenu ("Move earlier", moveMenu);

    if (! scannedPlugins_.empty())
        menu.addSeparator();

    for (int index = 0; index < static_cast<int> (scannedPlugins_.size()); ++index)
        menu.addItem (100 + index, scannedPlugins_[static_cast<std::size_t> (index)].name
                                      + "  (" + scannedPlugins_[static_cast<std::size_t> (index)].pluginFormatName
                                      + ", " + scannedPlugins_[static_cast<std::size_t> (index)].manufacturerName + ")");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pluginButton_),
                        [this, scanLabel] (int result)
                        {
                            if (result == 1)
                            {
                                setStatus ("Scanning " + scanLabel.substring (5) + "...");
                                const int found = engine_.plugins().scan();
                                scannedPlugins_.clear();

                                for (const auto& type : engine_.plugins().known().getTypes())
                                    scannedPlugins_.push_back (type);

                                setStatus ("Found " + juce::String (found) + " plug-in(s).");
                                syncPlugins();
                                return;
                            }

                            if (result >= 200 && result < 300)
                            {
                                openPluginEditor (result - 200);
                                return;
                            }

                            if (result >= 300 && result < 400)
                            {
                                auto* track = selectedTrack();

                                if (track == nullptr)
                                    return;

                                auto plugins = track->plugins;
                                const int slot = result - 300;

                                if (slot < 0 || slot >= static_cast<int> (plugins.size()))
                                    return;

                                pluginWindow_.reset();
                                plugins.erase (plugins.begin() + slot);
                                history_.execute (project_, std::make_unique<waveform::SetPluginsCommand> (track->id, std::move (plugins)));
                                syncPlugins();
                                refreshHistoryButtons();
                                setStatus ("Plug-in removed.");
                                return;
                            }

                            if (result >= 400 && result < 500)
                            {
                                auto* track = selectedTrack();

                                if (track == nullptr)
                                    return;

                                const int slot = result - 400;

                                if (slot <= 0 || slot >= static_cast<int> (track->plugins.size()))
                                    return;

                                auto plugins = track->plugins;
                                std::swap (plugins[static_cast<std::size_t> (slot)], plugins[static_cast<std::size_t> (slot - 1)]);
                                int index = -1;

                                for (std::size_t i = 0; i < project_.tracks.size(); ++i)
                                    if (project_.tracks[i].id == track->id)
                                        index = static_cast<int> (i);

                                if (index >= 0)
                                    engine_.plugins().swapSlots (index, slot, slot - 1);

                                history_.execute (project_, std::make_unique<waveform::SetPluginsCommand> (track->id, std::move (plugins)));
                                refreshHistoryButtons();
                                setStatus ("Plug-in moved earlier.");
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

                            auto plugins = project_.tracks[static_cast<std::size_t> (index)].plugins;

                            if (static_cast<int> (plugins.size()) >= waveform::PluginHost::kSlots)
                            {
                                setStatus ("This track already has four plug-ins.");
                                return;
                            }

                            const int slot = static_cast<int> (plugins.size());
                            pluginWindow_.reset();
                            const auto error = engine_.plugins().load (index, slot, description, rate, block);

                            if (error.isNotEmpty())
                            {
                                setStatus (error);
                                return;
                            }

                            plugins.push_back (waveform::PluginInsert {
                                description.createIdentifierString().toStdString(),
                                description.name.toStdString(),
                                engine_.plugins().stateBase64 (index, slot).toStdString()
                            });
                            history_.execute (project_, std::make_unique<waveform::SetPluginsCommand> (selectedTrackId_, std::move (plugins)));
                            refreshHistoryButtons();
                            const int latency = engine_.plugins().latency (index);
                            setStatus ("Loaded " + description.name + ". Latency " + juce::String (latency) + " samples, compensated on the other tracks.");
                        });
}

void MainComponent::showLfo()
{
    auto* track = selectedTrack();

    if (track == nullptr)
    {
        setStatus ("Select a track to modulate.");
        return;
    }

    auto* rate = new juce::Slider();
    auto* depth = new juce::Slider();
    auto* enable = new juce::ToggleButton ("Modulate volume");
    auto* apply = new juce::TextButton ("Assign");
    rate->setRange (0.05, 12.0, 0.01);
    rate->setValue (track->modulator.rateHz);
    rate->setSliderStyle (juce::Slider::LinearHorizontal);
    rate->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    depth->setRange (0.0, 24.0, 0.1);
    depth->setValue (track->modulator.depth);
    depth->setSliderStyle (juce::Slider::LinearHorizontal);
    depth->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    enable->setToggleState (track->modulator.enabled, juce::dontSendNotification);
    auto* content = new juce::Component();
    content->addAndMakeVisible (rate);
    content->addAndMakeVisible (depth);
    content->addAndMakeVisible (enable);
    content->addAndMakeVisible (apply);
    content->setSize (320, 150);
    rate->setBounds (16, 16, 288, 28);
    depth->setBounds (16, 52, 288, 28);
    enable->setBounds (16, 84, 180, 24);
    apply->setBounds (200, 112, 100, 28);
    auto* window = new juce::DocumentWindow ("LFO", juce::Colour (waveform::StudioLookAndFeel::kPanel), juce::DocumentWindow::closeButton);
    window->setContentOwned (content, true);
    window->setUsingNativeTitleBar (true);
    window->centreWithSize (340, 190);
    window->setVisible (true);
    const auto id = track->id;
    apply->onClick = [this, id, rate, depth, enable, window]
    {
        waveform::Modulator modulator;
        modulator.enabled = enable->getToggleState();
        modulator.rateHz = rate->getValue();
        modulator.depth = depth->getValue();
        modulator.target = "mixer.gainDb";
        history_.execute (project_, std::make_unique<waveform::SetModulatorCommand> (id, modulator));
        syncEngine();
        refreshHistoryButtons();
        setStatus (modulator.enabled ? "LFO is moving the track volume." : "LFO bypassed.");
        juce::MessageManager::callAsync ([window] { delete window; });
    };
}

void MainComponent::convertAudioToMidi()
{
    const auto* track = selectedTrack();
    const waveform::AudioClip* audioClip = nullptr;

    if (track != nullptr)
        for (const auto& clip : track->clips)
            if (const auto* audio = std::get_if<waveform::AudioClip> (&clip); audio != nullptr && audio->id == selectedClipId_)
                audioClip = audio;

    if (audioClip == nullptr && track != nullptr)
        for (const auto& clip : track->clips)
            if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
                audioClip = audio;

    if (audioClip == nullptr)
    {
        setStatus ("Select an audio clip to convert.");
        return;
    }

    waveform::ClipLibrary library;
    std::string error;
    auto location = waveform::pathFromUtf8 (audioClip->file);

    if (projectFile_.existsAsFile() && ! location.is_absolute())
        location = waveform::resolveAudioPath (waveform::pathFromUtf8 (projectFile_.getFullPathName().toStdString()), audioClip->file);

    auto audio = library.load (waveform::pathToUtf8 (location), 48000.0, error);

    if (audio == nullptr)
    {
        setStatus (juce::String (error));
        return;
    }

    const auto detected = waveform::audioToMidi (audio->left.data(), static_cast<int> (audio->frames()), audio->sampleRate);

    if (detected.empty())
    {
        setStatus ("No steady pitch was found in that clip.");
        return;
    }

    waveform::MidiClip midi;
    midi.name = "From " + audioClip->name;
    midi.startQuarterNotes = audioClip->startQuarterNotes;
    midi.lengthQuarterNotes = std::max (0.25, audioClip->lengthSeconds * project_.tempoBpm / 60.0);

    for (const auto& note : detected)
    {
        waveform::MidiNote midiNote;
        midiNote.note = note.note;
        midiNote.velocity = 100;
        midiNote.startQuarterNotes = note.startSeconds * project_.tempoBpm / 60.0;
        midiNote.lengthQuarterNotes = std::max (0.25, note.lengthSeconds * project_.tempoBpm / 60.0);
        midi.notes.push_back (midiNote);
    }

    std::string destination;

    for (const auto& candidate : project_.tracks)
        if (candidate.type == waveform::TrackType::Midi)
            destination = candidate.id;

    if (destination.empty())
    {
        auto command = std::make_unique<waveform::AddTrackCommand> (waveform::TrackType::Midi, "From audio");
        destination = command->trackId();
        history_.execute (project_, std::move (command));
    }

    history_.execute (project_, std::make_unique<waveform::AddMidiClipCommand> (destination, std::move (midi)));
    selectedTrackId_ = destination;
    rebuildTracks();
    syncEngine();
    refreshHistoryButtons();
    setStatus ("Audio-to-MIDI wrote a note from the clip's pitch.");
}

void MainComponent::showRackEditor()
{
    auto* track = selectedTrack();

    if (track == nullptr)
    {
        setStatus ("Select a track before editing its rack.");
        return;
    }

    auto* editor = new RackEditor();
    editor->setSize (560, 460);
    const auto id = track->id;
    editor->onCommit = [this, id] (waveform::DeviceRack rack)
    {
        history_.execute (project_, std::make_unique<waveform::SetRackCommand> (id, std::move (rack)));
        syncEngine();
        refreshHistoryButtons();
    };
    editor->setRack (track->rack);
    rackWindow_ = std::make_unique<juce::DocumentWindow> ("Rack",
                                                          juce::Colour (waveform::StudioLookAndFeel::kPanel),
                                                          juce::DocumentWindow::closeButton);
    rackWindow_->setContentOwned (editor, true);
    rackWindow_->setUsingNativeTitleBar (true);
    rackWindow_->centreWithSize (580, 500);
    rackWindow_->setVisible (true);
}

void MainComponent::showScriptConsole()
{
    auto* content = new ScriptContent();
    content->setSize (420, 280);
    content->onRun = [this, content] (const std::string& text)
    {
        const auto result = waveform::runScript (project_, text);
        content->setResult (result.message);
        rebuildTracks();
        applyModelToControls();
        syncEngine();
        setStatus (result.ok ? "Script applied." : juce::String (result.message));
    };

    scriptWindow_ = std::make_unique<juce::DocumentWindow> ("Script",
                                                            juce::Colour (waveform::StudioLookAndFeel::kPanel),
                                                            juce::DocumentWindow::closeButton);
    scriptWindow_->setContentOwned (content, true);
    scriptWindow_->setUsingNativeTitleBar (true);
    scriptWindow_->centreWithSize (440, 320);
    scriptWindow_->setVisible (true);
}

void MainComponent::showMidiMap()
{
    auto* content = new MapContent();
    content->setSize (360, 220);
    content->onLearn = [this, content]
    {
        learningControl_ = true;
        content->setBody ("Move a controller. The next CC is mapped to the selected track's volume.");
        setStatus ("MIDI learn is armed.");
    };
    content->onClear = [this, content]
    {
        controlMap_.clear();
        learningControl_ = false;
        content->setBody ("No mappings.");
    };
    content->setBody ("No mappings. Learn assigns a CC to volume.");

    midiMapWindow_ = std::make_unique<juce::DocumentWindow> ("MIDI map",
                                                             juce::Colour (waveform::StudioLookAndFeel::kPanel),
                                                             juce::DocumentWindow::closeButton);
    midiMapWindow_->setContentOwned (content, true);
    midiMapWindow_->setUsingNativeTitleBar (true);
    midiMapWindow_->centreWithSize (380, 260);
    midiMapWindow_->setVisible (true);
}

void MainComponent::applyLearnedControl()
{
    const int cc = engine_.lastController();
    const int value = engine_.lastControllerValue();

    if (cc < 0)
        return;

    if (learningControl_)
    {
        learningControl_ = false;
        controlMap_.erase (std::remove_if (controlMap_.begin(), controlMap_.end(), [cc] (const waveform::ControlMapping& mapping) { return mapping.cc == cc; }), controlMap_.end());
        controlMap_.push_back (waveform::ControlMapping { cc, "mixer.gainDb" });
        setStatus ("CC " + juce::String (cc) + " controls the selected track volume.");

        if (auto* content = dynamic_cast<MapContent*> (midiMapWindow_ != nullptr ? midiMapWindow_->getContentComponent() : nullptr))
        {
            juce::String text;

            for (const auto& mapping : controlMap_)
                text << "CC " << mapping.cc << "  ->  volume\n";

            content->setBody (text);
        }
    }

    if (selectedTrackId_.empty())
        return;

    if (waveform::applyControlChange (project_, selectedTrackId_, cc, value, controlMap_))
    {
        syncEngine();
        rebuildTracks();
    }
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

    if (const auto* track = selectedTrack(); track != nullptr && track->type == waveform::TrackType::Midi)
    {
        int id = 1;

        if (track->instrument == waveform::InstrumentKind::Sampler)
            id = 2;
        else if (track->instrument == waveform::InstrumentKind::Fm)
            id = 3;
        else if (track->instrument == waveform::InstrumentKind::Drum)
            id = 4;

        instrumentBox_.setSelectedId (id, juce::dontSendNotification);
    }

    updatingControls_ = false;
}

void MainComponent::rebuildSelection()
{
    for (std::size_t index = 0; index < rows_.size() && index < project_.tracks.size(); ++index)
        rows_[index]->setSelected (project_.tracks[index].id == selectedTrackId_);

    deviceStrip_.setTrack (selectedTrack());
    midiEffectBar_.setEffects (selectedTrack() != nullptr ? selectedTrack()->midiEffects : std::vector<waveform::MidiEffect> {});
    applyModelToControls();
}
