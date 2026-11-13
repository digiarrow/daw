#include "ArrangementView.hpp"

#include "StudioLookAndFeel.hpp"
#include "waveform/timing.hpp"

#include <algorithm>
#include <cmath>

namespace
{

constexpr int kRuler = 22;
constexpr int kRow = 32;
constexpr int kNames = 132;
constexpr float kPixelsPerQuarter = 28.0f;

double snapped (double quarterNotes)
{
    return std::max (0.0, std::round (quarterNotes * 4.0) / 4.0);
}

} // namespace

void ArrangementView::setState (const waveform::Project* project,
                                double playheadQuarterNotes,
                                const std::string& selectedClipId,
                                const std::string& armedTrackId)
{
    project_ = project;
    playhead_ = playheadQuarterNotes;
    selectedClipId_ = selectedClipId;
    armedTrackId_ = armedTrackId;
    repaint();
}

double ArrangementView::quarterAt (int x) const
{
    return scroll_ + static_cast<double> (x - kNames) / kPixelsPerQuarter;
}

float ArrangementView::xForQuarter (double quarterNotes) const
{
    return static_cast<float> (kNames) + static_cast<float> ((quarterNotes - scroll_) * kPixelsPerQuarter);
}

double ArrangementView::clipLengthQuarters (const waveform::Clip& clip) const
{
    if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
    {
        const double tempo = project_ != nullptr ? project_->tempoBpm : 120.0;
        return std::max (0.25, audio->lengthSeconds * tempo / 60.0);
    }

    const auto* midi = std::get_if<waveform::MidiClip> (&clip);
    return midi != nullptr ? std::max (0.25, midi->lengthQuarterNotes) : 1.0;
}

ArrangementView::Hit ArrangementView::findHit (juce::Point<int> position) const
{
    Hit hit;

    if (project_ == nullptr)
        return hit;

    if (position.y < kRuler)
    {
        hit.ruler = true;
        const double quarter = quarterAt (position.x);
        hit.loopStart = std::abs (quarter - project_->loopStartQuarterNotes) < 0.35;
        hit.loopEnd = std::abs (quarter - project_->loopEndQuarterNotes) < 0.35;
        return hit;
    }

    const int row = (position.y - kRuler) / kRow;

    if (row < 0 || row >= static_cast<int> (project_->tracks.size()))
        return hit;

    hit.trackIndex = row;

    if (position.x < 28)
    {
        hit.arm = true;
        return hit;
    }

    if (position.x < kNames)
        return hit;

    const double quarter = quarterAt (position.x);
    const auto& track = project_->tracks[static_cast<std::size_t> (row)];

    for (int index = static_cast<int> (track.clips.size()) - 1; index >= 0; --index)
    {
        const auto& clip = track.clips[static_cast<std::size_t> (index)];
        const double start = std::holds_alternative<waveform::AudioClip> (clip)
                                 ? std::get<waveform::AudioClip> (clip).startQuarterNotes
                                 : std::get<waveform::MidiClip> (clip).startQuarterNotes;
        const double end = start + clipLengthQuarters (clip);

        if (quarter >= start && quarter <= end)
        {
            hit.clipIndex = index;
            break;
        }
    }

    return hit;
}

bool ArrangementView::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.isString() && project_ != nullptr;
}

void ArrangementView::itemDropped (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (project_ == nullptr || ! onDropFile)
        return;

    const auto hit = findHit (details.localPosition);

    if (hit.trackIndex < 0 || hit.trackIndex >= static_cast<int> (project_->tracks.size()))
        return;

    const auto start = std::max (0.0, snapped (quarterAt (details.localPosition.x)));
    onDropFile (details.description.toString().toStdString(),
                project_->tracks[static_cast<std::size_t> (hit.trackIndex)].id,
                start);
}

void ArrangementView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (waveform::StudioLookAndFeel::kBackground));

    if (project_ == nullptr)
        return;

    const auto ruler = juce::Rectangle<int> (kNames, 0, getWidth() - kNames, kRuler);
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kPanel));
    graphics.fillRect (ruler);
    const double shownLoopStart = draggingLoop_ ? previewLoopStart_ : project_->loopStartQuarterNotes;
    const double shownLoopEnd = draggingLoop_ ? previewLoopEnd_ : project_->loopEndQuarterNotes;
    const float loopStart = xForQuarter (shownLoopStart);
    const float loopEnd = xForQuarter (shownLoopEnd);
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper).withAlpha (project_->loopEnabled ? 0.35f : 0.15f));
    graphics.fillRect (juce::Rectangle<float> (loopStart, 0.0f, std::max (4.0f, loopEnd - loopStart), static_cast<float> (kRuler)));
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
    graphics.fillRect (loopStart - 1.0f, 0.0f, 2.0f, static_cast<float> (kRuler));
    graphics.fillRect (loopEnd - 1.0f, 0.0f, 2.0f, static_cast<float> (kRuler));

    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
    const double quartersPerBar = waveform::Timing::quarterNotesPerBar (project_->timeSignature);
    const int bars = 64;

    for (int bar = 0; bar < bars; ++bar)
    {
        const double quarter = static_cast<double> (bar) * quartersPerBar;
        const float x = xForQuarter (quarter);

        if (x < kNames || x > getWidth())
            continue;

        graphics.drawText (juce::String (bar + 1), static_cast<int> (x) + 4, 2, 40, kRuler - 4, juce::Justification::centredLeft);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kLine));
        graphics.drawVerticalLine (static_cast<int> (x), static_cast<float> (kRuler), static_cast<float> (getHeight()));
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
    }

    for (std::size_t index = 0; index < project_->tracks.size(); ++index)
    {
        const int y = kRuler + static_cast<int> (index) * kRow;
        const auto& track = project_->tracks[index];
        graphics.setColour (juce::Colour (index % 2 == 0 ? 0xff17201c : 0xff1c2622));
        graphics.fillRect (0, y, getWidth(), kRow - 2);
        graphics.setColour (track.id == armedTrackId_ ? juce::Colour (waveform::StudioLookAndFeel::kRose)
                                                       : juce::Colour (waveform::StudioLookAndFeel::kLine));
        graphics.fillRoundedRectangle (6.0f, static_cast<float> (y + 6), 18.0f, 18.0f, 3.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
        graphics.drawText ("R", 6, y + 6, 18, 18, juce::Justification::centred);
        graphics.drawText (track.name, 30, y, kNames - 34, kRow - 2, juce::Justification::centredLeft);

        for (const auto& clip : track.clips)
        {
            const bool audio = std::holds_alternative<waveform::AudioClip> (clip);
            const double start = audio ? std::get<waveform::AudioClip> (clip).startQuarterNotes
                                       : std::get<waveform::MidiClip> (clip).startQuarterNotes;
            const auto& id = waveform::clipId (clip);
            const float x = xForQuarter (draggingClip_ && id == dragClipId_ ? previewStart_ : start);
            const float width = std::max (8.0f, static_cast<float> (clipLengthQuarters (clip) * kPixelsPerQuarter));
            graphics.setColour (juce::Colour (audio ? 0xff3d6b56 : 0xff8a6238));
            graphics.fillRoundedRectangle (x, static_cast<float> (y + 4), width, static_cast<float> (kRow - 10), 4.0f);

            if (id == selectedClipId_)
            {
                graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
                graphics.drawRoundedRectangle (x, static_cast<float> (y + 4), width, static_cast<float> (kRow - 10), 4.0f, 1.5f);
            }

            graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
            const auto name = audio ? std::get<waveform::AudioClip> (clip).name : std::get<waveform::MidiClip> (clip).name;
            graphics.drawText (name, static_cast<int> (x) + 4, y + 4, static_cast<int> (width) - 8, kRow - 10, juce::Justification::centredLeft);
        }
    }

    const float playhead = xForQuarter (playhead_);

    if (playhead >= kNames)
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kMint));
        graphics.drawLine (playhead, 0.0f, playhead, static_cast<float> (getHeight()), 1.5f);
    }
}

void ArrangementView::mouseDown (const juce::MouseEvent& event)
{
    const auto hit = findHit (event.getPosition());

    if (hit.ruler && project_ != nullptr && (hit.loopStart || hit.loopEnd))
    {
        draggingLoop_ = true;
        loopDragStart_ = hit.loopStart && ! hit.loopEnd;
        previewLoopStart_ = project_->loopStartQuarterNotes;
        previewLoopEnd_ = project_->loopEndQuarterNotes;
        return;
    }

    if (hit.ruler)
    {
        if (onSeek)
            onSeek (std::max (0.0, quarterAt (event.x)));

        return;
    }

    if (hit.arm && hit.trackIndex >= 0 && onArmTrack)
    {
        onArmTrack (project_->tracks[static_cast<std::size_t> (hit.trackIndex)].id);
        return;
    }

    if (hit.clipIndex >= 0 && hit.trackIndex >= 0)
    {
        const auto& track = project_->tracks[static_cast<std::size_t> (hit.trackIndex)];
        const auto& clip = track.clips[static_cast<std::size_t> (hit.clipIndex)];
        dragTrackId_ = track.id;
        dragClipId_ = waveform::clipId (clip);
        dragClipStart_ = std::holds_alternative<waveform::AudioClip> (clip)
                             ? std::get<waveform::AudioClip> (clip).startQuarterNotes
                             : std::get<waveform::MidiClip> (clip).startQuarterNotes;
        dragOriginQuarter_ = quarterAt (event.x);
        previewStart_ = dragClipStart_;
        draggingClip_ = true;

        if (onSelectClip)
            onSelectClip (dragTrackId_, dragClipId_);
    }
}

void ArrangementView::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingLoop_ && project_ != nullptr)
    {
        const double quarter = snapped (quarterAt (event.x));

        if (loopDragStart_)
            previewLoopStart_ = std::min (quarter, previewLoopEnd_ - 0.25);
        else
            previewLoopEnd_ = std::max (quarter, previewLoopStart_ + 0.25);

        previewLoopStart_ = std::max (0.0, previewLoopStart_);
        repaint();
        return;
    }

    if (! draggingClip_)
        return;

    previewStart_ = snapped (dragClipStart_ + quarterAt (event.x) - dragOriginQuarter_);
    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (draggingClip_ && std::abs (previewStart_ - dragClipStart_) > 0.001 && onMoveClip)
        onMoveClip (dragTrackId_, dragClipId_, previewStart_);

    if (draggingLoop_ && onLoopEdit)
        onLoopEdit (true, previewLoopStart_, previewLoopEnd_);

    draggingClip_ = false;
    draggingLoop_ = false;
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto hit = findHit (event.getPosition());

    if (hit.trackIndex < 0 || project_ == nullptr || hit.clipIndex >= 0 || hit.arm)
        return;

    const auto& track = project_->tracks[static_cast<std::size_t> (hit.trackIndex)];

    if (track.type == waveform::TrackType::Midi && onCreateMidiClip)
        onCreateMidiClip (track.id, snapped (quarterAt (event.x)));
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scroll_ = std::max (0.0, scroll_ - static_cast<double> (wheel.deltaY + wheel.deltaX) * 4.0);
    repaint();
}
