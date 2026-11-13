#include "MidiEffectBar.hpp"

#include <algorithm>
#include <cmath>

namespace
{

waveform::MidiEffect effectFromControls (int kindId, double amount)
{
    waveform::MidiEffect effect;
    effect.enabled = kindId > 1;

    switch (kindId)
    {
        case 2:
            effect.kind = waveform::MidiEffectKind::Arpeggiator;
            effect.division = amount < 0.4 ? 8 : 16;
            effect.octaves = 2;
            break;
        case 3:
            effect.kind = waveform::MidiEffectKind::Chord;
            effect.chord = amount < 0.34 ? waveform::ChordKind::Major
                                         : amount < 0.67 ? waveform::ChordKind::Minor
                                                         : waveform::ChordKind::Power;
            break;
        case 4:
            effect.kind = waveform::MidiEffectKind::Scale;
            effect.root = static_cast<int> (std::clamp (amount, 0.0, 11.0));
            effect.scale = waveform::ScaleKind::Major;
            break;
        case 5:
            effect.kind = waveform::MidiEffectKind::Pitch;
            effect.semitones = static_cast<int> (std::lround (amount));
            break;
        case 6:
            effect.kind = waveform::MidiEffectKind::Velocity;
            effect.velocityAdd = static_cast<int> (std::lround (amount));
            effect.velocityScale = 1.0;
            break;
        default:
            effect.enabled = false;
            break;
    }

    return effect;
}

int kindIdFor (const waveform::MidiEffect& effect)
{
    if (! effect.enabled)
        return 1;

    switch (effect.kind)
    {
        case waveform::MidiEffectKind::Arpeggiator: return 2;
        case waveform::MidiEffectKind::Chord: return 3;
        case waveform::MidiEffectKind::Scale: return 4;
        case waveform::MidiEffectKind::Pitch: return 5;
        case waveform::MidiEffectKind::Velocity: return 6;
    }

    return 1;
}

double amountFor (const waveform::MidiEffect& effect)
{
    switch (effect.kind)
    {
        case waveform::MidiEffectKind::Arpeggiator: return effect.division >= 16 ? 1.0 : 0.0;
        case waveform::MidiEffectKind::Chord: return effect.chord == waveform::ChordKind::Minor ? 0.5 : effect.chord == waveform::ChordKind::Power ? 1.0 : 0.0;
        case waveform::MidiEffectKind::Scale: return static_cast<double> (effect.root);
        case waveform::MidiEffectKind::Pitch: return static_cast<double> (effect.semitones);
        case waveform::MidiEffectKind::Velocity: return static_cast<double> (effect.velocityAdd);
    }

    return 0.0;
}

} // namespace

MidiEffectBar::MidiEffectBar()
{
    for (int index = 0; index < waveform::kMidiEffectSlots; ++index)
    {
        kinds_[static_cast<std::size_t> (index)].addItem ("Off", 1);
        kinds_[static_cast<std::size_t> (index)].addItem ("Arp", 2);
        kinds_[static_cast<std::size_t> (index)].addItem ("Chord", 3);
        kinds_[static_cast<std::size_t> (index)].addItem ("Scale", 4);
        kinds_[static_cast<std::size_t> (index)].addItem ("Pitch", 5);
        kinds_[static_cast<std::size_t> (index)].addItem ("Velocity", 6);
        kinds_[static_cast<std::size_t> (index)].setSelectedId (1, juce::dontSendNotification);
        kinds_[static_cast<std::size_t> (index)].onChange = [this] { publish(); };
        amounts_[static_cast<std::size_t> (index)].setRange (-24.0, 24.0, 1.0);
        amounts_[static_cast<std::size_t> (index)].setValue (0.0, juce::dontSendNotification);
        amounts_[static_cast<std::size_t> (index)].setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 18);
        amounts_[static_cast<std::size_t> (index)].onDragEnd = [this] { publish(); };
        amounts_[static_cast<std::size_t> (index)].onValueChange = [this, index]
        {
            if (! amounts_[static_cast<std::size_t> (index)].isMouseButtonDown())
                publish();
        };
        addAndMakeVisible (kinds_[static_cast<std::size_t> (index)]);
        addAndMakeVisible (amounts_[static_cast<std::size_t> (index)]);
    }

    addAndMakeVisible (importButton_);
    addAndMakeVisible (exportButton_);
}

void MidiEffectBar::setEffects (const std::vector<waveform::MidiEffect>& effects)
{
    updating_ = true;

    for (int index = 0; index < waveform::kMidiEffectSlots; ++index)
    {
        const bool present = index < static_cast<int> (effects.size());
        const auto effect = present ? effects[static_cast<std::size_t> (index)] : waveform::MidiEffect {};
        kinds_[static_cast<std::size_t> (index)].setSelectedId (present ? kindIdFor (effect) : 1, juce::dontSendNotification);
        amounts_[static_cast<std::size_t> (index)].setValue (present ? amountFor (effect) : 0.0, juce::dontSendNotification);
    }

    updating_ = false;
}

void MidiEffectBar::publish()
{
    if (updating_ || ! onChange)
        return;

    std::vector<waveform::MidiEffect> effects;

    for (int index = 0; index < waveform::kMidiEffectSlots; ++index)
    {
        const int kind = kinds_[static_cast<std::size_t> (index)].getSelectedId();

        if (kind <= 1)
            continue;

        effects.push_back (effectFromControls (kind, amounts_[static_cast<std::size_t> (index)].getValue()));
    }

    onChange (std::move (effects));
}

void MidiEffectBar::resized()
{
    auto area = getLocalBounds();
    exportButton_.setBounds (area.removeFromRight (96).reduced (2, 2));
    importButton_.setBounds (area.removeFromRight (96).reduced (2, 2));

    for (int index = 0; index < waveform::kMidiEffectSlots; ++index)
    {
        auto slot = area.removeFromLeft (std::max (40, area.getWidth() / (waveform::kMidiEffectSlots - index)));
        kinds_[static_cast<std::size_t> (index)].setBounds (slot.removeFromLeft (78).reduced (2, 2));
        amounts_[static_cast<std::size_t> (index)].setBounds (slot.reduced (2, 4));
    }
}
