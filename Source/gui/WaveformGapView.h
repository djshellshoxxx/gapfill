#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "../dsp/VisualisationState.h"

namespace gapfill
{

//==============================================================================
/** The scrolling "where are the spaces" display.

    Shows the incoming energy, the events the detector found, the regions it
    considers usable space, the protection it is applying, and the fills it
    placed - all from lock-free frames produced by the audio thread. */
class WaveformGapView : public juce::Component,
                        private juce::Timer
{
public:
    explicit WaveformGapView (VisualisationFifo& fifo);
    ~WaveformGapView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setSpectralMode (bool shouldShowBands);
    void setLookaheadMs (float ms);
    void setActive (bool shouldRun);

private:
    void timerCallback() override;
    void pushColumn (const VizFrame& frame);

    VisualisationFifo& fifo;

    std::vector<VizFrame> columns;
    int writeIndex = 0;
    int numColumns = 0;

    VizFrame accumulator;
    int accumulatorCount = 0;
    int framesPerColumn = 3;

    bool spectralMode = false;
    float lookaheadMs = 40.0f;

    static constexpr int maxColumns = 2048;
    static constexpr int drainPerTick = 512;
    std::vector<VizFrame> drainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformGapView)
};

//==============================================================================
/** Small vertical level meter used in the mix strip. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::Colour colour) : tint (colour) {}

    void setLevel (float newLevel)
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, newLevel);

        if (std::abs (clamped - level) > 0.002f)
        {
            level = clamped;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (b, 2.5f);

        const auto db = juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, level));
        const auto norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);

        auto filled = b.withTop (b.getBottom() - b.getHeight() * norm);
        g.setColour (level > 0.99f ? juce::Colours::red : tint);
        g.fillRoundedRectangle (filled, 2.5f);
    }

private:
    juce::Colour tint;
    float level = 0.0f;
};

//==============================================================================
/** Horizontal gap-confidence readout. */
class ConfidenceBar : public juce::Component
{
public:
    void setConfidence (float c)
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, c);

        if (std::abs (clamped - value) > 0.004f)
        {
            value = clamped;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (b, 3.0f);

        auto filled = b.withWidth (b.getWidth() * value);
        g.setColour (colours::accent.withAlpha (0.35f + 0.55f * value));
        g.fillRoundedRectangle (filled, 3.0f);

        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("GAP CONFIDENCE  " + juce::String ((int) std::round (value * 100.0f)) + "%",
                    getLocalBounds().reduced (6, 0), juce::Justification::centredLeft);
    }

private:
    float value = 0.0f;
};

} // namespace gapfill
