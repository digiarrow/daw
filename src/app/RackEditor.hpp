#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class RackEditor : public juce::Component
{
public:
    RackEditor();
    void setRack (waveform::DeviceRack rack);
    std::function<void (waveform::DeviceRack)> onCommit;

    void resized() override;

private:
    void refresh();
    void commit();
    [[nodiscard]] waveform::DeviceChain* chain();
    [[nodiscard]] waveform::EffectSlot* effect();

    waveform::DeviceRack rack_;
    int chainIndex_ = 0;
    int effectIndex_ = 0;
    int macroIndex_ = 0;
    bool updating_ = false;

    juce::ComboBox chainBox_;
    juce::ComboBox effectBox_;
    juce::TextButton addChain_ { "Add chain" };
    juce::TextButton removeChain_ { "Remove chain" };
    juce::TextButton addEffect_ { "Add effect" };
    juce::TextButton removeEffect_ { "Remove effect" };
    juce::Slider gain_;
    juce::Slider parameters_[4];
    juce::ComboBox macroBox_;
    juce::Slider macro_;
    juce::TextButton mapMacro_ { "Map macro to P1" };
    juce::Label hint_;
};
