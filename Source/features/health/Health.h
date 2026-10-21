#pragma once

// N-USE-b Transport & Sidechain Health (docs/dev/12 N-USE-b).
//
// Three layers, none of which touches the audio decisions:
//   HealthMonitor  audio thread: a few comparisons per block turn plain
//                  observations into hysteresis-filtered codes, published as ONE
//                  atomic bitmask (no allocation, no locks).
//   HealthTracker  message thread: remembers when each code appeared (for the
//                  detail list) and resolves what the footer shows.
//   describe()/selectFooter()  pure text and priority rules.
//
// Nothing is shown when everything is healthy (no "all OK" ticks).

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace gapfill::health
{

/** Permanent values (persisted as bit positions in the muted mask). Append only. */
enum class HealthCode : std::uint8_t
{
    NoTransport = 0,      ///< no valid tempo/play state for > 1 s while a tempo-dependent feature is on
    TransportJump,        ///< a jump was detected in the last 10 s (informational)
    NoSidechain,          ///< detSource != Main but the sidechain bus is not enabled/connected
    SidechainSilent,      ///< bus connected, level < -80 dBFS for >= 2 s while detSource != Main
    FreeTimingFallback,   ///< E-2 fallback BPM in use
    OverloadWarn,         ///< estimated CPU > 70 % of the block time for >= 2 s
    OverloadCritical,     ///< > 90 % or a deadline overrun in the last 2 s
    QueueOverflow,        ///< fill events / commands were dropped in the last 10 s
    BypassActive,
    PanicActive,          ///< N-USE-a hold
    Count
};

inline constexpr int numHealthCodes = (int) HealthCode::Count;

constexpr std::uint32_t bitOf (HealthCode c) noexcept         { return 1u << (unsigned) c; }
constexpr std::uint32_t allCodesMask = (1u << numHealthCodes) - 1u;

/** PanicActive and OverloadCritical are safety messages: they cannot be muted. */
constexpr bool isMutable (HealthCode c) noexcept
{
    return c != HealthCode::PanicActive && c != HealthCode::OverloadCritical;
}

/** What the audio thread observed during one processed chunk. Plain data. */
struct Observations
{
    int numSamples = 0;
    double sampleRate = 48000.0;

    bool transportValid = false;        ///< host gave a usable tempo / play state
    bool tempoFeatureOn = false;        ///< a tempo-dependent feature is switched on
    bool transportJumped = false;       ///< pulse: a jump was detected in this chunk
    bool scRequested = false;           ///< detSource != Main
    bool scBusEnabled = false;          ///< the sidechain bus is enabled by the host
    float scPeak = 0.0f;                ///< linear peak of the sidechain in this chunk
    bool freeTimingFallback = false;    ///< E-2 fallback BPM in use (tempo feature on, transport invalid, no host tempo seen recently)
    float cpu01 = 0.0f;                 ///< smoothed estimate, telemetry::CpuMeter
    bool deadlineOverrun = false;       ///< pulse: a block took longer than it lasted
    std::uint32_t droppedEvents = 0;    ///< pulse: events/commands dropped since the last chunk
    bool bypassed = false;
    bool panicActive = false;           ///< N-USE-a (wired by WP6 from LiveControl)
};

/** Hold times (seconds). A code is set after the condition has held for `set` and
    cleared after `clear` seconds of absence, so the footer never flickers. */
struct Timing { float set; float clear; };

class HealthMonitor
{
public:
    static constexpr float jumpLatchSeconds = 10.0f;
    static constexpr float overflowLatchSeconds = 10.0f;
    static constexpr float overrunLatchSeconds = 2.0f;
    static constexpr float silenceDb = -80.0f;
    static constexpr float warnCpu = 0.70f;
    static constexpr float criticalCpu = 0.90f;

    static Timing timingFor (HealthCode) noexcept;

    /** Audio thread. Bounded work (ten comparisons), no allocation. */
    void update (const Observations&) noexcept;

    /** Any thread. */
    std::uint32_t getMask() const noexcept     { return mask.load (std::memory_order_relaxed); }

    /** Not while the audio thread runs (prepare / tests). */
    void reset() noexcept;

private:
    struct Slot { float rise = 0.0f; float fall = 0.0f; bool active = false; };

    void step (HealthCode, bool condition, float dt) noexcept;

    std::array<Slot, (size_t) numHealthCodes> slots {};
    float jumpLatch = 0.0f, overflowLatch = 0.0f, overrunLatch = 0.0f;
    float scSilentFor = 0.0f;          ///< seconds the sidechain has been below silenceDb
    std::atomic<std::uint32_t> mask { 0 };
};

//==============================================================================
struct Description
{
    juce::String message;       ///< the footer line
    juce::String explanation;   ///< detail view: what it means
    juce::String action;        ///< detail view: what to do
};

/** Context numbers the texts quote. */
struct DescribeContext
{
    float cpu01 = 0.0f;
    double fallbackBpm = 120.0;
};

Description describe (HealthCode, const DescribeContext& = {});

/** Priority order (highest first): PanicActive > OverloadCritical >
    NoSidechain/SidechainSilent > NoTransport/FreeTimingFallback > OverloadWarn >
    QueueOverflow > BypassActive > TransportJump. */
const std::array<HealthCode, (size_t) numHealthCodes>& priorityOrder() noexcept;

/** The code the footer shows: highest priority among set codes that are not
    muted. Returns false when nothing is to be shown (healthy, or all muted). */
bool highestVisible (std::uint32_t activeMask, std::uint32_t mutedMask, HealthCode& out) noexcept;

/** Message-thread record of when each code appeared (for the detail list). */
class HealthTracker
{
public:
    struct Entry
    {
        HealthCode code = HealthCode::NoTransport;
        std::uint32_t onsetMs = 0;
        bool muted = false;
    };

    /** Feed the published mask; `nowMs` is any monotonic millisecond counter. */
    void update (std::uint32_t activeMask, std::uint32_t nowMs) noexcept;

    /** All active codes in priority order (muted ones included, flagged). */
    int list (Entry* out, int maxOut, std::uint32_t mutedMask) const noexcept;

    std::uint32_t getMask() const noexcept     { return current; }

    /** How long `code` has been active, in seconds (0 when it is not). */
    float secondsActive (HealthCode code, std::uint32_t nowMs) const noexcept
    {
        return (current & bitOf (code)) != 0 ? (float) (std::uint32_t) (nowMs - onset[(size_t) code]) * 0.001f : 0.0f;
    }

private:
    std::uint32_t current = 0;
    std::array<std::uint32_t, (size_t) numHealthCodes> onset {};
};

//==============================================================================
/** What the footer shows, by the U-9 priority order for the statuses this
    package owns: health > fix-it > last gap outcome. (Learn, export, preview and
    capture outrank all of them and live in the editor's own status line.) */
enum class FooterKind { Nothing, Health, Fixit, LastGap, Info, Hover };

struct FooterCandidates
{
    bool haveHealth = false;
    bool haveFixit = false;
    bool haveLastGap = false;
    bool haveInfo = false;     ///< U-9: learn / export / preview / capture (outrank health)
    bool haveHover = false;    ///< U-9: hovered or dragged control (outranks only the last gap line)
};

FooterKind selectFooter (const FooterCandidates&) noexcept;

} // namespace gapfill::health
