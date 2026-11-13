#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

class ArrangementView : public juce::Component,
                          public juce::DragAndDropTarget
{
public:
    void setState (const waveform::Project* project,
                   double playheadQuarterNotes,
                   const std::string& selectedClipId,
                   const std::string& armedTrackId,
                   const std::string& automationTrackId,
                   bool showAutomation);

    std::function<void (const std::string& trackId, const std::string& clipId)> onSelectClip;
    std::function<void (const std::string& trackId)> onArmTrack;
    std::function<void (const std::string& trackId, const std::string& clipId, double startQuarterNotes)> onMoveClip;
    std::function<void (const std::string& trackId, double startQuarterNotes)> onCreateMidiClip;
    std::function<void (double quarterNotes)> onSeek;
    std::function<void (bool enabled, double startQuarterNotes, double endQuarterNotes)> onLoopEdit;
    std::function<void (const std::string& trackId, std::vector<waveform::AutomationPoint> points)> onAutomation;
    std::function<void (const std::string& trackId, const std::string& clipId, std::vector<waveform::WarpMarker> warp, std::vector<waveform::CompSegment> comp)> onMarkers;
    std::function<void (const std::string& path, const std::string& trackId, double startQuarterNotes)> onDropFile;

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;

    void paint (juce::Graphics& graphics) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    struct Hit
    {
        int trackIndex = -1;
        int clipIndex = -1;
        bool loopStart = false;
        bool loopEnd = false;
        bool ruler = false;
        bool arm = false;
    };

    [[nodiscard]] Hit findHit (juce::Point<int> position) const;
    [[nodiscard]] double quarterAt (int x) const;
    [[nodiscard]] float xForQuarter (double quarterNotes) const;
    [[nodiscard]] double clipLengthQuarters (const waveform::Clip& clip) const;

    const waveform::Project* project_ = nullptr;
    double playhead_ = 0.0;
    double scroll_ = 0.0;
    std::string selectedClipId_;
    std::string armedTrackId_;
    std::string automationTrackId_;
    bool showAutomation_ = false;
    bool draggingPoint_ = false;
    int dragPoint_ = -1;
    std::vector<waveform::AutomationPoint> automationEdit_;

    bool draggingClip_ = false;
    bool draggingLoop_ = false;
    bool loopDragStart_ = false;
    double previewLoopStart_ = 0.0;
    double previewLoopEnd_ = 4.0;
    double dragOriginQuarter_ = 0.0;
    double dragClipStart_ = 0.0;
    std::string dragTrackId_;
    std::string dragClipId_;
    double previewStart_ = 0.0;
};
