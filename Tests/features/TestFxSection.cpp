// The Effects section (docs/dev/12 FX-0 GUI) and its help.

#include "../support/TestRig.h"

#include "gui/HelpRegistry.h"
#include "gui/SlotHost.h"
#include "gui/slots/EffectsSection.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    float param (GapFillAudioProcessor& p, const char* id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }
}

GF_TEST (FxSection, every_control_the_cards_show_is_a_real_parameter)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (fxgui::cardDefinitions().size() == 11);

    for (const auto& def : fxgui::cardDefinitions())
    {
        GF_EXPECT_MSG (proc.getValueTreeState().getParameter (def.amountId) != nullptr, def.amountId);

        for (const auto& c : def.controls)
            if (c.kind != fxgui::ControlDef::Kind::Button)            // buttons name actions, not parameters
                GF_EXPECT_MSG (proc.getValueTreeState().getParameter (c.paramId) != nullptr, c.paramId);
    }

    // Chain order of the cards is the permanent order.
    int last = -1;

    for (const auto& def : fxgui::cardDefinitions())
    {
        GF_EXPECT ((int) def.card > last);
        last = (int) def.card;
    }
}

GF_TEST (FxSection, is_registered_in_the_effects_slot_and_hosted)
{
    GF_EXPECT (! getSlotFactories (Slot::EffectsSection).empty());

    GapFillAudioProcessor proc;
    juce::Component parent;
    parent.setBounds (0, 0, 800, 600);
    SlotHost host (proc, parent);
    GF_EXPECT (host.getNumComponents (Slot::EffectsSection) == 1);

    host.setSlotBounds (Slot::EffectsSection, { 10, 10, 640, 400 });
    juce::Component* section = nullptr;

    for (auto* c : parent.getChildren())
        if (dynamic_cast<fxgui::EffectsSection*> (c) != nullptr)
            section = c;

    GF_EXPECT (section != nullptr && section->isVisible() && section->getWidth() == 640);
}

GF_TEST (FxSection, controls_are_hidden_until_the_effect_is_enabled)
{
    GapFillAudioProcessor proc;
    fxgui::EffectsSection section (proc);
    section.setSize (640, 500);

    auto& swell = section.getCard (0);
    const auto collapsed = swell.getPreferredHeight (640);

    GF_EXPECT (! swell.isEnabledCard());

    for (auto* child : swell.getChildren())
        if (dynamic_cast<GapKnob*> (child) != nullptr || dynamic_cast<GapCombo*> (child) != nullptr)
            GF_EXPECT (! child->isVisible());

    swell.setEnabledCard (true);
    GF_EXPECT (swell.isEnabledCard());
    GF_EXPECT_NEAR (param (proc, "swellAmount"), 50.0f, 1.0e-3);      // first enable = 50 %

    swell.setFolded (false);
    GF_EXPECT (swell.getPreferredHeight (640) > collapsed);

    int visibleControls = 0;

    for (auto* child : swell.getChildren())
        if ((dynamic_cast<GapKnob*> (child) != nullptr || dynamic_cast<GapCombo*> (child) != nullptr) && child->isVisible())
            ++visibleControls;

    GF_EXPECT (visibleControls == 4);                                   // Amount + Curve + Filter + Target
}

GF_TEST (FxSection, switching_off_remembers_the_amount_and_on_restores_it)
{
    GapFillAudioProcessor proc;
    fxgui::EffectsSection section (proc);
    section.setSize (640, 500);

    auto& rat = section.getCard (2);
    setParams (proc, { { "ratAmount", 73.0f } });
    GF_EXPECT (rat.isEnabledCard());

    rat.setEnabledCard (false);
    GF_EXPECT (param (proc, "ratAmount") == 0.0f);
    GF_EXPECT_NEAR (proc.getFxUiState().rememberedAmount[(int) fx::CardId::Ratchet].load(), 73.0f, 1.0e-3);

    rat.setEnabledCard (true);
    GF_EXPECT_NEAR (param (proc, "ratAmount"), 73.0f, 0.05);
}

GF_TEST (FxSection, fold_state_persists_in_the_state)
{
    GapFillAudioProcessor a;
    {
        fxgui::EffectsSection section (a);
        auto& tape = section.getCard (3);
        GF_EXPECT (tape.isFolded());                                    // folded by default
        tape.setFolded (false);
    }

    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    fxgui::EffectsSection section (b);
    GF_EXPECT (! section.getCard (3).isFolded());
    GF_EXPECT (section.getCard (4).isFolded());
}

GF_TEST (FxSection, solo_sets_the_carrier_algorithm)
{
    GapFillAudioProcessor proc;
    fxgui::EffectsSection section (proc);
    section.setSize (640, 500);

    for (auto* child : section.getCard (0).getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child))
            if (b->getButtonText() == "Solo")
                b->onClick();

    GF_EXPECT (param (proc, "fillType") == (float) ((int) Algo::Reverse + 1));

    for (auto* child : section.getCard (1).getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child))
            if (b->getButtonText() == "Solo")
                b->onClick();

    GF_EXPECT (param (proc, "fillType") == (float) ((int) Algo::Granular + 1));
}

GF_TEST (FxSection, status_lines_come_from_the_meters)
{
    GapFillAudioProcessor proc;
    fxgui::EffectsSection section (proc);
    setParams (proc, { { "swellAmount", 100.0f }, { "cloudAmount", 100.0f } });

    auto& m = const_cast<fx::FxMeters&> (proc.getFxMeters());
    m.swellNoTransport.store (true);
    section.getCard (0).refresh();
    GF_EXPECT (section.getCard (0).getReadout().contains ("no transport"));

    m.swellNoTransport.store (false);
    m.swellLandingBar.store (17);
    m.swellLandingBeat.store (1);
    section.getCard (0).refresh();
    GF_EXPECT (section.getCard (0).getReadout().contains ("bar 17.1"));

    m.cloudGrains.store (40);
    m.cloudLimited.store (true);
    section.getCard (1).refresh();
    GF_EXPECT (section.getCard (1).getReadout().contains ("limited"));
}

GF_TEST (FxSection, help_sections_are_registered_in_the_feature_band)
{
    int found = 0;

    for (const auto& s : HelpRegistry::getSections())
        if (s.order >= 4000 && s.order < 4100)
            ++found;

    GF_EXPECT (found >= 8);          // overview, swell, cloud, ratchet, tape, crush, lanes, scrub
}

GF_TEST (FxSection, scrub_controls_are_hosted_in_the_performance_strip)
{
    GF_EXPECT (! getSlotFactories (Slot::PerformanceStrip).empty());

    GapFillAudioProcessor proc;
    juce::Component parent;
    parent.setBounds (0, 0, 800, 200);
    SlotHost host (proc, parent);
    host.setSlotBounds (Slot::PerformanceStrip, { 0, 0, 400, 100 });

    int toggles = 0, knobs = 0;

    for (auto* c : parent.getChildren())
    {
        for (auto* inner : c->getChildren())
        {
            toggles += dynamic_cast<GapToggle*> (inner) != nullptr;
            knobs += dynamic_cast<GapKnob*> (inner) != nullptr;
        }
    }

    GF_EXPECT (toggles >= 1 && knobs >= 2);
}
