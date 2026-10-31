#pragma once

// F-L07 recall pipeline, audio-thread half (docs/dev/12 F-L07).
//
//   message thread: SceneController resolves a scene into an Overlay and
//                   publishes it through a TripleBuffer<Overlay> (lock-free).
//   audio thread:   SceneEngine consumes it once per chunk, computes the
//                   quantised boundary from the host PPQ, and exposes an
//                   OverlayView that Effective::applyScene reads at the PPQ of
//                   every scheduling decision (so the result never depends on
//                   where the host cut the block).
//   commit:         when the boundary is reached the engine reports the id; the
//                   message thread writes the values into the APVTS and then
//                   publishes a Clear.

#include "SceneModel.h"
#include "../common/EngineTargets.h"
#include "../common/TripleBuffer.h"
#include "../../dsp/EngineSettings.h"

#include <atomic>
#include <cstdint>

namespace gapfill { struct AnalysisSettings; namespace Effective { struct ModulationInputs; } }

namespace gapfill::scenes
{
enum ModelDomain : std::uint8_t { ModelCloud = 1, ModelGroove = 2 };

inline constexpr int kMaxOverlayEntries = kMaxSceneParams;

/** Published message -> audio. Trivially copyable (~1 KB). */
struct Overlay
{
    enum Kind : std::uint8_t { Set = 0, Cancel = 1, Clear = 2 };

    std::uint8_t kind = Set;
    std::uint8_t quant = (std::uint8_t) Quant::Immediate;   ///< resolved: never Inherit
    std::uint8_t waitForTransport = 0;                       ///< true: do not fall back to Immediate
    std::uint8_t pad = 0;
    std::uint32_t id = 0;                                    ///< 0 is never used by a real request
    std::uint16_t n = 0;
    std::uint16_t idx[kMaxOverlayEntries] {};
    float real[kMaxOverlayEntries] {};
    std::uint8_t fixedBoundary = 0;                         ///< group recall: preserve the published PPQ
    double targetPpq = 0.0;
    std::uint8_t scopeVersion = 2;                          ///< v1 retains released target dispatch
    std::uint8_t externalGroup = 0;                        ///< 0: local request; else coherent co-op guard
    std::uint64_t externalCancelEpoch = 0;
    std::uint8_t modelDomains = 0;
    std::uint8_t cloudModelVersion = 1;
    std::uint8_t grooveModelVersion = 1;
};

/** What the scheduler sees. Owned by the audio thread (stable address). */
struct OverlayView
{
    bool active = false;
    bool immediate = false;
    double boundaryPpq = 0.0;
    int n = 0;
    targets::Ref refs[kMaxOverlayEntries] {};
    float real[kMaxOverlayEntries] {};
    std::uint8_t modelDomains = 0;
    std::uint8_t cloudModelVersion = 1;
    std::uint8_t grooveModelVersion = 1;
    std::uint8_t scopeVersion = 2;

    /** True when the overlay governs a decision taken at `ppq`. */
    bool appliesAt (double ppq, bool ppqValid) const noexcept
    {
        return active && (n > 0 || modelDomains != 0) && (immediate || (ppqValid && ppq >= boundaryPpq));
    }

    void applyTo (EngineSettings&) const noexcept;

    /** Apply effect-card/voice-effect settings and coherent model metadata. Caller first checks
        appliesAt() at the segment start, before effect-chain preparation. */
    void applyFxTo (EngineSettings&) const noexcept;
    /** New scenes project raw base/modulation values before Effective::applyBlock.
        Caller gates appliesAt() for this segment. Legacy raw targets are unchanged;
        their model selectors still apply at the boundary. */
    void applyBaseTo (EngineSettings&, Effective::ModulationInputs&) const noexcept;
    /** Opt-in detector/crossover projection before this segment's analysis. */
    void applyAnalysisTo (AnalysisSettings&) const noexcept;

    void clear() noexcept { active = false; immediate = false; boundaryPpq = 0.0; n = 0; modelDomains = 0; cloudModelVersion = 1; grooveModelVersion = 1; }
};

//==============================================================================
namespace quantiser
{
    /** First boundary strictly after `nowPpq + guardQuarters` for `mode`
        (Beat/Bar/Phrase). `tsNum/tsDen` <= 0 mean 4/4. `phraseBars` <= 0 means
        no phrase grid (Phrase behaves like Bar). */
    double nextBoundary (Quant mode, double nowPpq, double guardQuarters, int tsNum, int tsDen,
                         int phraseBars, int phraseOffsetBars) noexcept;

    double barLength (int tsNum, int tsDen) noexcept;     ///< quarter notes
    double beatLength (int tsDen) noexcept;               ///< quarter notes
}

struct TransportSnap
{
    bool   valid = false;           ///< playing with a valid tempo and position
    double ppq = 0.0;               ///< at the first sample of the chunk
    double samplesPerBeat = 0.0;
    double sampleRate = 48000.0;
    int    tsNum = 0, tsDen = 0;
    int    numSamples = 0;
    int    phraseBars = 0;          ///< 0 = Off
    int    phraseOffset = 0;
};

class SceneEngine
{
public:
    SceneEngine();

    void reset() noexcept;

    struct Result
    {
        std::uint32_t appliedId = 0;       ///< non-zero: the boundary was reached in this chunk
        bool usedTransportFallback = false;
        std::uint32_t missedDeadlineId = 0; ///< fixed group recall was cancelled, never applied late
    };

    /** Audio thread, once per processed chunk (before the engine runs). */
    Result beginChunk (TripleBuffer<Overlay>& pub, const TransportSnap&) noexcept;

    /** Preflight before history/analysis processing: consumes and arms the
        latest request without applying it or advancing the audio timeline.
        The caller must forward any missedDeadlineId to the message thread. */
    Result stagePublished (TripleBuffer<Overlay>& pub, const TransportSnap&) noexcept;

    /** Number of old-setting samples before a pending boundary, clamped to
        0..numSamples. Zero means apply at the start; numSamples means no split
        within this chunk. Pure query, with no queue or timeline side effects. */
    int samplesUntilBoundary (const TransportSnap&) const noexcept;

    /** Drops a not-yet-applied request (panic, SceneCancel). An applied overlay
        stays until the message thread clears it. */
    void cancelPending() noexcept;

    const OverlayView& view() const noexcept            { return currentView; }
    bool hasPending() const noexcept                    { return phase == Phase::Pending; }
    bool isApplied() const noexcept                     { return phase == Phase::Applied; }
    std::uint32_t activeId() const noexcept             { return phase == Phase::None ? 0u : active.id; }
    std::uint8_t pendingGroup() const noexcept         { return phase == Phase::Pending ? active.externalGroup : 0; }
    std::uint64_t pendingCancelEpoch() const noexcept   { return phase == Phase::Pending ? active.externalCancelEpoch : 0; }

    /** For the UI countdown (any thread). boundary < 0: none. */
    double getBoundaryPpqForUi() const noexcept         { return uiBoundary.load (std::memory_order_relaxed); }
    std::uint32_t getPendingIdForUi() const noexcept    { return uiPending.load (std::memory_order_relaxed); }

private:
    enum class Phase : std::uint8_t { None, Pending, Applied };

    void load (const Overlay&) noexcept;
    void arm (const TransportSnap&, Result&) noexcept;
    Result adoptPublished (TripleBuffer<Overlay>&, const TransportSnap&) noexcept;

    Overlay incoming, active;
    OverlayView currentView;
    Phase phase = Phase::None;
    double lastEndPpq = -1.0;
    bool lastValid = false;
    bool boundaryPreflight = false;
    std::atomic<double> uiBoundary { -1.0 };
    std::atomic<std::uint32_t> uiPending { 0 };
};

/** Builds the parameter index -> EngineSettings target table (message thread). */
void initOverlayTargets();

/** The EngineSettings field a parameter index drives (Kind::None when there is none). Audio-thread safe. */
targets::Ref overlayTargetFor (int paramIndex) noexcept;

} // namespace gapfill::scenes
