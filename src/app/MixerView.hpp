#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class MixerView : public juce::Component
{
public:
    MixerView();
    ~MixerView() override;
    void setProject (const waveform::Project& project);
    void setPeaks (const std::vector<float>& peaks);

    std::function<void (const std::string& trackId, waveform::MixerState state)> onMixer;
    std::function<void (const std::string& trackId, waveform::Send send)> onSend;

    void resized() override;
    void paint (juce::Graphics& graphics) override;

private:
    struct Strip;

    waveform::Project project_;
    std::vector<float> peaks_;
    juce::OwnedArray<Strip> strips_;
};
