// Editor slots (docs/dev/02-FOUNDATIONS.md F0-9).

#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "gui/SlotHost.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    std::unique_ptr<juce::Component> named (const char* name)
    {
        auto c = std::make_unique<juce::Component> (name);
        return c;
    }

    // Registered at start-up in slots no shipped feature uses.
    const SlotRegistrar testB ({ Slot::HistoryDrawerExtra, 20, [] (GapFillAudioProcessor&) { return named ("second"); } });
    const SlotRegistrar testA ({ Slot::HistoryDrawerExtra, 10, [] (GapFillAudioProcessor&) { return named ("first"); } });
}

GF_TEST (Slots, factories_are_sorted_by_order_not_registration)
{
    const auto f = getSlotFactories (Slot::HistoryDrawerExtra);
    GF_EXPECT (f.size() == 2);
    GF_EXPECT (f[0].order == 10 && f[1].order == 20);
}

GF_TEST (Slots, host_lays_components_out_left_to_right_with_8px_spacing)
{
    GapFillAudioProcessor proc;
    juce::Component parent;
    parent.setBounds (0, 0, 400, 100);
    SlotHost host (proc, parent);

    GF_EXPECT (host.getNumComponents (Slot::HistoryDrawerExtra) == 2);
    GF_EXPECT (host.getNumComponents (Slot::FileMenuExtra) == 0);

    host.setSlotBounds (Slot::HistoryDrawerExtra, { 10, 20, 208, 30 });

    juce::Component* first = nullptr;
    juce::Component* second = nullptr;

    for (auto* c : parent.getChildren())
    {
        if (c->getName() == "first")  first = c;
        if (c->getName() == "second") second = c;
    }

    GF_EXPECT (first != nullptr && second != nullptr);
    GF_EXPECT (first->getBounds() == juce::Rectangle<int> (10, 20, 100, 30));
    GF_EXPECT (second->getBounds() == juce::Rectangle<int> (118, 20, 100, 30));
    GF_EXPECT (first->isVisible() && second->isVisible());

    host.setSlotVisible (Slot::HistoryDrawerExtra, false);
    GF_EXPECT (! first->isVisible());

    host.setSlotVisible (Slot::HistoryDrawerExtra, true);
    host.setSlotBounds (Slot::HistoryDrawerExtra, {});
    GF_EXPECT_MSG (! first->isVisible(), "an unreserved (empty) slot hides its components");
}

GF_TEST (Slots, capture_button_is_hosted_in_the_creative_extra_slot)
{
    GapFillAudioProcessor proc;
    juce::Component parent;
    SlotHost host (proc, parent);
    // CAPTURE plus any feature button registered next to it (E-1 adds AUDITION).
    GF_EXPECT (host.getNumComponents (Slot::CreativeExtra) >= 1);
}

GF_TEST (Slots, editor_places_capture_where_the_creative_panel_used_to_and_survives_reopen)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "capture", 0.0f } });

    for (int round = 0; round < 3; ++round)           // editor destroyed/recreated repeatedly
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (1100, 700);

        juce::Component* capture = nullptr;

        for (auto* c : editor.getChildren())
            if (dynamic_cast<GapToggle*> (c) != nullptr)
                capture = c;

        GF_EXPECT_MSG (capture != nullptr, "capture toggle is a direct child of the editor");
        GF_EXPECT (capture->isVisible() && capture->getWidth() > 24 && capture->getHeight() == 24);
    }
}
