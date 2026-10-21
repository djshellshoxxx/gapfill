// C-0 Effective-value display on knobs (docs/dev/04-SPEC-CREATIVE.md C-0).

#include "../support/TestRig.h"

#include "gui/Knob.h"

using namespace gapfill;
using namespace gftest;

GF_TEST (EffectiveDisplay, ids_map_to_real_parameters_and_back)
{
    GapFillAudioProcessor proc;

    for (int i = 0; i < EffectiveCreative::numControls; ++i)
    {
        const auto* id = effectiveControlId (i);
        GF_EXPECT (id != nullptr);
        GF_EXPECT_MSG (proc.getValueTreeState().getParameter (id) != nullptr, id);
        GF_EXPECT (effectiveControlIndex (id) == i);
    }

    GF_EXPECT (effectiveControlId (EffectiveCreative::numControls) == nullptr);
    GF_EXPECT (effectiveControlIndex ("mix") == -1);
    GF_EXPECT (effectiveControlIndex (nullptr) == -1);
}

GF_TEST (EffectiveDisplay, processor_publishes_the_block_stage_values_each_block)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (! proc.getEffectiveCreative().valid.load());
    setParams (proc, { { "fill", 61.0f }, { "wStutter", 33.0f } });
    renderOffline (proc, drumLoop (48000.0, 120.0, 1), 512, 48000.0);

    const auto& eff = proc.getEffectiveCreative();
    GF_EXPECT (eff.valid.load());
    GF_EXPECT_NEAR (eff.get (effectiveControlIndex ("fill")), 0.61, 1.0e-4);
    GF_EXPECT_NEAR (eff.get (effectiveControlIndex ("wStutter")), 0.33, 1.0e-4);
}

GF_TEST (EffectiveDisplay, knob_is_untouched_until_an_effective_value_is_set)
{
    GapFillAudioProcessor proc;
    GapKnob knob (proc.getValueTreeState(), "density", "Density", "How often gaps get filled.", colours::primary);

    GF_EXPECT (! knob.getEffectiveValue().has_value());
    GF_EXPECT ((double) knob.getSlider().getProperties()[GapKnob::effectiveProperty] < 0.0);
    GF_EXPECT (knob.getSlider().getDescription() == "How often gaps get filled.");
}

GF_TEST (EffectiveDisplay, setting_and_clearing_the_effective_value_updates_marker_and_description)
{
    GapFillAudioProcessor proc;
    GapKnob knob (proc.getValueTreeState(), "density", "Density", "How often gaps get filled.", colours::primary);

    knob.setEffectiveValue (0.84);
    GF_EXPECT (knob.getEffectiveValue().has_value());
    GF_EXPECT_NEAR (*knob.getEffectiveValue(), 0.84, 1.0e-9);
    GF_EXPECT_NEAR ((double) knob.getSlider().getProperties()[GapKnob::effectiveProperty], 0.84, 1.0e-9);
    GF_EXPECT (knob.getSlider().getDescription().contains ("Effective value"));
    GF_EXPECT (knob.getSlider().getDescription().contains ("84"));

    knob.setEffectiveValue (7.0);    // clamped into 0..1
    GF_EXPECT_NEAR (*knob.getEffectiveValue(), 1.0, 1.0e-9);

    knob.setEffectiveValue (std::nullopt);
    GF_EXPECT (! knob.getEffectiveValue().has_value());
    GF_EXPECT ((double) knob.getSlider().getProperties()[GapKnob::effectiveProperty] < 0.0);
    GF_EXPECT (knob.getSlider().getDescription() == "How often gaps get filled.");
}

GF_TEST (EffectiveDisplay, a_knob_without_a_processor_or_effective_control_never_polls)
{
    // A parameter that is not macro-targetable has no effective index: the timer never starts.
    GapFillAudioProcessor proc;
    GapKnob mix (proc.getValueTreeState(), "wet", "Wet", "Wet level.", colours::primary);
    GF_EXPECT (! mix.getEffectiveValue().has_value());
}
