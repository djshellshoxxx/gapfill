#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace gapfill
{

//==============================================================================
/** Detector modes. */
enum class DetMode  { Transient = 0, Phrase, Spectral, NumModes };

/** Where the event/gap timing comes from. */
enum class DetSource { Main = 0, Sidechain, Hybrid, NumSources };

/** The fill algorithms. Order must stay stable - it is part of the saved state
    (via the fillType choice parameter) and of the per-algorithm weight IDs. */
enum class Algo
{
    Reverse = 0, Stutter, Delay, Granular, Freeze, PitchRise, PitchFall,
    Filter, Texture, Crush, Distort, Scramble, Spectral, Comb, NumAlgos
};

inline constexpr int numAlgos = (int) Algo::NumAlgos;

const char* algoName (Algo a) noexcept;

/** Tempo divisions used by the sync options. */
enum class Division
{
    Whole = 0, Half, Quarter, Eighth, Sixteenth, ThirtySecond,
    QuarterT, EighthT, SixteenthT, QuarterD, EighthD, SixteenthD, NumDivisions
};

/** Length of one division in quarter-note beats. */
double divisionInBeats (int index) noexcept;
juce::StringArray divisionNames();

//==============================================================================
/** Every automatable parameter, as (member name, stable string id).

    The string ids are permanent: they are the persistence + automation keys and
    must never change. Member names are only used for the cached-pointer struct.
*/
#define GAPFILL_PARAMS(X) \
    /* ---- detection ---- */ \
    X (detMode,       "detMode") \
    X (detSource,     "detSource") \
    X (sensitivity,   "sensitivity") \
    X (gapThresh,     "gapThresh") \
    X (minGap,        "minGap") \
    X (maxFill,       "maxFill") \
    X (maxFillSync,   "maxFillSync") \
    X (maxFillDiv,    "maxFillDiv") \
    X (gapRelease,    "gapRelease") \
    X (protectHold,   "protectHold") \
    X (retrigGuard,   "retrigGuard") \
    X (lookahead,     "lookahead") \
    X (audition,      "audition") \
    /* ---- creative macros ---- */ \
    X (fill,          "fill") \
    X (intensity,     "intensity") \
    X (complexity,    "complexity") \
    X (chaos,         "chaos") \
    X (density,       "density") \
    X (fillType,      "fillType") \
    X (seed,          "seed") \
    X (syncEnable,    "syncEnable") \
    X (stutterDiv,    "stutterDiv") \
    X (delayDiv,      "delayDiv") \
    X (pingPong,      "pingPong") \
    X (wetMono,       "wetMono") \
    X (width,         "width") \
    /* ---- auto-mode algorithm weights ---- */ \
    X (wReverse,      "wReverse") \
    X (wStutter,      "wStutter") \
    X (wDelay,        "wDelay") \
    X (wGranular,     "wGranular") \
    X (wFreeze,       "wFreeze") \
    X (wPitchRise,    "wPitchRise") \
    X (wPitchFall,    "wPitchFall") \
    X (wFilter,       "wFilter") \
    X (wTexture,      "wTexture") \
    X (wCrush,        "wCrush") \
    X (wDistort,      "wDistort") \
    X (wScramble,     "wScramble") \
    X (wSpectral,     "wSpectral") \
    X (wComb,         "wComb") \
    /* ---- protection ---- */ \
    X (protTrans,     "protTrans") \
    X (protLow,       "protLow") \
    X (protCenter,    "protCenter") \
    X (protNext,      "protNext") \
    X (protSC,        "protSC") \
    /* ---- spectral lanes ---- */ \
    X (xoverLM,       "xoverLM") \
    X (xoverMH,       "xoverMH") \
    X (bandLowOn,     "bandLowOn") \
    X (bandMidOn,     "bandMidOn") \
    X (bandHighOn,    "bandHighOn") \
    X (bandLowAmt,    "bandLowAmt") \
    X (bandMidAmt,    "bandMidAmt") \
    X (bandHighAmt,   "bandHighAmt") \
    X (bandLowInt,    "bandLowInt") \
    X (bandMidInt,    "bandMidInt") \
    X (bandHighInt,   "bandHighInt") \
    X (bandLowProb,   "bandLowProb") \
    X (bandMidProb,   "bandMidProb") \
    X (bandHighProb,  "bandHighProb") \
    X (bandSolo,      "bandSolo") \
    /* ---- mix ---- */ \
    X (dry,           "dry") \
    X (wet,           "wet") \
    X (output,        "output") \
    X (bypass,        "bypass")

//==============================================================================
/** Cached raw-value pointers so the audio thread never touches the ValueTree. */
struct ParamCache
{
   #define GAPFILL_DECL(member, id) std::atomic<float>* member = nullptr;
    GAPFILL_PARAMS (GAPFILL_DECL)
   #undef GAPFILL_DECL

    void connect (juce::AudioProcessorValueTreeState& state)
    {
       #define GAPFILL_CONNECT(member, id) member = state.getRawParameterValue (id); jassert (member != nullptr);
        GAPFILL_PARAMS (GAPFILL_CONNECT)
       #undef GAPFILL_CONNECT

        algoWeights[(int) Algo::Reverse]   = wReverse;
        algoWeights[(int) Algo::Stutter]   = wStutter;
        algoWeights[(int) Algo::Delay]     = wDelay;
        algoWeights[(int) Algo::Granular]  = wGranular;
        algoWeights[(int) Algo::Freeze]    = wFreeze;
        algoWeights[(int) Algo::PitchRise] = wPitchRise;
        algoWeights[(int) Algo::PitchFall] = wPitchFall;
        algoWeights[(int) Algo::Filter]    = wFilter;
        algoWeights[(int) Algo::Texture]   = wTexture;
        algoWeights[(int) Algo::Crush]     = wCrush;
        algoWeights[(int) Algo::Distort]   = wDistort;
        algoWeights[(int) Algo::Scramble]  = wScramble;
        algoWeights[(int) Algo::Spectral]  = wSpectral;
        algoWeights[(int) Algo::Comb]      = wComb;
    }

    std::atomic<float>* algoWeights[numAlgos] {};
};

/** Relaxed read helper - the audio thread never needs ordering guarantees here. */
inline float pv (const std::atomic<float>* p) noexcept
{
    return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f;
}

inline bool pb (const std::atomic<float>* p) noexcept  { return pv (p) > 0.5f; }
inline int  pi (const std::atomic<float>* p) noexcept  { return (int) std::lround (pv (p)); }

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** All parameter ids, in declaration order (used by randomise / reset / tests). */
juce::StringArray allParameterIDs();

/** The ids that "Randomize Fill" is allowed to touch (creative parameters only). */
juce::StringArray creativeParameterIDs();

/** The ids that "Randomize All" is allowed to touch (never mix/output/latency). */
juce::StringArray randomisableParameterIDs();

} // namespace gapfill
