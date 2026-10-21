#pragma once

// F-L17 Mutation Lab controller (message thread only). Owned by ui::UiState so the settings, favourites and
// generations survive the editor being closed and reopened; previews never do.
//
//   mutate()         four children of the current sound (the Parent)
//   preview(c)       hear child c through the audio-side overlay: no parameter is touched, no host notification
//   keep(c)          ONE Undo snapshot, then the unpinned genes are written with host gestures
//   revert()         pops one generation and restores that generation's parent (one Undo step)
//   addFavourite(c)  <= 8, saved in the <Mutation> state subtree
//
// Generations (<= 16), previews and the overlay are never serialised.

#include "Genome.h"
#include "Overlay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>
#include <vector>

namespace gapfill::mutation
{

inline constexpr int kMaxGenerations = 16;
inline constexpr int kMaxFavourites = 8;
inline constexpr int kMaxFavouriteName = 16;

struct Favourite
{
    juce::String name;
    Genes genes {};
};

struct Generation
{
    Genes parent {};
    ChildSet children;
    std::uint32_t pins = 0;
    int index = 0;
    int kept = -1;                 ///< which child was kept (-1 none)
};

class MutationLab
{
public:
    explicit MutationLab (OverlayState&);

    /** Binds the lab to a processor's parameters. `pushUndo` snapshots the whole sound. */
    void attach (juce::AudioProcessorValueTreeState&, std::function<void()> pushUndo);
    bool isAttached() const noexcept   { return apvts != nullptr; }

    //--- persisted settings -------------------------------------------------------
    static constexpr std::uint32_t defaultSeed() noexcept { return 0x6a7b1c2du; }
    std::uint32_t seed = defaultSeed();
    float rate = kDefaultRate;
    int scope = 0;
    std::uint32_t pins = 0;
    std::vector<Favourite> favourites;

    void setRate (float r) noexcept        { rate = std::min (kMaxRate, std::max (kMinRate, r)); }
    void setScope (int s) noexcept         { scope = std::min (2, std::max (0, s)); }
    void setPinned (int gene, bool on) noexcept;
    bool isPinned (int gene) const noexcept { return gene >= 0 && gene < kNumGenes && (pins & (1u << gene)) != 0; }
    void reseed();

    //--- session -----------------------------------------------------------------
    Genes readParameters() const;
    void mutate();
    bool breedWith (int favouriteIndex);
    int numGenerations() const noexcept            { return (int) generations.size(); }
    const Generation* currentGeneration() const noexcept { return generations.empty() ? nullptr : &generations.back(); }

    bool preview (int child);
    void endPreview();
    int previewing() const noexcept                { return previewChild; }

    bool keep (int child);
    bool revert();
    bool addFavourite (int child);
    bool removeFavourite (int index);

    /** Called by the panel's timer (15 Hz): retries a full overlay queue and ends the preview when
        anything outside it changed the sound (preset change, Undo, state load, automation). */
    void tick();

    int overlayMessages() const noexcept { return sentMessages; }   // test hook

private:
    void sendOverlay (bool active, const Genes*);
    void writeGenes (const Genes&, std::uint32_t skipMask, int count);

    OverlayState& overlay;
    juce::AudioProcessorValueTreeState* apvts = nullptr;
    std::function<void()> pushUndo;
    std::vector<Generation> generations;
    int generationCounter = 0;
    int previewChild = -1;
    Genes previewBaseline {};
    std::optional<OverlayMsg> pendingMsg;
    int sentMessages = 0;
};

//--- state subtree (registered in StateSubtrees as "Mutation", cap 2 KiB) -----------------------------------
bool validateMutationSubtree (const juce::ValueTree&);
void applyMutationSubtree (MutationLab&, const juce::ValueTree&);
juce::ValueTree serialiseMutationSubtree (const MutationLab&);
void resetMutationSubtree (MutationLab&);

} // namespace gapfill::mutation
