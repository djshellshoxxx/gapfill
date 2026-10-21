#pragma once

// F-D03 Quality modes and adaptive quality (docs/dev/12 F-D03, absorbs F-X07).
//
// qualityMode: Full (default, bit-identical to the baseline), Balanced, Lite, Auto.
//
//   Aspect                     Full   Balanced   Lite
//   effective voice pool         4       3         2
//   SpectralSmear FFT order     10       9         9      (one order lower)
//   Cloud max grains/voice      48      32        16
//   bus-effect tail caps       x1.0    x0.5      x0.25
//   F-L20 / F-X02 / F-L21 cards  on      on     off ("off in Lite")
//   reported latency          unchanged unchanged unchanged
//
// Deviation (docs/progress/WP10.md): the SP-0 STFT size of the bus cards is NOT lowered
// per level; their buffers are allocated once in prepare and a size change would
// allocate. Lite switches the three spectral cards off instead, which is the saving
// that matters. The Auto governor and the switch-when-idle rule are as specified.
//
// Switching: a requested level is applied only at a moment where no voice is
// sounding (engine idle). If it cannot be applied for kForceAfterSeconds, the
// processor fades the voices and the bus effects out (the card fadeOut) and then
// applies it, so a manual or Auto change is never stuck behind a long fill.
//
// Auto: starts Full; steps down one level when the CPU estimate stays above 85 % for
// >= 3 s or two deadline overruns land within 5 s; steps up when it stays below 55 %
// for >= 20 s; minimum dwell 10 s between changes. In non-realtime rendering Auto
// behaves as Full (bounces are reproducible) and the governor is frozen.
// The CPU estimate is the F-S01 CpuMeter (an estimate of this instance only).
//
// AutoGovernor is a pure state machine driven by an injected time step, so its
// hysteresis is tested against simulated CPU series.

#include <cstdint>

namespace gapfill
{
struct EngineSettings;
}

namespace gapfill::quality
{

enum Mode  { ModeFull = 0, ModeBalanced, ModeLite, ModeAuto, NumModes };
enum Level { Full = 0, Balanced = 1, Lite = 2, NumLevels };

struct Caps
{
    int   voices;           ///< effective voice pool
    int   cloudGrains;      ///< max grains per cloud voice
    float tailScale;        ///< multiplier on every bus-effect tail cap
    bool  spectralLower;    ///< SpectralSmear runs one FFT order lower
    bool  spectralCardsOff; ///< F-L20, F-X02, F-X01, F-L21 cards disabled
};

inline constexpr Caps kCaps[NumLevels] =
{
    /* Full     */ { 4, 48, 1.00f, false, false },
    /* Balanced */ { 3, 32, 0.50f, true,  false },
    /* Lite     */ { 2, 16, 0.25f, true,  true  },
};

inline const Caps& capsFor (int level) noexcept
{
    return kCaps[level < 0 ? 0 : (level >= NumLevels ? NumLevels - 1 : level)];
}

const char* levelName (int level) noexcept;

/** Writes the level in force into the engine settings: voice pool level, F-L20/F-X02/F-L21 cards on/off
    (fx.lite), bus-effect tail scale and the cloud grain cap. Full leaves every field at its default. */
void applyToSettings (EngineSettings&, int level) noexcept;

/** "Quality: Balanced (auto)" / "Quality: Lite"; empty for Full in manual mode. */
const char* footerText (int mode, int appliedLevel, char* buffer, int bufferSize) noexcept;

inline constexpr float kHotThreshold = 0.85f;
inline constexpr double kHotSeconds = 3.0;
inline constexpr double kOverrunWindowSeconds = 5.0;
inline constexpr float kCalmThreshold = 0.55f;
inline constexpr double kCalmSeconds = 20.0;
inline constexpr double kMinDwellSeconds = 10.0;
inline constexpr double kForceAfterSeconds = 1.0;

class AutoGovernor
{
public:
    void reset() noexcept;

    /** Advances by `dtSeconds`. `cpu01` is the smoothed estimate, `overrunCount` the
        cumulative number of deadline overruns. Returns the governor's level (0..2). */
    int update (double dtSeconds, float cpu01, std::uint32_t overrunCount) noexcept;

    int level() const noexcept   { return current; }

private:
    int current = Full;
    double hot = 0.0, calm = 0.0, sinceChange = kMinDwellSeconds, clock = 0.0;
    std::uint32_t lastOverruns = 0;
    double overrunAt[2] { -1.0e9, -1.0e9 };
    bool seeded = false;
};

/** Applies the requested level at the right moment and tells the processor what to do. */
class QualityController
{
public:
    struct Step
    {
        int level = Full;          ///< level in force for this chunk
        bool changed = false;      ///< the level changed in this call: reset the spectral processor
        bool forceFade = false;    ///< pending too long: fade voices and bus effects out now
    };

    void reset() noexcept;

    /** Audio thread, once per chunk. `engineIdle`: no voice (any kind) is sounding. */
    Step update (int mode, bool nonRealtime, double dtSeconds, float cpu01, std::uint32_t overrunCount,
                 bool engineIdle) noexcept;

    int appliedLevel() const noexcept     { return applied; }
    int requestedLevel() const noexcept   { return requested; }
    int autoLevel() const noexcept        { return governor.level(); }

private:
    AutoGovernor governor;
    int applied = Full;
    int requested = Full;
    double pendingFor = 0.0;
};

} // namespace gapfill::quality
