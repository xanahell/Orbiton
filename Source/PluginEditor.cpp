#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff15161a), kAccent (0xffff7a3d), kAccent2 (0xff4fb3d1), kText (0xffecebe6), kMute (0xff8d8f98);
}

MotionEditor::MotionEditor (MotionAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto addCombo = [this] (const char* id)
    {
        auto c = std::make_unique<Combo>();
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            c->box.addItemList (param->choices, 1);
        addAndMakeVisible (c->box);
        c->attach = std::make_unique<ComboAttachment> (proc.apvts, id, c->box);
        combos.push_back (std::move (c));
    };

    auto addKnob = [this] (const char* id, const char* name, juce::Colour colour)
    {
        auto k = std::make_unique<Knob>();
        k->slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        k->slider.setColour (juce::Slider::thumbColourId, kText);
        k->slider.setColour (juce::Slider::textBoxTextColourId, kText);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->label.setText (name, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setColour (juce::Label::textColourId, kMute);
        addAndMakeVisible (k->slider);
        addAndMakeVisible (k->label);
        k->attach = std::make_unique<SliderAttachment> (proc.apvts, id, k->slider);
        knobs.push_back (std::move (k));
    };

    for (auto* id : { "mode", "shape", "sync", "dtype" })
        addCombo (id);

    // Row 1: Motion engine (5 knobs). Row 2: effects (7 knobs).
    addKnob ("type", "Type", kAccent);
    addKnob ("rate", "Rate", kAccent);
    addKnob ("depth", "Depth", kAccent);
    addKnob ("pan", "Pan", kAccent);
    addKnob ("mix", "Mix", kAccent);
    addKnob ("phaser", "Phaser", kAccent2);
    addKnob ("trem", "Tremolo", kAccent2);
    addKnob ("mrate", "Mod Rate", kAccent2);
    addKnob ("drive", "Drive", kAccent2);
    addKnob ("smix", "Sat Mix", kAccent2);
    addKnob ("bits", "Bits", kAccent2);
    addKnob ("srate", "Crush Hz", kAccent2);

    setSize (680, 440);
}

void MotionEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (28.f, juce::Font::bold));
    g.drawText ("Motion", 20, 10, 300, 32, juce::Justification::centredLeft);
    g.setColour (kMute);
    g.setFont (juce::FontOptions (13.f));
    g.drawText ("Chorus / Flanger / Pan  |  Phaser  |  Tremolo  |  Drive  |  Bitcrusher",
                20, 38, getWidth() - 40, 18, juce::Justification::centredLeft);
}

void MotionEditor::layoutRow (juce::Rectangle<int> row, int from, int to)
{
    const int w = row.getWidth() / (to - from);
    for (int i = from; i < to; ++i)
    {
        auto cell = row.removeFromLeft (w).reduced (6);
        knobs[(size_t) i]->label.setBounds (cell.removeFromTop (20));
        knobs[(size_t) i]->slider.setBounds (cell);
    }
}

void MotionEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (46);
    auto cb = r.removeFromTop (28);
    const int cw = cb.getWidth() / (int) combos.size();
    for (auto& c : combos)
        c->box.setBounds (cb.removeFromLeft (cw).reduced (4, 0));
    r.removeFromTop (10);
    auto row1 = r.removeFromTop (r.getHeight() / 2);
    layoutRow (row1, 0, 5);
    layoutRow (r, 5, (int) knobs.size());
}
