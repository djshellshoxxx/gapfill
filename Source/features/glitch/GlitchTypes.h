#pragma once

// F-G01 Glitch rack: settings snapshot and shared constants.
//
// Portable core (no JUCE): the processor fills a Settings value from its parameter
// cache once per chunk (GlitchHooks.cpp) and hands it to GlitchRack::process.
// Every module is neutral at its default, and the rack is skipped entirely while
// it is off, so existing projects render bit-identically.
//
// Author: GapFill contributors, 2026-11-07. Licence: see LICENSE.

#include <array>
#include <cstdint>

namespace gapfill::glitch
{

/** Rhythmic divisions used by every tempo-synced glitch module, shortest first.
    Index values are stored in host parameters: append only. */
enum class Div : int
{
    D64 = 0, D32, D16T, D16, D8T, D16D, D8, D4T, D8D, D4, D2, Bar1, Bar2, Bar4, Bar8, Count
};

inline constexpr int kNumDivs = (int) Div::Count;

/** Division length in quarter-note beats. Bars use the time signature (4/4 when unknown). */
inline double divBeats (int div, int tsNum = 4, int tsDen = 4) noexcept
{
    static constexpr std::array<double, kNumDivs> beats { 0.0625, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5,
                                                          2.0 / 3.0, 0.75, 1.0, 2.0, -1.0, -2.0, -4.0, -8.0 };
    const auto i = div < 0 ? 0 : (div >= kNumDivs ? kNumDivs - 1 : div);
    const auto b = beats[(std::size_t) i];

    if (b > 0.0)
        return b;

    const auto num = tsNum > 0 ? tsNum : 4;
    const auto den = tsDen > 0 ? tsDen : 4;
    return -b * (double) num * 4.0 / (double) den;
}

inline constexpr const char* kDivNames[kNumDivs] = { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8",
                                                     "1/4T", "1/8.",  "1/4",  "1/2",   "1 bar", "2 bars",
                                                     "4 bars", "8 bars" };

enum class Target : int { Fills = 0, Output };                  ///< where the rack is inserted
enum class ClockMode : int { Off = 0, Tempo, Free };             ///< stutter, gator, reverse
enum class FilterMode : int { Off = 0, LowPass, HighPass, BandPass };
enum class LfoShape : int { Sine = 0, Triangle, RampUp, RampDown, Square, RandomStep, Count };
enum class FlipMode : int { Off = 0, Swap, InvertLeft, InvertRight, InvertBoth };
enum class GatePattern : int { Straight = 0, Offbeat, Dotted, Tresillo, Euclid5, Gallop, Random, Count };

inline constexpr int kPhaserStageChoices[] = { 2, 4, 6, 8, 12 };
inline constexpr int kNumPhaserStageChoices = 5;

/** Upper bounds that size every buffer in prepare(); nothing grows afterwards. */
inline constexpr double kMaxSliceSeconds   = 2.0;   ///< stutter / STAB slice
inline constexpr double kMaxReverseSeconds = 4.0;   ///< reverse segment (1 bar at 60 BPM)
inline constexpr int    kMaxLookbackUnits  = 4;     ///< "Slice pick" reaches this many units back

struct Settings
{
    bool   on = false;
    Target target = Target::Fills;
    float  mix01 = 1.0f;
    int    seed = 1;

    // Timing shared by the grid modules (C: swing and timing).
    float swing01 = 0.0f;           ///< 0 = straight, 1 = 75 % swing (the second step of a pair starts at 3/4)
    float timeVar01 = 0.0f;         ///< deterministic per-step boundary displacement, up to 10 % of a step

    // B: stutter.
    ClockMode stutMode = ClockMode::Off;
    int   stutDiv = (int) Div::D16;
    float stutMs = 125.0f;
    int   stutRepeats = 4;          ///< units per stutter window (1 = capture only)
    float stutProb01 = 0.5f;        ///< probability that a window stutters
    float stutReverse01 = 0.0f;     ///< probability that a repeat plays backwards
    float stutVar01 = 0.0f;         ///< probability that a window uses a neighbouring division
    float stutPick01 = 0.0f;        ///< probability that a window repeats an older slice
    float stutGate01 = 1.0f;        ///< part of each repeat that sounds
    float stutDecay01 = 0.0f;       ///< gain lost per repeat
    float stutSmoothMs = 3.0f;      ///< boundary crossfade

    // I: STAB macro.
    bool  stab = false;             ///< trigger / hold
    bool  stabInfinite = false;     ///< repeat until released instead of a fixed count
    int   stabDiv = (int) Div::D16;
    int   stabRepeats = 4;
    float stabDecay01 = 0.0f;
    float stabGate01 = 1.0f;
    float stabReleaseMs = 30.0f;

    // H: reverse.
    ClockMode revMode = ClockMode::Off;
    int   revDiv = (int) Div::D4;
    float revMs = 500.0f;
    float revProb01 = 1.0f;
    float revMix01 = 1.0f;
    float revFadeMs = 8.0f;

    // D: microgator.
    ClockMode gateMode = ClockMode::Off;
    int   gateDiv = (int) Div::D32;
    float gateHz = 16.0f;
    float gateLen01 = 0.5f;
    float gateDepth01 = 1.0f;
    float gateAttackMs = 1.0f;
    float gateReleaseMs = 3.0f;
    GatePattern gatePattern = GatePattern::Straight;
    float gateVar01 = 0.0f;
    float gateStereo01 = 0.0f;

    // F: HP/LP auto-sweep.
    FilterMode filtMode = FilterMode::Off;
    bool  filtSync = true;
    int   filtDiv = (int) Div::Bar1;
    float filtHz = 0.25f;
    float filtMinHz = 200.0f;
    float filtMaxHz = 8000.0f;
    float filtDepth01 = 1.0f;
    float filtRes01 = 0.2f;
    LfoShape filtShape = LfoShape::Sine;
    float filtStereoDeg = 0.0f;

    // G: phaser.
    float phMix01 = 0.0f;           ///< 0 = off
    int   phStages = 4;
    bool  phSync = false;
    int   phDiv = (int) Div::Bar1;
    float phHz = 0.3f;
    float phDepth01 = 0.7f;
    float phFeedback = 0.3f;        ///< -0.95 .. 0.95
    float phCentreHz = 800.0f;
    float phStereoDeg = 90.0f;

    // E: phase and pan inversion.
    bool  polLeft = false;
    bool  polRight = false;
    float panInvert01 = 0.0f;       ///< 1 = left and right swapped (mirror image)
    FlipMode flipMode = FlipMode::Off;
    int   flipDiv = (int) Div::D8;
    float flipProb01 = 0.5f;
};

/** Host/transport facts for one chunk. */
struct Timing
{
    double sampleRate = 48000.0;
    bool   playing = false;          ///< host transport running with a valid ppq and tempo
    double ppqAtStart = 0.0;
    double samplesPerBeat = 24000.0; ///< host tempo, or the resolved fallback tempo while stopped
    int    tsNum = 4, tsDen = 4;
    bool   bypassed = false;
};

/** True when a settings value would change the signal. */
inline bool anyModuleActive (const Settings& s) noexcept
{
    return s.stutMode != ClockMode::Off || s.stab || s.revMode != ClockMode::Off || s.gateMode != ClockMode::Off
        || s.filtMode != FilterMode::Off || s.phMix01 > 0.0f || s.polLeft || s.polRight || s.panInvert01 > 0.0f
        || s.flipMode != FlipMode::Off;
}

} // namespace gapfill::glitch
