#pragma once

#include "Knob.h"
#include "WaveformGapView.h"
#include "../Parameters.h"

namespace gapfill
{

//==============================================================================
class PanelBase : public juce::Component
{
public:
    PanelBase (juce::String panelTitle, juce::Colour panelTint)
        : title (std::move (panelTitle)), tint (panelTint) {}

    void paint (juce::Graphics& g) override
    {
        GapFillLookAndFeel::drawPanel (g, getLocalBounds().toFloat(), title, tint);
    }

protected:
    juce::Rectangle<int> contentArea() const { return getLocalBounds().reduced (10).withTrimmedTop (18); }

    juce::String title;
    juce::Colour tint;
};

//==============================================================================
class DetectionPanel : public PanelBase
{
public:
    explicit DetectionPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    GapCombo mode, source, maxFillDiv;
    GapKnob sensitivity, gapThreshold, minGap, maxFill, gapRelease, lookahead, protectHold, retrigger;
    GapToggle maxFillSync, audition;
};

//==============================================================================
class CreativePanel : public PanelBase
{
public:
    explicit CreativePanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    GapKnob fill, intensity, complexity, chaos, density;
    GapCombo fillType, stutterDiv, delayDiv;
    GapKnob seed, width;
    GapToggle sync, pingPong, monoWet, capture;
};

//==============================================================================
class WeightsPanel : public PanelBase
{
public:
    WeightsPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::OwnedArray<GapBar> bars;
    juce::Viewport weightViewport;
    juce::Component weightContent;
    juce::TextButton allOn { "ALL ON" }, allOff { "ALL OFF" }, drumsOnly { "RHYTHMIC" }, texturesOnly { "TEXTURAL" };
    juce::Label hint;

    void applySet (const juce::StringArray& enabled);
};

//==============================================================================
class ProtectionPanel : public PanelBase
{
public:
    explicit ProtectionPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    GapKnob transientProtect, lowProtect, centreProtect, nextProtect, sidechainProtect;
    juce::Label note;
};

//==============================================================================
class SpectralPanel : public PanelBase
{
public:
    explicit SpectralPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setBandConfidence (int band, float conf);

private:
    struct Lane
    {
        std::unique_ptr<GapToggle> enabled;
        std::unique_ptr<GapKnob> amount, intensity, probability;
        float confidence = 0.0f;
        juce::Rectangle<int> meterBounds;
    };

    Lane lanes[3];
    GapKnob xoverLowMid, xoverMidHigh;
    GapCombo solo;
    juce::Label hint;
};

//==============================================================================
class MixPanel : public PanelBase
{
public:
    explicit MixPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

    void setLevels (float in, float out, float wet);
    void setConfidence (float c) { confidence.setConfidence (c); }
    void setStatusText (const juce::String& text);

    /** Drops the held meter peaks. The old peak is meaningless once the
        settings have been thrown away, so RESET clears it. */
    void resetMeterPeaks();

private:
    GapKnob dry, wet, output;
    // The gradient is the same on every meter, per the identity spec, so
    // these are no longer tinted individually.
    LevelMeter inputMeter, outputMeter, wetMeter;
    juce::Label inLabel, outLabel, wetLabel, status;
    ConfidenceBar confidence;
};

} // namespace gapfill
