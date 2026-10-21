#pragma once

#include "../Parameters.h"
#include "../features/fx/FxSettings.h"
#include "../features/groove/GrooveSettings.h"
#include "../features/keydetect/KeySettings.h"
#include "../features/scale/ScaleLock.h"

#include <cstdint>

namespace gapfill
{

//==============================================================================
// Performance / creative settings (docs/dev/02-FOUNDATIONS.md F0-5). Every
// field is NEUTRAL at its default: the engine behaves exactly as it did before
// the feature that owns the field existed.

struct BreathSettings  { bool on = false; int bars = 4; float depth01 = 0.0f; int curve = 0; };   // C-6
struct PhraseSettings  { int bars = 0; float bias01 = 0.0f; };                                    // C-7
struct EvolveSettings  { int periodBars = 0; };                                                   // C-8
struct EnergySettings  { float adapt01 = 0.0f; int target = 2; int level = 1; };                  // F-L22: target 0 Density, 1 Intensity, 2 Both; level 0 Low, 1 Mid, 2 High

struct PerformanceSettings
{
    bool  lockOn = false;             // C-5
    float mistakes01 = 0.0f;          // C-3
    int   envShape = 0;               // C-4: 0 Flat
    float envAmount01 = 0.0f;
    BreathSettings breath;            // C-6
    PhraseSettings phrase;            // C-7
    EvolveSettings evolve;            // C-8
    EnergySettings energy;            // F-L22
    groove::GrooveSettings groove;    // F-L09
    keydetect::KeySettings key;       // F-X03
    bool  likeActive = false;         // C-11
    int   quantDivision = -1;         // E-2 fill-length quantise, -1 off
    bool  modSync = false;            // E-2
    float modSyncSeconds = 0.0f;
    int   bandType[3] { 0, 0, 0 };    // C-9: 0 Auto, else Algo+1
    int   tsNum = 0, tsDen = 0;       // host time signature, valid when both > 0
};

//==============================================================================
/** Everything the scheduler needs, captured once per block from the parameters
    and the host playhead. */
struct EngineSettings
{
    DetMode mode = DetMode::Transient;

    float fill01       = 0.35f;
    float intensity01  = 0.50f;
    float complexity01 = 0.40f;
    float chaos01      = 0.25f;
    float density01    = 0.65f;

    /** 0 = Auto, otherwise (Algo index + 1). */
    int   fillTypeIndex = 0;
    float weights[numAlgos] {};

    int   seed = 1;

    float minGapMs      = 60.0f;
    float maxFillMs     = 400.0f;
    float protectHoldMs = 40.0f;
    float lookaheadMs   = 40.0f;

    float protTrans01 = 0.60f;
    float protNext01  = 0.70f;
    float protSC01    = 0.70f;

    float stutterSeconds = 0.125f;
    float delaySeconds   = 0.125f;
    bool  pingPong       = true;

    float xoverLM = 180.0f;
    float xoverMH = 3500.0f;

    bool  bandOn[3]   { true, true, true };
    float bandAmt[3]  { 1.0f, 1.0f, 1.0f };
    float bandInt[3]  { 0.5f, 0.5f, 0.5f };
    float bandProb[3] { 0.45f, 0.75f, 0.9f };

    double ppqAtBlockStart = 0.0;
    double samplesPerBeat  = 0.0;
    bool   transportValid  = false;
    bool   sidechainActive = false;

    /** Output position minus source-history position. Zero while the history
        follows the input; it grows while Capture holds the history frozen so
        fills keep reading the latched material. */
    std::int64_t historyOffset = 0;

    /** Creative / performance settings owned by the feature tickets. */
    PerformanceSettings perf;

    /** Voice / bus effect settings (docs/dev/12 FX-0, WP8). Neutral = every Amount 0. */
    fx::FxSettings fx;

    /** Scale Lock (docs/dev/12 N-MUS). Neutral = type Off. */
    scale::ScaleSettings scaleLock;
};

} // namespace gapfill
