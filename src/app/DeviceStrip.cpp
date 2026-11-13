#include "DeviceStrip.hpp"

#include "StudioLookAndFeel.hpp"
#include "waveform/effects.hpp"

namespace
{

struct Knob
{
    const char* name;
    double* value;
    double minimum;
    double maximum;
};

void knobsFor (waveform::EffectSlot& slot, Knob* knobs, int& count)
{
    count = 3;

    switch (slot.kind)
    {
        case waveform::EffectKind::Eq:
            knobs[0] = { "Low", &slot.p0, -18, 18 };
            knobs[1] = { "Mid", &slot.p1, -18, 18 };
            knobs[2] = { "High", &slot.p3, -18, 18 };
            break;
        case waveform::EffectKind::Compressor:
            knobs[0] = { "Thresh", &slot.p0, -60, 0 };
            knobs[1] = { "Ratio", &slot.p1, 1, 20 };
            knobs[2] = { "Gain", &slot.p4, 0, 24 };
            break;
        case waveform::EffectKind::Delay:
            knobs[0] = { "Time", &slot.p0, 0.01, 4 };
            knobs[1] = { "Feedback", &slot.p1, 0, 0.9 };
            knobs[2] = { "Mix", &slot.p2, 0, 1 };
            break;
        case waveform::EffectKind::Reverb:
            knobs[0] = { "Size", &slot.p0, 0.05, 1 };
            knobs[1] = { "Damp", &slot.p1, 0, 0.95 };
            knobs[2] = { "Mix", &slot.p2, 0, 1 };
            break;
        case waveform::EffectKind::Limiter:
            knobs[0] = { "Ceiling", &slot.p0, -12, 0 };
            knobs[1] = { "Release", &slot.p1, 0.01, 0.5 };
            count = 2;
            break;
        case waveform::EffectKind::Utility:
            knobs[0] = { "Gain", &slot.p0, -48, 24 };
            knobs[1] = { "Width", &slot.p1, 0, 2 };
            count = 2;
            break;
        case waveform::EffectKind::Chorus:
            knobs[0] = { "Rate", &slot.p0, 0.05, 5 };
            knobs[1] = { "Depth", &slot.p1, 0, 1 };
            knobs[2] = { "Mix", &slot.p2, 0, 1 };
            break;
        case waveform::EffectKind::Saturator:
            knobs[0] = { "Drive", &slot.p0, 0, 1 };
            knobs[1] = { "Mix", &slot.p1, 0, 1 };
            count = 2;
            break;
    }
}

} // namespace

struct DeviceStrip::Card : public juce::Component
{
    Card (waveform::EffectSlot slot, int index, DeviceStrip& owner)
        : slot_ (slot), index_ (index), owner_ (owner)
    {
        title_.setText (waveform::effectName (slot.kind), juce::dontSendNotification);
        addAndMakeVisible (title_);
        up_.setButtonText ("Up");
        down_.setButtonText ("Down");
        remove_.setButtonText ("X");
        addAndMakeVisible (up_);
        addAndMakeVisible (down_);
        addAndMakeVisible (remove_);
        up_.onClick = [this] { if (owner_.onMove) owner_.onMove (index_, -1); };
        down_.onClick = [this] { if (owner_.onMove) owner_.onMove (index_, 1); };
        remove_.onClick = [this] { if (owner_.onRemove) owner_.onRemove (index_); };

        Knob knobs[3];
        int count = 0;
        knobsFor (slot_, knobs, count);

        for (int knob = 0; knob < count; ++knob)
        {
            sliders_[knob].setRange (knobs[knob].minimum, knobs[knob].maximum, 0.01);
            sliders_[knob].setValue (*knobs[knob].value, juce::dontSendNotification);
            sliders_[knob].setSliderStyle (juce::Slider::LinearHorizontal);
            sliders_[knob].setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
            sliders_[knob].setTooltip (knobs[knob].name);
            addAndMakeVisible (sliders_[knob]);
            sliders_[knob].onDragEnd = [this]
            {
                Knob current[3];
                int currentCount = 0;
                knobsFor (slot_, current, currentCount);

                for (int knobIndex = 0; knobIndex < currentCount; ++knobIndex)
                    *current[knobIndex].value = sliders_[knobIndex].getValue();

                if (owner_.onChange)
                    owner_.onChange (index_, slot_);
            };
        }

        sliderCount_ = count;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);
        auto header = area.removeFromTop (22);
        remove_.setBounds (header.removeFromRight (28));
        down_.setBounds (header.removeFromRight (48));
        up_.setBounds (header.removeFromRight (40));
        title_.setBounds (header);

        for (int index = 0; index < sliderCount_; ++index)
        {
            area.removeFromTop (2);
            sliders_[index].setBounds (area.removeFromTop (22));
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 6);
    }

    waveform::EffectSlot slot_;
    int index_ = 0;
    int sliderCount_ = 0;
    DeviceStrip& owner_;
    juce::Label title_;
    juce::TextButton up_, down_, remove_;
    juce::Slider sliders_[3];
};

DeviceStrip::DeviceStrip() = default;
DeviceStrip::~DeviceStrip() = default;

void DeviceStrip::setTrack (const waveform::Track* track)
{
    track_ = track;
    cards_.clear();

    if (track_ != nullptr)
    {
        for (int index = 0; index < static_cast<int> (track_->effects.size()); ++index)
            cards_.add (new Card (track_->effects[static_cast<std::size_t> (index)], index, *this));
    }

    for (auto* card : cards_)
        addAndMakeVisible (card);

    resized();
    repaint();
}

void DeviceStrip::paint (juce::Graphics& graphics)
{
    graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kBackground));
    graphics.fillAll();

    if (cards_.isEmpty())
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("No inserts. Use Add effect.", getLocalBounds(), juce::Justification::centred);
    }
}

void DeviceStrip::resized()
{
    auto area = getLocalBounds();
    const int width = 210;

    for (auto* card : cards_)
        card->setBounds (area.removeFromLeft (width).reduced (2));
}
