#pragma once

#include "waveform/project.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::function<void (const std::string& trackId, const std::string& clipId, double startQuarterNotes, double lengthQuarterNotes)> onResizeClip;
    std::function<void (const std::string& trackId, const std::string& clipId, double fadeInSeconds, double fadeOutSeconds)> onFadeClip;
    void setClipPeaks (std::unordered_map<std::string, std::vector<float>> peaks);
    void setAutomationParameter (std::string parameter);
    std::function<void (const std::string& trackId, double startQuarterNotes)> onCreateMidiClip;
    std::function<void (double quarterNotes)> onSeek;
    std::function<void (bool enabled, double startQuarterNotes, double endQuarterNotes)> onLoopEdit;
    std::function<void (const std::string& trackId, std::vector<waveform::AutomationPoint> points)> onAutomation;
    std::function<void (const std::string& trackId, const std::string& clipId, std::vector<waveform::AutomationPoint> points)> onClipEnvelope;
    std::function<void (const std::string& trackId, const std::string& clipId, std::vector<waveform::WarpMarker> warp, std::vector<waveform::CompSegment> comp)> onMarkers;
    std::function<void (const std::string& path, const std::string& trackId, double startQuarterNotes)> onDropFile;
    std::function<void (const std::string& path, const std::string& trackId, const std::string& clipId)> onAddTake;

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
        bool trimStart = false;
        bool trimEnd = false;
        bool fadeIn = false;
        bool fadeOut = false;
    };

    enum class DragKind
    {
        None,
        Move,
        TrimStart,
        TrimEnd,
        FadeIn,
        FadeOut
    };

    [[nodiscard]] Hit findHit (juce::Point<int> position) const;
    [[nodiscard]] double quarterAt (int x) const;
    [[nodiscard]] float xForQuarter (double quarterNotes) const;
    [[nodiscard]] double clipLengthQuarters (const waveform::Clip& clip) const;

    struct EnvelopeMap
    {
        bool found = false;
        std::string trackId;
        double startQuarter = 0.0;
        double lengthQuarter = 1.0;
        double lengthLocal = 1.0;
        const std::vector<waveform::AutomationPoint>* points = nullptr;
    };

    [[nodiscard]] bool editingEnvelope() const noexcept { return automationParameter_ == "clip.envelope"; }
    [[nodiscard]] EnvelopeMap selectedEnvelope() const;
    [[nodiscard]] double localFromQuarter (const EnvelopeMap& map, double quarter) const;
    [[nodiscard]] double quarterFromLocal (const EnvelopeMap& map, double local) const;

    const waveform::Project* project_ = nullptr;
    double playhead_ = 0.0;
    double scroll_ = 0.0;
    std::string selectedClipId_;
    std::string armedTrackId_;
    std::string automationTrackId_;
    std::string automationParameter_ { "mixer.gainDb" };
    bool showAutomation_ = false;
    bool draggingPoint_ = false;
    int dragPoint_ = -1;
    std::vector<waveform::AutomationPoint> automationEdit_;

    bool draggingClip_ = false;
    DragKind dragKind_ = DragKind::None;
    double previewLength_ = 1.0;
    double originalLength_ = 1.0;
    double previewFadeIn_ = 0.0;
    double previewFadeOut_ = 0.0;
    std::unordered_map<std::string, std::vector<float>> peaks_;
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
