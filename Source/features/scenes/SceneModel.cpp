#include "SceneModel.h"

#include "../../Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace gapfill::scenes
{

namespace
{
    struct Tables
    {
        std::vector<juce::String> ids;
        std::unordered_map<std::string, int> index;
        std::map<std::uint16_t, std::vector<juce::String>> scopeCache;
        std::vector<juce::String> byScope[8];     // one list per scope bit
    };

    Tables& tables()
    {
        static Tables t;
        return t;
    }

    std::once_flag initFlag;

   #define GAPFILL_SCENE_ADD(member, id) out.emplace_back (id);

    void groupFx (std::vector<juce::String>& out)
    {
        juce::ignoreUnused (out);
        GAPFILL_PARAMS_SWELL (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_CLOUD (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_RATCHET (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_RCRUSH (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_TAPEDESTR (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_RESONATOR (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_FSDELAY (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_DUB (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_FORMANT (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_SPFREEZE (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_CONVO (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_SCRUB (GAPFILL_SCENE_ADD)
        // hgran (F-X01, 4 ids) is not scene-recallable: the default scope is already at the 128-id file cap.
        GAPFILL_PARAMS_BANDFX (GAPFILL_SCENE_ADD)
    }

    void groupMusical (std::vector<juce::String>& out)
    {
        juce::ignoreUnused (out);
        GAPFILL_PARAMS_SCALE (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_KEYDET (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_PHRASE (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_BREATHING (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_TENSION (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_GROOVE (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_CALL (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_ENERGY (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_BATTLE (GAPFILL_SCENE_ADD)
        GAPFILL_PARAMS_BUDGET (GAPFILL_SCENE_ADD)
    }

    void groupMacros (std::vector<juce::String>& out)
    {
        juce::ignoreUnused (out);
        GAPFILL_PARAMS_MACROS (GAPFILL_SCENE_ADD)
    }
   #undef GAPFILL_SCENE_ADD

    void build()
    {
        auto& t = tables();
        const auto all = allParameterIDs();

        for (int i = 0; i < all.size(); ++i)
        {
            t.ids.push_back (all[i]);
            t.index.emplace (all[i].toStdString(), i);
        }

        std::vector<juce::String> raw[8];

        for (const char* id : { "fill", "intensity", "complexity", "chaos", "density", "fillType" })
            raw[0].emplace_back (id);

        for (int a = 0; a < numAlgos; ++a)
            raw[1].emplace_back (algoWeightID ((Algo) a));

        groupFx (raw[2]);
        groupMusical (raw[3]);
        groupMacros (raw[4]);

        for (const char* id : { "detMode", "detSource", "sensitivity", "gapThresh", "minGap", "maxFill",
                                "maxFillSync", "maxFillDiv", "gapRelease", "protectHold", "retrigGuard" })
            raw[5].emplace_back (id);

        for (const char* id : { "protTrans", "protLow", "protCenter", "protNext", "protSC" })
            raw[6].emplace_back (id);

        for (const char* id : { "xoverLM", "xoverMH", "bandLowOn", "bandMidOn", "bandHighOn",
                                "bandLowAmt", "bandMidAmt", "bandHighAmt", "bandLowInt", "bandMidInt",
                                "bandHighInt", "bandLowProb", "bandMidProb", "bandHighProb" })
            raw[7].emplace_back (id);

        for (int b = 0; b < 8; ++b)
            for (const auto& id : raw[b])
                if (! isNeverId (id) && t.index.count (id.toStdString()) > 0)
                    t.byScope[b].push_back (id);
    }
}

void initTables()
{
    std::call_once (initFlag, build);
}

bool isNeverId (const juce::String& id)
{
    static const char* const never[] = { "dry", "wet", "output", "bypass", "lookahead", "seed", "wetSolo",
                                         "wetSafety", "qualityMode", "capture", "audition" };

    for (const auto* n : never)
        if (id == n)
            return true;

    return id.startsWith ("coop") || id.startsWith ("midiOut");
}

const std::vector<juce::String>& idsForScope (std::uint16_t scopeMask)
{
    initTables();
    auto& t = tables();

    if (const auto it = t.scopeCache.find (scopeMask); it != t.scopeCache.end())
        return it->second;

    // Parameter order, no duplicates.
    std::vector<int> picked;

    for (int b = 0; b < 8; ++b)
        if ((scopeMask & (1u << b)) != 0)
            for (const auto& id : t.byScope[b])
                picked.push_back (t.index[id.toStdString()]);

    std::sort (picked.begin(), picked.end());
    picked.erase (std::unique (picked.begin(), picked.end()), picked.end());

    std::vector<juce::String> out;

    for (const auto i : picked)
        out.push_back (t.ids[(size_t) i]);

    return t.scopeCache.emplace (scopeMask, std::move (out)).first->second;
}

int paramIndexOf (const juce::String& id)
{
    initTables();
    const auto& t = tables();
    const auto it = t.index.find (id.toStdString());
    return it == t.index.end() ? -1 : it->second;
}

const juce::String& paramIdAt (int index)
{
    initTables();
    static const juce::String empty;
    const auto& t = tables();
    return index >= 0 && index < (int) t.ids.size() ? t.ids[(size_t) index] : empty;
}

int numParams()
{
    initTables();
    return (int) tables().ids.size();
}

//==============================================================================
bool Scene::operator== (const Scene& o) const noexcept
{
    if (name != o.name || scope != o.scope || quant != o.quant || params.size() != o.params.size())
        return false;

    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].index != o.params[i].index || ! juce::exactlyEqual (params[i].value01, o.params[i].value01))
            return false;

    return true;
}

Scene captureScene (juce::AudioProcessorValueTreeState& apvts, const juce::String& name, std::uint16_t scope)
{
    Scene s;
    s.name = name.substring (0, kMaxSceneNameLen);
    s.scope = scope;

    for (const auto& id : idsForScope (scope))
    {
        if (auto* p = apvts.getParameter (id))
        {
            const auto v = p->getValue();

            if (! juce::exactlyEqual (v, p->getDefaultValue()))
                s.params.push_back ({ (std::uint16_t) paramIndexOf (id), v });
        }
    }

    return s;
}

bool isSceneValid (const Scene& scene, juce::String& why)
{
    if (scene.params.size() > (size_t) kMaxSceneParams)
    {
        why = "too many parameters";
        return false;
    }

    if (scene.name.length() > kMaxSceneNameLen)
    {
        why = "name too long";
        return false;
    }

    for (const auto& p : scene.params)
    {
        if (p.index >= (std::uint16_t) numParams())
        {
            why = "unknown parameter";
            return false;
        }

        if (isNeverId (paramIdAt (p.index)))
        {
            why = "parameter not allowed in scenes: " + paramIdAt (p.index);
            return false;
        }

        if (! std::isfinite (p.value01) || p.value01 < 0.0f || p.value01 > 1.0f)
        {
            why = "value out of range";
            return false;
        }
    }

    return true;
}

bool resolveScene (juce::AudioProcessorValueTreeState& apvts, const Scene& scene,
                   std::vector<ResolvedParam>& out, juce::String& why)
{
    out.clear();

    if (! isSceneValid (scene, why))
        return false;

    std::unordered_map<int, float> stored;

    for (const auto& p : scene.params)
        stored[p.index] = p.value01;

    for (const auto& id : idsForScope (scene.scope))
    {
        auto* p = apvts.getParameter (id);

        if (p == nullptr)
            continue;

        const auto idx = paramIndexOf (id);
        const auto it = stored.find (idx);
        const auto v01 = it != stored.end() ? it->second : p->getDefaultValue();
        out.push_back ({ (std::uint16_t) idx, p->convertFrom0to1 (v01) });
    }

    if (out.size() > (size_t) kMaxSceneParams)
    {
        why = "scope covers too many parameters";
        out.clear();
        return false;
    }

    return true;
}

//==============================================================================
bool SceneBank::store (int index, Scene s)
{
    if (index < 0 || index > size() || index >= kMaxScenes)
        return false;

    if (index == size())
        scenes.push_back (std::move (s));
    else
        scenes[(size_t) index] = std::move (s);

    return true;
}

bool SceneBank::remove (int index)
{
    if (index < 0 || index >= size())
        return false;

    scenes.erase (scenes.begin() + index);

    if (current == index)
        current = -1;
    else if (current > index)
        --current;

    return true;
}

bool SceneBank::duplicate (int index)
{
    if (index < 0 || index >= size() || size() >= kMaxScenes)
        return false;

    auto copy = scenes[(size_t) index];
    copy.name = (copy.name + " 2").substring (0, kMaxSceneNameLen);
    scenes.insert (scenes.begin() + index + 1, std::move (copy));

    if (current > index)
        ++current;

    return true;
}

bool SceneBank::move (int from, int to)
{
    if (from < 0 || from >= size() || to < 0 || to >= size())
        return false;

    if (from == to)
        return true;

    auto s = std::move (scenes[(size_t) from]);
    scenes.erase (scenes.begin() + from);
    scenes.insert (scenes.begin() + to, std::move (s));

    if (current == from)
        current = to;
    else if (from < current && to >= current)
        --current;
    else if (from > current && to <= current)
        ++current;

    return true;
}

bool SceneBank::rename (int index, const juce::String& newName)
{
    if (index < 0 || index >= size())
        return false;

    scenes[(size_t) index].name = newName.substring (0, kMaxSceneNameLen);
    return true;
}

void SceneBank::clear()
{
    scenes.clear();
    current = -1;
    name = {};
    options = {};
}

void SceneBank::assign (const SceneBank& other)
{
    *this = other;
}

//------------------------------------------------------------------------------
namespace
{
    juce::String encodeParams (const std::vector<SceneParam>& params)
    {
        juce::MemoryBlock mb (params.size() * 6);
        auto* d = static_cast<std::uint8_t*> (mb.getData());

        for (size_t i = 0; i < params.size(); ++i)
        {
            std::memcpy (d + i * 6, &params[i].index, 2);
            std::memcpy (d + i * 6 + 2, &params[i].value01, 4);
        }

        return juce::Base64::toBase64 (mb.getData(), mb.getSize());
    }

    bool decodeParams (const juce::String& text, std::vector<SceneParam>& out)
    {
        out.clear();

        if (text.isEmpty())
            return true;

        juce::MemoryOutputStream mos;

        if (! juce::Base64::convertFromBase64 (mos, text))
            return false;

        const auto size = mos.getDataSize();

        if (size % 6 != 0 || size / 6 > (size_t) kMaxSceneParams)
            return false;

        const auto* d = static_cast<const std::uint8_t*> (mos.getData());

        for (size_t i = 0; i < size / 6; ++i)
        {
            SceneParam p;
            std::memcpy (&p.index, d + i * 6, 2);
            std::memcpy (&p.value01, d + i * 6 + 2, 4);
            out.push_back (p);
        }

        return true;
    }
}

juce::ValueTree SceneBank::toValueTree (std::size_t maxBytes, bool* truncated) const
{
    juce::ValueTree t (subtreeName);
    t.setProperty ("v", 1, nullptr);
    t.setProperty ("name", name, nullptr);
    t.setProperty ("cur", current, nullptr);
    t.setProperty ("wrap", options.wrap ? 1 : 0, nullptr);
    t.setProperty ("q", (int) options.quant, nullptr);
    t.setProperty ("nt", options.immediateWithoutTransport ? 1 : 0, nullptr);

    if (truncated != nullptr)
        *truncated = false;

    std::size_t used = (std::size_t) t.toXmlString().getNumBytesAsUTF8();

    for (const auto& s : scenes)
    {
        juce::ValueTree n ("S");
        n.setProperty ("n", s.name, nullptr);
        n.setProperty ("sc", (int) s.scope, nullptr);
        n.setProperty ("q", (int) s.quant, nullptr);
        n.setProperty ("d", encodeParams (s.params), nullptr);

        const auto cost = (std::size_t) n.toXmlString().getNumBytesAsUTF8() + 2;

        if (used + cost > maxBytes)
        {
            if (truncated != nullptr)
                *truncated = true;

            break;
        }

        used += cost;
        t.appendChild (n, nullptr);
    }

    return t;
}

bool SceneBank::validateTree (const juce::ValueTree& t)
{
    if (! t.hasType (subtreeName) || t.getNumChildren() > kMaxScenes)
        return false;

    if ((int) t.getProperty ("q", 3) < 1 || (int) t.getProperty ("q", 3) > 4)
        return false;

    if (t.getProperty ("name").toString().length() > kMaxBankNameLen)
        return false;

    for (const auto& n : t)
    {
        if (! n.hasType ("S"))
            return false;

        Scene s;
        s.name = n.getProperty ("n").toString();
        const auto sc = (int) n.getProperty ("sc", -1);
        const auto q = (int) n.getProperty ("q", -1);

        if (sc < 0 || sc > 255 || q < 0 || q > 4)
            return false;

        s.scope = (std::uint16_t) sc;
        s.quant = (Quant) q;

        if (! decodeParams (n.getProperty ("d").toString(), s.params))
            return false;

        juce::String why;

        if (! isSceneValid (s, why))
            return false;
    }

    return true;
}

void SceneBank::restore (const juce::ValueTree& t)
{
    clear();

    if (! t.hasType (subtreeName))
        return;

    name = t.getProperty ("name").toString().substring (0, kMaxBankNameLen);
    current = (int) t.getProperty ("cur", -1);
    options.wrap = (int) t.getProperty ("wrap", 0) != 0;
    options.quant = (Quant) juce::jlimit (1, 4, (int) t.getProperty ("q", (int) Quant::Bar));
    options.immediateWithoutTransport = (int) t.getProperty ("nt", 1) != 0;

    for (const auto& n : t)
    {
        Scene s;
        s.name = n.getProperty ("n").toString();
        s.scope = (std::uint16_t) juce::jlimit (0, 255, (int) n.getProperty ("sc", kDefaultScope));
        s.quant = (Quant) juce::jlimit (0, 4, (int) n.getProperty ("q", 0));
        decodeParams (n.getProperty ("d").toString(), s.params);
        scenes.push_back (std::move (s));
    }

    if (current >= size())
        current = -1;
}

juce::String SceneBank::canonicalPayload() const
{
    juce::String out;

    for (const auto& s : scenes)
    {
        out << s.name << '|' << (int) s.scope << '|' << (int) s.quant << '|';

        for (const auto& p : s.params)
            out << paramIdAt (p.index) << '=' << juce::String (p.value01, 6) << ';';

        out << '\n';
    }

    return out;
}

} // namespace gapfill::scenes
