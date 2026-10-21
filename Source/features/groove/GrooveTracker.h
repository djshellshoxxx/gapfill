#pragma once

// F-L09 Groove inheritance (docs/dev/features/F-L09): the tracker.
//
// Measures how early/late the detected hits sit against the host's PPQ grid,
// per grid slot, with bounded memory (8 slots, exponential averages). Pure and
// allocation-free; times are PPQ values supplied by the caller, so the tracker
// is independent of block size, sample rate and lookahead latency.

#include <array>
#include <cstdint>

namespace gapfill::groove
{

inline constexpr int kSlots = 8;

/** Offsets are in grid steps, positive = late. */
struct GrooveSnapshot
{
    float offsetSteps[kSlots] {};
    float confidence = 0.0f;
    float swingPercent = 50.0f;
    bool  valid = false;           ///< confidence >= kConfidenceGate
};

inline constexpr float kConfidenceGate = 0.35f;
inline constexpr float kMaxOffsetSteps = 0.40f;       ///< hits further from the grid are ignored (not groove)

class GrooveTracker
{
public:
    void reset() noexcept;
    void setGrid (int gridSel) noexcept;                       ///< 0: 1/8 (0.5 beat), 1: 1/16 (0.25 beat); changing clears
    int  grid() const noexcept { return gridSelection; }
    double stepBeats() const noexcept { return gridSelection == 0 ? 0.5 : 0.25; }

    /** A detection event at host PPQ `eventPpq` (INPUT time of the hit). */
    void onEvent (double eventPpq, float strength01) noexcept;

    /** The transport jumped: clear and ignore events until one beat has passed. */
    void onTransportJump() noexcept;

    /** Once per hop / chunk: handles the settle window and idle decay. */
    void tick (double ppqNow, bool transportValid) noexcept;

    GrooveSnapshot snapshot() const noexcept;

    /** Restores a held snapshot (offsets, and a confidence that keeps it usable). */
    void load (const GrooveSnapshot&) noexcept;

    int acceptedEvents() const noexcept { return accepted; }

private:
    struct Slot { float mean = 0.0f, var = 0.0f; std::uint8_t n = 0; double lastPpq = 0.0; };

    std::array<Slot, kSlots> slots {};
    int gridSelection = 1;
    bool settleArmed = false;
    double settleUntil = -1.0e18;
    int accepted = 0;
};

} // namespace gapfill::groove
