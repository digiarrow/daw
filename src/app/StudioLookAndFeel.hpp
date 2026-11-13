#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace waveform
{

/// Dark ink and copper. Deliberately not the palette of any existing DAW.
class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr juce::uint32 kBackground = 0xff121816;
    static constexpr juce::uint32 kPanel = 0xff1c2622;
    static constexpr juce::uint32 kLine = 0xff31423b;
    static constexpr juce::uint32 kText = 0xffe7f2ea;
    static constexpr juce::uint32 kDim = 0xff8ea399;
    static constexpr juce::uint32 kMint = 0xff7dcea0;
    static constexpr juce::uint32 kCopper = 0xffd4894c;
    static constexpr juce::uint32 kRose = 0xffd4656a;

    StudioLookAndFeel()
    {
        const auto panel = juce::Colour (kPanel);
        const auto text = juce::Colour (kText);
        const auto line = juce::Colour (kLine);

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (kBackground));
        setColour (juce::DocumentWindow::backgroundColourId, juce::Colour (kBackground));
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonColourId, panel);
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6b56));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::arrowColourId, juce::Colour (kMint));
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2c4038));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::Slider::thumbColourId, juce::Colour (kCopper));
        setColour (juce::Slider::trackColourId, line);
        setColour (juce::Slider::backgroundColourId, panel);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, panel);
        setColour (juce::Slider::textBoxOutlineColourId, line);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3d6b56));
        setColour (juce::ScrollBar::backgroundColourId, juce::Colour (kBackground));
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, juce::Colour (kMint));
        setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (kDim));
        setColour (juce::TextEditor::backgroundColourId, panel);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff3d6b56));
        setColour (juce::CaretComponent::caretColourId, juce::Colour (kCopper));
        setColour (juce::ListBox::backgroundColourId, juce::Colour (kBackground));
    }
};

} // namespace waveform
