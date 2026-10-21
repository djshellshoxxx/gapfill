#include "MacroModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gapfill::macros
{

namespace
{
    int toPermille (float v) noexcept        { return (int) std::lround (std::clamp (std::isfinite (v) ? v : 0.0f, 0.0f, 1.0f) * 1000.0f); }
    float fromPermille (int v) noexcept      { return (float) v * 0.001f; }

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

            if (p.isEmpty() || ! p.containsOnly ("0123456789-") || p.length() > 9)
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

    for (auto& a : available)
        a = true;

    publish();
}

void MacroModel::setTargetAvailability (const bool* a) noexcept
{
    for (int i = 0; i < kNumTargets; ++i)
        available[i] = a[i];
}

bool MacroModel::isTargetAvailable (int target) const noexcept
{
    return target >= 0 && target < kNumTargets && available[target];
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

int MacroModel::ownerOf (int target) const noexcept
{
    for (int m = 0; m < kNumMacros; ++m)
        for (int j = 0; j < cfg.count[m]; ++j)
            if (cfg.map[m][j].target == target)
                return m;

    return -1;
}

MacroModel::AddResult MacroModel::addMapping (int macro, Mapping m, int* owner)
{
    if (macro < 0 || macro >= kNumMacros || m.target >= kNumTargets)
        return AddResult::Invalid;

    if (! isTargetAvailable (m.target))
        return AddResult::Unavailable;

    if (cfg.count[macro] >= kMaxMappings)
        return AddResult::Full;

    if (const auto o = ownerOf (m.target); o >= 0)
    {
        if (owner != nullptr)
            *owner = o;

        return AddResult::Claimed;
    }

    cfg.map[macro][cfg.count[macro]++] = normalised (m);
    publish();
    return AddResult::Ok;
}

bool MacroModel::removeMapping (int macro, int index)
{
    if (macro < 0 || macro >= kNumMacros || index < 0 || index >= cfg.count[macro])
        return false;

    for (int j = index; j < cfg.count[macro] - 1; ++j)
        cfg.map[macro][j] = cfg.map[macro][j + 1];

    cfg.map[macro][--cfg.count[macro]] = Mapping {};
    publish();
    return true;
}

bool MacroModel::editMapping (int macro, int index, Mapping m)
{
    if (macro < 0 || macro >= kNumMacros || index < 0 || index >= cfg.count[macro])
        return false;

    m.target = cfg.map[macro][index].target;
    cfg.map[macro][index] = normalised (m);
    publish();
    return true;
}

bool MacroModel::rename (int macro, const juce::String& name)
{
    if (macro < 0 || macro >= kNumMacros)
        return false;

    const auto defaults = defaultConfig();
    const auto clean = sanitiseName (name, juce::String (defaults.names[macro]));
    std::memset (cfg.names[macro], 0, sizeof (cfg.names[macro]));
    std::memcpy (cfg.names[macro], clean.toRawUTF8(), std::min<size_t> (kNameBytes, clean.getNumBytesAsUTF8()));
    publish();
    return true;
}

void MacroModel::resetMacro (int macro)
{
    if (macro < 0 || macro >= kNumMacros)
        return;

    const auto d = defaultConfig();
    std::memcpy (cfg.names[macro], d.names[macro], sizeof (cfg.names[macro]));
    cfg.lfo[macro] = d.lfo[macro];
    cfg.count[macro] = 0;
    cfg.map[macro][0] = Mapping {};

    for (int j = 0; j < kMaxMappings; ++j)
        cfg.map[macro][j] = Mapping {};

    // Re-add the factory mappings the other macros do not already own.
    for (int j = 0; j < d.count[macro]; ++j)
        if (ownerOf (d.map[macro][j].target) < 0)
            cfg.map[macro][cfg.count[macro]++] = d.map[macro][j];

    publish();
}

bool MacroModel::setLfo (int macro, LfoSettings l)
{
    if (macro < 0 || macro >= kNumMacros)
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
    cfg = defaultConfig();
    publish();
}

//==============================================================================
juce::ValueTree MacroModel::toValueTree() const
{
    juce::ValueTree t (subtreeName);
    t.setProperty ("v", 1, nullptr);

    for (int m = 0; m < kNumMacros; ++m)
    {
        juce::ValueTree n ("M");
        n.setProperty ("i", m, nullptr);
        n.setProperty ("n", juce::String::fromUTF8 (cfg.names[m]), nullptr);

        juce::StringArray maps;

        for (int j = 0; j < cfg.count[m]; ++j)
        {
            const auto& mp = cfg.map[m][j];
            maps.add (juce::String (mp.target) + "," + juce::String (toPermille (mp.min01)) + "," + juce::String (toPermille (mp.max01))
                      + "," + juce::String ((int) mp.curve) + "," + juce::String ((int) std::lround (mp.smoothMs))
                      + "," + juce::String ((int) mp.bipolar));
        }

        n.setProperty ("m", maps.joinIntoString (";"), nullptr);

        const auto& l = cfg.lfo[m];
        n.setProperty ("l", juce::String ((int) l.enabled) + "," + juce::String ((int) l.shape) + "," + juce::String ((int) l.tempoSync)
                            + "," + juce::String ((int) l.division) + "," + juce::String ((int) std::lround (l.rateHz * 100.0f))
                            + "," + juce::String (toPermille (l.depth01)) + "," + juce::String ((int) std::lround (l.phaseDeg)), nullptr);
        t.appendChild (n, nullptr);
    }

    return t;
}

bool MacroModel::validateTree (const juce::ValueTree& t)
{
    if (! t.hasType (subtreeName) || t.getNumChildren() > kNumMacros)
        return false;

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
    cfg = defaultConfig();

    if (t.hasType (subtreeName))
    {
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
