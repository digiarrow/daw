#include "SessionView.hpp"

#include "StudioLookAndFeel.hpp"

namespace
{

juce::String slotLabel (const waveform::Clip& clip)
{
    if (const auto* audio = std::get_if<waveform::AudioClip> (&clip))
        return juce::String (audio->name);

    if (const auto* midi = std::get_if<waveform::MidiClip> (&clip))
        return juce::String (midi->name) + "  " + juce::String (static_cast<int> (midi->notes.size()));

    return {};
}

const waveform::SessionSlot* slotFor (const waveform::Track& track, int scene)
{
    for (const auto& slot : track.session)
        if (slot.scene == scene)
            return &slot;

    return nullptr;
}

} // namespace

void SessionView::setState (const waveform::Project* project,
                            const std::array<int, 64>& active,
                            const std::array<int, 64>& pending,
                            const std::string& selectedClipId)
{
    project_ = project;
    active_ = active;
    pending_ = pending;
    selectedClipId_ = selectedClipId;
    repaint();
}

void SessionView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (waveform::StudioLookAndFeel::kBackground));

    if (project_ == nullptr || project_->tracks.empty())
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("Add a track, then double-click a slot to put a clip in the session.",
                           getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int tracks = std::min (static_cast<int> (project_->tracks.size()), 64);
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
    graphics.drawText ("Scene", 8, 4, 64, 22, juce::Justification::centredLeft);

    for (int track = 0; track < tracks; ++track)
    {
        auto header = cellBounds (track, -1);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (header.toFloat(), 4.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
        graphics.drawText (project_->tracks[static_cast<std::size_t> (track)].name, header.reduced (4, 0), juce::Justification::centred, true);

        auto arrange = cellBounds (track, waveform::kSessionScenes);
        const bool following = track < 64 && active_[static_cast<std::size_t> (track)] < 0;
        graphics.setColour (juce::Colour (following ? 0xff24352e : waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (arrange.toFloat(), 4.0f);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("Arrangement", arrange, juce::Justification::centred, true);
    }

    for (int scene = 0; scene < waveform::kSessionScenes; ++scene)
    {
        auto label = cellBounds (-1, scene);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kCopper));
        graphics.drawText (juce::String (scene + 1), label, juce::Justification::centred);

        for (int track = 0; track < tracks; ++track)
        {
            auto bounds = cellBounds (track, scene);
            const auto* slot = slotFor (project_->tracks[static_cast<std::size_t> (track)], scene);
            const bool playing = active_[static_cast<std::size_t> (track)] == scene;
            const bool waiting = pending_[static_cast<std::size_t> (track)] == scene;
            const bool selected = slot != nullptr && waveform::clipId (slot->clip) == selectedClipId_;

            graphics.setColour (juce::Colour (playing ? 0xff3d5a45 : slot != nullptr ? 0xff2a3138 : 0xff171c20));
            graphics.fillRoundedRectangle (bounds.toFloat().reduced (2.0f), 5.0f);
            graphics.setColour (juce::Colour (selected || waiting ? waveform::StudioLookAndFeel::kCopper : waveform::StudioLookAndFeel::kLine));
            graphics.drawRoundedRectangle (bounds.toFloat().reduced (2.0f), 5.0f, waiting ? 2.0f : 1.0f);

            if (slot != nullptr)
            {
                graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kText));
                graphics.drawText (slotLabel (slot->clip), bounds.reduced (6, 2), juce::Justification::centred, true);
            }
        }
    }
}

SessionView::Cell SessionView::cellAt (juce::Point<int> position) const
{
    Cell cell;

    if (project_ == nullptr)
        return cell;

    const int tracks = std::min (static_cast<int> (project_->tracks.size()), 64);

    for (int track = 0; track < tracks; ++track)
    {
        if (cellBounds (track, waveform::kSessionScenes).contains (position))
        {
            cell.track = track;
            cell.arrange = true;
            return cell;
        }

        for (int scene = 0; scene < waveform::kSessionScenes; ++scene)
        {
            if (cellBounds (track, scene).contains (position))
            {
                cell.track = track;
                cell.scene = scene;
                return cell;
            }
        }
    }

    for (int scene = 0; scene < waveform::kSessionScenes; ++scene)
    {
        if (cellBounds (-1, scene).contains (position))
        {
            cell.scene = scene;
            cell.sceneButton = true;
            return cell;
        }
    }

    return cell;
}

juce::Rectangle<int> SessionView::cellBounds (int track, int scene) const
{
    const int label = 56;
    const int top = 28;
    const int gap = 4;
    const int tracks = project_ == nullptr ? 1 : std::max (1, std::min (static_cast<int> (project_->tracks.size()), 64));
    const int columns = std::max (1, tracks);
    const int width = std::max (72, (getWidth() - label - 8) / columns);
    const int height = std::max (22, (getHeight() - top - 8) / (waveform::kSessionScenes + 1));

    if (track < 0)
        return { 4, top + scene * height, label - 8, height - gap };

    return { label + track * width, scene < 0 ? 2 : top + scene * height, width - gap, (scene < 0 ? top : height) - gap };
}

void SessionView::mouseDown (const juce::MouseEvent& event)
{
    const auto cell = cellAt (event.getPosition());

    if (cell.sceneButton && onLaunchScene)
        onLaunchScene (cell.scene);
    else if (cell.arrange && onReturn)
        onReturn (cell.track);
    else if (cell.track >= 0 && cell.scene >= 0 && project_ != nullptr
             && cell.track < static_cast<int> (project_->tracks.size()))
    {
        if (event.mods.isRightButtonDown())
        {
            if (onClear)
                onClear (cell.track, cell.scene);

            return;
        }

        if (onSelect)
            onSelect (cell.track, cell.scene);

        if (slotFor (project_->tracks[static_cast<std::size_t> (cell.track)], cell.scene) != nullptr && onLaunch)
            onLaunch (cell.track, cell.scene);
    }
}

void SessionView::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto cell = cellAt (event.getPosition());

    if (cell.track < 0 || cell.scene < 0 || project_ == nullptr)
        return;

    if (cell.track >= static_cast<int> (project_->tracks.size()))
        return;

    const auto* slot = slotFor (project_->tracks[static_cast<std::size_t> (cell.track)], cell.scene);

    if (slot == nullptr && onCreate)
        onCreate (cell.track, cell.scene);
    else if (slot != nullptr && onLaunch)
        onLaunch (cell.track, cell.scene);
}
