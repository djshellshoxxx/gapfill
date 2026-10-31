#include "MacroModel.h"
#include "../modulation/ExtendedState.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gapfill::macros
{

namespace
{
    int toPermille (float v) noexcept        { return (int) std::lround (std::clamp (std::isfinite (v) ? v : 0.0f, 0.0f, 1.0f) * 1000.0f); }
    float fromPermille (int v) noexcept      { return (float) v * 0.001f; }

    bool exactInteger (const juce::var& value, int expected)
    {
        if (value.isString()) return value.toString() == juce::String (expected);
        if (! value.isInt() && ! value.isInt64() && ! value.isDouble()) return false;
        return (double) value == (double) expected;
    }

    /** Parses a comma separated integer tuple of `expected` (or, when `minimum` < expected, at
        least `minimum`) values; missing trailing values keep what the caller pre-filled. */
    bool parseInts (const juce::String& text, int expected, int* out, int minimum = -1)
    {
        const auto parts = juce::StringArray::fromTokens (text, ",", "");

        if (parts.size() > expected || parts.size() < (minimum < 0 ? expected : minimum))
            return false;

        for (int i = 0; i < parts.size(); ++i)
        {
            const auto p = parts[i].trim();

            const auto digits = p.startsWithChar ('-') ? p.substring (1) : p;
            if (p.isEmpty() || digits.isEmpty() || ! digits.containsOnly ("0123456789") || p.length() > 9)
                return false;

            out[i] = p.getIntValue();
        }

        return true;
    }

    struct ParsedMacro
    {
        int index = -1;
        juce::String name;
        std::uint8_t count = 0;
        Mapping map[kMaxMappings];
        LfoSettings lfo;
    };

    bool parseMacro (const juce::ValueTree& n, ParsedMacro& out)
    {
        const auto idx = (int) n.getProperty ("i", -1);

        if (idx < 0 || idx >= kNumMacros)
            return false;

        out.index = idx;
        out.name = n.getProperty ("n").toString();

        if (MacroModel::sanitiseName (out.name, {}) != out.name || out.name.isEmpty())
            return false;

        const auto maps = n.getProperty ("m").toString();
        out.count = 0;

        if (maps.isNotEmpty())
        {
            if (maps.startsWithChar (';') || maps.endsWithChar (';') || maps.contains (";;")) return false;
            const auto entries = juce::StringArray::fromTokens (maps, ";", "");

            if (entries.size() > kMaxMappings)
                return false;

            for (const auto& e : entries)
            {
                int v[6] = { 0, 0, 1000, 0, 0, 0 };         // C-2 tuples (target,min,max,curve) omit smoothing and bipolar

                if (! parseInts (e, 6, v, 4))
                    return false;

                if (v[0] < 0 || v[0] >= kNumTargets || v[1] < 0 || v[1] > 1000 || v[2] < 0 || v[2] > 1000
                    || v[3] < 0 || v[3] > 3 || v[4] < 0 || v[4] > 500 || (v[5] != 0 && v[5] != 1))
                    return false;

                auto& m = out.map[out.count++];
                m.target = (std::uint8_t) v[0];
                m.min01 = fromPermille (v[1]);
                m.max01 = fromPermille (v[2]);
                m.curve = (std::uint8_t) v[3];
                m.smoothMs = (float) v[4];
                m.bipolar = (std::uint8_t) v[5];
            }
        }

        out.lfo = {};
        const auto l = n.getProperty ("l").toString();

        if (l.isNotEmpty())
        {
            int v[7];

            if (! parseInts (l, 7, v))
                return false;

            if ((v[0] != 0 && v[0] != 1) || v[1] < 0 || v[1] > 4 || (v[2] != 0 && v[2] != 1) || v[3] < 0
                || v[3] >= (int) Division::NumDivisions || v[4] < 1 || v[4] > 2000 || v[5] < 0 || v[5] > 1000
                || v[6] < 0 || v[6] > 360)
                return false;

            out.lfo.enabled = (std::uint8_t) v[0];
            out.lfo.shape = (std::uint8_t) v[1];
            out.lfo.tempoSync = (std::uint8_t) v[2];
            out.lfo.division = (std::uint8_t) v[3];
            out.lfo.rateHz = (float) v[4] * 0.01f;
            out.lfo.depth01 = fromPermille (v[5]);
            out.lfo.phaseDeg = (float) v[6];
        }

        return true;
    }
}

//==============================================================================
MacroModel::MacroModel()
{
    cfg = defaultConfig();
    extended = modulation::extendedFromLegacy (cfg);

    for (auto& a : available)
        a = true;

    publish();
}

void MacroModel::setTargetAvailability (const bool* a) noexcept
{
    for (int i = 0; i < kNumTargets; ++i)
        available[i] = a[i];
}

void MacroModel::setExtendedTargetAvailability (const bool* a) noexcept
{
    for (int target = 0; target < modulation::kTargetCount; ++target) available[target] = a[target];
}

bool MacroModel::isTargetAvailable (int target) const noexcept
{
    return target >= 0 && target < (extendedEnabled ? modulation::kTargetCount : kNumTargets) && available[target];
}

void MacroModel::publish() noexcept
{
    pub.publish (cfg);
    extendedPub.publish (currentSnapshot());
}

modulation::ConfigSnapshot MacroModel::currentSnapshot() const noexcept
{
    modulation::ConfigSnapshot snapshot;
    snapshot.enabled = extendedEnabled ? 1 : 0;
    snapshot.config = extendedEnabled ? extended : modulation::extendedFromLegacy (cfg);
    return snapshot;
}

bool MacroModel::undoModeChange()
{
    if (! modeUndoAvailable) return false;
    const auto previous = modeUndo;
    modeUndo = currentSnapshot();
    extendedEnabled = previous.enabled != 0;
    extended = previous.config;
    cfg = previous.config.retainedLegacy;
    publish();
    return true;
}

bool MacroModel::enableExtended()
{
    if (extendedEnabled) return false;
    modeUndo = currentSnapshot();
    modeUndoAvailable = true;
    extended = modulation::extendedFromLegacy (cfg);
    extendedEnabled = true;
    publish();
    return true;
}

bool MacroModel::returnToLegacy()
{
    if (! extendedEnabled) return false;
    modeUndo = currentSnapshot();
    modeUndoAvailable = true;
    cfg = extended.retainedLegacy;
    extendedEnabled = false;
    publish();
    return true;
}

juce::String MacroModel::sanitiseName (const juce::String& raw, const juce::String& fallback)
{
    juce::String clean;

    for (auto c : raw.trim())
        if (c >= 32 && c != 127)
            clean += c;

    clean = clean.trim();

    while (clean.getNumBytesAsUTF8() > (size_t) kNameBytes && clean.isNotEmpty())
        clean = clean.dropLastCharacters (1);

    return clean.isEmpty() ? fallback : clean;
}

Mapping MacroModel::normalised (Mapping m) noexcept
{
    m.target = (std::uint8_t) std::min<int> (m.target, kNumTargets - 1);
    m.curve = (std::uint8_t) std::min<int> (m.curve, 3);
    m.bipolar = m.bipolar != 0 ? 1 : 0;
    m.pad = 0;
    m.min01 = fromPermille (toPermille (m.min01));
    m.max01 = fromPermille (toPermille (m.max01));
    m.smoothMs = (float) std::lround (std::clamp (std::isfinite (m.smoothMs) ? m.smoothMs : 0.0f, 0.0f, 500.0f));
    return m;
}

Mapping MacroModel::normaliseActive (Mapping mapping) const noexcept
{
    if (! extendedEnabled) return normalised (mapping);
    mapping.curve = (std::uint8_t) std::min<int> (mapping.curve, 3);
    mapping.bipolar = mapping.bipolar != 0 ? 1 : 0;
    mapping.pad = 0;
    const auto bounded = [] (float value, float high)
    { return std::clamp (std::isfinite (value) ? value : 0.0f, 0.0f, high); };
    mapping.min01 = bounded (mapping.min01, 1);
    mapping.max01 = bounded (mapping.max01, 1);
    mapping.smoothMs = bounded (mapping.smoothMs, 500);
    return mapping;
}

int MacroModel::ownerOf (int target) const noexcept
{
    for (int m = 0; m < activeMacroCount(); ++m)
        for (int j = 0; j < activeCounts()[m]; ++j)
            if (activeMaps()[m][j].target == target)
                return m;

    return -1;
}

MacroModel::AddResult MacroModel::addMapping (int macro, Mapping m, int* owner)
{
    if (macro < 0 || macro >= activeMacroCount() || m.target >= (extendedEnabled ? modulation::kTargetCount : kNumTargets))
        return AddResult::Invalid;

    if (! isTargetAvailable (m.target))
        return AddResult::Unavailable;

    if (activeCounts()[macro] >= kMaxMappings)
        return AddResult::Full;

    if (const auto o = ownerOf (m.target); o >= 0)
    {
        if (owner != nullptr)
            *owner = o;

        return AddResult::Claimed;
    }

    activeMaps()[macro][activeCounts()[macro]++] = normaliseActive (m);
    publish();
    return AddResult::Ok;
}

bool MacroModel::removeMapping (int macro, int index)
{
    if (macro < 0 || macro >= activeMacroCount() || index < 0 || index >= activeCounts()[macro])
        return false;

    for (int j = index; j < activeCounts()[macro] - 1; ++j)
        activeMaps()[macro][j] = activeMaps()[macro][j + 1];

    activeMaps()[macro][--activeCounts()[macro]] = Mapping {};
    publish();
    return true;
}

bool MacroModel::editMapping (int macro, int index, Mapping m)
{
    if (macro < 0 || macro >= activeMacroCount() || index < 0 || index >= activeCounts()[macro])
        return false;

    m.target = activeMaps()[macro][index].target;
    activeMaps()[macro][index] = normaliseActive (m);
    publish();
    return true;
}

bool MacroModel::rename (int macro, const juce::String& name)
{
    if (macro < 0 || macro >= activeMacroCount())
        return false;

    const auto defaults = modulation::extendedFromLegacy (defaultConfig());
    const auto clean = sanitiseName (name, juce::String (defaults.names[macro]));
    std::memset (activeNames()[macro], 0, sizeof (activeNames()[macro]));
    std::memcpy (activeNames()[macro], clean.toRawUTF8(), std::min<size_t> (kNameBytes, clean.getNumBytesAsUTF8()));
    publish();
    return true;
}

void MacroModel::resetMacro (int macro)
{
    if (macro < 0 || macro >= activeMacroCount())
        return;

    const auto d = modulation::extendedFromLegacy (defaultConfig());
    std::memcpy (activeNames()[macro], d.names[macro], sizeof (activeNames()[macro]));
    if (! extendedEnabled) cfg.lfo[macro] = d.retainedLegacy.lfo[macro];
    activeCounts()[macro] = 0;
    activeMaps()[macro][0] = Mapping {};

    for (int j = 0; j < kMaxMappings; ++j)
        activeMaps()[macro][j] = Mapping {};

    // Re-add the factory mappings the other macros do not already own.
    for (int j = 0; j < d.count[macro]; ++j)
        if (ownerOf (d.map[macro][j].target) < 0)
            activeMaps()[macro][activeCounts()[macro]++] = d.map[macro][j];

    publish();
}

bool MacroModel::setLfo (int macro, LfoSettings l)
{
    if (extendedEnabled || macro < 0 || macro >= kNumMacros)
        return false;

    l.enabled = l.enabled != 0 ? 1 : 0;
    l.shape = (std::uint8_t) std::min<int> (l.shape, 4);
    l.tempoSync = l.tempoSync != 0 ? 1 : 0;
    l.division = (std::uint8_t) std::min<int> (l.division, (int) Division::NumDivisions - 1);
    l.rateHz = (float) std::lround (std::clamp (std::isfinite (l.rateHz) ? l.rateHz : 1.0f, 0.01f, 20.0f) * 100.0f) * 0.01f;
    l.depth01 = fromPermille (toPermille (l.depth01));
    l.phaseDeg = (float) std::lround (std::clamp (std::isfinite (l.phaseDeg) ? l.phaseDeg : 0.0f, 0.0f, 360.0f));

    cfg.lfo[macro] = l;                 // one LFO per macro: at most 4 in total
    publish();
    return true;
}

void MacroModel::resetAll()
{
    modeUndoAvailable = false;
    extendedEnabled = false;
    cfg = defaultConfig();
    extended = modulation::extendedFromLegacy (cfg);
    publish();
}

bool MacroModel::upgradeTargetSchema()
{
    if (extendedEnabled || cfg.targetSchemaVersion == 2) return false;
    cfg.targetSchemaVersion = 2;
    publish();
    return true;
}

//==============================================================================
juce::ValueTree MacroModel::toValueTree() const
{
    if (extendedEnabled) return modulation::toExtendedTree (extended);
    return legacyConfigToTree (cfg);
}

juce::ValueTree MacroModel::legacyConfigToTree (const MacroConfig& config)
{
    // The public snapshot codec must bound raw POD input before names/array access
    // and before integer conversion; normal model edits already satisfy these rules.
    for (int macro = 0; macro < kNumMacros; ++macro)
    {
        if (config.count[macro] > kMaxMappings || config.names[macro][kNameBytes] != 0) return {};
        const auto& lfo = config.lfo[macro];
        if (! std::isfinite (lfo.rateHz) || lfo.rateHz < .01f || lfo.rateHz > 20
            || ! std::isfinite (lfo.depth01) || lfo.depth01 < 0 || lfo.depth01 > 1
            || ! std::isfinite (lfo.phaseDeg) || lfo.phaseDeg < 0 || lfo.phaseDeg > 360) return {};
        for (int index = 0; index < config.count[macro]; ++index)
        {
            const auto smooth = config.map[macro][index].smoothMs;
            if (! std::isfinite (smooth) || smooth < 0 || smooth > 500) return {};
        }
    }

    juce::ValueTree t (subtreeName);
    t.setProperty ("v", 2, nullptr);
    t.setProperty ("targets", (int) config.targetSchemaVersion, nullptr);

    for (int m = 0; m < kNumMacros; ++m)
    {
        juce::ValueTree n ("M");
        n.setProperty ("i", m, nullptr);
        n.setProperty ("n", juce::String::fromUTF8 (config.names[m]), nullptr);

        juce::StringArray maps;

        for (int j = 0; j < config.count[m]; ++j)
        {
            const auto& mp = config.map[m][j];
            maps.add (juce::String (mp.target) + "," + juce::String (toPermille (mp.min01)) + "," + juce::String (toPermille (mp.max01))
                      + "," + juce::String ((int) mp.curve) + "," + juce::String ((int) std::lround (mp.smoothMs))
                      + "," + juce::String ((int) mp.bipolar));
        }

        n.setProperty ("m", maps.joinIntoString (";"), nullptr);

        const auto& l = config.lfo[m];
        n.setProperty ("l", juce::String ((int) l.enabled) + "," + juce::String ((int) l.shape) + "," + juce::String ((int) l.tempoSync)
                            + "," + juce::String ((int) l.division) + "," + juce::String ((int) std::lround (l.rateHz * 100.0f))
                            + "," + juce::String (toPermille (l.depth01)) + "," + juce::String ((int) std::lround (l.phaseDeg)), nullptr);
        t.appendChild (n, nullptr);
    }

    return t;
}

bool MacroModel::validateTree (const juce::ValueTree& t)
{
    if (t.hasType (modulation::kSubtreeName)) return modulation::validateExtendedTree (t);
    const bool legacy = t.hasType (legacySubtreeName);
    const auto version = t.getProperty ("v", legacy ? 1 : 0);
    const auto targets = t.getProperty ("targets", 0);
    if ((! legacy && ! t.hasType (subtreeName)) || t.getNumChildren() > kNumMacros
        || (legacy ? ! exactInteger (version, 1)
                   : ! exactInteger (version, 2) || (! exactInteger (targets, 1) && ! exactInteger (targets, 2))))
        return false;

    // Match the project subtree registry's XML budget, including escaping.
    if (t.toXmlString().getNumBytesAsUTF8() > 2 * 1024) return false;

    bool seen[kNumMacros] {};

    for (const auto& n : t)
    {
        ParsedMacro p;

        if (! n.hasType ("M") || ! parseMacro (n, p) || seen[p.index])
            return false;

        seen[p.index] = true;
    }

    return true;
}

void MacroModel::restore (const juce::ValueTree& t)
{
    if (t.isValid() && ! validateTree (t)) return;  // no partial direct restore
    modeUndoAvailable = false;
    if (t.hasType (modulation::kSubtreeName))
    {
        modulation::ExtendedConfig next;
        if (! modulation::fromExtendedTree (t, next)) return;
        bool claimed[modulation::kTargetCount] {};
        for (int macro = 0; macro < modulation::kMacroCount; ++macro)
        {
            int kept = 0;
            for (int index = 0; index < next.count[macro]; ++index)
            {
                const auto mapping = next.map[macro][index];
                if (claimed[mapping.target]) continue;
                claimed[mapping.target] = true;
                next.map[macro][kept++] = mapping;
            }
            for (int index = kept; index < kMaxMappings; ++index) next.map[macro][index] = {};
            next.count[macro] = (std::uint8_t) kept;
        }
        extended = next;
        cfg = next.retainedLegacy;
        extendedEnabled = true;
        publish();
        return;
    }
    extendedEnabled = false;
    cfg = defaultConfig();

    if (t.isValid())
    {
        cfg.targetSchemaVersion = t.hasType (legacySubtreeName) ? 1 : (std::uint8_t) (int) t["targets"];
        bool claimed[kNumTargets] {};

        for (const auto& n : t)
        {
            ParsedMacro p;

            if (n.hasType ("M") && parseMacro (n, p))
            {
                std::memset (cfg.names[p.index], 0, sizeof (cfg.names[p.index]));
                std::memcpy (cfg.names[p.index], p.name.toRawUTF8(), p.name.getNumBytesAsUTF8());
                cfg.count[p.index] = 0;
                cfg.lfo[p.index] = p.lfo;

                for (int j = 0; j < kMaxMappings; ++j)
                    cfg.map[p.index][j] = Mapping {};

                for (int j = 0; j < p.count; ++j)
                    cfg.map[p.index][cfg.count[p.index]++] = p.map[j];
            }
        }

        // A target belongs to one macro: the lower index wins, later claims are dropped.
        for (int m = 0; m < kNumMacros; ++m)
        {
            int kept = 0;
            Mapping keep[kMaxMappings];

            for (int j = 0; j < cfg.count[m]; ++j)
            {
                const auto target = cfg.map[m][j].target;

                if (! claimed[target])
                {
                    claimed[target] = true;
                    keep[kept++] = cfg.map[m][j];
                }
            }

            for (int j = 0; j < kMaxMappings; ++j)
                cfg.map[m][j] = j < kept ? keep[j] : Mapping {};

            cfg.count[m] = (std::uint8_t) kept;
        }
    }

    publish();
}

} // namespace gapfill::macros
