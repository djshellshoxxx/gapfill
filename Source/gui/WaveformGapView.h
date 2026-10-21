#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "MeterMath.h"
#include "../dsp/VisualisationState.h"

namespace gapfill
{

//==============================================================================
/** The scrolling "where are the spaces" display.

    Shows the incoming energy, the events the detector found, the regions it
    considers usable space, the protection it is applying, and the fills it
    placed - all from lock-free frames produced by the audio thread. */
class WaveformGapView : public juce::Component,
                        public juce::TooltipClient,
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

    /** E-3: explains the skipped-gap marker under the mouse ("<label> - X ms free, Y ms needed. <advice>"). */
    juce::String getTooltip() override;

    /** Index of the newest-to-oldest-agnostic column holding a skipped-gap marker within
        `slop` columns of x (component coordinates); -1 when none. Public for tests. */
    int findRejectedColumnNear (float x, int slop = 3) const;

    /** Test access to the last column geometry computed by paint(). */
    const VizFrame& getColumn (int i) const noexcept { return columns[(size_t) ((writeIndex + i) % juce::jmax (1, numColumns))]; }
    int getNumColumns() const noexcept { return numColumns; }
    void injectFrameForTests (const VizFrame& f)  { for (int i = 0; i < framesPerColumn; ++i) { accumulate (f); } }

private:
    void accumulate (const VizFrame& f);
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

    // Geometry of the last paint, used to map the mouse to a column (E-3 tooltip).
    float plotLeft = 0.0f;
    float plotColumnWidth = 1.0f;

    static constexpr int maxColumns = 2048;
    static constexpr int drainPerTick = 512;
    std::vector<VizFrame> drainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformGapView)
};

//==============================================================================
/** Small vertical level meter used in the mix strip.

    Follows the identity spec: a smooth teal -> orange -> yellow -> red gradient
    rather than discrete LEDs, a peak line that holds for 1.5s and then falls at
    20dB/s, and a numeric peak readout when the meter is wide enough to show one. */
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter() = default;

    void setLevel (float newLevel)
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, newLevel);
        const auto db = toDb (clamped);

        if (peak.push (db, juce::Time::getMillisecondCounter()))
        {
            // The peak now has to be animated back down.
            if (! isTimerRunning())
                startTimerHz ((int) meter::kTimerHz);
        }

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

        auto bar = b;
        const bool showReadout = b.getWidth() >= 26.0f;
        const auto readout = showReadout ? bar.removeFromTop (11.0f) : juce::Rectangle<float>();

        const auto norm = normalised (toDb (level));

        if (norm > 0.0f)
        {
            // One gradient across the whole track, clipped to the filled part,
            // so a given level always has the same colour.
            auto grad = meter::makeGradient (bar.getX(), bar.getBottom(), bar.getY());

            juce::Graphics::ScopedSaveState save (g);
            auto filled = bar.withTop (bar.getBottom() - bar.getHeight() * norm);
            g.reduceClipRegion (filled.getSmallestIntegerContainer());
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, 2.5f);
        }

        // Peak hold line.
        if (peak.peakDb > kFloorDb)
        {
            const auto y = bar.getBottom() - bar.getHeight() * normalised (peak.peakDb);
            g.setColour (peak.peakDb >= -0.1f ? colours::clip : colours::text);
            g.fillRect (bar.getX(), juce::jlimit (bar.getY(), bar.getBottom() - 1.0f, y),
                        bar.getWidth(), 1.0f);
        }

        if (showReadout)
        {
            g.setColour (peak.peakDb >= -0.1f ? colours::clip : colours::textMuted);
            g.setFont (fonts::mono (9.0f));
            g.drawText (peak.peakDb <= kFloorDb ? juce::String ("-inf")
                                                : juce::String (peak.peakDb, 1),
                        readout, juce::Justification::centredRight, false);
        }
    }

    /** Clears the held peak - used when the meter is re-shown or reset. */
    void resetPeak()
    {
        peak.reset();
        stopTimer();
        repaint();
    }

private:
    void timerCallback() override
    {
        // 20dB per second at the timer rate (meter::PeakHold).
        const auto step = peak.tick (juce::Time::getMillisecondCounter());

        if (step == meter::PeakHold::Tick::Held)
            return;

        if (step == meter::PeakHold::Tick::Settled)
            stopTimer();

        repaint();
    }

    static float toDb (float gain)         { return meter::toDb (gain); }
    static float normalised (float db)     { return meter::normalised (db); }

    static constexpr float kFloorDb = meter::kFloorDb;

    float level = 0.0f;
    meter::PeakHold peak;
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
