#include "PerformanceTargets.h"
#include "../../dsp/AnalysisEngine.h"
#include "../effective/Effective.h"
#include "../breathing/Breathing.h"
#include "../budget/Budget.h"
#include "../evolve/Evolve.h"
#include "../phrase/Phrase.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace gapfill::performanceTargets
{
namespace
{
    int integer (float value, int low, int high) noexcept
    { return (int) std::lround (std::clamp (value, (float) low, (float) high)); }

    struct Row
    {
        const char* id;
        void (*engine) (EngineSettings&, float) noexcept = nullptr;
        void (*analysis) (AnalysisSettings&, float) noexcept = nullptr;
        void (*modulation) (Effective::ModulationInputs&, float) noexcept = nullptr;
    };
   #define PCT(id, field) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v * .01f; } }
   #define REAL(id, field) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v; } }
   #define INT(id, field, lo, hi) { id, +[] (EngineSettings& s, float v) noexcept { s.field = integer (v, lo, hi); } }
   #define FLAG(id, field) { id, +[] (EngineSettings& s, float v) noexcept { s.field = v > .5f; } }
   #define ANALYSIS(id, field, value) { id, nullptr, +[] (AnalysisSettings& s, float v) noexcept { s.field = value; } }
   #define MOD(id, field, value) { id, nullptr, nullptr, +[] (Effective::ModulationInputs& s, float v) noexcept { s.field = value; } }
    constexpr Row rows[] =
    {
        FLAG ("pingPong", pingPong), FLAG ("fl01_lock", perf.lockOn), PCT ("mistakes", perf.mistakes01),
        INT ("fillShape", perf.envShape, 0, 4), PCT ("fillShapeAmount", perf.envAmount01),
        { "evolve", +[] (EngineSettings& s, float v) noexcept { s.perf.evolve.periodBars = evolve::kPeriodChoices[integer (v, 0, 3)]; } },
        INT ("bandLowType", perf.bandType[0], 0, numAlgos), INT ("bandMidType", perf.bandType[1], 0, numAlgos),
        INT ("bandHighType", perf.bandType[2], 0, numAlgos),
        MOD ("personality", personality, integer (v, 0, 8)), MOD ("macrosOn", macros.on, v > .5f),
        MOD ("macroActivity", macros.activity, v), MOD ("macroChaos", macros.chaos, v),
        MOD ("macroSpace", macros.space, v), MOD ("macroDestruction", macros.destruction, v),
        { "scaleRoot", +[] (EngineSettings& s, float v) noexcept { s.scaleLock.set = scale::makeScaleSet (integer (v, 0, 11), s.scaleLock.set.type); } },
        { "scaleType", +[] (EngineSettings& s, float v) noexcept { s.scaleLock.set = scale::makeScaleSet (s.scaleLock.set.root, integer (v, 0, 12)); } },
        FLAG ("scaleFollow", perf.key.autoOn), REAL ("fx03_window", perf.key.windowSeconds),
        PCT ("fx03_minConf", perf.key.minConfidence01), FLAG ("fx03_hold", perf.key.hold),
        { "phraseBars", +[] (EngineSettings& s, float v) noexcept { s.perf.phrase.bars = phrase::kBarChoices[integer (v, 0, 4)]; } },
        PCT ("phraseBias", perf.phrase.bias01), INT ("phraseOffset", perf.phrase.offsetBars, 0, 15),
        FLAG ("densityBreath", perf.breath.on),
        { "densityBreathBars", +[] (EngineSettings& s, float v) noexcept { s.perf.breath.bars = breathing::kBarChoices[integer (v, 0, 3)]; } },
        PCT ("densityBreathDepth", perf.breath.depth01), INT ("densityBreathCurve", perf.breath.curve, 0, breathing::NumCurves - 1),
        FLAG ("fm21_on", perf.tension.on),
        { "fm21_bars", +[] (EngineSettings& s, float v) noexcept { s.perf.tension.bars = 2 << integer (v, 0, 4); } },
        INT ("fm21_start", perf.tension.startBar, 0, 255), INT ("fm21_shape", perf.tension.shape, 0, tension::NumShapes - 1),
        INT ("fm21_end", perf.tension.end, 0, 2), PCT ("fm21_depthDensity", perf.tension.depth[tension::Density]),
        PCT ("fm21_depthIntensity", perf.tension.depth[tension::Intensity]), PCT ("fm21_depthWidth", perf.tension.depth[tension::Width]),
        PCT ("fm21_depthFx", perf.tension.depth[tension::FxSend]),
        INT ("fl09_grid", perf.groove.grid, 0, 1), FLAG ("fl09_hold", perf.groove.hold),
        INT ("fl10_mode", perf.call.mode, 0, 2), PCT ("fl10_strength", perf.call.strength01),
        INT ("fl10_calls", perf.call.calls, 2, 4), INT ("fl10_replies", perf.call.replies, 1, 4),
        INT ("energyTarget", perf.energy.target, 0, 2),
        { "battleEvery", +[] (EngineSettings& s, float v) noexcept { constexpr int bars[] = { 0, 1, 2, 4, 8 }; s.perf.battleEvery = bars[integer (v, 0, 4)]; } },
        { "fillBudget", +[] (EngineSettings& s, float v) noexcept { s.perf.budget.perBar = budget::kBudgetChoices[integer (v, 0, 6)]; } },
        INT ("quietZone", perf.budget.zone, 0, budget::NumZones - 1),
        { "detMode", +[] (EngineSettings& s, float v) noexcept { s.mode = (DetMode) integer (v, 0, 2); },
                     +[] (AnalysisSettings& s, float v) noexcept { s.mode = (DetMode) integer (v, 0, 2); } },
        ANALYSIS ("detSource", source, (DetSource) integer (v, 0, 2)),
        ANALYSIS ("sensitivity", sensitivity01, v * .01f), ANALYSIS ("gapThresh", gapThresh01, v * .01f),
        ANALYSIS ("gapRelease", gapReleaseMs, v), ANALYSIS ("retrigGuard", retrigGuardMs, v),
        ANALYSIS ("protectHold", protectHoldMs, v),
        ANALYSIS ("xoverLM", xoverLowMid, v), ANALYSIS ("xoverMH", xoverMidHigh, v),
        INT ("maxFillSync", perf.sceneMaxFillSync, 0, 1),
        INT ("maxFillDiv", perf.sceneMaxFillDivision, 0, (int) Division::NumDivisions - 1),
        REAL ("protLow", perf.sceneProtectLowHz), PCT ("protCenter", perf.sceneProtectCenter01),
        MOD ("fm23_macro5", macros.extendedInputs.knob01[4], std::clamp (v * .01f, 0.0f, 1.0f)),
        MOD ("fm23_macro6", macros.extendedInputs.knob01[5], std::clamp (v * .01f, 0.0f, 1.0f)),
        MOD ("fm23_macro7", macros.extendedInputs.knob01[6], std::clamp (v * .01f, 0.0f, 1.0f)),
        MOD ("fm23_macro8", macros.extendedInputs.knob01[7], std::clamp (v * .01f, 0.0f, 1.0f)),
        MOD ("fm23_src1", macros.extendedInputs.source[0], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src2", macros.extendedInputs.source[1], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src3", macros.extendedInputs.source[2], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src4", macros.extendedInputs.source[3], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src5", macros.extendedInputs.source[4], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src6", macros.extendedInputs.source[5], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src7", macros.extendedInputs.source[6], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_src8", macros.extendedInputs.source[7], (gapfill::modulation::MacroSource) integer (v, 0, 3)),
        MOD ("fm23_depth1", macros.extendedInputs.depth[0], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth2", macros.extendedInputs.depth[1], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth3", macros.extendedInputs.depth[2], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth4", macros.extendedInputs.depth[3], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth5", macros.extendedInputs.depth[4], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth6", macros.extendedInputs.depth[5], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth7", macros.extendedInputs.depth[6], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_depth8", macros.extendedInputs.depth[7], std::clamp (v * .01f, -1.0f, 1.0f)),
        MOD ("fm23_lfoAShape", macros.extendedInputs.lfo[0].shape, (gapfill::modulation::LfoShape) integer (v, 0, 5)),
        MOD ("fm23_lfoBShape", macros.extendedInputs.lfo[1].shape, (gapfill::modulation::LfoShape) integer (v, 0, 5)),
        MOD ("fm23_lfoADiv", macros.extendedInputs.lfo[0].division, (std::uint8_t) integer (v, 0, 15)),
        MOD ("fm23_lfoBDiv", macros.extendedInputs.lfo[1].division, (std::uint8_t) integer (v, 0, 15)),
        MOD ("fm23_lfoAHz", macros.extendedInputs.lfo[0].freeHz, std::clamp (v, .05f, 10.0f)),
        MOD ("fm23_lfoBHz", macros.extendedInputs.lfo[1].freeHz, std::clamp (v, .05f, 10.0f))
    };
   #undef PCT
   #undef REAL
   #undef INT
   #undef FLAG
   #undef ANALYSIS
   #undef MOD
    static_assert (std::size (rows) < 255);
    bool valid (int index, float value) noexcept
    { return index >= 0 && index < (int) std::size (rows) && std::isfinite (value); }
}
int indexForId (const char* id) noexcept
{
    for (int i = 0; i < (int) std::size (rows); ++i)
        if (std::strcmp (id, rows[i].id) == 0) return i;
    return -1;
}
void apply (EngineSettings& s, int index, float real) noexcept
{
    if (valid (index, real) && rows[index].engine != nullptr) rows[index].engine (s, real);
}
void applyAnalysis (AnalysisSettings& s, int index, float real) noexcept
{
    if (valid (index, real) && rows[index].analysis != nullptr) rows[index].analysis (s, real);
}
void applyModulation (Effective::ModulationInputs& s, int index, float real) noexcept
{
    if (valid (index, real) && rows[index].modulation != nullptr) rows[index].modulation (s, real);
}
}
