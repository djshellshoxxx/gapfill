#include "RatingStore.h"

#include <algorithm>
#include <cstring>

namespace gapfill::taste
{

juce::String recipeToHex (const FillRecipe& r)
{
    std::uint8_t bytes[FillRecipe::serialisedBytes];
    toBytes (r, bytes);
    return juce::String::toHexString (bytes, FillRecipe::serialisedBytes, 0);
}

bool recipeFromHex (const juce::String& text, FillRecipe& out)
{
    if (text.length() != 2 * FillRecipe::serialisedBytes || ! text.containsOnly ("0123456789abcdefABCDEF"))
        return false;

    std::uint8_t bytes[FillRecipe::serialisedBytes];

    for (int i = 0; i < FillRecipe::serialisedBytes; ++i)
        bytes[i] = (std::uint8_t) text.substring (2 * i, 2 * i + 2).getHexValue32();

    const auto r = fromBytes (bytes);

    if (! isValid (r))
        return false;

    out = r;
    return true;
}

juce::File RatingStore::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GapFill").getChildFile ("ratings.xml");
}

RatingStore::RatingStore (juce::File f) : path (std::move (f)) {}

bool RatingStore::parse (const juce::File& f, std::vector<RatingEntry>& out) const
{
    out.clear();

    if (! f.existsAsFile())
        return true;

    if (f.getSize() > kMaxLibraryBytes)
        return false;

    const auto xml = juce::XmlDocument::parse (f);

    if (xml == nullptr || ! xml->hasTagName ("Ratings"))
        return false;

    for (auto* e : xml->getChildWithTagNameIterator ("R"))
    {
        RatingEntry entry;
        const auto v = e->getIntAttribute ("v", 0);

        if ((v != 1 && v != -1) || ! recipeFromHex (e->getStringAttribute ("r"), entry.recipe))
            continue;                                       // skip a bad entry, keep the rest

        entry.rating = v;
        entry.seq = (std::uint32_t) std::max (0, e->getIntAttribute ("s", 0));
        out.push_back (entry);

        if ((int) out.size() >= kMaxLibraryEntries * 2)
            break;
    }

    return true;
}

void RatingStore::normalise (std::vector<RatingEntry>& v) const
{
    // De-duplicate by recipe bytes (largest seq wins), order by seq, FIFO cap.
    std::stable_sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.seq < b.seq; });
    std::vector<RatingEntry> out;

    for (auto it = v.rbegin(); it != v.rend(); ++it)
    {
        const bool dup = std::any_of (out.begin(), out.end(), [&] (const RatingEntry& o)
                                      { return std::memcmp (&o.recipe, &it->recipe, sizeof (FillRecipe)) == 0; });

        if (! dup)
            out.push_back (*it);
    }

    std::reverse (out.begin(), out.end());

    if ((int) out.size() > kMaxLibraryEntries)
        out.erase (out.begin(), out.begin() + ((int) out.size() - kMaxLibraryEntries));

    v = std::move (out);
}

bool RatingStore::load()
{
    lastNotice.clear();
    std::vector<RatingEntry> read;

    if (! parse (path, read))
    {
        list.clear();
        lastNotice = "Your ratings file could not be read; starting with an empty library.";
        return false;
    }

    list = std::move (read);
    normalise (list);
    nextSeq = list.empty() ? 1u : list.back().seq + 1u;
    cleared = false;
    return true;
}

void RatingStore::add (const FillRecipe& r, int rating)
{
    if ((rating != 1 && rating != -1) || ! isValid (r))
        return;

    RatingEntry e;
    e.recipe = r;
    e.rating = rating;
    e.seq = nextSeq++;
    list.push_back (e);
    normalise (list);
}

bool RatingStore::removeLast()
{
    if (list.empty())
        return false;

    list.pop_back();
    cleared = true;                 // an undone rating must not come back from the merge with the file
    return true;
}

void RatingStore::clear()
{
    list.clear();
    cleared = true;
}

bool RatingStore::save()
{
    // Merge with what another instance may have written since we read.
    std::vector<RatingEntry> merged = list;

    if (! cleared)
    {
        std::vector<RatingEntry> onDisk;

        if (parse (path, onDisk))
            merged.insert (merged.end(), onDisk.begin(), onDisk.end());
    }

    normalise (merged);

    juce::XmlElement root ("Ratings");
    root.setAttribute ("v", 1);

    for (const auto& e : merged)
    {
        auto* r = root.createNewChildElement ("R");
        r->setAttribute ("r", recipeToHex (e.recipe));
        r->setAttribute ("v", e.rating);
        r->setAttribute ("s", (int) e.seq);
    }

    if (! path.getParentDirectory().createDirectory())
    {
        lastNotice = "Your ratings could not be saved (folder not writable).";
        return false;
    }

    juce::TemporaryFile temp (path);

    if (! root.writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        lastNotice = "Your ratings could not be saved.";
        return false;
    }

    list = std::move (merged);
    nextSeq = list.empty() ? 1u : list.back().seq + 1u;
    cleared = false;
    return true;
}

Counts RatingStore::counts() const noexcept
{
    Counts c;

    for (const auto& e : list)
        addRating (c, e.recipe, e.rating);

    return c;
}

} // namespace gapfill::taste
