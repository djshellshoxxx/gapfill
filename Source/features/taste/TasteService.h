#pragma once

// F-X05: the processor-owned taste service.
//
//  * audio thread:   apply (multiplies the published bias into the weights), onCommand (queues ratings)
//  * message thread: everything else (ratings, library file, counts, project state)
//
// The audio thread only reads 18 quantised multipliers through a sequence lock; it never
// touches the file or the model. Off, or on but not ready, writes nothing.

#include "RatingStore.h"
#include "../../dsp/EngineEvents.h"
#include "../../dsp/EngineSettings.h"
#include "../../dsp/SpscQueue.h"

#include <atomic>

namespace gapfill::taste
{

/** The project's saved profile (what `<Taste>` stores). */
struct ProjectProfile
{
    std::uint16_t likes = 0, skips = 0;
    std::int8_t q[kFeatures] {};
    bool ready = false;
};

class TasteService
{
public:
    explicit TasteService (juce::File libraryFile = RatingStore::defaultFile());

    /** Message thread: points the service at another library file (tests, portable installs). */
    void useLibraryFile (juce::File);

    // ---- audio thread ------------------------------------------------------------
    void apply (EngineSettings&) const noexcept;
    void onCommand (const Command&) noexcept;

    // ---- message thread ----------------------------------------------------------
    void onFillEvent (const FillEvent&) noexcept;                 ///< remembers the latest natural fill
    void timerTick (std::uint32_t nowMs, bool on, float strength01);

    void rate (const FillRecipe&, int rating);
    void rateLatest (int rating);
    bool undoLast();
    void rebuildFromLibrary();
    void resetProfile (bool alsoClearLibrary);

    Counts counts() const { return displayCounts; }
    bool ready() const noexcept { return profile.ready; }
    const RatingStore& store() const noexcept { return library; }
    bool hasLatest() const noexcept { return haveLatest; }
    bool firstUseNoticeNeeded() const noexcept { return ! noticeShown; }
    void markNoticeShown() noexcept { noticeShown = true; }

    ProjectProfile projectProfile() const noexcept { return profile; }
    void setProjectProfile (const ProjectProfile&);

    TasteBias currentBias() const noexcept;                     ///< what the audio thread sees (any thread)

private:
    struct RatingEvent { CommandType type = CommandType::None; FillRecipe recipe; };

    void recompute (bool slew = false);
    void publish();

    RatingStore library;
    bool libraryLoaded = false;
    Counts displayCounts;
    ProjectProfile profile;
    float lastStrength = 0.5f;
    FillRecipe latest;
    bool haveLatest = false;
    bool noticeShown = false;
    bool dirty = false;
    std::uint32_t dirtySinceMs = 0;

    SpscQueue<RatingEvent, 16> events;

    // sequence-locked copy of the profile for the audio thread
    std::atomic<std::uint32_t> seq { 0 };
    std::atomic<std::int8_t> pubQ[kFeatures];
    std::atomic<bool> pubReady { false };
};

} // namespace gapfill::taste
