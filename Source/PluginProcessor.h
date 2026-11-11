#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class MotionAudioProcessor : public juce::AudioProcessor
{
public:
    MotionAudioProcessor();
    ~MotionAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Motion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    struct DelayLine
    {
        std::vector<float> buf;
        int w = 0;
        void init (int n) { buf.assign ((size_t) n, 0.f); w = 0; }
        float read (float delaySamples) const
        {
            const int n = (int) buf.size();
            float rp = (float) w - delaySamples;
            while (rp < 0.f) rp += (float) n;
            int i0 = (int) rp;
            const float fr = rp - (float) i0;
            if (i0 >= n) i0 -= n;
            int i1 = i0 + 1;
            if (i1 >= n) i1 -= n;
            return buf[(size_t) i0] * (1.f - fr) + buf[(size_t) i1] * fr;
        }
        void write (float x)
        {
            buf[(size_t) w] = x;
            if (++w >= (int) buf.size()) w = 0;
        }
    };

    struct Biquad
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    double sr = 44100.0;
    int maxBlock = 512;

    // Motion engine
    std::array<std::array<DelayLine, 2>, 4> dl;
    juce::dsp::IIR::Filter<float> hp[2];
    double motionPhase = 0.0;

    // Phaser / tremolo
    std::array<Biquad, 4> apL, apR;
    double phaserPhase = 0.0, tremPhase = 0.0;

    // Saturation (oversampled) and bitcrusher
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    float hold[2] { 0.f, 0.f }, ctr[2] { 0.f, 0.f };

    Smoother sType, sDepth, sPan, sMix, sRate, sMod, sPhaser, sTrem;

    std::atomic<float> *pType, *pRate, *pDepth, *pPan, *pMix, *pMode, *pShape, *pSync,
                       *pPhaser, *pTrem, *pMRate, *pDrive, *pDType, *pSMix, *pBits, *pSRate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionAudioProcessor)
};
