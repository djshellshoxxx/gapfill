#pragma once

// E-1 Fill Audition: the dedicated preview voice that lives OUTSIDE the pool of
// four natural voices (docs/dev/02 R5), plus its request mailbox and status code.
// Audio-thread owned; the editor only ever reads the packed status code that the
// processor publishes through MeterState::previewStatus.

#include "../../dsp/FillVoice.h"
#include "../../dsp/FillRecipe.h"

#include <cstdint>

namespace gapfill
{

enum class PreviewStatus : std::uint8_t
{
    Idle = 0, Playing, RejectedNoSource, RejectedNoBand, RejectedBuild, RejectedBypassed
};

/** MeterState::previewStatus layout: bits 0-7 PreviewStatus, bits 8-15 algo + 1
    (0 = none), bits 16-30 a sequence number that changes on every new
    outcome so the editor can show a rejection for 2 s even when the same
    reason repeats. */
inline constexpr std::uint32_t packPreviewStatus (PreviewStatus s, int algo, std::uint32_t seq) noexcept
{
    return (std::uint32_t) s | (((std::uint32_t) (algo + 1) & 0xFFu) << 8) | ((seq & 0x7FFFu) << 16);
}

inline constexpr PreviewStatus previewStatusOf (std::uint32_t code) noexcept   { return (PreviewStatus) (code & 0xFFu); }
inline constexpr int           previewAlgoOf (std::uint32_t code) noexcept     { return (int) ((code >> 8) & 0xFFu) - 1; }
inline constexpr std::uint32_t previewSeqOf (std::uint32_t code) noexcept      { return (code >> 16) & 0x7FFFu; }

/** One-line status text for a rejection (nullptr for Idle / Playing). */
const char* previewRejectionText (PreviewStatus s) noexcept;

/** Spec limits (docs/dev/03 E-1). */
inline constexpr double kPreviewMaxSeconds   = 2.0;    ///< soft cap, min (maxFill, 2000 ms)
inline constexpr double kPreviewHardCapSecs  = 5.0;
inline constexpr double kPreviewRestartFade  = 0.005;  ///< previous preview fades over 5 ms
inline constexpr std::uint32_t kPreviewStaleMs     = 500;   ///< a Preview command older than this is discarded
inline constexpr std::uint32_t kPreviewHeartbeatMs = 400;   ///< editor: blockCounter must have moved this recently

/** True when a command stamped `issuedMs` (0 = unstamped) is too old to honour. */
inline constexpr bool isStalePreviewCommand (std::uint32_t issuedMs, std::uint32_t nowMs) noexcept
{
    return issuedMs != 0 && (std::uint32_t) (nowMs - issuedMs) > kPreviewStaleMs;
}

struct PreviewRequest
{
    bool pending = false;
    std::uint8_t velocity = 100;
    bool hasRecipe = false;
    FillRecipe recipe;
};

/** The preview voice and the short tail that lets a restarted preview fade out
    over 5 ms without needing a second full voice (a voice's buffers are about
    0.6 MB at 192 kHz; the tail is 2 x 5 ms of floats). */
class PreviewChannel
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();                                   ///< hard clear (prepare / tests)

    /** Fades the preview out (panic, bypass, reset): never a hard cut. */
    void fadeOut (int samples) noexcept;

    /** Moves the currently playing preview (if any) into the fade-out tail. */
    void beginRestart (const SourceHistory&) noexcept;

    void render (juce::AudioBuffer<float>& wet, int startSample, int numSamples,
                 const SourceHistory&) noexcept;

    bool isActive() const noexcept                  { return voice.isActive(); }

    /** Called once per process(): a finished preview returns the status to Idle. */
    void endBlock() noexcept
    {
        if (status == PreviewStatus::Playing && ! voice.isActive())
        {
            status = PreviewStatus::Idle;
            algo = -1;
        }
    }

    FillVoice voice;
    PreviewRequest request;
    PreviewStatus status = PreviewStatus::Idle;
    int algo = -1;
    std::uint32_t seq = 0;
    std::uint32_t counter = 0;      ///< ++ per started preview; reset by FillEngine::reset() only

    std::uint32_t code() const noexcept             { return packPreviewStatus (status, algo, seq); }

private:
    double sr = 48000.0;
    int tailLength = 0;
    juce::AudioBuffer<float> tail;
    int tailPos = 0;
    bool tailActive = false;
};

} // namespace gapfill
