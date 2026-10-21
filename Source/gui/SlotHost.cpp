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

SlotLayout defaultLayoutFor (Slot slot) noexcept
{
    switch (slot)
    {
        case Slot::OptionsExtra:   return SlotLayout::Column;
        case Slot::PerformanceTab: return SlotLayout::Stack;
        default:                   return SlotLayout::Row;
    }
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

            const auto height = comp->getHeight() > 0 ? comp->getHeight() : 96;
            parent.addAndMakeVisible (*comp);

            if (defaultLayoutFor ((Slot) i) == SlotLayout::Stack)
                comp->addComponentListener (this);

            entries.push_back ({ (Slot) i, factory.order, std::move (comp), height });
        }
    }
}

SlotHost::~SlotHost()
{
    for (auto& e : entries)
    {
        e.component->removeComponentListener (this);

        if (auto* p = e.component->getParentComponent())
            p->removeChildComponent (e.component.get());
    }
}

void SlotHost::setSlotContainer (Slot slot, juce::Component* container)
{
    containers[(size_t) slot] = container;
    auto* target = container != nullptr ? container : &parent;

    for (auto& e : entries)
        if (e.slot == slot)
            target->addAndMakeVisible (*e.component);

    layout (slot);
}

int SlotHost::getStackHeight (Slot slot) const
{
    if (defaultLayoutFor (slot) != SlotLayout::Stack)
        return 0;

    int total = 0;
    int n = 0;

    for (const auto& e : entries)
        if (e.slot == slot)
        {
            total += e.preferredHeight;
            ++n;
        }

    return n > 0 ? total + spacing * (n - 1) : 0;
}

void SlotHost::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (inLayout || ! wasResized)
        return;

    for (auto& e : entries)
        if (e.component.get() == &c && c.getHeight() > 0 && c.getHeight() != e.preferredHeight)
        {
            // A card folded or unfolded itself: remember the new height and re-stack.
            e.preferredHeight = c.getHeight();
            layout (e.slot);
            return;
        }
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
    const juce::ScopedValueSetter<bool> guard (inLayout, true);

    if (defaultLayoutFor (slot) == SlotLayout::Stack)
    {
        auto y = area.getY();

        for (auto& e : entries)
        {
            if (e.slot != slot)
                continue;

            e.component->setVisible (show);
            e.component->setBounds (area.getX(), y, area.getWidth(), e.preferredHeight);
            y += e.preferredHeight + spacing;
        }

        // A scrolling container is sized to its content; the viewport scrolls it.
        if (auto* container = containers[(size_t) slot])
            container->setSize (area.getWidth(), juce::jmax (1, y - spacing - area.getY()));

        return;
    }

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
