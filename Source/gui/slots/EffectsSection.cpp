#include "EffectsSection.h"

#include "../SlotHost.h"
#include "../HelpRegistry.h"

namespace gapfill::fxgui
{

namespace
{
    constexpr int kHeaderHeight = 28;
    constexpr int kRowHeight = 96;
    constexpr int kPad = 8;

    int cellWidth (ControlDef::Kind k)
    {
        switch (k)
        {
            case ControlDef::Kind::Knob:   return 72;
            case ControlDef::Kind::Combo:  return 120;
            case ControlDef::Kind::Toggle: return 96;
        }

        return 72;
    }

    void setParameterReal (juce::AudioProcessorValueTreeState& state, const juce::String& id, float realValue)
    {
        if (auto* p = state.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (realValue));
            p->endChangeGesture();
        }
    }

    float getParameterReal (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        if (auto* raw = state.getRawParameterValue (id))
            return raw->load();

        return 0.0f;
    }
}

const std::vector<CardDef>& cardDefinitions()
{
    using K = ControlDef::Kind;

    static const std::vector<CardDef> defs =
    {
        { fx::CardId::Swell, "Reverse Swell", "swellAmount",
          "A mode of Reverse: the fill is played backwards, crescendos and brightens into the landing point.",
          (int) Algo::Reverse,
          { { K::Combo, "swellCurve",  "Curve",  "Shape of the build-up: Linear, Exponential, S-curve or Late." },
            { K::Knob,  "swellFilter", "Filter", "How far the low-pass opens while the swell builds. 0 leaves the colour alone." },
            { K::Combo, "swellTarget", "Target", "Where the swell peaks: the end of the gap, the next beat or the next bar." } } },

        { fx::CardId::Cloud, "Granular Cloud", "cloudAmount",
          "A mode of Granular: a dense cloud of grains scattered over recent audio.",
          (int) Algo::Granular,
          { { K::Knob, "cloudDensity", "Density", "Grains per second." },
            { K::Knob, "cloudSize",    "Size",    "Length of each grain." },
            { K::Knob, "cloudSpan",    "Span",    "How far back in time the grains are taken from." },
            { K::Knob, "cloudJitter",  "Jitter",  "0 takes every grain from the middle of the span, 100 spreads them over all of it.", true },
            { K::Knob, "cloudPitch",   "Pitch",   "Random pitch spread of each grain, in cents either way.", true },
            { K::Knob, "cloudSpread",  "Spread",  "Random stereo placement of each grain.", true } } },

        { fx::CardId::Ratchet, "Ratchet", "ratAmount",
          "Re-triggers the start of a fill in speeding up or slowing down repeats.",
          -1,
          { { K::Combo, "ratDiv",     "Division", "Length of the first repeat, in musical time." },
            { K::Knob,  "ratRepeats", "Repeats",  "Number of repeats, including the first." },
            { K::Knob,  "ratAccel",   "Accel",    "Positive speeds the repeats up, negative slows them down." },
            { K::Knob,  "ratGate",    "Gate",     "How much of each repeat is audible." } } },

        { fx::CardId::TapeDestr, "Tape Destruction", "tdAmount",
          "Wow, flutter, saturation, dropouts and splices. Amount is the overall depth.",
          -1,
          { { K::Knob, "tdWow",     "Wow",     "Slow pitch drift." },
            { K::Knob, "tdFlutter", "Flutter", "Fast pitch wobble." },
            { K::Knob, "tdSat",     "Sat",     "Tape saturation." },
            { K::Knob, "tdDropout", "Dropout", "How often the signal drops out." },
            { K::Knob, "tdSplice",  "Splice",  "How often the tape jumps." } } },

        { fx::CardId::RCrush, "Rhythmic Crush", "rcAmount",
          "Bit and rate reduction that can hold in musical time. Amount blends it with the dry fill.",
          -1,
          { { K::Knob,  "rcBits", "Bits", "Bit depth." },
            { K::Knob,  "rcRate", "Rate", "Sample-hold factor (used when Hold is Free)." },
            { K::Combo, "rcHold", "Hold", "Hold the sample for a musical division, following an 8-step pattern." },
            { K::Knob,  "rcTone", "Tone", "Negative darkens, positive thins out the low end." } } },
    };

    return defs;
}

//==============================================================================
EffectCardView::EffectCardView (GapFillAudioProcessor& p, const CardDef& d)
    : proc (p), def (d)
{
    auto& state = proc.getValueTreeState();

    led.setClickingTogglesState (false);
    led.setButtonText ("");
    led.setTooltip (juce::String ("Switch ") + def.title + " on or off. Off means Amount 0.");
    led.setTitle (juce::String (def.title) + " enable");
    led.onClick = [this] { toggleEnabled(); };
    addAndMakeVisible (led);

    fold.setButtonText (def.title);
    fold.setTitle (juce::String (def.title) + " fold");
    fold.setTooltip (def.blurb);
    fold.onClick = [this] { setFolded (! isFolded()); };
    addAndMakeVisible (fold);

    if (def.carrierAlgo >= 0)
    {
        solo.setButtonText ("Solo");
        solo.setTooltip ("Set Fill Type to the algorithm this effect is a mode of, so every fill uses it.");
        solo.onClick = [this] { soloThisEffect(); };
        addAndMakeVisible (solo);
    }

    amountKnob = std::make_unique<GapKnob> (state, def.amountId, "Amount",
                                            "How much of this effect is used. 0 is off.", colours::primary);
    addAndMakeVisible (*amountKnob);

    bool haveAdvanced = false;

    for (const auto& c : def.controls)
    {
        std::unique_ptr<juce::Component> comp;

        switch (c.kind)
        {
            case ControlDef::Kind::Knob:   comp = std::make_unique<GapKnob> (state, c.paramId, c.label, c.tooltip, colours::primary); break;
            case ControlDef::Kind::Combo:  comp = std::make_unique<GapCombo> (state, c.paramId, c.label, c.tooltip); break;
            case ControlDef::Kind::Toggle: comp = std::make_unique<GapToggle> (state, c.paramId, c.label, c.tooltip); break;
        }

        addAndMakeVisible (*comp);
        controls.push_back (std::move (comp));
        haveAdvanced = haveAdvanced || c.advanced;
    }

    if (haveAdvanced)
    {
        more.setButtonText ("More");
        more.setClickingTogglesState (true);
        more.setTooltip ("Show the remaining controls of this effect.");
        more.onClick = [this] { showMore = more.getToggleState(); updateVisibility(); if (auto* parent = getParentComponent()) parent->resized(); };
        addAndMakeVisible (more);
    }

    readout.setColour (juce::Label::textColourId, colours::textDim);
    readout.setFont (juce::Font (juce::FontOptions (11.0f)));
    readout.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (readout);

    setTitle (def.title);
    refresh();
}

bool EffectCardView::isEnabledCard() const
{
    return getParameterReal (proc.getValueTreeState(), def.amountId) > 0.05f;
}

bool EffectCardView::isFolded() const
{
    return proc.getFxUiState().folded[(int) def.card].load (std::memory_order_relaxed);
}

void EffectCardView::setFolded (bool shouldFold)
{
    proc.getFxUiState().folded[(int) def.card].store (shouldFold, std::memory_order_relaxed);
    updateVisibility();

    if (auto* parent = getParentComponent())
        parent->resized();
}

void EffectCardView::setEnabledCard (bool shouldEnable)
{
    auto& state = proc.getValueTreeState();
    auto& ui = proc.getFxUiState();
    const auto current = getParameterReal (state, def.amountId);

    if (shouldEnable == (current > 0.05f))
        return;

    if (shouldEnable)
    {
        const auto remembered = ui.rememberedAmount[(int) def.card].load (std::memory_order_relaxed);
        setParameterReal (state, def.amountId, remembered > 0.0f ? remembered : fx::FxUiState::kDefaultEnableAmount);
    }
    else
    {
        ui.rememberedAmount[(int) def.card].store (current, std::memory_order_relaxed);
        setParameterReal (state, def.amountId, 0.0f);
    }

    refresh();
}

void EffectCardView::toggleEnabled()
{
    setEnabledCard (! isEnabledCard());
}

void EffectCardView::soloThisEffect()
{
    if (def.carrierAlgo >= 0)
        setParameterReal (proc.getValueTreeState(), "fillType", (float) (def.carrierAlgo + 1));
}

bool EffectCardView::bodyVisible() const
{
    return isEnabledCard() && ! isFolded();
}

void EffectCardView::updateVisibility()
{
    const bool body = bodyVisible();
    amountKnob->setVisible (body);
    more.setVisible (body && more.getButtonText().isNotEmpty());
    solo.setVisible (def.carrierAlgo >= 0);

    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setVisible (body && (! def.controls[i].advanced || showMore));

    readout.setVisible (isEnabledCard());
    resized();
    repaint();
}

void EffectCardView::refresh()
{
    const bool on = isEnabledCard();
    led.setToggleState (on, juce::dontSendNotification);

    juce::String text;
    const auto& m = proc.getFxMeters();

    if (def.card == fx::CardId::Swell && on)
    {
        if (m.swellNoTransport.load (std::memory_order_relaxed))
            text = "Swell: no transport - landing at gap end";
        else if (m.swellLandingBar.load (std::memory_order_relaxed) > 0)
            text = "Last landing: bar " + juce::String (m.swellLandingBar.load (std::memory_order_relaxed)) + "."
                     + juce::String (m.swellLandingBeat.load (std::memory_order_relaxed));
    }
    else if (def.card == fx::CardId::Cloud && on)
    {
        const auto n = m.cloudGrains.load (std::memory_order_relaxed);

        if (n > 0)
            text = "~" + juce::String (n) + " grains" + (m.cloudLimited.load (std::memory_order_relaxed) ? ", density limited" : "");
    }
    else if (def.card == fx::CardId::Ratchet && on && m.tempoFallback.load (std::memory_order_relaxed))
    {
        text = "No transport - using the fallback tempo";
    }

    if (text != readout.getText())
        readout.setText (text, juce::dontSendNotification);

    if (bodyVisible() != amountKnob->isVisible())
        updateVisibility();
}

int EffectCardView::getPreferredHeight (int width) const
{
    int h = kHeaderHeight;

    if (! bodyVisible())
        return h + (isEnabledCard() && readout.getText().isNotEmpty() ? 18 : 0);

    int x = kPad, rows = 1;
    x += cellWidth (ControlDef::Kind::Knob);                         // Amount

    for (size_t i = 0; i < def.controls.size(); ++i)
    {
        if (def.controls[i].advanced && ! showMore)
            continue;

        const auto w = cellWidth (def.controls[i].kind);

        if (x + w + kPad > width)
        {
            ++rows;
            x = kPad;
        }

        x += w;
    }

    return h + rows * kRowHeight + (readout.getText().isNotEmpty() ? 18 : 0) + kPad;
}

void EffectCardView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight);

    led.setBounds (header.removeFromLeft (28).reduced (6));

    if (more.isVisible() || more.getButtonText().isNotEmpty())
        more.setBounds (header.removeFromRight (56).reduced (2));

    if (def.carrierAlgo >= 0)
        solo.setBounds (header.removeFromRight (56).reduced (2));

    fold.setBounds (header);

    if (! bodyVisible())
    {
        readout.setBounds (area.withHeight (18).reduced (kPad, 0));
        return;
    }

    int x = kPad, y = kHeaderHeight;
    amountKnob->setBounds (x, y, cellWidth (ControlDef::Kind::Knob), kRowHeight);
    x += cellWidth (ControlDef::Kind::Knob);

    for (size_t i = 0; i < controls.size(); ++i)
    {
        if (def.controls[i].advanced && ! showMore)
            continue;

        const auto w = cellWidth (def.controls[i].kind);

        if (x + w + kPad > getWidth())
        {
            x = kPad;
            y += kRowHeight;
        }

        controls[i]->setBounds (x, y, w, kRowHeight);
        x += w;
    }

    readout.setBounds (kPad, getHeight() - kPad - 18, getWidth() - 2 * kPad, 18);
}

void EffectCardView::paint (juce::Graphics& g)
{
    g.setColour (colours::edge);
    g.fillRect (0, 0, getWidth(), 1);

    // The LED: lit while the effect is on.
    const auto r = led.getBounds().toFloat().reduced (4.0f);
    g.setColour (isEnabledCard() ? colours::success : colours::edge);
    g.fillEllipse (r.withSizeKeepingCentre (8.0f, 8.0f));
}

//==============================================================================
EffectsSection::EffectsSection (GapFillAudioProcessor& p)
    : proc (p)
{
    setTitle ("Effects");
    setWantsKeyboardFocus (false);

    for (const auto& def : cardDefinitions())
    {
        cards.push_back (std::make_unique<EffectCardView> (proc, def));
        content.addAndMakeVisible (*cards.back());
    }

    // F-L14: which spectral lanes feed the effects.
    routingLabel.setText ("Effects on lanes", juce::dontSendNotification);
    routingLabel.setColour (juce::Label::textColourId, colours::textDim);
    routingLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    content.addAndMakeVisible (routingLabel);

    const struct { const char* id; const char* name; } lanes[] = { { "bandLowFx", "LOW" }, { "bandMidFx", "MID" }, { "bandHighFx", "HIGH" } };

    for (const auto& l : lanes)
    {
        routing.push_back (std::make_unique<GapToggle> (proc.getValueTreeState(), l.id, l.name,
                                                        juce::String ("Let fills in the ") + l.name
                                                          + " lane use the effects. Off: that lane stays dry of effects."));
        content.addAndMakeVisible (*routing.back());
    }

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    startTimerHz (10);
}

EffectsSection::~EffectsSection()
{
    stopTimer();
}

void EffectsSection::timerCallback()
{
    bool changed = false;

    for (auto& c : cards)
    {
        const auto before = c->getPreferredHeight (content.getWidth());
        c->refresh();
        changed = changed || before != c->getPreferredHeight (content.getWidth());
    }

    if (changed)
        layoutCards();
}

void EffectsSection::layoutCards()
{
    const auto w = juce::jmax (200, viewport.getMaximumVisibleWidth());
    int y = 0;

    for (auto& c : cards)
    {
        const auto h = c->getPreferredHeight (w);
        c->setBounds (0, y, w, h);
        y += h;
    }

    routingLabel.setBounds (kPad, y + 4, 120, 24);

    int x = kPad + 124;

    for (auto& r : routing)
    {
        r->setBounds (x, y + 4, 80, 28);
        x += 84;
    }

    y += 40;
    content.setSize (w, y);
}

void EffectsSection::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutCards();
}

void EffectsSection::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

//==============================================================================
namespace
{
    const SlotRegistrar effectsSection ({ Slot::EffectsSection, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<EffectsSection> (proc);
        } });

    // Help: orders 4000-4099 belong to the voice-effects package.
    const HelpRegistrar helpEffects (4000, "Effects", [] (HelpWriter& w)
    {
        w.body ("Effects are optional extras that shape the generated fill material. Each one is off at Amount 0 "
                "and sounds exactly like GapFill without it. The Effects section lists them in the order the audio "
                "passes through them: Reverse Swell and Granular Cloud (modes of two fill types), then Ratchet, "
                "Tape Destruction and Rhythmic Crush. Controls appear once an effect is switched on. "
                "Effects only ever process the generated wet signal; your dry signal and the protection of hits "
                "are untouched.");
        w.body ("The switch next to a name sets Amount to 0 and remembers it. Solo sets Fill Type to the algorithm an effect is a mode of.");
    });

    const HelpRegistrar helpSwell (4010, "Reverse Swell", [] (HelpWriter& w)
    {
        w.body ("Reverse Swell is a mode of the Reverse fill type: the sound just before the hit that opened the gap is "
                "played backwards so it rises and brightens into a landing point, then stops short of the next hit. "
                "Amount is the chance that a Reverse fill becomes a swell. Raise the Reverse weight, or press Solo, to hear it more often.");
        w.entry ("Curve", "Linear, Exponential, S-curve or Late: how the level builds.");
        w.entry ("Filter", "How far a low-pass opens during the swell. 0 keeps the colour constant.");
        w.entry ("Target", "Gap end, the next beat or the next bar. Beat and bar need the host transport; without it the swell lands at the end of the gap and the card says so.");
    });

    const HelpRegistrar helpCloud (4020, "Granular Cloud", [] (HelpWriter& w)
    {
        w.body ("Granular Cloud is a mode of the Granular fill type with direct control of the grains. "
                "Amount is the chance that a Granular fill becomes a cloud. The cloud is limited to 48 grains at once "
                "and the card shows when the density was lowered to stay inside that.");
        w.entry ("Density / Size", "Grains per second and grain length.");
        w.entry ("Span", "How far back in time grains are taken from. While Capture is on, the captured sound is used.");
        w.entry ("Jitter", "0 takes every grain from the middle of the span; 100 spreads them evenly over it.");
        w.entry ("Pitch / Spread", "Random pitch (in cents, either way) and stereo position of each grain.");
    });

    const HelpRegistrar helpRatchet (4030, "Ratchet", [] (HelpWriter& w)
    {
        w.body ("Ratchet re-triggers the start of a fill several times, speeding up or slowing down. "
                "Amount is the chance that a fill is ratcheted. The repeats follow the host tempo; without a transport a 120 BPM fallback is used and the card says so. "
                "Repeats never run past the end of the fill, so the next hit stays protected.");
        w.entry ("Division", "Length of the first repeat.");
        w.entry ("Repeats", "Number of repeats, including the first.");
        w.entry ("Accel", "Positive makes each repeat shorter, negative longer. The shortest repeat is 8 ms.");
        w.entry ("Gate", "How much of each repeat sounds.");
    });

    const HelpRegistrar helpTape (4040, "Tape Destruction", [] (HelpWriter& w)
    {
        w.body ("Tape Destruction adds wow, flutter, saturation, dropouts and splices to a fill. It works with any fill type "
                "(Tape Stop is a separate fill type). Amount scales every control and blends the effect with the dry fill. "
                "The random events are seeded from the fill, so a fill with a given Seed repeats exactly.");
    });

    const HelpRegistrar helpCrush (4050, "Rhythmic Crush", [] (HelpWriter& w)
    {
        w.body ("Rhythmic Crush reduces bit depth and sample rate of the fill and can hold the sample for a musical division, "
                "stepping through a fixed 8-step pattern. Aliasing is part of the sound. This is separate from the Bit Crush fill type "
                "and the Crush modifier. Amount blends the crushed sound with the dry fill.");
        w.entry ("Hold", "Free uses the Rate control; a division holds each sample for that length (at most 4096 samples).");
        w.entry ("Tone", "Negative values darken, positive values remove low end.");
    });

    const HelpRegistrar helpBands (4060, "Effects per lane", [] (HelpWriter& w)
    {
        w.body ("In Spectral detection each fill belongs to a LOW, MID or HIGH lane. The LOW / MID / HIGH switches under the effects "
                "choose which lanes use them. A lane that is switched off stays free of effects; fills that cover the whole spectrum always use them.");
    });
}

} // namespace gapfill::fxgui
