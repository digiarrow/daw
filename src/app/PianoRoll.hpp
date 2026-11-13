#pragma once

#include "waveform/later.hpp"
#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class PianoRoll : public juce::Component
{
public:
    PianoRoll();
    void setClip (const waveform::MidiClip* clip);

    /// Replacement note list after a completed edit.
    std::function<void (std::vector<waveform::MidiNote>)> onCommit;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    enum class Drag
    {
        None,
        Draw,
        Move,
        Resize,
        Velocity
    };

    [[nodiscard]] int pitchAt (int y) const;
    [[nodiscard]] double quarterAt (int x) const;
    [[nodiscard]] float yForPitch (int pitch) const;
    [[nodiscard]] float xForQuarter (double quarterNotes) const;
    [[nodiscard]] double snap (double quarterNotes) const;
    [[nodiscard]] int noteAt (juce::Point<int> position, bool& edge) const;
    void quantize();

    waveform::MidiClip clip_;
    bool hasClip_ = false;
    std::vector<waveform::MidiNote> edit_;
    std::vector<waveform::MidiNote> before_;
    int selected_ = -1;
    Drag drag_ = Drag::None;
    double grabQuarter_ = 0.0;
    int grabPitch_ = 60;
    int scrollPitch_ = 72;
    double scrollQuarter_ = 0.0;
    int division_ = 16;

    juce::ComboBox snapBox_;
    juce::TextButton quantizeButton_ { "Quantize" };
    juce::ComboBox keyBox_;
    juce::ComboBox scaleBox_;
    juce::TextButton constrainButton_ { "In key" };
    juce::TextButton humanizeButton_ { "Humanize" };
    juce::TextButton legatoButton_ { "Legato" };
    juce::TextButton reverseButton_ { "Reverse" };
    juce::Slider slide_;
    juce::Label slideLabel_ { {}, "Slide" };
    int keyRoot_ = 0;
    bool minorKey_ = false;

    void transform (std::vector<waveform::MidiNote> notes);
};
