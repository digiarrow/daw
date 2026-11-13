#include "RackEditor.hpp"

#include "waveform/effects.hpp"

namespace
{

waveform::EffectKind kindAt (int id)
{
    switch (id)
    {
        case 2: return waveform::EffectKind::Compressor;
        case 3: return waveform::EffectKind::Delay;
        case 4: return waveform::EffectKind::Reverb;
        case 5: return waveform::EffectKind::Limiter;
        case 6: return waveform::EffectKind::Utility;
        case 7: return waveform::EffectKind::Chorus;
        case 8: return waveform::EffectKind::Saturator;
        default: return waveform::EffectKind::Eq;
    }
}

} // namespace

void RackEditor::setRack (waveform::DeviceRack rack)
{
    rack_ = std::move (rack);

    if (rack_.chains.empty())
    {
        waveform::DeviceChain chain;
        chain.name = "Chain A";
        rack_.chains.push_back (chain);
    }

    if (rack_.macros.empty())
        rack_.macros.push_back ({});

    chainIndex_ = std::clamp (chainIndex_, 0, static_cast<int> (rack_.chains.size()) - 1);
    refresh();
}

waveform::DeviceChain* RackEditor::chain()
{
    if (chainIndex_ < 0 || chainIndex_ >= static_cast<int> (rack_.chains.size()))
        return nullptr;

    return &rack_.chains[static_cast<std::size_t> (chainIndex_)];
}

waveform::EffectSlot* RackEditor::effect()
{
    auto* current = chain();

    if (current == nullptr || effectIndex_ < 0 || effectIndex_ >= static_cast<int> (current->effects.size()))
        return nullptr;

    return &current->effects[static_cast<std::size_t> (effectIndex_)];
}

void RackEditor::commit()
{
    if (onCommit)
        onCommit (rack_);
}

void RackEditor::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating_, true);
    chainBox_.clear (juce::dontSendNotification);
    effectBox_.clear (juce::dontSendNotification);

    for (int index = 0; index < static_cast<int> (rack_.chains.size()); ++index)
        chainBox_.addItem (juce::String (rack_.chains[static_cast<std::size_t> (index)].name) + " " + juce::String (index + 1), index + 1);

    chainBox_.setSelectedId (chainIndex_ + 1, juce::dontSendNotification);

    if (auto* current = chain())
    {
        gain_.setValue (current->gain, juce::dontSendNotification);

        for (int index = 0; index < static_cast<int> (current->effects.size()); ++index)
            effectBox_.addItem (waveform::effectName (current->effects[static_cast<std::size_t> (index)].kind), index + 1);

        if (! current->effects.empty())
        {
            effectIndex_ = std::clamp (effectIndex_, 0, static_cast<int> (current->effects.size()) - 1);
            effectBox_.setSelectedId (effectIndex_ + 1, juce::dontSendNotification);
            const auto& slot = current->effects[static_cast<std::size_t> (effectIndex_)];
            parameters_[0].setValue (slot.p0, juce::dontSendNotification);
            parameters_[1].setValue (slot.p1, juce::dontSendNotification);
            parameters_[2].setValue (slot.p2, juce::dontSendNotification);
            parameters_[3].setValue (slot.p3, juce::dontSendNotification);
        }
    }

    if (! rack_.macros.empty())
        macro_.setValue (rack_.macros.front().value, juce::dontSendNotification);
}

RackEditor::RackEditor()
{
    addAndMakeVisible (chainBox_);
    addAndMakeVisible (effectBox_);
    addAndMakeVisible (addChain_);
    addAndMakeVisible (removeChain_);
    addAndMakeVisible (addEffect_);
    addAndMakeVisible (removeEffect_);
    addAndMakeVisible (gain_);
    addAndMakeVisible (macro_);
    addAndMakeVisible (mapMacro_);
    addAndMakeVisible (hint_);
    hint_.setText ("Chains run in parallel. The macro writes its value into the mapped parameter.", juce::dontSendNotification);

    gain_.setRange (0.0, 1.0, 0.01);
    gain_.setSliderStyle (juce::Slider::LinearHorizontal);
    gain_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    macro_.setRange (0.0, 1.0, 0.01);
    macro_.setSliderStyle (juce::Slider::LinearHorizontal);
    macro_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);

    for (auto& slider : parameters_)
    {
        slider.setRange (-48.0, 48.0, 0.01);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible (slider);
        slider.onValueChange = [this, &slider]
        {
            if (updating_)
                return;

            if (auto* slot = effect())
            {
                const int index = static_cast<int> (&slider - parameters_);
                double* values[] = { &slot->p0, &slot->p1, &slot->p2, &slot->p3 };
                *values[index] = slider.getValue();
                commit();
            }
        };
    }

    chainBox_.onChange = [this]
    {
        if (! updating_)
        {
            chainIndex_ = std::max (0, chainBox_.getSelectedId() - 1);
            effectIndex_ = 0;
            refresh();
        }
    };
    effectBox_.onChange = [this]
    {
        if (! updating_)
        {
            effectIndex_ = std::max (0, effectBox_.getSelectedId() - 1);
            refresh();
        }
    };
    addChain_.onClick = [this]
    {
        if (rack_.chains.size() >= 4)
            return;

        waveform::DeviceChain chain;
        chain.name = "Chain";
        rack_.chains.push_back (chain);
        chainIndex_ = static_cast<int> (rack_.chains.size()) - 1;
        refresh();
        commit();
    };
    removeChain_.onClick = [this]
    {
        if (rack_.chains.size() <= 1)
            return;

        rack_.chains.erase (rack_.chains.begin() + chainIndex_);
        chainIndex_ = std::clamp (chainIndex_, 0, static_cast<int> (rack_.chains.size()) - 1);
        refresh();
        commit();
    };
    addEffect_.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "EQ");
        menu.addItem (2, "Compressor");
        menu.addItem (3, "Delay");
        menu.addItem (4, "Reverb");
        menu.addItem (5, "Limiter");
        menu.addItem (6, "Utility");
        menu.addItem (7, "Chorus");
        menu.addItem (8, "Saturator");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addEffect_), [this] (int result)
        {
            auto* current = chain();

            if (result == 0 || current == nullptr || current->effects.size() >= 4)
                return;

            current->effects.push_back (waveform::defaultEffect (kindAt (result)));
            effectIndex_ = static_cast<int> (current->effects.size()) - 1;
            refresh();
            commit();
        });
    };
    removeEffect_.onClick = [this]
    {
        if (auto* current = chain(); current != nullptr && effectIndex_ >= 0 && effectIndex_ < static_cast<int> (current->effects.size()))
        {
            current->effects.erase (current->effects.begin() + effectIndex_);
            effectIndex_ = std::max (0, effectIndex_ - 1);
            refresh();
            commit();
        }
    };
    gain_.onValueChange = [this]
    {
        if (! updating_)
            if (auto* current = chain())
            {
                current->gain = static_cast<float> (gain_.getValue());
                commit();
            }
    };
    macro_.onValueChange = [this]
    {
        if (updating_ || rack_.macros.empty())
            return;

        rack_.macros.front().value = macro_.getValue();
        commit();
    };
    mapMacro_.onClick = [this]
    {
        if (rack_.macros.empty() || effect() == nullptr)
            return;

        waveform::MacroAssignment assignment;
        assignment.chain = chainIndex_;
        assignment.effect = effectIndex_;
        assignment.parameter = 0;
        assignment.minimum = parameters_[0].getMinimum();
        assignment.maximum = parameters_[0].getMaximum();
        rack_.macros.front().assignments = { assignment };
        commit();
    };
}

void RackEditor::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto row = area.removeFromTop (28);
    chainBox_.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (6);
    addChain_.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (6);
    removeChain_.setBounds (row.removeFromLeft (110));
    area.removeFromTop (6);
    row = area.removeFromTop (28);
    effectBox_.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (6);
    addEffect_.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (6);
    removeEffect_.setBounds (row.removeFromLeft (120));
    area.removeFromTop (8);
    gain_.setBounds (area.removeFromTop (28));

    for (auto& slider : parameters_)
    {
        area.removeFromTop (4);
        slider.setBounds (area.removeFromTop (28));
    }

    area.removeFromTop (8);
    macro_.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    mapMacro_.setBounds (area.removeFromTop (28).removeFromLeft (160));
    hint_.setBounds (area.removeFromTop (40));
}
