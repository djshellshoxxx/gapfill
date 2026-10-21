#pragma once

// F-X05: the global ratings library (message thread only).
//   <userApplicationData>/GapFill/ratings.xml
//   <Ratings v="1"><R r="<32 hex = FillRecipe bytes>" v="1|-1" s="<seq>"/>...</Ratings>
// <= 256 entries, FIFO eviction, de-duplicated by recipe bytes (the last rating wins),
// atomic write (temp file + rename), merged by recipe bytes with what another
// instance wrote meanwhile. A corrupt or unreadable file is an empty library with
// a notice; nothing here touches the network.

#include "TasteModel.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace gapfill::taste
{

inline constexpr int kMaxLibraryEntries = 256;
inline constexpr juce::int64 kMaxLibraryBytes = 64 * 1024;

struct RatingEntry
{
    FillRecipe recipe;
    int rating = 0;               ///< +1 liked, -1 skipped
    std::uint32_t seq = 0;
};

class RatingStore
{
public:
    explicit RatingStore (juce::File libraryFile = defaultFile());

    static juce::File defaultFile();

    /** Reads the file. Returns false (and leaves an empty library plus a notice) when it is
        missing, too large, corrupt or unreadable; a missing file is not an error. */
    bool load();

    void add (const FillRecipe&, int rating);
    bool removeLast();             ///< undo the newest rating
    void clear();

    /** Re-reads the file, merges by recipe bytes (larger seq wins), caps to 256 and writes atomically. */
    bool save();

    Counts counts() const noexcept;
    const std::vector<RatingEntry>& entries() const noexcept { return list; }
    const juce::String& notice() const noexcept { return lastNotice; }
    const juce::File& file() const noexcept { return path; }

private:
    bool parse (const juce::File&, std::vector<RatingEntry>& out) const;
    void normalise (std::vector<RatingEntry>&) const;

    juce::File path;
    std::vector<RatingEntry> list;
    std::uint32_t nextSeq = 1;
    juce::String lastNotice;
    bool cleared = false;
};

juce::String recipeToHex (const FillRecipe&);
bool recipeFromHex (const juce::String&, FillRecipe&);

} // namespace gapfill::taste
