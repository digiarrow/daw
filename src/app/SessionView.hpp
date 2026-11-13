#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>

class SessionView : public juce::Component
{
public:
    void setState (const waveform::Project* project,
                   const std::array<int, 64>& active,
                   const std::array<int, 64>& pending,
                   const std::string& selectedClipId);

    std::function<void (int trackIndex, int scene)> onLaunch;
    std::function<void (int scene)> onLaunchScene;
    std::function<void (int trackIndex)> onReturn;
    std::function<void (int trackIndex, int scene)> onCreate;
    std::function<void (int trackIndex, int scene)> onSelect;
    std::function<void (int trackIndex, int scene)> onClear;

    void paint (juce::Graphics& graphics) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    struct Cell
    {
        int track = -1;
        int scene = -1;
        bool header = false;
        bool sceneButton = false;
        bool arrange = false;
    };

    [[nodiscard]] Cell cellAt (juce::Point<int> position) const;
    [[nodiscard]] juce::Rectangle<int> cellBounds (int track, int scene) const;

    const waveform::Project* project_ = nullptr;
    std::array<int, 64> active_ {};
    std::array<int, 64> pending_ {};
    std::string selectedClipId_;
};
