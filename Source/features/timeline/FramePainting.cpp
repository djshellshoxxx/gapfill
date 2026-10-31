#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../reasons/Gui.h"

namespace gapfill::timeline
{

void TimelineView::collectFrames()
{
    displayFrames.clear();
    const auto& history = proc.getExplainState().timelineModel.frames;
    const auto span = juce::jmax (std::int64_t { 1 }, windowSamples());
    const auto start = shownEnd - span;
    int previousBucket = -1;

    for (int i = 0; i < history.size(); ++i)
    {
        const auto& source = history.at (i);
        if (source.position < start || source.position > shownEnd)
            continue;
        const auto bucket = juce::jlimit (0, maxDisplayColumns - 1,
            (int) ((source.position - start) * maxDisplayColumns / span));
        if (bucket != previousBucket)
        {
            if (displayFrames.size() == (std::size_t) maxDisplayColumns)
            {
                displayFrames.back().incompleteBefore = true;
                break;
            }
            displayFrames.push_back (source);
            previousBucket = bucket;
            continue;
        }

        auto& target = displayFrames.back();
        target.peak = juce::jmax (target.peak, source.peak);
        target.energy = juce::jmax (target.energy, source.energy);
        target.confidence = juce::jmax (target.confidence, source.confidence);
        target.duck = juce::jmin (target.duck, source.duck);
        for (int band = 0; band < 3; ++band)
            target.bandConfidence[band] = juce::jmax (target.bandConfidence[band], source.bandConfidence[band]);
        target.flags |= source.flags;
        target.protectionKinds |= source.protectionKinds;
        target.incompleteBefore = target.incompleteBefore || source.incompleteBefore;
        target.periodSamples = (std::uint32_t) (source.position - target.position + source.periodSamples);
    }
}

void TimelineView::paintFrameLanes (juce::Graphics& g, juce::Rectangle<float> audio,
                                   juce::Rectangle<float> gap, juce::Rectangle<float> protection,
                                   juce::Rectangle<float> bands)
{
    const auto span = (double) juce::jmax (std::int64_t { 1 }, windowSamples());
    const auto start = shownEnd - windowSamples();
    const auto xOf = [&] (std::int64_t pos)
    {
        return audio.getX() + (float) ((double) (pos - start) / span) * audio.getWidth();
    };
    const auto plot = plotBounds().toFloat();
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());
    bool havePpq = false;
    const FrameHistory::Column* previous = nullptr;
    float protectedStart[4] {}, protectedEnd[4] {};
    bool protectedRun[4] {};
    const auto flushProtection = [&] (int kind)
    {
        if (! protectedRun[kind])
            return;
        const auto row = protection.getHeight() / 3.0f;
        const auto area = kind == 3
            ? juce::Rectangle<float> (protectedStart[kind], protection.getY(), protectedEnd[kind] - protectedStart[kind], protection.getHeight())
            : juce::Rectangle<float> (protectedStart[kind], protection.getY() + (float) kind * row,
                                       protectedEnd[kind] - protectedStart[kind], row);
        if (kind == 0 || kind == 3)
            reasons::drawSwatch (g, area, reasons::Pattern::Hatch, colours::accentAmber);
        else if (kind == 1)
        {
            g.setColour (colours::warning);
            for (float y = area.getY() + 2.0f; y < area.getBottom(); y += 4.0f)
                for (float x = area.getX(); x < area.getRight(); x += 4.0f)
                    g.fillRect (x, y, 1.0f, 1.0f);
        }
        else
            reasons::drawSwatch (g, area, reasons::Pattern::DashedEdge, colours::secondary);
        protectedRun[kind] = false;
    };

    for (const auto& column : displayFrames)
    {
        const auto x = xOf (column.position);
        const auto width = juce::jmax (1.0f, xOf (column.position + column.periodSamples) - x);
        const auto amplitude = juce::jlimit (0.0f, 1.0f, column.peak) * audio.getHeight() * 0.38f;
        g.setColour (colours::secondary.withAlpha (0.65f));
        g.fillRect (x, audio.getCentreY() - amplitude, width, amplitude * 2.0f);
        g.setColour (colours::text.withAlpha (0.65f));
        const auto energyY = audio.getBottom() - juce::jlimit (0.0f, 1.0f, column.energy) * audio.getHeight();
        g.fillRect (x, energyY, width, 1.0f);

        const auto confY = gap.getBottom() - juce::jlimit (0.0f, 1.0f, column.confidence) * gap.getHeight() * 0.92f - 2.0f;
        g.setColour (colours::accent);
        if (previous != nullptr && ! column.incompleteBefore && (column.flags & VizFrame::flagDiscontinuity) == 0)
        {
            const auto previousY = gap.getBottom() - juce::jlimit (0.0f, 1.0f, previous->confidence) * gap.getHeight() * 0.92f - 2.0f;
            g.drawLine (xOf (previous->position), previousY, x, confY, 1.3f);
        }
        if ((column.flags & VizFrame::flagGapEligible) != 0)
        {
            g.setColour (colours::accent.withAlpha (0.15f));
            g.fillRect (x, gap.getY(), width, gap.getHeight());
        }

        for (int kind = 0; kind < 4; ++kind)
        {
            const bool active = kind == 3
                ? column.protectionKinds == 0 && (column.flags & VizFrame::flagProtected) != 0
                : (column.protectionKinds & (1u << kind)) != 0;
            if (! active)
            {
                flushProtection (kind);
                continue;
            }
            if (! protectedRun[kind] || column.incompleteBefore)
            {
                flushProtection (kind);
                protectedStart[kind] = x;
                protectedRun[kind] = true;
            }
            protectedEnd[kind] = x + width;
        }
        if ((column.flags & VizFrame::flagLookahead) != 0)
        {
            g.setColour (colours::secondary);
            g.drawLine (x, protection.getCentreY(), x + width, protection.getCentreY(), 1.0f);
        }

        if (! bands.isEmpty())
            for (int band = 0; band < 3; ++band)
            {
                const auto laneHeight = bands.getHeight() / 3.0f;
                const auto bottom = bands.getY() + (float) (band + 1) * laneHeight;
                const auto height = juce::jlimit (0.0f, 1.0f, column.bandConfidence[band]) * (laneHeight - 2.0f);
                g.setColour (band == 0 ? colours::primary : band == 1 ? colours::secondary : colours::warning);
                g.fillRect (x, bottom - height, width, height);
            }

        if (column.incompleteBefore)
        {
            const auto from = previous != nullptr ? xOf (previous->position + previous->periodSamples) : x - 8.0f;
            reasons::drawSwatch (g, { from, audio.getY(), juce::jmax (8.0f, x - from), protection.getBottom() - audio.getY() },
                                reasons::Pattern::Hatch, colours::warning);
        }
        if ((column.flags & VizFrame::flagDiscontinuity) != 0)
        {
            g.setColour (colours::text);
            const float dashes[] { 2.0f, 3.0f };
            g.drawDashedLine ({ x, audio.getY(), x, plot.getBottom() - 14.0f }, dashes, 2, 1.0f);
        }

        if (column.ppqTick != INT32_MIN)
        {
            havePpq = true;
            const auto beatTicks = column.timeSigDenominator > 0 ? 384.0 / column.timeSigDenominator : 96.0;
            const auto beat = (int) std::floor ((double) column.ppqTick / beatTicks);
            const auto oldBeat = previous != nullptr && previous->ppqTick != INT32_MIN
                ? (int) std::floor ((double) previous->ppqTick / beatTicks) : beat - 1;
            if (beat != oldBeat)
            {
                g.setColour (colours::textMuted.withAlpha (0.45f));
                g.drawVerticalLine ((int) x, audio.getY(), plot.getBottom() - 14.0f);
                if (column.timeSigNumerator > 0 && column.timeSigDenominator > 0 && beat % column.timeSigNumerator == 0)
                {
                    g.setFont (fonts::mono (9.0f));
                    g.drawText (juce::String (beat / column.timeSigNumerator + 1),
                                juce::Rectangle<float> (x + 2.0f, audio.getY(), 40.0f, 11.0f),
                                juce::Justification::centredLeft, false);
                }
            }
        }
        previous = &column;
    }
    for (int kind = 0; kind < 4; ++kind)
        flushProtection (kind);

    if (! bands.isEmpty())
    {
        g.setFont (fonts::label (8.5f));
        g.setColour (colours::text);
        const char* names[] { "LOW", "MID", "HIGH" };
        for (int band = 0; band < 3; ++band)
            g.drawText (names[band], juce::Rectangle<float> (bands.getX() + 4.0f,
                        bands.getY() + (float) band * bands.getHeight() / 3.0f, 40.0f, bands.getHeight() / 3.0f),
                        juce::Justification::centredLeft, false);
    }

    if (! havePpq)
    {
        g.setColour (colours::textMuted);
        g.setFont (fonts::ui (9.0f));
        g.drawText ("No host tempo - time only", audio.reduced (4.0f), juce::Justification::topRight, true);
    }
}

} // namespace gapfill::timeline
