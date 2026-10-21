#include "Gui.h"

#include "../envelope/Envelope.h"
#include "../../Presets.h"
#include "../../gui/HelpRegistry.h"
#include "../../gui/SlotHost.h"

namespace gapfill::shapegui
{

namespace
{
    constexpr int kPad = 10;
    constexpr int kGap = 6;
    constexpr int kTitleHeight = 16;
    constexpr int kKnobW = 72, kKnobH = 80;
    constexpr int kComboW = 132, kComboH = 40;
    constexpr int kToggleW = 120, kToggleH = 28;

    juce::Rectangle<int> sizeOf (Cell::Kind k)
    {
        switch (k)
        {
            case Cell::Kind::Knob:   return { 0, 0, kKnobW, kKnobH };
            case Cell::Kind::Combo:  return { 0, 0, kComboW, kComboH };
            case Cell::Kind::Toggle: return { 0, 0, kToggleW, kToggleH };
        }

        return { 0, 0, kKnobW, kKnobH };
    }

    juce::String captureText (const CaptureLatch& c)
    {
        switch (c.getStatus())
        {
            case CaptureLatch::Held:    return "Capture: held (" + juce::String (c.getHeldMs() / 1000.0, 1) + " s)";
            case CaptureLatch::Pending: return "Capture: pending " + juce::String (juce::roundToInt (c.getProgress() * 100.0f)) + " %";
            default:                    return "Capture: off";
        }
    }
}

const std::vector<Group>& groups()
{
    using K = Cell::Kind;

    static const std::vector<Group> table =
    {
        { "CHARACTER", false,
          { { K::Combo, "personality", "Personality", "Tilts which fills are chosen and nudges Density, Complexity, Chaos and Intensity." },
            { K::Combo, "evolve", "Evolve", "Slowly drifts the fill character towards Surprise styles." } } },

        { "FILL SHAPE", true,
          { { K::Combo, "fillShape", "Shape", "How a fill develops over its own length." },
            { K::Knob,  "fillShapeAmount", "Amount", "How strongly the shape is applied; 0 = off." },
            { K::Knob,  "mistakes", "Mistakes", "Chance of one small human flaw per fill." } } },

        { "DENSITY AND PHRASE", true,
          { { K::Toggle, "densityBreath", "Breathing", "Swells Density up and down over a cycle of bars." },
            { K::Combo,  "densityBreathBars", "Cycle", "Length of one breathing cycle in bars." },
            { K::Knob,   "densityBreathDepth", "Depth", "Size of the Density swing." },
            { K::Combo,  "densityBreathCurve", "Curve", "Shape of the swing." },
            { K::Combo,  "phraseBars", "Phrase", "Length of your musical phrase; the last bar gets more fills." },
            { K::Knob,   "phraseBias", "Bias", "How much more likely a fill is in the last bar of the phrase." },
            { K::Knob,   "phraseOffset", "Offset", "Shifts where the phrase starts, in bars." },
            { K::Combo,  "fillBudget", "Fills / bar", "Most fills allowed to start in one bar." },
            { K::Combo,  "quietZone", "Quiet zone", "Part of the bar where no fill may start." } } },

        { "TIMING", true,
          { { K::Combo,  "fillQuant", "Quantise length", "Rounds fill length down to whole note values while tempo is known." },
            { K::Toggle, "modSync", "Sync mods", "Locks tremolo, pan motion and scramble slices to the tempo." } } },

        { "CAPTURE", true,
          { { K::Knob, "captureLength", "Length", "How much audio before the freeze point a latched Capture may use." } } },
    };

    return table;
}

//==============================================================================
ShapePreview::ShapePreview (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setTitle ("Fill shape preview");
    setDescription ("Curve of level over the length of a fill for the selected Fill Shape.");
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}

ShapePreview::~ShapePreview() { stopTimer(); }

void ShapePreview::timerCallback()
{
    const auto shape = (int) std::lround (state.getRawParameterValue ("fillShape")->load());
    const auto amount = state.getRawParameterValue ("fillShapeAmount")->load();

    if (shape != lastShape || std::abs (amount - lastAmount) > 0.01f)
    {
        lastShape = shape;
        lastAmount = amount;
        repaint();
    }
}

void ShapePreview::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    const auto shape = lastShape < 0 ? 0 : lastShape;
    const auto a = juce::jlimit (0.0f, 1.0f, lastAmount * 0.01f);
    const float knots[envelope::numKnots] { 0.2f, 0.9f, 0.4f, 0.7f, 0.5f };   // a representative Random Walk

    juce::Path p;
    const auto inner = b.reduced (4.0f);

    for (int i = 0; i <= 48; ++i)
    {
        const auto t = (float) i / 48.0f;
        const auto level = envelope::levelFor (envelope::shapeAt (shape, t, knots), a);
        const auto x = inner.getX() + t * inner.getWidth();
        const auto y = inner.getBottom() - level * inner.getHeight();

        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    g.setColour (colours::primary);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

//==============================================================================
ShapePanel::ShapePanel (GapFillAudioProcessor& p) : proc (p), preview (p.getValueTreeState())
{
    setTitle ("Shaping controls");
    setDescription ("Personality, evolve, fill shape, mistakes, breathing, phrase, fill budget, timing and capture controls.");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    auto& state = p.getValueTreeState();

    for (const auto& g : groups())
    {
        Placed pl { &g, {} };

        for (const auto& c : g.cells)
        {
            std::unique_ptr<juce::Component> comp;

            switch (c.kind)
            {
                case Cell::Kind::Knob:   comp = std::make_unique<GapKnob> (state, c.paramId, c.label, c.tooltip, colours::primary); break;
                case Cell::Kind::Combo:  comp = std::make_unique<GapCombo> (state, c.paramId, c.label, c.tooltip); break;
                case Cell::Kind::Toggle: comp = std::make_unique<GapToggle> (state, c.paramId, c.label, c.tooltip); break;
            }

            addAndMakeVisible (*comp);
            pl.controls.push_back (std::move (comp));
        }

        placed.push_back (std::move (pl));
    }

    addAndMakeVisible (preview);

    evolveLabel.setFont (fonts::ui (11.0f));
    evolveLabel.setColour (juce::Label::textColourId, colours::textMuted);
    evolveLabel.setTitle ("Evolve waypoint");
    addAndMakeVisible (evolveLabel);

    captureStatus.setFont (fonts::ui (11.0f));
    captureStatus.setColour (juce::Label::textColourId, colours::textMuted);
    captureStatus.setTitle ("Capture status");
    addAndMakeVisible (captureStatus);

    recapture.setTitle ("Recapture");
    recapture.setTooltip ("Refreshes the frozen Capture material with the latest live audio while Capture stays on.");
    recapture.onClick = [this] { proc.postCommand ({ CommandType::Recapture }); };
    addAndMakeVisible (recapture);

    release.setTitle ("Release capture");
    release.setTooltip ("Switches Capture off and returns to live audio; cancels a pending recapture.");
    release.onClick = [this]
    {
        proc.postCommand ({ CommandType::ReleaseCapture });

        if (auto* cap = proc.getValueTreeState().getParameter ("capture"))
        {
            cap->beginChangeGesture();
            cap->setValueNotifyingHost (0.0f);
            cap->endChangeGesture();
        }
    };
    addAndMakeVisible (release);

    advanced = proc.getUiState().getViewMode() == ui::ViewMode::Advanced;
    applyViewMode();
    timerCallback();
    startTimerHz (10);
}

ShapePanel::~ShapePanel() { stopTimer(); }

void ShapePanel::timerCallback()
{
    const auto wantAdvanced = proc.getUiState().getViewMode() == ui::ViewMode::Advanced;

    if (wantAdvanced != advanced)
    {
        advanced = wantAdvanced;
        applyViewMode();
    }

    const auto profile = proc.getFillEngine().getDecisionTelemetry().evolveProfile.load();
    const auto evolveOn = (int) std::lround (proc.getValueTreeState().getRawParameterValue ("evolve")->load()) > 0;
    const juce::String text = evolveOn ? (profile >= 0 ? juce::String ("-> ") + surpriseTargets (profile).name
                                                       : juce::String ("needs a playing transport"))
                                       : juce::String();

    if (evolveLabel.getText() != text)
        evolveLabel.setText (text, juce::dontSendNotification);

    const auto status = captureText (proc.getShaping().capture);

    if (captureStatus.getText() != status)
        captureStatus.setText (status, juce::dontSendNotification);
}

void ShapePanel::applyViewMode()
{
    advanced = proc.getUiState().getViewMode() == ui::ViewMode::Advanced;

    for (auto& pl : placed)
        for (size_t i = 0; i < pl.controls.size(); ++i)
        {
            // Evolve belongs to the Character group but is an Advanced control.
            const bool isEvolve = juce::String (pl.group->cells[i].paramId) == "evolve";
            pl.controls[i]->setVisible (advanced || (! pl.group->advancedOnly && ! isEvolve));
        }

    preview.setVisible (advanced);
    evolveLabel.setVisible (advanced);
    captureStatus.setVisible (advanced);
    recapture.setVisible (advanced);
    release.setVisible (advanced);
    setSize (kContentWidth, getPreferredHeight());
    resized();
}

int ShapePanel::getNumVisibleControls() const
{
    int n = 0;

    for (const auto& pl : placed)
        for (const auto& c : pl.controls)
            n += c->isVisible() ? 1 : 0;

    return n;
}

int ShapePanel::layoutGroups (bool apply)
{
    int y = kPad;
    const int right = kContentWidth - kPad;

    for (auto& pl : placed)
    {
        if (pl.group->advancedOnly && ! advanced)
            continue;

        // Each entry: the component (may be null when only measuring) and its size.
        struct Item { juce::Component* c; int w, h; };
        std::vector<Item> items;

        for (size_t i = 0; i < pl.controls.size(); ++i)
        {
            const auto isEvolve = juce::String (pl.group->cells[i].paramId) == "evolve";

            if (isEvolve && ! advanced)
                continue;

            const auto s = sizeOf (pl.group->cells[i].kind);
            items.push_back ({ pl.controls[i].get(), s.getWidth(), s.getHeight() });
        }

        const juce::String title (pl.group->title);

        if (title == "CHARACTER" && advanced)
            items.push_back ({ &evolveLabel, 200, 24 });
        else if (title == "FILL SHAPE")
            items.push_back ({ &preview, 140, kKnobH - 20 });
        else if (title == "CAPTURE")
        {
            items.push_back ({ &recapture, 96, 26 });
            items.push_back ({ &release, 86, 26 });
            items.push_back ({ &captureStatus, 200, 24 });
        }

        if (apply)
        {
            // The group title is painted at y; controls start below it.
            titleY.push_back (y);
        }

        y += kTitleHeight;
        int x = kPad, rowH = 0;

        for (auto& it : items)
        {
            if (x + it.w > right && x > kPad)
            {
                x = kPad;
                y += rowH + kGap;
                rowH = 0;
            }

            if (apply)
                it.c->setBounds (x, y, it.w, it.h);

            x += it.w + kGap;
            rowH = juce::jmax (rowH, it.h);
        }

        y += rowH + kPad;
    }

    return y;
}

int ShapePanel::getPreferredHeight() const
{
    return const_cast<ShapePanel*> (this)->layoutGroups (false);
}

void ShapePanel::resized()
{
    titleY.clear();
    layoutGroups (true);
}

void ShapePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::textMuted);
    g.setFont (fonts::sectionHeader (11.0f));

    size_t n = 0;

    for (const auto& pl : placed)
    {
        if (pl.group->advancedOnly && ! advanced)
            continue;

        if (n < titleY.size())
            g.drawText (pl.group->title, kPad, titleY[n], kContentWidth - 2 * kPad, kTitleHeight, juce::Justification::centredLeft);

        ++n;
    }
}

//==============================================================================
namespace
{
    class ShapeButton : public juce::TextButton
    {
    public:
        explicit ShapeButton (GapFillAudioProcessor& p) : juce::TextButton ("SHAPE"), proc (p)
        {
            setTitle ("Shaping controls");
            setTooltip ("Personality, evolve, fill shape, mistakes, breathing, phrase, fill budget, timing and capture length.");
            onClick = [this]
            {
                auto panel = std::make_unique<ShapePanel> (proc);
                panel->setSize (ShapePanel::kContentWidth, panel->getPreferredHeight());

                if (auto* top = getTopLevelComponent())
                {
                    const auto area = top->getLocalArea (this, getLocalBounds());
                    juce::CallOutBox::launchAsynchronously (std::move (panel), area, top);
                }
            };
        }

    private:
        GapFillAudioProcessor& proc;
    };

    const SlotRegistrar shapeButton ({ Slot::CreativeExtra, 120,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<ShapeButton> (proc);
        } });

    //==========================================================================
    /** E-2 Options row: the tempo used when the host provides none. */
    class TempoOption : public juce::Component,
                        private juce::Timer
    {
    public:
        explicit TempoOption (GapFillAudioProcessor& p) : proc (p)
        {
            setTitle ("Tempo when host provides none");

            label.setText ("Tempo when host provides none", juce::dontSendNotification);
            label.setFont (fonts::label (10.0f));
            label.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (label);

            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            slider.setRange (sync::kMinFallbackBpm, sync::kMaxFallbackBpm, 1.0);
            slider.setTitle ("Fallback tempo");
            slider.setTooltip ("Tempo used for synced lengths when the host reports none (always in Standalone). "
                               "A tempo the host reported earlier is used first.");
            slider.setTextValueSuffix (" BPM");
            slider.setValue (proc.getShaping().tempo.getFallbackBpm(), juce::dontSendNotification);
            slider.onValueChange = [this] { proc.getShaping().tempo.setFallbackBpm (slider.getValue()); };
            addAndMakeVisible (slider);

            hint.setFont (fonts::ui (10.0f));
            hint.setColour (juce::Label::textColourId, colours::textMuted);
            addAndMakeVisible (hint);

            setSize (400, 52);
            startTimerHz (2);
            timerCallback();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            hint.setBounds (r.removeFromBottom (14));
            slider.setBounds (r);
        }

        juce::Slider& getSlider() noexcept { return slider; }
        juce::String getHint() const { return hint.getText(); }

    private:
        void timerCallback() override
        {
            const bool standalone = juce::JUCEApplicationBase::isStandaloneApp();
            const bool hostTempo = proc.getShaping().tempo.hostTempoSeenRecently (juce::jmax (8000.0, proc.getSampleRate()));
            slider.setEnabled (standalone || ! hostTempo);
            hint.setText (standalone ? "Standalone has no host tempo, so synced lengths use this."
                                     : (hostTempo ? "Your host provides the tempo; this is only a fallback."
                                                  : "No host tempo seen recently: synced lengths use this."),
                          juce::dontSendNotification);
        }

        GapFillAudioProcessor& proc;
        juce::Label label, hint;
        juce::Slider slider;
    };

    const SlotRegistrar tempoOption ({ Slot::OptionsExtra, 260,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<TempoOption> (proc);
        } });
}

} // namespace gapfill::shapegui
