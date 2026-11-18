#pragma once

// C-2 / F-M23 message-thread half: edits the macro mappings, keeps the rules
// (<= 8 mappings per macro, a target belongs to ONE macro, lower index wins),
// publishes coherent snapshots and owns legacy <=2KiB and opt-in V3 <=8KiB
// subtree codecs. Existing four-macro APIs remain valid; explicit V3 mode
// extends editing indices to0..7 and keeps a complete legacy return snapshot.

#include "MacroEngine.h"
#include "../common/TripleBuffer.h"
#include "../modulation/ExtendedConfig.h"

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
    const modulation::ExtendedConfig& getExtended() const noexcept { return extended; }
    bool isExtendedEnabled() const noexcept { return extendedEnabled; }
    bool enableExtended();
    bool returnToLegacy();
    bool canUndoModeChange() const noexcept { return modeUndoAvailable; }
    bool undoModeChange();

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

    /** Explicitly activates repaired advanced target dispatch for a restored
        legacy configuration. Leaves names, mappings and LFO values intact. */
    bool upgradeTargetSchema();

    /** Which active macro (legacy0..3, opt-inV3 0..7) owns `target`, or -1. */
    int ownerOf (int target) const noexcept;

    /** Targets whose parameter exists in the layout (set once at construction by the owner). */
    void setTargetAvailability (const bool* available) noexcept;
    void setExtendedTargetAvailability (const bool* available) noexcept;
    bool isTargetAvailable (int target) const noexcept;

    TripleBuffer<MacroConfig>& publisher() noexcept  { return pub; }
    TripleBuffer<modulation::ConfigSnapshot>& extendedPublisher() noexcept { return extendedPub; }
    void publish() noexcept;

    //------------------------------------------------------------------------
    static constexpr const char* legacySubtreeName = "Macros";
    static constexpr const char* subtreeName = "MacrosV2";

    juce::ValueTree toValueTree() const;
    /** Unchanged schema2 writer, also used by the V3 retained legacy snapshot. */
    static juce::ValueTree legacyConfigToTree (const MacroConfig&);
    static bool validateTree (const juce::ValueTree&);
    void restore (const juce::ValueTree&);

    /** Printable, trimmed, 1..16 UTF-8 bytes; falls back to `fallback`. */
    static juce::String sanitiseName (const juce::String& raw, const juce::String& fallback);

private:
    static Mapping normalised (Mapping) noexcept;
    Mapping normaliseActive (Mapping) const noexcept;
    modulation::ConfigSnapshot currentSnapshot() const noexcept;
    int activeMacroCount() const noexcept { return extendedEnabled ? modulation::kMacroCount : kNumMacros; }
    auto activeMaps() noexcept -> Mapping (*)[kMaxMappings] { return extendedEnabled ? extended.map : cfg.map; }
    auto activeMaps() const noexcept -> const Mapping (*)[kMaxMappings] { return extendedEnabled ? extended.map : cfg.map; }
    auto activeNames() noexcept -> char (*)[kNameBytes + 1] { return extendedEnabled ? extended.names : cfg.names; }
    auto activeCounts() noexcept -> std::uint8_t* { return extendedEnabled ? extended.count : cfg.count; }
    auto activeCounts() const noexcept -> const std::uint8_t* { return extendedEnabled ? extended.count : cfg.count; }

    MacroConfig cfg;
    TripleBuffer<MacroConfig> pub;
    modulation::ExtendedConfig extended;
    TripleBuffer<modulation::ConfigSnapshot> extendedPub;
    bool extendedEnabled = false;
    modulation::ConfigSnapshot modeUndo; ///< Message-owned exact one-level setup history, never serialized.
    bool modeUndoAvailable = false;
    bool available[modulation::kTargetCount];
};

} // namespace gapfill::macros
