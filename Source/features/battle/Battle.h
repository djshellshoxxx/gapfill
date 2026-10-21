#pragma once

// F-M18 Beat Battles: alternate two saved fill styles per bar window
// (docs/dev/12 F-M18).
//
// Style A and B are two snapshots of the sound restricted to the scene whitelist
// (creative, weights, fx, musical). Window index  w = floor (barIndex / N)  with N
// = battleEvery bars (1, 2, 4, 8); even w -> A, odd w -> B. barIndex derives from
// the PPQ and the host time signature, anchored at PPQ 0, so seeking and looping
// reproduce the same pattern. The styles are applied as an OVERLAY at the decision
// stage (like a scene): no APVTS writes, host automation is never touched.
//
// Needs a valid transport AND a host time signature; inactive (status
// NoTransport / NeedsStyles) otherwise, and inactive when A equals B.

#include "../scenes/SceneOverlay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace gapfill::battle
{

inline constexpr std::uint16_t kBattleScope = scenes::ScopeCreative | scenes::ScopeWeights
                                              | scenes::ScopeFx | scenes::ScopeMusical;

/** Published message -> audio (trivially copyable, ~1.5 KB). */
struct BattleConfig
{
    std::uint8_t differ = 0;               ///< both styles stored and not identical
    std::uint8_t pad[3] {};
    std::uint16_t nA = 0, nB = 0;
    std::uint16_t idxA[scenes::kMaxOverlayEntries] {};
    float realA[scenes::kMaxOverlayEntries] {};
    std::uint16_t idxB[scenes::kMaxOverlayEntries] {};
    float realB[scenes::kMaxOverlayEntries] {};
};

/** What the Performance tab shows next to "Battle:". */
enum class Status : int { Off = 0, NeedsStyles = 1, NoTransport = 2, StyleA = 3, StyleB = 4 };

const char* statusText (Status) noexcept;

/** Audio-thread view (stable address inside LiveControl). */
struct BattleRuntime
{
    int everyBars = 0;                     ///< 0 = Off
    bool haveStyles = false;
    scenes::OverlayView a, b;

    /** 0 = A, 1 = B, -1 = inactive for this transport/time signature. */
    int styleAt (double ppq, bool ppqValid, int tsNum, int tsDen) const noexcept;

    void adopt (const BattleConfig&) noexcept;
    void applyAt (EngineSettings&, double ppq, bool ppqValid, int tsNum, int tsDen) const noexcept;
};

/** Pure: the window parity rule. Returns 0 (A) or 1 (B), or -1 when `everyBars` is 0. */
int styleForPosition (double ppq, int tsNum, int tsDen, int everyBars) noexcept;

//==============================================================================
/** Message-thread store of the two styles. Session state only (see docs). */
class BattleModel
{
public:
    /** Stores the in-scope parameters of `state` (an APVTS state tree as written by
        copyState(): <PARAM id value>) as style `which` (0 = A, 1 = B). Parameters
        the tree does not mention take their default. */
    bool setStyleFromState (int which, juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& state);

    /** Stores the current sound as style `which`. */
    bool setStyleFromCurrent (int which, juce::AudioProcessorValueTreeState& apvts);

    void clear();
    bool hasStyle (int which) const noexcept;
    bool stylesDiffer() const noexcept;

    TripleBuffer<BattleConfig>& publisher() noexcept   { return pub; }

private:
    struct Style { bool set = false; std::vector<scenes::ResolvedParam> params; };

    void publish();

    Style style[2];
    TripleBuffer<BattleConfig> pub;
};

} // namespace gapfill::battle
