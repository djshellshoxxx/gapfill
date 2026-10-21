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

namespace gapfill::scenes
{

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

    /** True when the overlay governs a decision taken at `ppq`. */
    bool appliesAt (double ppq, bool ppqValid) const noexcept
    {
        return active && n > 0 && (immediate || (ppqValid && ppq >= boundaryPpq));
    }

    void applyTo (EngineSettings&) const noexcept;

    void clear() noexcept { active = false; immediate = false; boundaryPpq = 0.0; n = 0; }
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
    };

    /** Audio thread, once per processed chunk (before the engine runs). */
    Result beginChunk (TripleBuffer<Overlay>& pub, const TransportSnap&) noexcept;

    /** Drops a not-yet-applied request (panic, SceneCancel). An applied overlay
        stays until the message thread clears it. */
    void cancelPending() noexcept;

    const OverlayView& view() const noexcept            { return currentView; }
    bool hasPending() const noexcept                    { return phase == Phase::Pending; }
    bool isApplied() const noexcept                     { return phase == Phase::Applied; }
    std::uint32_t activeId() const noexcept             { return phase == Phase::None ? 0u : active.id; }

    /** For the UI countdown (any thread). boundary < 0: none. */
    double getBoundaryPpqForUi() const noexcept         { return uiBoundary.load (std::memory_order_relaxed); }
    std::uint32_t getPendingIdForUi() const noexcept    { return uiPending.load (std::memory_order_relaxed); }

private:
    enum class Phase : std::uint8_t { None, Pending, Applied };

    void load (const Overlay&) noexcept;
    void arm (const TransportSnap&, Result&) noexcept;

    Overlay incoming, active;
    OverlayView currentView;
    Phase phase = Phase::None;
    double lastEndPpq = -1.0;
    bool lastValid = false;
    std::atomic<double> uiBoundary { -1.0 };
    std::atomic<std::uint32_t> uiPending { 0 };
};

/** Builds the parameter index -> EngineSettings target table (message thread). */
void initOverlayTargets();

} // namespace gapfill::scenes
