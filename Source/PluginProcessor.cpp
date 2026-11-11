#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kHalfPi = kPi * 0.5f;
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;
constexpr float kSpread[4] = { -1.f, -.33f, .33f, 1.f };

inline float lfoShape (float phase, bool tri)
{
    const float s = std::sin (phase);
    return tri ? (2.f / kPi) * std::asin (juce::jlimit (-1.f, 1.f, s)) : s;
}

inline float saturate (float v, int type)
{
    switch (type)
    {
        case 1:  return (std::tanh (v + .3f) - std::tanh (.3f)) * .9f;   // tube (asymmetric)
        case 2:  return juce::jlimit (-1.f, 1.f, v);                      // hard clip
        case 3:  return std::sin (v * kHalfPi);                           // wavefolder
        default: return std::tanh (v);                                    // soft
    }
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MotionAudioProcessor::createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + " %"; });

    auto f = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, 1 }, name,
                                            juce::NormalisableRange<float> (0.f, 1.f, 0.001f), def, pct));
    };
    auto c = [&] (const char* id, const char* name, juce::StringArray items, int def)
    {
        p.push_back (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def));
    };

    c ("mode",  "Mode",  { "Vocal", "Mix / Master" }, 0);
    c ("shape", "LFO Shape", { "Sine", "Triangle" }, 0);
    c ("sync",  "Rate Sync", { "Rate: Free", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" }, 0);
    c ("dtype", "Drive Type", { "Drive: Soft (tanh)", "Drive: Tube (Asymmetric)", "Drive: Hard Clip", "Drive: Wavefolder" }, 0);

    f ("type", "Type", .3f);
    f ("rate", "Rate", .6f);
    f ("depth", "Depth", .5f);
    f ("pan", "Pan", .6f);
    f ("mix", "Mix", .5f);
    f ("phaser", "Phaser", 0.f);
    f ("trem", "Tremolo", 0.f);
    f ("mrate", "Mod Rate", .35f);
    f ("drive", "Drive", 0.f);
    f ("smix", "Sat Mix", 1.f);
    f ("bits", "Bits", 1.f);
    f ("srate", "Crush Hz", 1.f);

    return { p.begin(), p.end() };
}

MotionAudioProcessor::MotionAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pType = apvts.getRawParameterValue ("type");     pRate = apvts.getRawParameterValue ("rate");
    pDepth = apvts.getRawParameterValue ("depth");   pPan = apvts.getRawParameterValue ("pan");
    pMix = apvts.getRawParameterValue ("mix");       pMode = apvts.getRawParameterValue ("mode");
    pShape = apvts.getRawParameterValue ("shape");   pSync = apvts.getRawParameterValue ("sync");
    pPhaser = apvts.getRawParameterValue ("phaser"); pTrem = apvts.getRawParameterValue ("trem");
    pMRate = apvts.getRawParameterValue ("mrate");   pDrive = apvts.getRawParameterValue ("drive");
    pDType = apvts.getRawParameterValue ("dtype");   pSMix = apvts.getRawParameterValue ("smix");
    pBits = apvts.getRawParameterValue ("bits");     pSRate = apvts.getRawParameterValue ("srate");
}

bool MotionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MotionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    const int len = (int) (0.06 * sr) + 8;
    for (auto& voice : dl)
        for (auto& line : voice)
            line.init (len);

    for (auto& f : hp)
    {
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 180.0f, 0.707f);
        f.reset();
    }

    for (auto& b : apL) b = {};
    for (auto& b : apR) b = {};
    motionPhase = phaserPhase = tremPhase = 0.0;
    hold[0] = hold[1] = ctr[0] = ctr[1] = 0.f;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    for (auto* s : { &sType, &sDepth, &sPan, &sMix, &sRate, &sMod, &sPhaser, &sTrem })
        s->reset (sr, 0.03);

    sType.setCurrentAndTargetValue (pType->load());
    sDepth.setCurrentAndTargetValue (pDepth->load());
    sPan.setCurrentAndTargetValue (pPan->load());
    sMix.setCurrentAndTargetValue (pMix->load());
    sRate.setCurrentAndTargetValue (1.f);
    sMod.setCurrentAndTargetValue (1.f);
    sPhaser.setCurrentAndTargetValue (pPhaser->load());
    sTrem.setCurrentAndTargetValue (pTrem->load());
}

void MotionAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- read parameters -------------------------------------------------
    const bool mixMode = pMode->load() > 0.5f;
    const float md = mixMode ? 1.f : .6f, mp = mixMode ? 1.f : .5f, mm = mixMode ? 1.f : .6f;
    const bool tri = pShape->load() > 0.5f;

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0) bpm = *b;

    static constexpr float beats[] = { 0.f, 1.f, 2.f, 4.f, 8.f, 16.f };
    const int sy = juce::jlimit (0, 5, (int) std::lround (pSync->load()));
    const float rateKnob = pRate->load();
    const float hz = sy > 0 ? (float) (bpm / 60.0) / beats[sy]
                            : .05f + std::pow (rateKnob, 2.2f) * 7.95f;
    const float modHz = .2f + std::pow (pMRate->load(), 2.f) * 11.f;

    sType.setTargetValue (pType->load());
    sDepth.setTargetValue (pDepth->load());
    sPan.setTargetValue (pPan->load());
    sMix.setTargetValue (pMix->load());
    sRate.setTargetValue (hz);
    sMod.setTargetValue (modHz);
    sPhaser.setTargetValue (pPhaser->load());
    sTrem.setTargetValue (pTrem->load());

    // ---- stage 1: Motion (4-voice modulated delay + pan) -> phaser -> tremolo
    float apB0 = 1.f, apB1 = 0.f, apB2 = 0.f, apA1 = 0.f, apA2 = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const float t = sType.getNextValue();
        const float dep = sDepth.getNextValue();
        const float pn = sPan.getNextValue();
        const float mx = sMix.getNextValue();
        const float rateNow = sRate.getNextValue();
        const float modNow = sMod.getNextValue();
        const float phAmt = sPhaser.getNextValue();
        const float trAmt = sTrem.getNextValue();

        motionPhase += kTwoPi * (double) rateNow / sr;
        if (motionPhase >= kTwoPi) motionPhase -= kTwoPi;

        const float base = .018f + (.0045f - .018f) * t;
        const float depthSec = dep * md * (.006f + (.0018f - .006f) * t);
        const float fb = std::max (0.f, (t - .4f) / .6f) * .65f;
        const float mxe = std::min (mx, 1.f) * mm;
        const float dryG = std::cos (mxe * kHalfPi);
        const float wetG = std::sin (mxe * kHalfPi) * 1.1f;

        const float inL = L[i], inR = R[i];
        const float hL = hp[0].processSample (inL);
        const float hR = hp[1].processSample (inR);

        float wl = 0.f, wr = 0.f;
        for (int v = 0; v < 4; ++v)
        {
            const float lfo = lfoShape ((float) motionPhase + (float) v * kHalfPi, tri);
            float d = (base * (1.f + ((float) v - 1.5f) * .12f * (1.f - t)) + depthSec * lfo) * (float) sr;
            d = juce::jmax (d, 2.f);

            const float yl = dl[(size_t) v][0].read (d);
            const float yr = dl[(size_t) v][1].read (d);
            const float f = (v % 2) ? -fb : fb;
            dl[(size_t) v][0].write (hL + f * yl);
            dl[(size_t) v][1].write (hR + f * yr);

            // stereo panner (same law as the Web Audio StereoPanner)
            const float pan = juce::jlimit (-1.f, 1.f, kSpread[v] * pn * mp + lfo * pn * mp * .45f);
            const float x = pan <= 0.f ? pan + 1.f : pan;
            const float gl = std::cos (x * kHalfPi), gr = std::sin (x * kHalfPi);
            float ol, orr;
            if (pan <= 0.f) { ol = yl + yr * gl; orr = yr * gr; }
            else            { ol = yl * gl;      orr = yr + yl * gr; }
            wl += ol * .5f;
            wr += orr * .5f;
        }

        float pl = inL * dryG + wl * wetG;
        float pr = inR * dryG + wr * wetG;

        // phaser: 4 allpass stages, centre 700 Hz swept +-1.25 octaves, coefficients every 32 samples
        phaserPhase += kTwoPi * (double) (modNow * .2f) / sr;
        if (phaserPhase >= kTwoPi) phaserPhase -= kTwoPi;
        if ((i & 31) == 0)
        {
            const float fc = juce::jlimit (20.f, (float) sr * .45f,
                                           700.f * std::pow (2.f, 1.25f * (float) std::sin (phaserPhase)));
            const float w0 = 2.f * kPi * fc / (float) sr, alpha = std::sin (w0) / (2.f * .7f), cw = std::cos (w0);
            const float a0 = 1.f + alpha;
            apB0 = (1.f - alpha) / a0; apB1 = -2.f * cw / a0; apB2 = (1.f + alpha) / a0;
            apA1 = -2.f * cw / a0;     apA2 = (1.f - alpha) / a0;
            for (auto* bank : { &apL, &apR })
                for (auto& b : *bank) { b.b0 = apB0; b.b1 = apB1; b.b2 = apB2; b.a1 = apA1; b.a2 = apA2; }
        }
        float aL = pl, aR = pr;
        for (size_t s = 0; s < 4; ++s) { aL = apL[s].process (aL); aR = apR[s].process (aR); }
        pl += aL * phAmt;
        pr += aR * phAmt;

        // tremolo
        tremPhase += kTwoPi * (double) modNow / sr;
        if (tremPhase >= kTwoPi) tremPhase -= kTwoPi;
        const float tg = 1.f - trAmt * .5f + trAmt * .5f * (float) std::sin (tremPhase);

        L[i] = pl * tg;
        R[i] = pr * tg;
    }

    // ---- stage 2: saturation, 4x oversampled ---------------------------------
    const float drive = pDrive->load();
    const bool satOn = drive > .01f;
    const float k = 1.f + drive * drive * 40.f;
    const float smix = pSMix->load();
    const int dtype = juce::jlimit (0, 3, (int) std::lround (pDType->load()));

    juce::dsp::AudioBlock<float> block (buffer);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        auto sub = block.getSubBlock ((size_t) start, (size_t) len);
        auto up = oversampler->processSamplesUp (sub);
        if (satOn)
        {
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                float* d = up.getChannelPointer (ch);
                for (size_t j = 0; j < up.getNumSamples(); ++j)
                {
                    const float x = d[j];
                    d[j] = x * (1.f - smix) + saturate (x * k, dtype) * .8f * smix;
                }
            }
        }
        oversampler->processSamplesDown (sub);
    }

    // ---- stage 3: bitcrusher (bit depth + sample-rate reduction) --------------
    const int bits = 2 + (int) std::lround (pBits->load() * 14.f);
    const float q = std::pow (2.f, (float) (bits - 1));
    const float sr2 = pSRate->load();
    const float r = .02f + sr2 * sr2 * .98f;
    if (! (bits >= 16 && r >= .999f))
    {
        float* chans[2] = { L, R };
        for (int ch = 0; ch < 2; ++ch)
        {
            float h = hold[ch], c = ctr[ch];
            float* x = chans[ch];
            for (int i = 0; i < n; ++i)
            {
                c += r;
                if (c >= 1.f) { c -= 1.f; h = std::round (x[i] * q) / q; }
                x[i] = h;
            }
            hold[ch] = h;
            ctr[ch] = c;
        }
    }
}

juce::AudioProcessorEditor* MotionAudioProcessor::createEditor() { return new MotionEditor (*this); }

void MotionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MotionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MotionAudioProcessor(); }
