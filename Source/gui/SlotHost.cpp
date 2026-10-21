#include "SlotHost.h"

#include <algorithm>

namespace gapfill
{

namespace
{
    std::vector<SlotComponentFactory>& registry()
    {
        static std::vector<SlotComponentFactory> factories;   // append-only at start-up
        return factories;
    }
}

void registerSlotComponent (SlotComponentFactory f)
{
    registry().push_back (std::move (f));
}

std::vector<SlotComponentFactory> getSlotFactories (Slot slot)
{
    std::vector<SlotComponentFactory> out;

    for (const auto& f : registry())
        if (f.slot == slot)
            out.push_back (f);

    // Static-init order between translation units is unspecified: sort here.
    std::stable_sort (out.begin(), out.end(),
                      [] (const SlotComponentFactory& a, const SlotComponentFactory& b) { return a.order < b.order; });
    return out;
}

//==============================================================================
SlotHost::SlotHost (GapFillAudioProcessor& proc, juce::Component& parentComponent)
    : parent (parentComponent)
{
    for (auto& v : visible)
        v = true;

    for (int i = 0; i < (int) Slot::Count; ++i)
    {
        for (const auto& factory : getSlotFactories ((Slot) i))
        {
            if (factory.make == nullptr)
                continue;

            auto comp = factory.make (proc);

            if (comp == nullptr)
                continue;

            parent.addAndMakeVisible (*comp);
            entries.push_back ({ (Slot) i, factory.order, std::move (comp) });
        }
    }
}

SlotHost::~SlotHost()
{
    for (auto& e : entries)
        parent.removeChildComponent (e.component.get());
}

void SlotHost::setSlotBounds (Slot slot, juce::Rectangle<int> newBounds)
{
    bounds[(size_t) slot] = newBounds;
    layout (slot);
}

juce::Rectangle<int> SlotHost::getSlotBounds (Slot slot) const
{
    return bounds[(size_t) slot];
}

void SlotHost::setSlotVisible (Slot slot, bool shouldBeVisible)
{
    visible[(size_t) slot] = shouldBeVisible;
    layout (slot);
}

int SlotHost::getNumComponents (Slot slot) const
{
    int n = 0;

    for (const auto& e : entries)
        if (e.slot == slot)
            ++n;

    return n;
}

void SlotHost::layout (Slot slot)
{
    std::vector<juce::Component*> comps;

    for (const auto& e : entries)
        if (e.slot == slot)
            comps.push_back (e.component.get());

    if (comps.empty())
        return;

    const auto area = bounds[(size_t) slot];
    const bool show = visible[(size_t) slot] && ! area.isEmpty();
    const auto n = (int) comps.size();
    const bool vertical = (slot == Slot::OptionsExtra);

    const auto total = vertical ? area.getHeight() : area.getWidth();
    const auto each = juce::jmax (0, (total - spacing * (n - 1)) / n);
    auto pos = vertical ? area.getY() : area.getX();

    for (auto* c : comps)
    {
        c->setVisible (show);

        if (vertical)
            c->setBounds (area.getX(), pos, area.getWidth(), each);
        else
            c->setBounds (pos, area.getY(), each, area.getHeight());

        pos += each + spacing;
    }
}

} // namespace gapfill
