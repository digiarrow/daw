#pragma once

#include "ArrangementView.hpp"
#include "BrowserPanel.hpp"
#include "DeviceStrip.hpp"
#include "MixerView.hpp"
#include "DeviceEngine.hpp"
#include "PianoRoll.hpp"
#include "RackEditor.hpp"
#include "SessionView.hpp"
#include "waveform/commands.hpp"
#include "waveform/later.hpp"
#include "waveform/project.hpp"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::Slider::Listener,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    class TrackRow;

    void timerCallback() override;
    void sliderValueChanged (juce::Slider* slider) override;
    void sliderDragStarted (juce::Slider* slider) override;
    void sliderDragEnded (juce::Slider* slider) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void syncEngine();
    void rebuildTracks();
    void rebuildSelection();
    void applyModelToControls();
    void refreshDeviceControls();
    void refreshTransportReadout();
    void refreshHistoryButtons();
    void showWarnings();
    void setStatus (const juce::String& text);

    void commitTempo (double bpm);
    void commitMaster (double gainDb);
    void commitTimeSignature();
    void commitLoop();
    void togglePlay();
    void undo();
    void redo();

    void chooseAudioFile();
    void chooseProjectOpen();
    void showRecent();
    void rememberProject (const juce::File& file);
    void autosave();
    void restoreAutosave();
    void chooseProjectSave();
    void addAudioFile (const juce::File& file);
    void dropAudioFile (const juce::File& file, const std::string& trackId, double startQuarterNotes);
    void saveProject (const juce::File& file);
    void loadProject (const juce::File& file);
    void loadDemo();
    void addTrack (waveform::TrackType type);
    void showDeviceSettings();
    void refreshArrangement();
    void refreshPiano();
    void refreshMidiInputs();
    void armTrack (const std::string& trackId);
    void selectClip (const std::string& trackId, const std::string& clipId);
    void moveClip (const std::string& trackId, const std::string& clipId, double startQuarterNotes);
    void createMidiClip (const std::string& trackId, double startQuarterNotes);
    void commitNotes (std::vector<waveform::MidiNote> notes);
    void record();
    void commitTake();
    void collectProject();
    void exportWav();
    void showPluginMenu();
    void syncPlugins();
    void openPluginEditor (int slot = 0);
    void showScriptConsole();
    void showRackEditor();
    void showLfo();
    void convertAudioToMidi();
    void showMidiMap();
    void applyLearnedControl();

    [[nodiscard]] std::filesystem::path projectPath() const;
    [[nodiscard]] waveform::Track* selectedTrack();
    [[nodiscard]] std::string ensureAudioTrack();

    waveform::DeviceEngine engine_;
    waveform::Project project_;
    waveform::History history_;
    juce::File projectFile_;

    juce::Label titleLabel_;
    juce::Label deviceLabel_;
    juce::Label positionLabel_;
    juce::Label timeLabel_;
    juce::Label statusLabel_;
    juce::Label hintLabel_;
    juce::Label bpmCaption_;
    juce::Label masterCaption_;

    juce::TextButton playButton_ { "Play" };
    juce::TextButton pauseButton_ { "Pause" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton recordButton_ { "Record" };
    juce::TextButton deviceButton_ { "Audio Device" };
    juce::TextButton undoButton_ { "Undo" };
    juce::TextButton redoButton_ { "Redo" };
    juce::TextButton addFileButton_ { "Add Audio File" };
    juce::TextButton addAudioTrackButton_ { "Add Audio Track" };
    juce::TextButton addMidiTrackButton_ { "Add MIDI Track" };
    juce::TextButton addEffectButton_ { "Add effect" };
    juce::TextButton makeGroupButton_ { "Make Group" };
    juce::TextButton addReturnButton_ { "Add Return" };
    juce::TextButton mixerButton_ { "Mixer" };
    juce::TextButton collectButton_ { "Collect" };
    juce::TextButton exportButton_ { "Export WAV" };
    juce::TextButton loadDemoButton_ { "Load Demo" };
    juce::TextButton openButton_ { "Open" };
    juce::TextButton recentButton_ { "Recent" };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton arrangementButton_ { "Arrangement" };
    juce::TextButton sessionButton_ { "Session" };
    juce::TextButton pluginButton_ { "Plug-in" };
    juce::TextButton automationButton_ { "Volume lane" };
    juce::TextButton scriptButton_ { "Script" };
    juce::TextButton rackButton_ { "Rack" };
    juce::TextButton lfoButton_ { "LFO" };
    juce::TextButton audioToMidiButton_ { "Audio to MIDI" };
    juce::TextButton midiMapButton_ { "MIDI map" };

    juce::ToggleButton loopButton_ { "Loop" };
    juce::ToggleButton metronomeButton_ { "Click" };
    juce::ToggleButton toneButton_ { "Test Tone" };

    juce::Slider bpmSlider_;
    juce::Slider masterSlider_;
    juce::ComboBox numeratorBox_;
    juce::ComboBox denominatorBox_;
    juce::ComboBox sampleRateBox_;
    juce::ComboBox bufferBox_;
    juce::ComboBox countInBox_;
    juce::ComboBox midiInputBox_;
    juce::ComboBox launchBox_;
    juce::ComboBox instrumentBox_;

    BrowserPanel browser_;
    DeviceStrip deviceStrip_;
    MixerView mixer_;
    ArrangementView arrangement_;
    SessionView session_;
    PianoRoll pianoRoll_;

    juce::Viewport trackViewport_;
    juce::Component trackList_;
    std::vector<std::unique_ptr<TrackRow>> rows_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<juce::DocumentWindow> deviceWindow_;
    std::unique_ptr<juce::DocumentWindow> pluginWindow_;
    std::unique_ptr<juce::DocumentWindow> scriptWindow_;
    std::unique_ptr<juce::DocumentWindow> rackWindow_;
    std::unique_ptr<juce::DocumentWindow> exportWindow_;
    std::unique_ptr<juce::DocumentWindow> midiMapWindow_;
    std::vector<waveform::ControlMapping> controlMap_;
    bool learningControl_ = false;
    int seenControlEpoch_ = 0;
    std::vector<juce::PluginDescription> scannedPlugins_;

    juce::Rectangle<int> transportBounds_;
    juce::Rectangle<int> masterBounds_;
    juce::Rectangle<float> meterLeftBounds_;
    juce::Rectangle<float> meterRightBounds_;

    std::string selectedTrackId_;
    std::string selectedClipId_;
    bool showSession_ = false;
    bool showAutomation_ = true;
    bool showMixer_ = false;
    std::array<int, 64> sessionActive_ {};
    std::array<int, 64> sessionPending_ {};
    std::string armedTrackId_;
    std::vector<juce::String> midiInputIds_;
    int seenRecordEpoch_ = 0;
    bool updatingControls_ = false;
    bool draggingTempo_ = false;
    bool draggingMaster_ = false;
    double dragTempo_ = 120.0;
    double dragMasterDb_ = 0.0;
    float meterLeft_ = 0.0f;
    float meterRight_ = 0.0f;
    int seenDeviceGeneration_ = -1;
    int autosaveTicks_ = 0;
};
