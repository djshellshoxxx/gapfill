#pragma once

// Editor slots (docs/dev/02-FOUNDATIONS.md F0-9, docs/dev/12 F0-10).
//
// PluginEditor::resized() reserves ONE rectangle per slot; features register a
// SlotComponentFactory from their own file and never touch resized().
//
// Slot components:
//  - receive the processor and own their ParameterAttachments, tooltips,
//    accessibility titles and keyboard focus;
//  - must tolerate being created/destroyed at any time (editor re-open);
//  - must NOT touch audio memory: read atomics / processor message-thread
//    state only.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace gapfill
{

class GapFillAudioProcessor;

enum class Slot
{
    TopBarRight, CreativeExtra, PerformanceStrip, VisualiserOverlay, OptionsExtra, FooterStatus,
    PerformanceTab, EffectsSection, FileMenuExtra, HistoryDrawerExtra,   // added by docs/dev/12 F0-10
    Count
};

struct SlotComponentFactory
{
    Slot slot;
    int  order;      ///< sorts within a slot (ascending; keep unique per slot)
    std::function<std::unique_ptr<juce::Component> (GapFillAudioProcessor&)> make;
};

/** Static-registration helper. Append-only at start-up, read-only afterwards. */
void registerSlotComponent (SlotComponentFactory);

struct SlotRegistrar
{
    explicit SlotRegistrar (SlotComponentFactory f) { registerSlotComponent (std::move (f)); }
};

/** Snapshot of the registered factories for one slot, sorted by order. */
std::vector<SlotComponentFactory> getSlotFactories (Slot);

//==============================================================================
/** Owns the slot component instances of one editor and lays them out. */
class SlotHost
{
public:
    /** Instantiates every registered factory and adds the components to `parent`. */
    SlotHost (GapFillAudioProcessor&, juce::Component& parent);
    ~SlotHost();

    /** Reserves `bounds` (in the parent's coordinates) for the slot and lays its
        components out: left-to-right with 8 px spacing, top-to-bottom for
        OptionsExtra. An empty rectangle hides the slot's components. */
    void setSlotBounds (Slot, juce::Rectangle<int> bounds);

    juce::Rectangle<int> getSlotBounds (Slot) const;

    /** Shows or hides all components of a slot (e.g. with the tab they belong to). */
    void setSlotVisible (Slot, bool shouldBeVisible);

    int getNumComponents (Slot) const;

    static constexpr int spacing = 8;

private:
    struct Entry
    {
        Slot slot;
        int order;
        std::unique_ptr<juce::Component> component;
    };

    void layout (Slot);

    juce::Component& parent;
    std::vector<Entry> entries;
    juce::Rectangle<int> bounds[(size_t) Slot::Count];
    bool visible[(size_t) Slot::Count];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotHost)
};

} // namespace gapfill
