#pragma once
#include "PluginProcessor.h"

class MotionEditor : public juce::AudioProcessorEditor
{
public:
    explicit MotionEditor (MotionAudioProcessor&);
    ~MotionEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };
    struct Combo
    {
        juce::ComboBox box;
        std::unique_ptr<ComboAttachment> attach;
    };

    void layoutRow (juce::Rectangle<int> row, int from, int to);

    MotionAudioProcessor& proc;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionEditor)
};
