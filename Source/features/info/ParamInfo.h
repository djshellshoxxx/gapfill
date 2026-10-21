#pragma once

// U-9 parameter info registry (docs/dev/05 U-9, docs/dev/12 F0-1).
//
// One entry per parameter id: short name, a one-line description (<= 90 chars),
// the typical range and the unit. Every feature registers its own entries from
// Source/features/<f>/Info.cpp with a static ParamInfoRegistrar, exactly like the
// Help sections; the table is never a shared hot file. The core parameters are
// registered in CoreInfo.cpp.
//
// Tooltips of every GapKnob / GapCombo / GapToggle / GapBar are generated from
// the entry ("Density - How often an eligible gap gets filled. Typical: 40-70 %"),
// and the status line shows "Name value (base -> effective)" for the control
// being hovered or dragged.
//
// Contract: message thread only after start-up (registration happens during
// static initialisation, single threaded). The registry never owns audio state.

#include <juce_core/juce_core.h>

#include <functional>
#include <initializer_list>
#include <optional>
#include <vector>

namespace gapfill::info
{

inline constexpr int kMaxOneLineLength = 90;

struct ParamInfo
{
    const char* id;            ///< permanent parameter id
    const char* shortName;     ///< "Density"
    const char* oneLine;       ///< <= 90 chars, sentence case, no trailing full stop needed
    const char* typicalRange;  ///< "40-70" (may be empty)
    const char* unit;          ///< "%", "ms", "Hz", "dB" or ""
};

class ParamInfoRegistry
{
public:
    static ParamInfoRegistry& get();

    void add (const ParamInfo&);

    /** Null when the id has no entry. */
    const ParamInfo* find (const juce::String& id) const noexcept;

    const std::vector<ParamInfo>& all() const noexcept   { return entries; }

    /** Optional "effective value" provider. A feature that modulates a parameter
        (macros, personality, breathing ...) installs one; the status line then shows
        "(base -> effective)" while the two differ. Returning nullopt = no modulation. */
    using EffectiveProvider = std::function<std::optional<float> (const juce::String& id)>;
    void setEffectiveProvider (EffectiveProvider p)   { effective = std::move (p); }
    std::optional<float> effectiveValue (const juce::String& id) const;

private:
    std::vector<ParamInfo> entries;
    EffectiveProvider effective;
};

struct ParamInfoRegistrar
{
    ParamInfoRegistrar (std::initializer_list<ParamInfo> list)
    {
        for (const auto& i : list)
            ParamInfoRegistry::get().add (i);
    }
};

/** "Density - How often an eligible gap gets filled. Typical: 40-70 %". */
juce::String tooltipFor (const ParamInfo&);

/** Tooltip for a parameter id, or `fallback` when it has no entry. */
juce::String tooltipFor (const juce::String& id, const juce::String& fallback);

/** Ids in `allParameterIDs()` without an entry (coverage test and CI helper). */
juce::StringArray idsMissingInfo();

} // namespace gapfill::info
