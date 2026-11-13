#include "PianoRoll.hpp"

#include "StudioLookAndFeel.hpp"
#include "waveform/later.hpp"
#include "waveform/midi.hpp"

#include <algorithm>
#include <cmath>

namespace
{

constexpr int kHeader = 28;
constexpr int kKeyboard = 48;
constexpr int kVelocity = 36;
constexpr int kKeyHeight = 12;
constexpr float kPixelsPerQuarter = 64.0f;
constexpr int kLowPitch = 36;
constexpr int kHighPitch = 96;

} // namespace

PianoRoll::PianoRoll()
{
    for (auto* box : { &snapBox_, &keyBox_, &scaleBox_ })
        addAndMakeVisible (box);

    for (auto* button : { &quantizeButton_, &constrainButton_, &humanizeButton_, &legatoButton_, &reverseButton_ })
        addAndMakeVisible (button);

    addAndMakeVisible (slideLabel_);
    addAndMakeVisible (slide_);
    slide_.setRange (-12.0, 12.0, 0.1);
    slide_.setSliderStyle (juce::Slider::LinearHorizontal);
    slide_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    slide_.onDragEnd = [this]
    {
        if (! hasClip_ || selected_ < 0 || onCommit == nullptr)
            return;

        auto notes = clip_.notes;

        if (selected_ >= static_cast<int> (notes.size()))
            return;

        notes[static_cast<std::size_t> (selected_)].slideSemitones = static_cast<float> (slide_.getValue());
        onCommit (std::move (notes));
    };

    const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    for (int note = 0; note < 12; ++note)
        keyBox_.addItem (names[note], note + 1);

    keyBox_.setSelectedId (1, juce::dontSendNotification);
    scaleBox_.addItem ("Major", 1);
    scaleBox_.addItem ("Minor", 2);
    scaleBox_.setSelectedId (1, juce::dontSendNotification);
    keyBox_.onChange = [this] { keyRoot_ = std::max (0, keyBox_.getSelectedId() - 1); repaint(); };
    scaleBox_.onChange = [this] { minorKey_ = scaleBox_.getSelectedId() == 2; repaint(); };
    constrainButton_.onClick = [this]
    {
        auto notes = clip_.notes;
        const waveform::MusicalKey key { keyRoot_, minorKey_ };

        for (auto& note : notes)
            note.note = waveform::constrainToKey (note.note, key);

        transform (std::move (notes));
    };
    humanizeButton_.onClick = [this] { transform (waveform::humanizeNotes (clip_.notes, 0.03, 8, 1u)); };
    legatoButton_.onClick = [this] { transform (waveform::makeLegato (clip_.notes)); };
    reverseButton_.onClick = [this] { transform (waveform::reverseNotes (clip_.notes, clip_.lengthQuarterNotes)); };
    snapBox_.addItem ("1/4", 4);
    snapBox_.addItem ("1/8", 8);
    snapBox_.addItem ("1/16", 16);
    snapBox_.addItem ("1/32", 32);
    snapBox_.setSelectedId (division_, juce::dontSendNotification);
    snapBox_.onChange = [this] { division_ = std::max (4, snapBox_.getSelectedId()); };
    quantizeButton_.onClick = [this] { quantize(); };
}

void PianoRoll::setClip (const waveform::MidiClip* clip)
{
    hasClip_ = clip != nullptr;

    if (clip != nullptr)
        clip_ = *clip;
    else
        clip_ = {};

    if (drag_ == Drag::None)
        edit_.clear();

    repaint();
}

int PianoRoll::pitchAt (int y) const
{
    const int grid = y - kHeader;

    if (grid < 0)
        return scrollPitch_;

    return std::clamp (scrollPitch_ - grid / kKeyHeight, kLowPitch, kHighPitch);
}

double PianoRoll::quarterAt (int x) const
{
    return scrollQuarter_ + static_cast<double> (x - kKeyboard) / kPixelsPerQuarter;
}

float PianoRoll::yForPitch (int pitch) const
{
    return static_cast<float> (kHeader + (scrollPitch_ - pitch) * kKeyHeight);
}

float PianoRoll::xForQuarter (double quarterNotes) const
{
    return static_cast<float> (kKeyboard) + static_cast<float> ((quarterNotes - scrollQuarter_) * kPixelsPerQuarter);
}

double PianoRoll::snap (double quarterNotes) const
{
    const double grid = waveform::gridQuarterNotes (division_);
    return std::max (0.0, waveform::snapToGrid (quarterNotes, grid, 1.0));
}

int PianoRoll::noteAt (juce::Point<int> position, bool& edge) const
{
    edge = false;
    const auto& notes = edit_.empty() ? clip_.notes : edit_;

    for (int index = static_cast<int> (notes.size()) - 1; index >= 0; --index)
    {
        const auto& note = notes[static_cast<std::size_t> (index)];
        const float x = xForQuarter (note.startQuarterNotes);
        const float width = std::max (6.0f, static_cast<float> (note.lengthQuarterNotes * kPixelsPerQuarter));
        const float y = yForPitch (note.note);
        const auto bounds = juce::Rectangle<float> (x, y + 1.0f, width, static_cast<float> (kKeyHeight - 2));

        if (! bounds.contains (position.toFloat()))
            continue;

        edge = position.x > bounds.getRight() - 8.0f;
        return index;
    }

    return -1;
}

void PianoRoll::resized()
{
    snapBox_.setBounds (8, 2, 72, 24);
    quantizeButton_.setBounds (84, 2, 80, 24);
    keyBox_.setBounds (170, 2, 56, 24);
    scaleBox_.setBounds (230, 2, 78, 24);
    constrainButton_.setBounds (314, 2, 64, 24);
    humanizeButton_.setBounds (382, 2, 84, 24);
    legatoButton_.setBounds (470, 2, 68, 24);
    reverseButton_.setBounds (542, 2, 72, 24);
    slideLabel_.setBounds (624, 2, 40, 24);
    slide_.setBounds (664, 2, 160, 24);
}

void PianoRoll::transform (std::vector<waveform::MidiNote> notes)
{
    if (! hasClip_ || onCommit == nullptr)
        return;

    onCommit (std::move (notes));
}

void PianoRoll::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (waveform::StudioLookAndFeel::kPanel));

    if (! hasClip_)
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("Select a MIDI clip on the timeline. Double-click a MIDI lane to create one.",
                           getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int gridBottom = getHeight() - kVelocity;
    const auto& notes = edit_.empty() ? clip_.notes : edit_;

    for (int pitch = kHighPitch; pitch >= kLowPitch; --pitch)
    {
        const float y = yForPitch (pitch);

        if (y > gridBottom || y + kKeyHeight < kHeader)
            continue;

        const bool black = pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 || pitch % 12 == 8 || pitch % 12 == 10;
        const bool inKey = waveform::pitchInKey (pitch, waveform::MusicalKey { keyRoot_, minorKey_ });
        graphics.setColour (juce::Colour (inKey ? (black ? 0xff1c2a24 : 0xff31483c) : (black ? 0xff101614 : 0xff24302a)));
        graphics.fillRect (static_cast<float> (kKeyboard), y, static_cast<float> (getWidth() - kKeyboard), static_cast<float> (kKeyHeight));
        graphics.setColour (juce::Colour (black ? 0xff1a1a1a : 0xffd7e2da));
        graphics.fillRect (0.0f, y, static_cast<float> (kKeyboard - 1), static_cast<float> (kKeyHeight - 1));

        if (pitch % 12 == 0)
        {
            graphics.setColour (juce::Colour (0xff1a1a1a));
            graphics.drawText ("C" + juce::String (pitch / 12 - 1), 2, static_cast<int> (y), kKeyboard - 6, kKeyHeight, juce::Justification::centredLeft);
        }
    }

    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kLine).withAlpha (0.7f));
    const double grid = waveform::gridQuarterNotes (division_);
    const double last = std::max (16.0, clip_.lengthQuarterNotes + 4.0);

    for (double quarter = 0.0; quarter < last; quarter += (grid > 0.0 ? grid : 1.0))
    {
        const float x = xForQuarter (quarter);

        if (x < kKeyboard || x > getWidth())
            continue;

        graphics.drawVerticalLine (static_cast<int> (x), static_cast<float> (kHeader), static_cast<float> (gridBottom));
    }

    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
    {
        const auto& note = notes[static_cast<std::size_t> (index)];
        const float x = xForQuarter (note.startQuarterNotes);
        const float width = std::max (6.0f, static_cast<float> (note.lengthQuarterNotes * kPixelsPerQuarter));
        const float y = yForPitch (note.note);
        graphics.setColour (juce::Colour (index == selected_ ? waveform::StudioLookAndFeel::kCopper : waveform::StudioLookAndFeel::kMint));
        graphics.fillRoundedRectangle (x, y + 1.0f, width, static_cast<float> (kKeyHeight - 2), 2.0f);
        const float velocityWidth = static_cast<float> (getWidth() - kKeyboard) * static_cast<float> (note.velocity) / 127.0f;
        graphics.fillRect (static_cast<float> (kKeyboard), static_cast<float> (gridBottom + 8 + index % 2), std::max (2.0f, velocityWidth), 6.0f);
    }

    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
    graphics.drawText ("Velocity follows the selected note. Drag a note to move it, the right edge to resize, empty space to draw.",
                       200, 4, getWidth() - 210, 20, juce::Justification::centredLeft);
}

void PianoRoll::mouseDown (const juce::MouseEvent& event)
{
    if (! hasClip_)
        return;

    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    edit_ = clip_.notes;
    before_ = edit_;
    bool edge = false;
    selected_ = noteAt (event.getPosition(), edge);

    if (selected_ >= 0 && selected_ < static_cast<int> (edit_.size()))
        slide_.setValue (edit_[static_cast<std::size_t> (selected_)].slideSemitones, juce::dontSendNotification);

    if (event.y > getHeight() - kVelocity && selected_ >= 0)
    {
        drag_ = Drag::Velocity;
        return;
    }

    if (selected_ >= 0)
    {
        drag_ = edge ? Drag::Resize : Drag::Move;
        grabQuarter_ = quarterAt (event.x) - edit_[static_cast<std::size_t> (selected_)].startQuarterNotes;
        grabPitch_ = pitchAt (event.y) - edit_[static_cast<std::size_t> (selected_)].note;
        return;
    }

    if (event.y < kHeader || event.x < kKeyboard)
        return;

    waveform::MidiNote note;
    note.note = pitchAt (event.y);
    note.startQuarterNotes = snap (quarterAt (event.x));
    note.lengthQuarterNotes = std::max (0.25, waveform::gridQuarterNotes (division_));
    note.velocity = 100;
    note.channel = 1;
    edit_.push_back (note);
    selected_ = static_cast<int> (edit_.size()) - 1;
    drag_ = Drag::Draw;
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& event)
{
    if (drag_ == Drag::None || selected_ < 0 || selected_ >= static_cast<int> (edit_.size()))
        return;

    auto& note = edit_[static_cast<std::size_t> (selected_)];

    if (drag_ == Drag::Move)
    {
        note.startQuarterNotes = snap (quarterAt (event.x) - grabQuarter_);
        note.note = std::clamp (pitchAt (event.y) - grabPitch_, 0, 127);
    }
    else if (drag_ == Drag::Draw || drag_ == Drag::Resize)
    {
        const double end = std::max (note.startQuarterNotes + 0.05, snap (quarterAt (event.x)));
        note.lengthQuarterNotes = end - note.startQuarterNotes;
    }
    else if (drag_ == Drag::Velocity)
    {
        const float amount = std::clamp ((event.x - kKeyboard) / static_cast<float> (std::max (1, getWidth() - kKeyboard)), 0.0f, 1.0f);
        note.velocity = std::clamp (static_cast<int> (std::round (amount * 127.0f)), 1, 127);
    }

    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    const bool changed = edit_.size() != before_.size()
                      || ! std::equal (edit_.begin(), edit_.end(), before_.begin(),
                                       [] (const waveform::MidiNote& left, const waveform::MidiNote& right)
                                       {
                                           return left.note == right.note
                                               && left.velocity == right.velocity
                                               && left.channel == right.channel
                                               && std::abs (left.startQuarterNotes - right.startQuarterNotes) < 1.0e-9
                                               && std::abs (left.lengthQuarterNotes - right.lengthQuarterNotes) < 1.0e-9;
                                       });

    if (drag_ != Drag::None && changed && onCommit)
        onCommit (edit_);

    drag_ = Drag::None;
    edit_.clear();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaX) > 0.0f || juce::ModifierKeys::currentModifiers.isShiftDown())
        scrollQuarter_ = std::max (0.0, scrollQuarter_ - static_cast<double> (wheel.deltaX + wheel.deltaY) * 2.0);
    else
        scrollPitch_ = std::clamp (scrollPitch_ + static_cast<int> (std::round (wheel.deltaY * 8.0f)), kLowPitch + 8, kHighPitch);

    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (! hasClip_ || selected_ < 0)
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        auto notes = clip_.notes;

        if (selected_ >= 0 && selected_ < static_cast<int> (notes.size()))
        {
            notes.erase (notes.begin() + selected_);
            selected_ = -1;

            if (onCommit)
                onCommit (notes);
        }

        return true;
    }

    return false;
}

void PianoRoll::quantize()
{
    if (! hasClip_ || onCommit == nullptr)
        return;

    auto notes = clip_.notes;
    waveform::quantizeNotes (notes, waveform::gridQuarterNotes (division_), 1.0, false);
    onCommit (notes);
}
