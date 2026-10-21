#include "WaveformGapView.h"
#include "../Parameters.h"

namespace gapfill
{

namespace
{
    // The audio thread emits roughly this many frames per second (see
    // FillEngine::prepare), which is what the column rate is derived from.
    constexpr float kFramesPerSecond = 700.0f;
    constexpr float kWindowSeconds   = 4.0f;
}

WaveformGapView::WaveformGapView (VisualisationFifo& f)
    : fifo (f)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    drainBuffer.resize ((size_t) drainPerTick);
    columns.assign ((size_t) maxColumns, VizFrame {});
    numColumns = 600;
    startTimerHz (30);
}

WaveformGapView::~WaveformGapView()
{
    stopTimer();
}

void WaveformGapView::setActive (bool shouldRun)
{
    if (shouldRun)
    {
        if (! isTimerRunning())
            startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void WaveformGapView::setSpectralMode (bool shouldShowBands)
{
    if (spectralMode != shouldShowBands)
    {
        spectralMode = shouldShowBands;
        repaint();
    }
}

void WaveformGapView::setLookaheadMs (float ms)
{
    if (std::abs (ms - lookaheadMs) > 0.5f)
    {
        lookaheadMs = ms;
        repaint();
    }
}

void WaveformGapView::resized()
{
    numColumns = juce::jlimit (64, maxColumns, juce::jmax (64, getWidth()));
    framesPerColumn = juce::jmax (1, (int) std::round (kFramesPerSecond * kWindowSeconds / (float) numColumns));
}

//==============================================================================
void WaveformGapView::pushColumn (const VizFrame& frame)
{
    columns[(size_t) writeIndex] = frame;
    writeIndex = (writeIndex + 1) % juce::jmax (1, numColumns);
}

void WaveformGapView::timerCallback()
{
    int total = 0;

    for (;;)
    {
        const auto got = fifo.pop (drainBuffer.data(), drainPerTick);

        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i)
        {
            const auto& f = drainBuffer[(size_t) i];

            accumulator.peak   = juce::jmax (accumulator.peak, f.peak);
            accumulator.conf   = juce::jmax (accumulator.conf, f.conf);
            accumulator.energy = juce::jmax (accumulator.energy, f.energy);
            accumulator.duck   = juce::jmin (accumulator.duck, f.duck);
            accumulator.flags  = (std::uint8_t) (accumulator.flags | f.flags);

            for (int b = 0; b < 3; ++b)
                accumulator.bandConf[b] = juce::jmax (accumulator.bandConf[b], f.bandConf[b]);

            if (f.algo != 0xFF)
            {
                accumulator.algo = f.algo;
                accumulator.band = f.band;
            }

            if (++accumulatorCount >= framesPerColumn)
            {
                pushColumn (accumulator);
                accumulator = {};
                accumulatorCount = 0;
                ++total;
            }
        }

        if (got < drainPerTick)
            break;
    }

    if (total > 0)
        repaint();
}

//==============================================================================
void WaveformGapView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (colours::background.brighter (0.02f));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    auto plot = bounds.reduced (8.0f);
    auto legend = plot.removeFromTop (18.0f);

    juce::Rectangle<float> bandArea;

    if (spectralMode)
        bandArea = plot.removeFromBottom (juce::jmin (54.0f, plot.getHeight() * 0.32f));

    auto fillLane = plot.removeFromBottom (juce::jmin (22.0f, plot.getHeight() * 0.18f));

    const auto centreY = plot.getCentreY();
    const auto halfHeight = plot.getHeight() * 0.46f;
    const auto n = juce::jmax (1, numColumns);
    const auto colWidth = plot.getWidth() / (float) n;

    // ---- legend ----------------------------------------------------------
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.08f));

        struct Entry { const char* label; juce::Colour colour; };
        const Entry entries[] =
        {
            { "SIGNAL",      colours::waveform },
            { "EVENT",       juce::Colours::white },
            { "USABLE GAP",  colours::accent },
            { "PROTECTED",   colours::accentAmber },
            { "FILL",        colours::accentWarm },
        };

        auto x = legend.getX();

        for (const auto& e : entries)
        {
            g.setColour (e.colour);
            g.fillRoundedRectangle (x, legend.getCentreY() - 3.0f, 12.0f, 6.0f, 2.0f);
            g.setColour (colours::textDim);
            const auto w = 6.2f * (float) juce::String (e.label).length() + 8.0f;
            g.drawText (e.label, juce::Rectangle<float> (x + 16.0f, legend.getY(), w, legend.getHeight()),
                        juce::Justification::centredLeft);
            x += w + 26.0f;

            if (x > legend.getRight() - 60.0f)
                break;
        }

        g.setColour (colours::textDim);
        g.drawText (juce::String (kWindowSeconds, 1) + " s  |  lookahead "
                      + juce::String ((int) std::round (lookaheadMs)) + " ms",
                    legend.toNearestInt(), juce::Justification::centredRight);
    }

    // ---- lookahead strip on the right ------------------------------------
    {
        const auto lookaheadColumns = juce::jlimit (0.0f, plot.getWidth() * 0.4f,
                                                    plot.getWidth() * (lookaheadMs * 0.001f) / kWindowSeconds);

        if (lookaheadColumns > 1.0f)
        {
            g.setColour (colours::accent.withAlpha (0.06f));
            g.fillRect (plot.getRight() - lookaheadColumns, plot.getY(), lookaheadColumns, plot.getHeight());
            g.setColour (colours::accent.withAlpha (0.28f));
            g.drawVerticalLine ((int) (plot.getRight() - lookaheadColumns),
                                plot.getY(), plot.getBottom());
        }
    }

    // ---- centre line -----------------------------------------------------
    g.setColour (colours::outline.withAlpha (0.55f));
    g.drawHorizontalLine ((int) centreY, plot.getX(), plot.getRight());

    // ---- columns ---------------------------------------------------------
    juce::RectangleList<float> gapRects, protRects, fillRects;

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (writeIndex + i) % n;
        const auto& c = columns[(size_t) idx];
        const auto x = plot.getX() + (float) i * colWidth;
        const auto w = juce::jmax (1.0f, colWidth + 0.5f);

        if ((c.flags & VizFrame::flagGapEligible) != 0)
            gapRects.addWithoutMerging ({ x, plot.getY(), w, plot.getHeight() });

        if ((c.flags & VizFrame::flagProtected) != 0)
            protRects.addWithoutMerging ({ x, plot.getY(), w, 5.0f });

        if ((c.flags & VizFrame::flagFillActive) != 0)
            fillRects.addWithoutMerging ({ x, fillLane.getY(), w, fillLane.getHeight() });
    }

    g.setColour (colours::accent.withAlpha (0.12f));
    g.fillRectList (gapRects);

    // waveform body
    {
        juce::RectangleList<float> waveRects;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = (writeIndex + i) % n;
            const auto& c = columns[(size_t) idx];
            const auto amp = juce::jlimit (0.0f, 1.0f, std::sqrt (c.peak));

            if (amp <= 0.0005f)
                continue;

            const auto h = juce::jmax (1.0f, amp * halfHeight);
            waveRects.addWithoutMerging ({ plot.getX() + (float) i * colWidth, centreY - h,
                                           juce::jmax (1.0f, colWidth * 0.86f), h * 2.0f });
        }

        g.setColour (colours::waveform.withAlpha (0.92f));
        g.fillRectList (waveRects);
    }

    // gap-confidence curve
    {
        juce::Path conf;
        bool started = false;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = (writeIndex + i) % n;
            const auto y = plot.getBottom() - columns[(size_t) idx].conf * plot.getHeight() * 0.96f;
            const auto x = plot.getX() + (float) i * colWidth;

            if (! started) { conf.startNewSubPath (x, y); started = true; }
            else           { conf.lineTo (x, y); }
        }

        if (started)
        {
            g.setColour (colours::accent.withAlpha (0.75f));
            g.strokePath (conf, juce::PathStrokeType (1.3f));
        }
    }

    // protection band + event markers
    g.setColour (colours::accentAmber.withAlpha (0.65f));
    g.fillRectList (protRects);

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (writeIndex + i) % n;

        if ((columns[(size_t) idx].flags & VizFrame::flagEvent) == 0)
            continue;

        const auto x = plot.getX() + (float) i * colWidth;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (x, plot.getY(), juce::jmax (1.0f, colWidth * 0.9f), plot.getHeight());
    }

    // ---- fill lane -------------------------------------------------------
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (fillLane, 3.0f);

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (writeIndex + i) % n;
        const auto& c = columns[(size_t) idx];

        if ((c.flags & VizFrame::flagFillActive) == 0)
            continue;

        // E-1: auditioned fills use their own colour (and the "PREVIEW" caption below).
        const auto colour = (c.flags & VizFrame::flagPreview) != 0 ? colours::accentGreen
                          : (c.band <= 2 ? colours::bandColour (c.band) : colours::accentWarm);
        g.setColour (colour.withAlpha (0.85f));
        g.fillRect (plot.getX() + (float) i * colWidth, fillLane.getY(),
                    juce::jmax (1.0f, colWidth + 0.5f), fillLane.getHeight());
    }

    // Name the algorithm of the most recent fill.
    {
        int recentAlgo = -1;
        bool recentIsPreview = false;

        for (int i = n - 1; i >= juce::jmax (0, n - 160); --i)
        {
            const auto idx = (writeIndex + i) % n;
            const auto& c = columns[(size_t) idx];

            if ((c.flags & VizFrame::flagFillActive) != 0 && c.algo != 0xFF)
            {
                recentAlgo = c.algo;
                recentIsPreview = (c.flags & VizFrame::flagPreview) != 0;
                break;
            }
        }

        g.setColour (recentAlgo >= 0 ? (recentIsPreview ? colours::accentGreen : colours::accentWarm) : colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.5f)).withExtraKerningFactor (0.1f));
        g.drawText (recentAlgo >= 0 ? juce::String (recentIsPreview ? "PREVIEW  " : "")
                                        + juce::String (algoName ((Algo) recentAlgo)).toUpperCase()
                                    : juce::String ("NO FILL"),
                    fillLane.reduced (7.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
    }

    // ---- spectral lanes --------------------------------------------------
    if (spectralMode && bandArea.getHeight() > 12.0f)
    {
        const auto laneHeight = bandArea.getHeight() / 3.0f;
        const char* names[] = { "LOW", "MID", "HIGH" };

        for (int b = 0; b < 3; ++b)
        {
            // Draw high at the top, low at the bottom.
            const auto row = 2 - b;
            auto lane = juce::Rectangle<float> (bandArea.getX(), bandArea.getY() + (float) row * laneHeight,
                                                bandArea.getWidth(), laneHeight - 2.0f);

            g.setColour (colours::panelRaised);
            g.fillRoundedRectangle (lane, 2.5f);

            for (int i = 0; i < n; ++i)
            {
                const auto idx = (writeIndex + i) % n;
                const auto conf = columns[(size_t) idx].bandConf[b];

                if (conf <= 0.05f)
                    continue;

                g.setColour (colours::bandColour (b).withAlpha (0.18f + 0.62f * conf));
                g.fillRect (lane.getX() + (float) i * colWidth, lane.getY(),
                            juce::jmax (1.0f, colWidth + 0.5f), lane.getHeight());
            }

            g.setColour (colours::text.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.12f));
            g.drawText (names[b], lane.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    // ---- playhead --------------------------------------------------------
    g.setColour (colours::accent.withAlpha (0.55f));
    g.drawVerticalLine ((int) plot.getRight() - 1, plot.getY(), plot.getBottom());
}

} // namespace gapfill
