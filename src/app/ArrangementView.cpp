#include "ArrangementView.hpp"

#include "StudioLookAndFeel.hpp"
#include "waveform/clips.hpp"
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
                                const std::string& armedTrackId,
                                const std::string& automationTrackId,
                                bool showAutomation)
{
    project_ = project;
    playhead_ = playheadQuarterNotes;
    selectedClipId_ = selectedClipId;
    armedTrackId_ = armedTrackId;
    automationTrackId_ = automationTrackId;
    showAutomation_ = showAutomation;
    repaint();
}

void ArrangementView::setAutomationParameter (std::string parameter)
{
    if (! parameter.empty())
        automationParameter_ = std::move (parameter);

    repaint();
}

void ArrangementView::setClipPeaks (std::unordered_map<std::string, std::vector<float>> peaks)
{
    peaks_ = std::move (peaks);
    repaint();
}

namespace
{

juce::Rectangle<int> automationLane (const juce::Component& component, bool shown)
{
    if (! shown)
        return {};

    return { kNames, component.getHeight() - 58, std::max (0, component.getWidth() - kNames - 8), 50 };
}

double decibelsAt (int y, juce::Rectangle<int> lane)
{
    if (lane.getHeight() <= 0)
        return 0.0;

    const double mix = 1.0 - std::clamp ((static_cast<double> (y) - lane.getY()) / lane.getHeight(), 0.0, 1.0);
    return -48.0 + mix * 54.0;
}

int yForDecibels (double decibels, juce::Rectangle<int> lane)
{
    const double mix = std::clamp ((decibels + 48.0) / 54.0, 0.0, 1.0);
    return lane.getBottom() - static_cast<int> (mix * lane.getHeight());
}

} // namespace

double ArrangementView::quarterAt (int x) const
{
    return scroll_ + static_cast<double> (x - kNames) / kPixelsPerQuarter;
}

float ArrangementView::xForQuarter (double quarterNotes) const
{
    return static_cast<float> (kNames) + static_cast<float> ((quarterNotes - scroll_) * kPixelsPerQuarter);
}

ArrangementView::EnvelopeMap ArrangementView::selectedEnvelope() const
{
    EnvelopeMap map;

    if (project_ == nullptr || selectedClipId_.empty())
        return map;

    for (const auto& track : project_->tracks)
    {
        for (const auto& clip : track.clips)
        {
            if (waveform::clipId (clip) != selectedClipId_)
                continue;

            map.found = true;
            map.trackId = track.id;
            map.lengthQuarter = std::max (0.25, clipLengthQuarters (clip));

            if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
            {
                map.startQuarter = audio->startQuarterNotes;
                map.lengthLocal = std::max (0.01, audio->lengthSeconds);
                map.points = &audio->envelope;
            }
            else if (const auto* midi = std::get_if<waveform::MidiClip> (&clip))
            {
                map.startQuarter = midi->startQuarterNotes;
                map.lengthLocal = std::max (0.25, midi->lengthQuarterNotes);
                map.points = &midi->envelope;
            }

            return map;
        }
    }

    return map;
}

double ArrangementView::localFromQuarter (const EnvelopeMap& map, double quarter) const
{
    const double span = std::max (0.25, map.lengthQuarter);
    const double mix = std::clamp ((quarter - map.startQuarter) / span, 0.0, 1.0);
    return mix * map.lengthLocal;
}

double ArrangementView::quarterFromLocal (const EnvelopeMap& map, double local) const
{
    const double span = std::max (0.01, map.lengthLocal);
    const double mix = std::clamp (local / span, 0.0, 1.0);
    return map.startQuarter + mix * std::max (0.25, map.lengthQuarter);
}

double ArrangementView::clipLengthQuarters (const waveform::Clip& clip) const
{
    if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
    {
        if (! audio->warp.empty())
        {
            double end = 0.25;

            for (const auto& marker : audio->warp)
                end = std::max (end, marker.timelineQuarters);

            return end;
        }

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
            const float clipX = xForQuarter (start);
            const float width = std::max (8.0f, static_cast<float> ((end - start) * kPixelsPerQuarter));
            const float local = static_cast<float> (position.x) - clipX;
            const int rowTop = kRuler + row * kRow;
            const bool top = position.y - rowTop < 14;

            if (local < 10.0f && top)
                hit.fadeIn = true;
            else if (width - local < 10.0f && top)
                hit.fadeOut = true;
            else if (local < 8.0f)
                hit.trimStart = true;
            else if (width - local < 8.0f)
                hit.trimEnd = true;

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

    const auto& track = project_->tracks[static_cast<std::size_t> (hit.trackIndex)];

    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown() && hit.clipIndex >= 0 && onAddTake)
    {
        const auto& clip = track.clips[static_cast<std::size_t> (hit.clipIndex)];

        if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
        {
            onAddTake (details.description.toString().toStdString(), track.id, audio->id);
            return;
        }
    }

    const auto start = std::max (0.0, snapped (quarterAt (details.localPosition.x)));
    onDropFile (details.description.toString().toStdString(), track.id, start);
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
            const double modelStart = audio ? std::get<waveform::AudioClip> (clip).startQuarterNotes
                                            : std::get<waveform::MidiClip> (clip).startQuarterNotes;
            const auto& id = waveform::clipId (clip);
            const bool preview = draggingClip_ && id == dragClipId_;
            const double start = preview ? previewStart_ : modelStart;
            const double length = preview && dragKind_ != DragKind::Move ? previewLength_ : clipLengthQuarters (clip);
            const float x = xForQuarter (start);
            const float width = std::max (8.0f, static_cast<float> (length * kPixelsPerQuarter));
            graphics.setColour (juce::Colour (audio ? 0xff3d6b56 : 0xff8a6238));
            graphics.fillRoundedRectangle (x, static_cast<float> (y + 4), width, static_cast<float> (kRow - 10), 4.0f);

            if (audio)
            {
                const auto peaks = peaks_.find (id);

                if (peaks != peaks_.end() && ! peaks->second.empty())
                {
                    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kMint).withAlpha (0.85f));
                    const float bodyTop = static_cast<float> (y + 8);
                    const float bodyHeight = static_cast<float> (kRow - 18);

                    for (std::size_t column = 0; column < peaks->second.size(); ++column)
                    {
                        const float barX = x + width * static_cast<float> (column) / static_cast<float> (peaks->second.size());
                        const float barHeight = bodyHeight * std::clamp (peaks->second[column], 0.0f, 1.0f);
                        graphics.fillRect (barX, bodyTop + bodyHeight - barHeight, std::max (1.0f, width / static_cast<float> (peaks->second.size()) - 1.0f), barHeight);
                    }
                }

                const auto& audioClip = std::get<waveform::AudioClip> (clip);
                const double fadeIn = preview ? previewFadeIn_ : audioClip.fadeInSeconds;
                const double fadeOut = preview ? previewFadeOut_ : audioClip.fadeOutSeconds;
                const double tempo = project_->tempoBpm > 0.0 ? project_->tempoBpm : 120.0;
                const double shownSeconds = std::max (0.01, length * 60.0 / tempo);
                graphics.setColour (juce::Colour (0xffd7efe4).withAlpha (0.55f));

                if (fadeIn > 0.0)
                {
                    const float fadeWidth = width * static_cast<float> (std::min (fadeIn, shownSeconds) / shownSeconds);
                    juce::Path fade;
                    fade.startNewSubPath (x, static_cast<float> (y + kRow - 6));
                    fade.lineTo (x, static_cast<float> (y + 6));
                    fade.lineTo (x + fadeWidth, static_cast<float> (y + 6));
                    fade.closeSubPath();
                    graphics.fillPath (fade);
                }

                if (fadeOut > 0.0)
                {
                    const float fadeWidth = width * static_cast<float> (std::min (fadeOut, shownSeconds) / shownSeconds);
                    juce::Path fade;
                    fade.startNewSubPath (x + width, static_cast<float> (y + kRow - 6));
                    fade.lineTo (x + width, static_cast<float> (y + 6));
                    fade.lineTo (x + width - fadeWidth, static_cast<float> (y + 6));
                    fade.closeSubPath();
                    graphics.fillPath (fade);
                }
            }

            if (id == selectedClipId_)
            {
                graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
                graphics.drawRoundedRectangle (x, static_cast<float> (y + 4), width, static_cast<float> (kRow - 10), 4.0f, 1.5f);
            }

            if (audio)
            {
                const auto& audioClip = std::get<waveform::AudioClip> (clip);
                graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kMint));

                for (const auto& marker : audioClip.warp)
                    graphics.drawLine (xForQuarter (start + marker.timelineQuarters), static_cast<float> (y + 6),
                                       xForQuarter (start + marker.timelineQuarters), static_cast<float> (y + kRow - 8), 1.0f);

                for (const auto& segment : audioClip.comp)
                {
                    const float left = xForQuarter (start + segment.startQuarter);
                    const float right = xForQuarter (start + segment.endQuarter);
                    graphics.setColour (juce::Colour (segment.takeIndex % 2 == 0 ? 0xffd4894c : 0xff7dcea0).withAlpha (0.85f));
                    graphics.fillRect (left, static_cast<float> (y + kRow - 12), std::max (2.0f, right - left), 4.0f);
                }
            }

            const std::vector<waveform::AutomationPoint>* envelope = nullptr;

            if (const auto* audioClip = std::get_if<waveform::AudioClip> (&clip))
                envelope = &audioClip->envelope;
            else if (const auto* midiClip = std::get_if<waveform::MidiClip> (&clip))
                envelope = &midiClip->envelope;

            if (draggingPoint_ && editingEnvelope() && id == selectedClipId_)
                envelope = &automationEdit_;

            if (envelope != nullptr && envelope->size() >= 2)
            {
                EnvelopeMap map;
                map.startQuarter = start;
                map.lengthQuarter = std::max (0.25, length);
                map.lengthLocal = audio ? std::max (0.01, std::get<waveform::AudioClip> (clip).lengthSeconds)
                                        : std::max (0.25, std::get<waveform::MidiClip> (clip).lengthQuarterNotes);
                juce::Path envelopePath;

                for (std::size_t pointIndex = 0; pointIndex < envelope->size(); ++pointIndex)
                {
                    const float pointX = xForQuarter (quarterFromLocal (map, (*envelope)[pointIndex].quarterNotes));
                    const float pointY = static_cast<float> (y + 6) + (1.0f - std::clamp (static_cast<float> ((*envelope)[pointIndex].value), 0.0f, 1.0f)) * static_cast<float> (kRow - 16);

                    if (pointIndex == 0)
                        envelopePath.startNewSubPath (pointX, pointY);
                    else
                        envelopePath.lineTo (pointX, pointY);
                }

                graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
                graphics.strokePath (envelopePath, juce::PathStrokeType (1.25f));
            }

            graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
            const auto name = audio ? std::get<waveform::AudioClip> (clip).name : std::get<waveform::MidiClip> (clip).name;
            graphics.drawText (name, static_cast<int> (x) + 4, y + 4, static_cast<int> (width) - 8, kRow - 10, juce::Justification::centredLeft);
        }
    }

    if (showAutomation_ && project_ != nullptr)
    {
        const auto lane = automationLane (*this, true);
        graphics.setColour (juce::Colour (0xff18211d));
        graphics.fillRoundedRectangle (lane.toFloat(), 4.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText (editingEnvelope() ? "clip envelope" : automationParameter_, 8, lane.getY(), kNames - 12, lane.getHeight(), juce::Justification::centredLeft);

        const waveform::Track* automated = nullptr;

        for (const auto& track : project_->tracks)
            if (track.id == automationTrackId_)
                automated = &track;

        const auto* points = &automationEdit_;
        const auto envelope = selectedEnvelope();

        if (editingEnvelope())
        {
            if (! draggingPoint_ && envelope.points != nullptr)
                points = envelope.points;
        }
        else if (! draggingPoint_ && automated != nullptr)
        {
            for (const auto& candidate : automated->automation)
                if (candidate.parameter == automationParameter_)
                    points = &candidate.points;
        }

        if (points != nullptr && points->size() >= 2)
        {
            juce::Path path;

            for (std::size_t index = 0; index < points->size(); ++index)
            {
                const float x = editingEnvelope()
                                    ? xForQuarter (quarterFromLocal (envelope, (*points)[index].quarterNotes))
                                    : xForQuarter ((*points)[index].quarterNotes);
                const float y = editingEnvelope()
                                    ? static_cast<float> (lane.getBottom() - static_cast<int> (std::clamp ((*points)[index].value, 0.0, 1.0) * lane.getHeight()))
                                    : static_cast<float> (yForDecibels ((*points)[index].value, lane));

                if (index == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }

            graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
            graphics.strokePath (path, juce::PathStrokeType (1.5f));
        }

        if (points != nullptr)
        {
            graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kMint));

            for (const auto& point : *points)
            {
                const float x = editingEnvelope()
                                    ? xForQuarter (quarterFromLocal (envelope, point.quarterNotes))
                                    : xForQuarter (point.quarterNotes);
                const float y = editingEnvelope()
                                    ? static_cast<float> (lane.getBottom() - static_cast<int> (std::clamp (point.value, 0.0, 1.0) * lane.getHeight()))
                                    : static_cast<float> (yForDecibels (point.value, lane));
                graphics.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            }
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
    const auto lane = automationLane (*this, showAutomation_);

    if (showAutomation_ && lane.contains (event.getPosition()) && project_ != nullptr && editingEnvelope())
    {
        const auto envelope = selectedEnvelope();

        if (! envelope.found || envelope.points == nullptr)
            return;

        automationEdit_ = *envelope.points;
        const double local = localFromQuarter (envelope, std::max (0.0, snapped (quarterAt (event.x))));
        const double value = lane.getHeight() > 0
                                 ? 1.0 - std::clamp ((static_cast<double> (event.y) - lane.getY()) / lane.getHeight(), 0.0, 1.0)
                                 : 0.0;
        int nearest = -1;
        float best = 8.0f;

        for (int index = 0; index < static_cast<int> (automationEdit_.size()); ++index)
        {
            const float dx = xForQuarter (quarterFromLocal (envelope, automationEdit_[static_cast<std::size_t> (index)].quarterNotes)) - static_cast<float> (event.x);
            const float dy = static_cast<float> (lane.getBottom() - static_cast<int> (std::clamp (automationEdit_[static_cast<std::size_t> (index)].value, 0.0, 1.0) * lane.getHeight())) - static_cast<float> (event.y);
            const float distance = std::sqrt (dx * dx + dy * dy);

            if (distance < best)
            {
                best = distance;
                nearest = index;
            }
        }

        if (event.mods.isRightButtonDown() && nearest >= 0)
            automationEdit_.erase (automationEdit_.begin() + nearest);
        else if (nearest >= 0)
            dragPoint_ = nearest;
        else
        {
            automationEdit_.push_back ({ local, value });
            dragPoint_ = static_cast<int> (automationEdit_.size()) - 1;
        }

        draggingPoint_ = true;
        repaint();
        return;
    }

    if (showAutomation_ && lane.contains (event.getPosition()) && project_ != nullptr && ! automationTrackId_.empty())
    {
        const waveform::Track* automated = nullptr;

        for (const auto& track : project_->tracks)
            if (track.id == automationTrackId_)
                automated = &track;

        automationEdit_.clear();

        if (automated != nullptr)
            for (const auto& candidate : automated->automation)
                if (candidate.parameter == automationParameter_)
                    automationEdit_ = candidate.points;

        const double quarter = std::max (0.0, snapped (quarterAt (event.x)));
        const double value = decibelsAt (event.y, lane);
        int nearest = -1;
        float best = 8.0f;

        for (int index = 0; index < static_cast<int> (automationEdit_.size()); ++index)
        {
            const float dx = xForQuarter (automationEdit_[static_cast<std::size_t> (index)].quarterNotes) - static_cast<float> (event.x);
            const float dy = static_cast<float> (yForDecibels (automationEdit_[static_cast<std::size_t> (index)].value, lane) - event.y);
            const float distance = std::sqrt (dx * dx + dy * dy);

            if (distance < best)
            {
                best = distance;
                nearest = index;
            }
        }

        if (event.mods.isRightButtonDown() && nearest >= 0)
            automationEdit_.erase (automationEdit_.begin() + nearest);
        else if (nearest >= 0)
            dragPoint_ = nearest;
        else
        {
            automationEdit_.push_back ({ quarter, value });
            dragPoint_ = static_cast<int> (automationEdit_.size()) - 1;
        }

        draggingPoint_ = true;
        repaint();
        return;
    }

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

        if ((event.mods.isCommandDown() || event.mods.isAltDown()) && std::holds_alternative<waveform::AudioClip> (clip) && onMarkers)
        {
            const auto& audio = std::get<waveform::AudioClip> (clip);
            auto warp = audio.warp;
            auto comp = audio.comp;
            const double local = std::max (0.0, snapped (quarterAt (event.x)) - audio.startQuarterNotes);

            if (event.mods.isAltDown())
                comp.push_back (waveform::CompSegment { local, local + 1.0, static_cast<int> (comp.size() % 3) });
            else
                warp.push_back (waveform::WarpMarker { local * 0.5, local });

            onMarkers (track.id, audio.id, std::move (warp), std::move (comp));
            return;
        }

        dragTrackId_ = track.id;
        dragClipId_ = waveform::clipId (clip);
        dragClipStart_ = std::holds_alternative<waveform::AudioClip> (clip)
                             ? std::get<waveform::AudioClip> (clip).startQuarterNotes
                             : std::get<waveform::MidiClip> (clip).startQuarterNotes;
        dragOriginQuarter_ = quarterAt (event.x);
        previewStart_ = dragClipStart_;
        originalLength_ = clipLengthQuarters (clip);
        previewLength_ = originalLength_;
        previewFadeIn_ = 0.0;
        previewFadeOut_ = 0.0;

        if (const auto* audioClip = std::get_if<waveform::AudioClip> (&clip))
        {
            previewFadeIn_ = audioClip->fadeInSeconds;
            previewFadeOut_ = audioClip->fadeOutSeconds;
        }

        if (hit.fadeIn)
            dragKind_ = DragKind::FadeIn;
        else if (hit.fadeOut)
            dragKind_ = DragKind::FadeOut;
        else if (hit.trimStart)
            dragKind_ = DragKind::TrimStart;
        else if (hit.trimEnd)
            dragKind_ = DragKind::TrimEnd;
        else
            dragKind_ = DragKind::Move;

        draggingClip_ = true;

        if (onSelectClip)
            onSelectClip (dragTrackId_, dragClipId_);
    }
}

void ArrangementView::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingPoint_ && dragPoint_ >= 0 && dragPoint_ < static_cast<int> (automationEdit_.size()))
    {
        const auto lane = automationLane (*this, true);

        if (editingEnvelope())
        {
            const auto envelope = selectedEnvelope();
            automationEdit_[static_cast<std::size_t> (dragPoint_)].quarterNotes = localFromQuarter (envelope, std::max (0.0, snapped (quarterAt (event.x))));
            automationEdit_[static_cast<std::size_t> (dragPoint_)].value = lane.getHeight() > 0
                                                                              ? 1.0 - std::clamp ((static_cast<double> (event.y) - lane.getY()) / lane.getHeight(), 0.0, 1.0)
                                                                              : 0.0;
        }
        else
        {
            automationEdit_[static_cast<std::size_t> (dragPoint_)].quarterNotes = std::max (0.0, snapped (quarterAt (event.x)));
            automationEdit_[static_cast<std::size_t> (dragPoint_)].value = decibelsAt (event.y, lane);
        }
        repaint();
        return;
    }

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

    const double quarter = snapped (quarterAt (event.x));
    const double tempo = project_ != nullptr && project_->tempoBpm > 0.0 ? project_->tempoBpm : 120.0;
    const double originalEnd = dragClipStart_ + originalLength_;

    if (dragKind_ == DragKind::TrimEnd)
    {
        previewStart_ = dragClipStart_;
        previewLength_ = std::max (0.25, quarter - dragClipStart_);
    }
    else if (dragKind_ == DragKind::TrimStart)
    {
        previewStart_ = std::clamp (quarter, 0.0, originalEnd - 0.25);
        previewLength_ = originalEnd - previewStart_;
    }
    else if (dragKind_ == DragKind::FadeIn || dragKind_ == DragKind::FadeOut)
    {
        const double clipSeconds = std::max (0.05, previewLength_ * 60.0 / tempo);
        const double localSeconds = std::abs (quarter - dragClipStart_) * 60.0 / tempo;
        const double fade = std::clamp (localSeconds, 0.0, clipSeconds * 0.5);

        if (dragKind_ == DragKind::FadeIn)
            previewFadeIn_ = fade;
        else
            previewFadeOut_ = fade;
    }
    else
    {
        previewStart_ = snapped (dragClipStart_ + quarterAt (event.x) - dragOriginQuarter_);
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (draggingPoint_)
    {
        draggingPoint_ = false;
        dragPoint_ = -1;

        if (editingEnvelope())
        {
            const auto envelope = selectedEnvelope();

            if (envelope.found && onClipEnvelope)
                onClipEnvelope (envelope.trackId, selectedClipId_, automationEdit_);
        }
        else if (onAutomation)
            onAutomation (automationTrackId_, automationEdit_);

        return;
    }

    if (draggingClip_ && dragKind_ == DragKind::Move && std::abs (previewStart_ - dragClipStart_) > 0.001 && onMoveClip)
        onMoveClip (dragTrackId_, dragClipId_, previewStart_);
    else if (draggingClip_ && (dragKind_ == DragKind::TrimStart || dragKind_ == DragKind::TrimEnd) && onResizeClip)
        onResizeClip (dragTrackId_, dragClipId_, previewStart_, previewLength_);
    else if (draggingClip_ && (dragKind_ == DragKind::FadeIn || dragKind_ == DragKind::FadeOut) && onFadeClip)
        onFadeClip (dragTrackId_, dragClipId_, previewFadeIn_, previewFadeOut_);

    if (draggingLoop_ && onLoopEdit)
        onLoopEdit (true, previewLoopStart_, previewLoopEnd_);

    draggingClip_ = false;
    dragKind_ = DragKind::None;
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
