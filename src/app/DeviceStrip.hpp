#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class DeviceStrip : public juce::Component
{
public:
    DeviceStrip();
    ~DeviceStrip() override;
    void setTrack (const waveform::Track* track);
    std::function<void (int index, waveform::EffectSlot slot)> onChange;
    std::function<void (int index)> onRemove;
    std::function<void (int index, int direction)> onMove;

    void resized() override;
    void paint (juce::Graphics& graphics) override;

private:
    struct Card;

    const waveform::Track* track_ = nullptr;
    juce::OwnedArray<Card> cards_;
};
