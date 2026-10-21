#pragma once

// Plain-data snapshot of the effect parameters (docs/dev/12 FX-0, WP8).
//
// Read once per block by hooks::gatherFeatureSettings (via fx::gatherFx) into
// EngineSettings::fx and latched into VoiceSetup::fxStart when a fill starts, so
// a running voice never changes behaviour mid-fill and the result is
// independent of host block size. Every field is NEUTRAL at its default.

#include <cstdint>

namespace gapfill::fx
{

struct SwellSettings
{
    float amount01 = 0.0f;
    int   curve = 1;            ///< 0 Linear, 1 Exponential, 2 S-curve, 3 Late
    float filter01 = 0.6f;
    int   target = 0;           ///< 0 Gap end, 1 Next beat, 2 Next bar
};

struct CloudSettings
{
    float amount01 = 0.0f;
    float densityHz = 20.0f;    ///< grains per second
    float sizeMs = 60.0f;
    float spanMs = 600.0f;
    float jitter01 = 0.4f;
    float pitchCents = 100.0f;
    float spread01 = 0.6f;
};

struct RatchetSettings
{
    float amount01 = 0.0f;      ///< probability a fill is ratcheted
    int   division = 4;         ///< Division index (1/16)
    int   repeats = 4;
    float accel = 0.0f;         ///< -1..1, negative decelerates
    float gate01 = 0.7f;
};

struct RcrushSettings
{
    float amount01 = 0.0f;      ///< dry/wet blend
    int   bits = 8;
    int   rate = 4;             ///< sample-hold factor when hold is Free
    int   hold = 0;             ///< 0 Free, else Division index + 1
    float tone = 0.0f;          ///< -1..1
};

struct TapeSettings
{
    float amount01 = 0.0f;      ///< master depth
    float wow01 = 0.3f, flutter01 = 0.3f, sat01 = 0.2f, dropout01 = 0.2f, splice01 = 0.0f;
};

struct ScrubSettings
{
    bool  on = false;
    float pos01 = 0.5f;
    float widthMs = 80.0f;
    float windowMs = 1000.0f;   ///< snapshot length; C-10 `captureLength` once Capture v2 exists
};

struct BandFxSettings
{
    bool low = true, mid = true, high = true;

    bool allOn() const noexcept { return low && mid && high; }
    bool forBand (int band) const noexcept { return band < 0 || band > 2 ? true : (band == 0 ? low : (band == 1 ? mid : high)); }
};

// ---- WP9: bus cards (docs/dev/12 section 7) ---------------------------------

struct ResonatorSettings
{
    float amount01 = 0.0f;
    int   note = 60;            ///< MIDI note 24-96
    int   mode = 0;             ///< 0 Manual, 1 Scale degrees
    float decay01 = 0.5f, damp01 = 0.4f, spread01 = 0.3f;
};

struct FsDelaySettings
{
    float amount01 = 0.0f;
    float shiftHz = 50.0f;      ///< -2000..2000
    int   time = 3;             ///< Division index (1/8)
    float feedback01 = 0.4f;    ///< 0-0.85
    float damp01 = 0.3f, stereo01 = 0.4f;
};

struct DubSettings
{
    float amount01 = 0.0f;
    int   div = 3;
    float feedback01 = 0.5f;    ///< 0-0.9
    float lowHz = 120.0f, highHz = 4000.0f;
    float move01 = 0.3f, sat01 = 0.2f;
    bool  ping = true;
};

struct FormantSettings
{
    float amount01 = 0.0f;
    float shiftSt = 0.0f;       ///< formant semitones -12..12
    float tilt01 = 0.0f;        ///< -1..1
    int   vowel = 0;            ///< 0 Shift only, 1..5 = A E I O U
};

struct SpFreezeSettings
{
    float amount01 = 0.0f;
    float lengthMs = 800.0f;
    float decay01 = 0.5f, smear01 = 0.3f, morph01 = 0.0f;
    float pitchSt = 0.0f;
    float width01 = 0.5f;
};

struct ConvoSettings
{
    float amount01 = 0.0f;
    float sizeMs = 120.0f;
    float damp01 = 0.4f;
};

/** Amounts below this are "off" for every card (docs/dev/12 FX-0). */
inline constexpr float kOffThreshold = 0.001f;

struct FxSettings
{
    SwellSettings swell;
    CloudSettings cloud;
    RatchetSettings ratchet;
    RcrushSettings rcrush;
    TapeSettings tape;
    ScrubSettings scrub;
    BandFxSettings bandFx;
    ResonatorSettings resonator;
    FsDelaySettings fsdelay;
    DubSettings dub;
    FormantSettings formant;
    SpFreezeSettings spfreeze;
    ConvoSettings convo;

    /** Quality "Lite" seam (F-L11 quality group, not landed): the spectral and
        convolution cards are disabled while true. */
    bool lite = false;

    /** True when any VoiceFx card could be active for a new fill. */
    bool anyVoiceFx() const noexcept
    {
        return ratchet.amount01 > kOffThreshold || rcrush.amount01 > kOffThreshold
            || tape.amount01 > kOffThreshold;
    }

    /** True when any BusFx card is enabled (Amount only; ringing tails are the chain's business). */
    bool anyBusFx() const noexcept
    {
        return resonator.amount01 > kOffThreshold || fsdelay.amount01 > kOffThreshold
            || dub.amount01 > kOffThreshold || formant.amount01 > kOffThreshold
            || spfreeze.amount01 > kOffThreshold || convo.amount01 > kOffThreshold;
    }
};

/** What a voice's effect chain learns when a fill starts. */
struct VoiceFxStart
{
    FxSettings settings;              ///< latched at fill start
    double samplesPerBeat = 0.0;      ///< 0 = no valid transport
    bool   tempoFallback = false;     ///< transport invalid: fixed fallback tempo was used
    bool   skipVoiceFx = false;       ///< F-L14: this band voice bypasses the VoiceFx cards
    std::uint64_t seed = 1;           ///< voice RNG seed (cards derive salted sub-RNGs)
    int    lengthSamples = 0;         ///< voice length
};

/** Render modes of a voice (recipe flags bit3 / bit4), decided by the scheduler. */
struct VoiceRenderModes
{
    bool swell = false;          ///< N-FX1 Reverse Swell (Algo::Reverse voices)
    bool cloud = false;          ///< N-FX2 Granular Cloud (Algo::Granular voices)
    int  swellLanding = 0;       ///< T: samples from voice start to the swell peak
    int  swellFade = 0;          ///< cosine fade after T
};

/** Fallback tempo for tempo-locked cards without a transport (E-2 replaces this
    with the remembered tempo). */
inline constexpr double kFallbackBpm = 120.0;

} // namespace gapfill::fx
