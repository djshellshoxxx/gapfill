#pragma once

// N-USE-a Panic and Recover (docs/dev/12 section 3, N-USE-a).
//
// One always-available action that silences GapFill's WET contribution with a
// FIXED 5 ms linear ramp (independent of the Wet knob smoother, the Lookahead
// and quantisation) and a way back (fixed 20 ms ramp up). The dry path, bypass,
// latency and parameter values are never touched.
//
// Threading: the request side is a pair of atomic counters, so the GUI thread,
// the editor shortcut and the audio-thread MIDI path can all request a panic
// without a queue. Everything else is audio-thread state.
//
// Panicked is NEVER saved in plugin state; options (recovery mode, toggling)
// live in the <Perf> subtree owned by LiveControl.

#include <atomic>
#include <cstdint>
#include <vector>

namespace gapfill::panic
{

enum class RecoveryMode : std::uint8_t { Manual = 0, Auto1s = 1, Auto2s = 2, Auto4s = 3 };

inline constexpr double kRampDownSeconds = 0.005;
inline constexpr double kRampUpSeconds = 0.020;

struct Options
{
    RecoveryMode recovery = RecoveryMode::Manual;
    bool panicToggles = true;     ///< pressing Panic while Panicked == Recover
};

/** What a panic press did (audio thread). */
enum class Action : std::uint8_t { None, Panic, Recover };

class PanicController
{
public:
    //==========================================================================
    // Any thread.
    void requestPanic() noexcept     { panicSeq.fetch_add (1, std::memory_order_release); }
    void requestRecover() noexcept   { recoverSeq.fetch_add (1, std::memory_order_release); }

    /** Message thread: options are read by the audio thread through atomics. */
    void setOptions (const Options&) noexcept;
    Options getOptions() const noexcept;

    /** True from the moment a panic starts until Recover is requested. */
    bool isPanicked() const noexcept { return panicked.load (std::memory_order_acquire); }

    //==========================================================================
    // Audio thread.

    /** Sizes the gain buffer for the largest chunk. Not real-time safe. */
    void prepare (double sampleRate, int maxChunk);

    /** Hard reset (prepareToPlay / reset()): gain 1, un-panicked. */
    void reset() noexcept;

    /** Top of processBlock: reads the request counters. A GUI panic starts at
        sample 0 of this block. Returns what the requests did. */
    Action beginBlock() noexcept;

    /** Direct path for a MIDI-originated panic press: starts the ramp at
        `sampleOffset` of the current block (bypasses every queue). Toggles into
        Recover when already panicked and the option allows it. */
    Action press (int sampleOffset) noexcept;

    /** Recover (button, MIDI, auto). No-op when not panicked. */
    Action recover() noexcept;

    /** Writes the next `n` wet gains (sequential chunks of one block). */
    const float* nextGains (int n) noexcept;

    float currentGain() const noexcept { return gain; }

    /** Samples spent panicked since the last panic (for tests / UI). */
    std::int64_t getPanickedSamples() const noexcept { return panickedSamples; }

private:
    enum class Phase : std::uint8_t { Idle, RampDown, Silent, RampUp };

    void startRampDown() noexcept;
    void startRampUp() noexcept;

    std::atomic<std::uint32_t> panicSeq { 0 }, recoverSeq { 0 };
    std::atomic<bool> panicked { false };
    std::atomic<std::uint8_t> recoveryMode { 0 };
    std::atomic<bool> toggles { true };

    double sr = 48000.0;
    std::uint32_t seenPanic = 0, seenRecover = 0;
    Phase phase = Phase::Idle;
    float gain = 1.0f;
    int blockPos = 0;              ///< samples already handed out in this block
    int pendingPanicAt = -1;       ///< block-relative start of the ramp
    int rampLeft = 0, rampLen = 1;
    float rampFrom = 1.0f;
    std::int64_t panickedSamples = 0;
    std::vector<float> gains;
};

} // namespace gapfill::panic
