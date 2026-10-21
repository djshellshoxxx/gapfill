#include "BankFile.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace gapfill::bank
{

using namespace scenes;

namespace
{
    struct ScopeName { const char* name; std::uint16_t bit; };
    constexpr ScopeName kScopeNames[] =
    {
        { "creative", ScopeCreative }, { "weights", ScopeWeights }, { "fx", ScopeFx }, { "musical", ScopeMusical },
        { "macros", ScopeMacros }, { "detector", ScopeDetector }, { "protection", ScopeProtection }, { "bands", ScopeBands }
    };

    const char* quantName (Quant q)
    {
        switch (q)
        {
            case Quant::Immediate: return "immediate";
            case Quant::Beat:      return "beat";
            case Quant::Bar:       return "bar";
            case Quant::Phrase:    return "phrase";
            case Quant::Inherit:   break;
        }

        return "inherit";
    }

    bool parseQuant (const juce::String& s, Quant& out)
    {
        for (auto q : { Quant::Inherit, Quant::Immediate, Quant::Beat, Quant::Bar, Quant::Phrase })
            if (s == quantName (q))
            {
                out = q;
                return true;
            }

        return false;
    }

    juce::String scopeToString (std::uint16_t scope)
    {
        juce::StringArray parts;

        for (const auto& s : kScopeNames)
            if ((scope & s.bit) != 0)
                parts.add (s.name);

        return parts.joinIntoString (",");
    }

    std::uint16_t scopeFromString (const juce::String& text)
    {
        std::uint16_t scope = 0;

        for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
            for (const auto& s : kScopeNames)
                if (part.trim() == s.name)
                    scope |= s.bit;

        return scope;
    }

    // Minimal SHA-256 (FIPS 180-4). The digest is an integrity check, not security; juce_cryptography
    // is not part of the module list, so the 40 lines live here.
    juce::String sha256Hex (const juce::String& text)
    {
        static const std::uint32_t k[64] =
        {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        std::uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

        std::vector<std::uint8_t> msg (text.toRawUTF8(), text.toRawUTF8() + text.getNumBytesAsUTF8());
        const auto bitLen = (std::uint64_t) msg.size() * 8u;
        msg.push_back (0x80);

        while (msg.size() % 64 != 56)
            msg.push_back (0);

        for (int i = 7; i >= 0; --i)
            msg.push_back ((std::uint8_t) (bitLen >> (8 * i)));

        auto rotr = [] (std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };

        for (size_t chunk = 0; chunk < msg.size(); chunk += 64)
        {
            std::uint32_t w[64];

            for (int i = 0; i < 16; ++i)
                w[i] = ((std::uint32_t) msg[chunk + (size_t) i * 4] << 24) | ((std::uint32_t) msg[chunk + (size_t) i * 4 + 1] << 16)
                     | ((std::uint32_t) msg[chunk + (size_t) i * 4 + 2] << 8) | (std::uint32_t) msg[chunk + (size_t) i * 4 + 3];

            for (int i = 16; i < 64; ++i)
            {
                const auto s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
                const auto s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];

            for (int i = 0; i < 64; ++i)
            {
                const auto S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
                const auto ch = (e & f) ^ (~e & g);
                const auto t1 = hh + S1 + ch + k[i] + w[i];
                const auto S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
                const auto maj = (a & b) ^ (a & c) ^ (b & c);
                const auto t2 = S0 + maj;
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }

            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        juce::String out;

        for (auto v : h)
            out << juce::String::toHexString ((int) v).paddedLeft ('0', 8);

        return out;
    }

    juce::String digestOf (const std::vector<Scene>& selected, const juce::String& bankName)
    {
        return sha256Hex (canonicalPayload (selected, bankName));
    }
}

juce::String sha256 (const juce::String& text)
{
    return sha256Hex (text);
}

juce::String canonicalPayload (const std::vector<Scene>& selected, const juce::String& bankName)
{
    juce::String out;
    out << bankName << '\n';

    for (const auto& s : selected)
    {
        out << s.name << '|' << scopeToString (s.scope) << '|' << quantName (s.quant) << '|';

        for (const auto& p : s.params)
            out << paramIdAt (p.index) << '=' << juce::String (p.value01, 6) << ';';

        out << '\n';
    }

    return out;
}

//==============================================================================
juce::String exportToString (const SceneBank& bank, const ExportOptions& options,
                             const juce::String& appVersion, const juce::String& created)
{
    std::vector<Scene> selected;

    if (options.selection.empty())
    {
        for (int i = 0; i < bank.size(); ++i)
            selected.push_back (bank.at (i));
    }
    else
    {
        for (const auto i : options.selection)
            if (i >= 0 && i < bank.size())
                selected.push_back (bank.at (i));
    }

    // Round the values to the file precision first so the digest matches what a reader sees.
    for (auto& s : selected)
        for (auto& p : s.params)
            p.value01 = juce::String (p.value01, 6).getFloatValue();

    juce::XmlElement root ("GapBank");
    root.setAttribute ("formatVersion", kFormatVersion);
    root.setAttribute ("appVersion", appVersion);
    root.setAttribute ("stateVersion", kStateVersionInFile);
    root.setAttribute ("name", bank.getName());
    root.setAttribute ("created", created);

    for (const auto& s : selected)
    {
        auto* e = root.createNewChildElement ("Scene");
        e->setAttribute ("name", s.name);
        e->setAttribute ("quant", quantName (s.quant));
        e->setAttribute ("scope", scopeToString (s.scope));

        for (const auto& p : s.params)
        {
            auto* pe = e->createNewChildElement ("P");
            pe->setAttribute ("id", paramIdAt (p.index));
            pe->setAttribute ("v", juce::String (p.value01, 6));
        }
    }

    if (options.includeMacros && options.macros.isValid())
        if (auto macros = options.macros.createXml())
            root.addChildElement (macros.release());

    root.createNewChildElement ("Digest")->setAttribute ("sha256", digestOf (selected, bank.getName()));
    return root.toString (juce::XmlElement::TextFormat().withoutHeader());
}

//==============================================================================
void migrateBank (juce::XmlElement& root, int fromVersion)
{
    // formatVersion 1 is the only version; the hook exists so the first real
    // change has a place to go (docs/dev/12 N-VAL import step).
    juce::ignoreUnused (root, fromVersion);
}

ImportReport parseString (const juce::String& text)
{
    ImportReport r;

    if ((juce::int64) text.getNumBytesAsUTF8() > kMaxFileBytes)
    {
        r.status = ParseStatus::TooLarge;
        r.message = "This file is larger than 256 KiB and is not a GapFill scene bank.";
        return r;
    }

    juce::XmlDocument doc (text);
    doc.setEmptyTextElementsIgnored (true);
    auto xml = doc.getDocumentElement();

    if (xml == nullptr || ! xml->hasTagName ("GapBank"))
    {
        r.status = ParseStatus::Malformed;
        r.message = "This is not a GapFill scene bank (.gapbank).";
        return r;
    }

    if (! xml->hasAttribute ("formatVersion"))
    {
        r.message = "The scene bank has no format version.";
        return r;
    }

    const auto version = xml->getIntAttribute ("formatVersion", -1);

    if (version > kFormatVersion)
    {
        r.status = ParseStatus::NewerVersion;
        r.message = "This bank was made by a newer GapFill (format " + juce::String (version)
                    + "). Update GapFill to import it.";
        return r;
    }

    if (version < 1)
    {
        r.message = "The scene bank has an invalid format version.";
        return r;
    }

    migrateBank (*xml, version);

    r.bankName = xml->getStringAttribute ("name").substring (0, kMaxBankNameLen);

    for (auto* se : xml->getChildWithTagNameIterator ("Scene"))
    {
        if ((int) r.accepted.size() + (int) r.skipped.size() >= kMaxScenes)
        {
            r.skipped.push_back ({ se->getStringAttribute ("name"), "more than 64 scenes" });
            continue;
        }

        Scene s;
        s.name = se->getStringAttribute ("name").substring (0, kMaxSceneNameLen);
        const auto fail = [&] (const juce::String& why) { r.skipped.push_back ({ s.name, why }); };

        if (se->getStringAttribute ("name").length() > kMaxSceneNameLen)
        {
            fail ("name longer than 32 characters");
            continue;
        }

        if (! parseQuant (se->getStringAttribute ("quant", "inherit"), s.quant))
        {
            fail ("unknown quantise mode");
            continue;
        }

        s.scope = scopeFromString (se->getStringAttribute ("scope", scopeToString (kDefaultScope)));

        bool bad = false;
        int unknownInScene = 0;

        for (auto* pe : se->getChildWithTagNameIterator ("P"))
        {
            if ((int) s.params.size() > kMaxSceneParams)
            {
                bad = true;
                fail ("more than 128 parameters");
                break;
            }

            const auto id = pe->getStringAttribute ("id");
            const auto vText = pe->getStringAttribute ("v");
            const auto index = paramIndexOf (id);

            if (index < 0)
            {
                ++unknownInScene;
                continue;
            }

            if (isNeverId (id))
            {
                bad = true;
                fail ("contains \"" + id + "\", which scenes may never store");
                break;
            }

            if (! vText.containsOnly ("0123456789.-+eE") || vText.isEmpty())
            {
                bad = true;
                fail ("invalid value for " + id);
                break;
            }

            const auto v = vText.getFloatValue();

            if (! std::isfinite (v) || v < 0.0f || v > 1.0f)
            {
                bad = true;
                fail ("value out of range for " + id);
                break;
            }

            s.params.push_back ({ (std::uint16_t) index, v });
        }

        if (bad)
            continue;

        if (s.params.size() > (size_t) kMaxSceneParams)
        {
            fail ("more than 128 parameters");
            continue;
        }

        r.unknownIds += unknownInScene;
        r.accepted.push_back (std::move (s));
    }

    if (auto* macros = xml->getChildByName ("Macros"))
        r.macros = juce::ValueTree::fromXml (*macros);

    if (auto* d = xml->getChildByName ("Digest"))
    {
        r.digestPresent = true;

        // The digest covers what the exporter wrote; compare against the file's own scenes
        // (including skipped ones would need them parsed, so a skipped scene also shows as a mismatch).
        r.digestMismatch = d->getStringAttribute ("sha256") != digestOf (r.accepted, r.bankName);
    }

    if (r.accepted.empty())
    {
        r.status = ParseStatus::Empty;
        r.message = r.skipped.empty() ? "The scene bank contains no scenes."
                                      : "No scene in this bank could be imported.";
        return r;
    }

    r.status = ParseStatus::Ok;
    r.message = juce::String ((int) r.accepted.size()) + " scene(s) ready to import"
                + (r.skipped.empty() ? juce::String() : ", " + juce::String ((int) r.skipped.size()) + " skipped")
                + (r.digestMismatch ? ". Warning: the file's integrity check does not match." : ".");
    return r;
}

ImportReport parseFile (const juce::File& file)
{
    ImportReport r;

    if (! file.existsAsFile())
    {
        r.message = "The file does not exist.";
        return r;
    }

    if (file.getSize() > kMaxFileBytes)
    {
        r.status = ParseStatus::TooLarge;
        r.message = "This file is larger than 256 KiB and is not a GapFill scene bank.";
        return r;
    }

    juce::MemoryBlock data;

    if (! file.loadFileAsData (data) || (juce::int64) data.getSize() > kMaxFileBytes)
    {
        r.message = "The file could not be read.";
        return r;
    }

    return parseString (juce::String::createStringFromData (data.getData(), (int) data.getSize()));
}

//==============================================================================
ApplyResult applyImport (SceneController& controller, const ImportReport& report, ImportMode mode)
{
    ApplyResult out;

    if (report.status != ParseStatus::Ok || report.accepted.empty())
    {
        out.message = report.message.isNotEmpty() ? report.message : "Nothing to import.";
        return out;
    }

    // Build the complete result first; only then swap it in.
    SceneBank next;
    next.assign (controller.getBank());

    switch (mode)
    {
        case ImportMode::Replace:
        {
            const auto options = next.options;
            next.clear();
            next.options = options;
            next.setName (report.bankName);

            for (const auto& s : report.accepted)
            {
                next.store (next.size(), s);
                ++out.added;
            }
            break;
        }

        case ImportMode::Append:
            for (const auto& s : report.accepted)
            {
                if (next.store (next.size(), s))
                    ++out.added;
                else
                    ++out.dropped;
            }
            break;

        case ImportMode::MergeByName:
            for (const auto& s : report.accepted)
            {
                int found = -1;

                for (int i = 0; i < next.size(); ++i)
                    if (next.at (i).name == s.name)
                        found = i;

                if (found >= 0)
                {
                    next.store (found, s);
                    ++out.replaced;
                }
                else if (next.store (next.size(), s))
                {
                    ++out.added;
                }
                else
                {
                    ++out.dropped;
                }
            }
            break;
    }

    controller.rememberForUndo();
    controller.replaceBank (next);

    out.ok = true;
    out.message = juce::String (out.added) + " added, " + juce::String (out.replaced) + " replaced"
                  + (out.dropped > 0 ? ", " + juce::String (out.dropped) + " did not fit" : juce::String())
                  + (report.skipped.empty() ? juce::String() : ", " + juce::String ((int) report.skipped.size()) + " skipped")
                  + ".";
    return out;
}

bool writeFile (const juce::File& target, const juce::String& text, bool allowOverwrite, juce::String& error)
{
    if (target.exists() && ! allowOverwrite)
    {
        error = "The file already exists. Choose a different name or confirm the overwrite.";
        return false;
    }

    if ((juce::int64) text.getNumBytesAsUTF8() > kMaxFileBytes)
    {
        error = "The bank is larger than 256 KiB and cannot be exported.";
        return false;
    }

    juce::TemporaryFile temp (target);

    if (! temp.getFile().replaceWithText (text, false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "The file could not be written. Check the folder permissions and free space.";
        return false;
    }

    return true;
}

} // namespace gapfill::bank
