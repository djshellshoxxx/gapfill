#include "MutationLab.h"

#include <algorithm>
#include <cmath>

namespace gapfill::mutation
{

MutationLab::MutationLab (OverlayState& o) : overlay (o) {}

void MutationLab::attach (juce::AudioProcessorValueTreeState& state, std::function<void()> undoSnapshot)
{
    apvts = &state;
    pushUndo = std::move (undoSnapshot);
}

void MutationLab::setPinned (int gene, bool on) noexcept
{
    if (gene < 0 || gene >= kNumGenes)
        return;

    if (on)
        pins |= (1u << gene);
    else
        pins &= ~(1u << gene);
}

void MutationLab::reseed()
{
    // Message thread randomness only; never the engine RNG.
    seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt();
}

Genes MutationLab::readParameters() const
{
    Genes g {};

    if (apvts == nullptr)
        return g;

    const auto& ids = geneIds();

    for (int i = 0; i < kNumGenes; ++i)
        if (auto* p = apvts->getParameter (ids[(size_t) i]))
            g[(size_t) i] = p->getValue();

    return g;
}

void MutationLab::mutate()
{
    if (apvts == nullptr)
        return;

    endPreview();
    Generation gen;
    gen.parent = readParameters();
    gen.pins = pins;
    gen.index = ++generationCounter;
    gen.children = makeChildren (gen.parent, pins, scope, rate, seed, gen.index);
    generations.push_back (gen);

    if ((int) generations.size() > kMaxGenerations)
        generations.erase (generations.begin());
}

bool MutationLab::breedWith (int favouriteIndex)
{
    if (apvts == nullptr || favouriteIndex < 0 || favouriteIndex >= (int) favourites.size())
        return false;

    endPreview();
    Generation gen;
    gen.parent = readParameters();
    gen.pins = pins;
    gen.index = ++generationCounter;
    gen.children = makeChildren (gen.parent, pins, scope, rate, seed, gen.index, &favourites[(size_t) favouriteIndex].genes);
    generations.push_back (gen);

    if ((int) generations.size() > kMaxGenerations)
        generations.erase (generations.begin());

    return true;
}

void MutationLab::sendOverlay (bool active, const Genes* genes)
{
    OverlayMsg m;
    m.active = active;

    if (genes != nullptr)
        std::copy (genes->begin(), genes->end(), m.v);

    ++sentMessages;

    if (! overlay.push (m))
        pendingMsg = m;                       // queue full: retry on the next tick
    else
        pendingMsg.reset();
}

bool MutationLab::preview (int child)
{
    if (generations.empty() || child < 0 || child >= kNumChildren || apvts == nullptr)
        return false;

    previewBaseline = readParameters();
    previewChild = child;
    sendOverlay (true, &generations.back().children.child[(size_t) child]);
    return true;
}

void MutationLab::endPreview()
{
    if (previewChild < 0 && ! pendingMsg.has_value())
        return;

    previewChild = -1;
    sendOverlay (false, nullptr);
}

void MutationLab::writeGenes (const Genes& genes, std::uint32_t skipMask, int count)
{
    const auto& ids = geneIds();

    for (int i = 0; i < count; ++i)
    {
        if (skipMask & (1u << i))
            continue;

        if (auto* p = apvts->getParameter (ids[(size_t) i]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (std::min (1.0f, std::max (0.0f, genes[(size_t) i])));
            p->endChangeGesture();
        }
    }
}

bool MutationLab::keep (int child)
{
    if (generations.empty() || child < 0 || child >= kNumChildren || apvts == nullptr)
        return false;

    auto& gen = generations.back();
    endPreview();

    if (pushUndo != nullptr)
        pushUndo();

    writeGenes (gen.children.child[(size_t) child], gen.pins, genesInScope (scope));
    gen.kept = child;
    return true;
}

bool MutationLab::revert()
{
    if (generations.empty() || apvts == nullptr)
        return false;

    endPreview();
    const auto previous = generations.back().parent;
    generations.pop_back();

    if (pushUndo != nullptr)
        pushUndo();

    writeGenes (previous, 0u, kNumGenes);
    return true;
}

bool MutationLab::addFavourite (int child)
{
    if (generations.empty() || child < 0 || child >= kNumChildren || (int) favourites.size() >= kMaxFavourites)
        return false;

    Favourite f;
    f.name = "Fav " + juce::String ((int) favourites.size() + 1);
    f.genes = generations.back().children.child[(size_t) child];
    favourites.push_back (std::move (f));
    return true;
}

bool MutationLab::removeFavourite (int index)
{
    if (index < 0 || index >= (int) favourites.size())
        return false;

    favourites.erase (favourites.begin() + index);
    return true;
}

void MutationLab::tick()
{
    if (pendingMsg.has_value() && overlay.push (*pendingMsg))
        pendingMsg.reset();

    if (previewChild >= 0 && apvts != nullptr)
    {
        // Anything else that moved the sound (preset, Undo, state load, host automation) ends the preview.
        const auto now = readParameters();

        for (int i = 0; i < kNumGenes; ++i)
            if (std::abs (now[(size_t) i] - previewBaseline[(size_t) i]) > 1.0e-6f)
            {
                endPreview();
                return;
            }
    }
}

//==============================================================================
namespace
{
    constexpr const char* kType = "Mutation";

    juce::String encode (const Genes& g)
    {
        juce::MemoryBlock block;

        for (const auto v : g)
        {
            const auto q = (std::uint16_t) std::lround (std::min (1.0f, std::max (0.0f, v)) * 65535.0f);
            const char bytes[2] = { (char) (q & 0xff), (char) (q >> 8) };
            block.append (bytes, 2);
        }

        return juce::Base64::toBase64 (block.getData(), block.getSize());
    }

    bool decode (const juce::String& text, Genes& out, const Genes& fallback)
    {
        juce::MemoryOutputStream stream;

        if (! juce::Base64::convertFromBase64 (stream, text))
            return false;

        const auto size = stream.getDataSize();

        if (size == 0 || size % 2 != 0 || size > (size_t) (2 * kNumGenes))      // an older (shorter) gene table is fine
            return false;

        out = fallback;
        const auto* bytes = static_cast<const unsigned char*> (stream.getData());

        for (size_t i = 0; i < size / 2; ++i)
            out[i] = (float) (bytes[2 * i] | (bytes[2 * i + 1] << 8)) / 65535.0f;

        return true;
    }

    bool printable (const juce::String& s)
    {
        if (s.isEmpty() || s.length() > kMaxFavouriteName)
            return false;

        for (auto c = s.getCharPointer(); ! c.isEmpty(); ++c)
            if (*c < 32 || *c == 127)
                return false;

        return true;
    }
}

bool validateMutationSubtree (const juce::ValueTree& t)
{
    if (! t.hasType (kType))
        return false;

    const auto rate = (double) t.getProperty ("rate", kDefaultRate);
    const auto scope = (int) t.getProperty ("scope", 0);

    if (! (rate >= kMinRate - 1e-6 && rate <= kMaxRate + 1e-6) || scope < 0 || scope > 2)
        return false;

    if (t.getNumChildren() > kMaxFavourites)
        return false;

    Genes scratch {};

    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const auto fav = t.getChild (i);

        if (! fav.hasType ("Fav") || ! printable (fav.getProperty ("name").toString())
             || ! decode (fav.getProperty ("g").toString(), scratch, scratch))
            return false;
    }

    return true;
}

void applyMutationSubtree (MutationLab& lab, const juce::ValueTree& t)
{
    lab.seed = (std::uint32_t) (juce::int64) t.getProperty ("seed", (juce::int64) lab.seed);
    lab.setRate ((float) (double) t.getProperty ("rate", kDefaultRate));
    lab.setScope ((int) t.getProperty ("scope", 0));
    lab.pins = (std::uint32_t) (juce::int64) t.getProperty ("pins", 0) & ((1u << kNumGenes) - 1u);
    lab.favourites.clear();

    const auto current = lab.isAttached() ? lab.readParameters() : Genes {};

    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        Favourite f;
        f.name = t.getChild (i).getProperty ("name").toString();
        decode (t.getChild (i).getProperty ("g").toString(), f.genes, current);     // shorter table: remaining genes keep current values
        lab.favourites.push_back (std::move (f));
    }
}

juce::ValueTree serialiseMutationSubtree (const MutationLab& lab)
{
    const bool isDefault = lab.rate == kDefaultRate && lab.scope == 0 && lab.pins == 0 && lab.favourites.empty()
                            && lab.seed == MutationLab::defaultSeed();

    if (isDefault)
        return {};                                             // absent subtree = defaults

    juce::ValueTree t (kType);
    t.setProperty ("v", kGeneTableVersion, nullptr);
    t.setProperty ("seed", (juce::int64) lab.seed, nullptr);
    t.setProperty ("rate", (double) lab.rate, nullptr);
    t.setProperty ("scope", lab.scope, nullptr);
    t.setProperty ("pins", (juce::int64) lab.pins, nullptr);

    for (const auto& f : lab.favourites)
    {
        juce::ValueTree fav ("Fav");
        fav.setProperty ("name", f.name.substring (0, kMaxFavouriteName), nullptr);
        fav.setProperty ("g", encode (f.genes), nullptr);
        t.appendChild (fav, nullptr);
    }

    return t;
}

void resetMutationSubtree (MutationLab& lab)
{
    lab.seed = MutationLab::defaultSeed();
    lab.rate = kDefaultRate;
    lab.scope = 0;
    lab.pins = 0;
    lab.favourites.clear();
}

} // namespace gapfill::mutation
