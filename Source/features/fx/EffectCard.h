#pragma once

// Effect-card framework (docs/dev/12 FX-0, WP8).
//
// One seam so each effect is a separate module that touches no hot file, is
// neutral when off, and obeys panic / reset / bounds uniformly.
//
//  * VoiceFx  - runs on ONE voice's output before the voice is summed. State
//               lives per voice slot (FillVoice owns one VoiceFxChain).
//  * BusFx    - runs once on the wet bus after FillEngine::process and BEFORE
//               the wet post-processing (low protect, centre protect, width),
//               so those still shape effect output.
//
// Contract shared by every card (docs/dev/12 section 7):
//  - Amount < kOffThreshold is "off": the card is never called.
//  - Output is sanitised and hard-limited at +12 dBFS by the chain.
//  - No allocation, locks or I/O after prepare().
//  - reset() clears all state without reallocating; fadeOut(n) lets a running
//    card finish within n samples (panic).

#include "FxSettings.h"
#include "../../dsp/FillRecipe.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace gapfill
{
class SourceHistory;
}

namespace gapfill::fx
{

/** Permanent chain order (docs/dev/12 FX-0). Indices are also the slot in
    FxMeters::cpu. Append only; never renumber. */
enum class CardId : int
{
    Swell = 0, Cloud, Ratchet, TapeDestr, Formant, RCrush,      // voice side
    Resonator, FsDelay, Dub, Convo, SpFreeze,                   // bus side
    Count
};

inline constexpr int kNumCards = (int) CardId::Count;

/** Largest block a VoiceFx card is asked to process in one call. The engine
    schedules on a 32-sample hop, so this is a generous bound. */
inline constexpr int kMaxVoiceBlock = 64;

struct VoiceFxContext
{
    double sampleRate = 48000.0;
    int    samplesLeft = 0;          ///< samples of the voice remaining after this block's first sample
    int    voiceLength = 0;
};

struct FillStartInfo
{
    FillRecipe recipe;
    std::int64_t outputPos = 0;      ///< output-timeline position where the fill starts
};

struct BusFxContext
{
    double sampleRate = 48000.0;
    const FillStartInfo* fillStarts = nullptr;   ///< fills started in this block (<= kMaxFillStarts)
    int numStarts = 0;
    std::int64_t blockStartPos = 0;              ///< output position of sample `start`
    /** The engine's protection duck per sample of the wet buffer (index = buffer
        sample), published only while a BusFx is enabled; null otherwise. */
    const float* duck = nullptr;

    // --- WP9 (bus cards) -------------------------------------------------------
    const FxSettings* settings = nullptr;        ///< this block's effect settings (set by FxChain)
    double samplesPerBeat = 0.0;                 ///< 0 = no valid transport (cards use kFallbackBpm)
    /** Scale Lock seam (N-MUS, WP7): 12-bit mask of the scale relative to its root
        (bit i set = the pitch class `scaleRoot + i` is in the scale). 0 = Scale
        Lock off (cards behave as Manual). */
    std::uint16_t scaleMask = 0;
    int scaleRoot = 0;                           ///< pitch class 0-11
    /** The dry source history (borrowed for the block; read on the audio thread only). */
    const SourceHistory* history = nullptr;
    std::int64_t historyOffset = 0;              ///< output position - historyOffset = history position
};

inline constexpr int kMaxFillStarts = 4;

//==============================================================================
class EffectCard
{
public:
    virtual ~EffectCard() = default;

    virtual CardId id() const noexcept = 0;
    virtual void prepare (double sampleRate, int maxBlock, int channels) = 0;
    virtual void reset() = 0;

    /** Lets a running card wind down within `samples` (panic / disable). */
    virtual void fadeOut (int samples) { juce::ignoreUnused (samples); reset(); }
    virtual bool isIdle() const noexcept { return true; }

    /** Samples by which the card lags the wet path (0 for voice cards). */
    virtual int latencySamples() const noexcept { return 0; }
    /** Longest tail the card may produce after its input stops (ms). */
    virtual int tailCapMs() const noexcept { return 0; }

    /** Amount semantics are per card; "off" cards are never invoked. */
    virtual bool enabled (const FxSettings&) const noexcept = 0;

    /** Number of processVoice/processBus calls (tests: card_off_never_called). */
    std::uint32_t callCount() const noexcept { return calls; }

protected:
    void noteCall() noexcept { ++calls; }

private:
    std::uint32_t calls = 0;
};

//==============================================================================
class VoiceFx : public EffectCard
{
public:
    /** A new fill starts on this voice. Returns true when the card will
        process this fill (it decides, e.g. by its probability roll). */
    virtual bool startFill (const VoiceFxStart&) = 0;

    /** In-place processing of one voice's block (n <= kMaxVoiceBlock). */
    virtual void processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept = 0;
};

class BusFx : public EffectCard
{
public:
    /** In-place processing of wet[start, start + n). */
    virtual void processBus (juce::AudioBuffer<float>& wet, int start, int n, const BusFxContext&) noexcept = 0;
};

} // namespace gapfill::fx
