#include "MixerView.hpp"

#include "StudioLookAndFeel.hpp"

struct MixerView::Strip : public juce::Component
{
    Strip (const waveform::Track& track, MixerView& owner)
        : id_ (track.id), returns_ (track.returns), owner_ (owner)
    {
        name_.setText (track.name, juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (name_);
        fader_.setSliderStyle (juce::Slider::LinearVertical);
        fader_.setRange (-48.0, 6.0, 0.1);
        fader_.setSkewFactorFromMidPoint (0.0);
        fader_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
        fader_.setValue (track.mixer.gainDb, juce::dontSendNotification);
        fader_.onDragEnd = [this] { publish(); };
        addAndMakeVisible (fader_);
        mute_.setButtonText ("M");
        solo_.setButtonText ("S");
        mute_.setClickingTogglesState (true);
        solo_.setClickingTogglesState (true);
        mute_.setToggleState (track.mixer.mute, juce::dontSendNotification);
        solo_.setToggleState (track.mixer.solo, juce::dontSendNotification);
        addAndMakeVisible (mute_);
        addAndMakeVisible (solo_);
        mute_.onClick = [this] { publish(); };
        solo_.onClick = [this] { publish(); };

        if (! returns_)
        {
            send_.setSliderStyle (juce::Slider::LinearHorizontal);
            send_.setRange (-60.0, 6.0, 0.1);
            send_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            send_.setValue (track.sends.empty() ? -60.0 : track.sends.front().levelDb, juce::dontSendNotification);
            send_.onDragEnd = [this] { publishSend(); };
            pre_.setButtonText ("Pre");
            pre_.setClickingTogglesState (true);
            pre_.setToggleState (! track.sends.empty() && track.sends.front().preFader, juce::dontSendNotification);
            pre_.onClick = [this] { publishSend(); };
            addAndMakeVisible (send_);
            addAndMakeVisible (pre_);
            targetId_ = track.sends.empty() ? std::string ("none") : track.sends.front().targetId;
        }
    }

    float peak = 0.0f;

    void paint (juce::Graphics& graphics) override
    {
        graphics.setColour (juce::Colour (returns_ ? 0xff24352e : waveform::StudioLookAndFeel::kPanel));
        graphics.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3), 6);
        auto meter = juce::Rectangle<float> (10, 28, static_cast<float> (getWidth() - 20), 8);
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kLine));
        graphics.fillRoundedRectangle (meter, 2);
        meter.setWidth (meter.getWidth() * std::clamp (peak, 0.0f, 1.0f));
        graphics.setColour (juce::Colour (peak > 0.9f ? waveform::StudioLookAndFeel::kRose : waveform::StudioLookAndFeel::kMint));
        graphics.fillRoundedRectangle (meter, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 6);
        name_.setBounds (area.removeFromTop (18));
        area.removeFromTop (12);
        auto buttons = area.removeFromBottom (24);
        solo_.setBounds (buttons.removeFromRight (getWidth() / 2).reduced (1));
        mute_.setBounds (buttons.reduced (1));

        if (! returns_)
        {
            pre_.setBounds (area.removeFromBottom (22).reduced (1));
            send_.setBounds (area.removeFromBottom (22));
        }

        fader_.setBounds (area.reduced (8, 0));
    }

private:
    void publish()
    {
        if (owner_.onMixer == nullptr)
            return;

        waveform::MixerState state;
        state.gainDb = fader_.getValue();
        state.pan = 0.0;
        state.mute = mute_.getToggleState();
        state.solo = solo_.getToggleState();
        owner_.onMixer (id_, state);
    }

    void publishSend()
    {
        if (owner_.onSend == nullptr)
            return;

        waveform::Send send;
        send.levelDb = send_.getValue();
        send.preFader = pre_.getToggleState();
        send.targetId = targetId_.empty() ? "none" : targetId_;
        owner_.onSend (id_, send);
    }

    std::string id_;
    std::string targetId_;
    bool returns_ = false;
    MixerView& owner_;
    juce::Label name_;
    juce::Slider fader_;
    juce::Slider send_;
    juce::TextButton mute_, solo_, pre_;
};

MixerView::MixerView() = default;
MixerView::~MixerView() = default;

void MixerView::setProject (const waveform::Project& project)
{
    project_ = project;
    strips_.clear();

    for (const auto& track : project_.tracks)
    {
        auto* strip = new Strip (track, *this);
        addAndMakeVisible (strip);
        strips_.add (strip);
    }

    resized();
}

void MixerView::setPeaks (const std::vector<float>& peaks)
{
    peaks_ = peaks;

    for (int index = 0; index < strips_.size(); ++index)
        strips_[index]->peak = index < static_cast<int> (peaks_.size()) ? peaks_[static_cast<std::size_t> (index)] : 0.0f;

    repaint();
}

void MixerView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (waveform::StudioLookAndFeel::kBackground));

    if (strips_.isEmpty())
    {
        graphics.setColour (juce::Colour (waveform::StudioLookAndFeel::kDim));
        graphics.drawText ("Add a track to see the mixer.", getLocalBounds(), juce::Justification::centred);
    }
}

void MixerView::resized()
{
    auto area = getLocalBounds();

    for (auto* strip : strips_)
        strip->setBounds (area.removeFromLeft (96).reduced (2));
}
