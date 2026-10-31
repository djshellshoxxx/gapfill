#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace gapfill::i18n
{
/** Marks displayed user/factory identities and device names, never model/state data.
    Text and combo items in this subtree stay verbatim; static field titles and
    descriptions may still translate. Set before attaching the translator. */
void markUserContent (juce::Component&);
/** Verify the selected typeface's glyphs; use JUCE default sans when necessary. */
juce::Font fontForText (juce::Font preferred, const juce::String& displayed);

/** Message-thread presentation adapter. Stores English originals, retains item
    IDs, purges dead SafePointers and never translates editable user text. The
    English steady-state refresh is O(1); unchanged pseudo fields allocate nothing.
    Own this after the editor's children, and destroy it before those children. */
class ComponentTranslator final : private juce::ComponentListener,
                                  private juce::ChangeListener
{
public:
    explicit ComponentTranslator (juce::Component& root);
    ~ComponentTranslator() override;
    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void componentChildrenChanged (juce::Component&) override;
    void componentBeingDeleted (juce::Component&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComponentTranslator)
};

/** Preserve known user-data item IDs explicitly (e.g. recent preset names).
    Call just before showing a newly built menu. IDs/callbacks are unchanged. */
void translateMenu (juce::PopupMenu&, const std::function<bool(int)>& preserveItem = {});
}
