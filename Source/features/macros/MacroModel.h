#pragma once

// C-2 / F-M23 message-thread half: edits the macro mappings, keeps the rules
// (<= 8 mappings per macro, a target belongs to ONE macro, lower index wins),
// publishes snapshots to the audio thread and owns the <Macros> subtree codec
// (<= 2 KiB, compact text; any violation drops the whole subtree).

#include "MacroEngine.h"
#include "../common/TripleBuffer.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace gapfill::macros
{

class MacroModel
{
public:
    enum class AddResult { Ok, Full, Invalid, Claimed, Unavailable };

    MacroModel();

    const MacroConfig& get() const noexcept          { return cfg; }

    /** Adds a mapping to `macro`. `Claimed`: another macro already owns the
        target (its index goes to `owner`). `Unavailable`: the target parameter
        does not exist in this build (F-M23 targets whose feature has not landed). */
    AddResult addMapping (int macro, Mapping, int* owner = nullptr);
    bool removeMapping (int macro, int index);
    bool editMapping (int macro, int index, Mapping);      ///< keeps the target, re-validates the values
    bool rename (int macro, const juce::String& name);
    void resetMacro (int macro);
    bool setLfo (int macro, LfoSettings);
    void resetAll();

    /** Which macro (0..3) owns `target`, or -1. */
    int ownerOf (int target) const noexcept;

    /** Targets whose parameter exists in the layout (set once at construction by the owner). */
    void setTargetAvailability (const bool* available) noexcept;
    bool isTargetAvailable (int target) const noexcept;

    TripleBuffer<MacroConfig>& publisher() noexcept  { return pub; }
    void publish() noexcept                          { pub.publish (cfg); }

    //------------------------------------------------------------------------
    static constexpr const char* subtreeName = "Macros";

    juce::ValueTree toValueTree() const;
    static bool validateTree (const juce::ValueTree&);
    void restore (const juce::ValueTree&);

    /** Printable, trimmed, 1..16 UTF-8 bytes; falls back to `fallback`. */
    static juce::String sanitiseName (const juce::String& raw, const juce::String& fallback);

private:
    static Mapping normalised (Mapping) noexcept;

    MacroConfig cfg;
    TripleBuffer<MacroConfig> pub;
    bool available[kNumTargets];
};

} // namespace gapfill::macros
