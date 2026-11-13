#pragma once

#include "waveform/midi_effects.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

class MidiEffectBar : public juce::Component
{
public:
    MidiEffectBar();

    void setEffects (const std::vector<waveform::MidiEffect>& effects);
    std::function<void (std::vector<waveform::MidiEffect>)> onChange;

    void resized() override;

    juce::TextButton& importButton() noexcept { return importButton_; }
    juce::TextButton& exportButton() noexcept { return exportButton_; }

private:
    void publish();

    bool updating_ = false;
    std::array<juce::ComboBox, waveform::kMidiEffectSlots> kinds_;
    std::array<juce::Slider, waveform::kMidiEffectSlots> amounts_;
    juce::TextButton importButton_ { "Import MIDI" };
    juce::TextButton exportButton_ { "Export MIDI" };
};
