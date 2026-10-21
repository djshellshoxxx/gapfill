#include "FeatureHooks.h"
#include "../dsp/FillEngine.h"
#include "breathing/Breathing.h"
#include "budget/Budget.h"
#include "evolve/Evolve.h"
#include "phrase/Phrase.h"
#include "audition/Audition.h"
#include "lock/LockCommands.h"
#include "lock/LockStage.h"
#include "fx/FxHooks.h"
#include "scale/ScaleHooks.h"
#include "context/ContextState.h"

#include <algorithm>

namespace gapfill::hooks
{

Effective::ModulationInputs gatherModulationInputs (const ParamCache& params) noexcept
{
    Effective::ModulationInputs in;

    in.personality = pi (params.personality);   // C-1
    in.macros.on = pb (params.macrosOn);          // C-2
    in.macros.activity = pv (params.macroActivity);
    in.macros.chaos = pv (params.macroChaos);
    in.macros.space = pv (params.macroSpace);
    in.macros.destruction = pv (params.macroDestruction);
    return in;
}

void gatherFeatureSettings (EngineSettings& s, const ParamCache& params) noexcept
{
    // F-M18 gatherBattle: Off, 1, 2, 4, 8 bars
    static constexpr int kBattleBars[] = { 0, 1, 2, 4, 8 };
    s.perf.battleEvery = kBattleBars[std::clamp (pi (params.battleEvery), 0, 4)];
    // E-2 sync: fill-length grid (-1 = Off) and Sync Mods (the stutter time is added
    // in updateTempoInfo, where the tempo is known).
    s.perf.quantDivision = pi (params.fillQuant) - 1;
    s.perf.modSync       = pb (params.modSync);

    // C-3 controlled mistakes
    s.perf.mistakes01 = pv (params.mistakes) * 0.01f;

    // C-4 intensity envelope
    s.perf.envShape    = pi (params.fillShape);
    s.perf.envAmount01 = pv (params.fillShapeAmount) * 0.01f;

    // C-8 evolve (choice 0..3 -> Off, 8, 16, 32 bars)
    s.perf.evolve.periodBars = evolve::kPeriodChoices[juce::jlimit (0, 3, pi (params.evolve))];

    // C-9 per-band fill type (0 Auto, otherwise Algo + 1)
    s.perf.bandType[0] = juce::jlimit (0, numAlgos, pi (params.bandLowType));
    s.perf.bandType[1] = juce::jlimit (0, numAlgos, pi (params.bandMidType));
    s.perf.bandType[2] = juce::jlimit (0, numAlgos, pi (params.bandHighType));

    // C-6 density breathing (choice 0..3 -> 1, 2, 4, 8 bars)
    s.perf.breath.on       = pb (params.densityBreath);
    s.perf.breath.bars     = breathing::kBarChoices[juce::jlimit (0, 3, pi (params.densityBreathBars))];
    s.perf.breath.depth01  = pv (params.densityBreathDepth) * 0.01f;
    s.perf.breath.curve    = juce::jlimit (0, breathing::NumCurves - 1, pi (params.densityBreathCurve));

    // F-D01 per-bar budget (choice 0..6 -> Off, 1, 2, 3, 4, 6, 8) and quiet zone
    s.perf.budget.perBar = budget::kBudgetChoices[juce::jlimit (0, 6, pi (params.fillBudget))];
    s.perf.budget.zone   = juce::jlimit (0, budget::NumZones - 1, pi (params.quietZone));

    // F-L13 sidechain-driven fills: gate mode, threshold and snap amount (the processor attaches the
    // tracker only while the sidechain bus is connected, so these stay inert otherwise)
    s.perf.sc.gate     = juce::jlimit (0, 2, pi (params.scGate));
    s.perf.sc.threshDb = juce::jlimit (-60.0f, 0.0f, pv (params.scThresh));
    s.perf.sc.snap01   = juce::jlimit (0.0f, 1.0f, pv (params.scSnap) * 0.01f);

    // C-7 phrase awareness (choice 0..4 -> Off, 2, 4, 8, 16 bars)
    s.perf.phrase.bars       = phrase::kBarChoices[juce::jlimit (0, 4, pi (params.phraseBars))];
    s.perf.phrase.bias01     = pv (params.phraseBias) * 0.01f;
    s.perf.phrase.offsetBars = juce::jlimit (0, 15, pi (params.phraseOffset));

    // One explicit call per feature, in this order (add yours below):
    lockstage::gather (s, params);       // C-5
    //   gatherMistakes (s, params);      // C-3
    //   gatherEnvelope (s, params);      // C-4
    //   gatherBreathing (s, params);     // C-6
    //   gatherPhrase (s, params);        // C-7
    //   gatherEvolve (s, params);        // C-8
    //   gatherBandType (s, params);      // C-9
    //   gatherSync (s, params);          // E-2
    //   doc 12 features, in group order: gatherSwell, gatherCloud, gatherScale, gatherKeydet,
    //   gatherBudget, gatherRatchet, gatherRcrush, gatherTapedestr, gatherResonator,
    //   gatherFsdelay, gatherDub, gatherFormant, gatherSpfreeze, gatherConvo, gatherScrub,
    //   gatherHgran, gatherWetout, gatherSafety, gatherMidiout, gatherScdrive, gatherBandfx,
    //   gatherGroove, gatherCall, gatherEnergy, gatherTension, gatherBattle, gatherSection,
    //   gatherLearn, gatherCoop, gatherQuality

    context::gatherContext (s, params); // WP7: F-L22 energy follow (tracker runs in ContextState::onChunk)
    scale::gatherScale (s, params);     // WP7: N-MUS Scale Lock
    fx::gatherFx (s, params);           // WP8: swell, cloud, ratchet, rcrush, tapedestr, bandfx (scrub: F-M19)
}

void dispatchCommand (FillEngine& engine, const Command& command) noexcept
{
    // One line per feature that owns commands; each returns true when it handled `command`.
    if (audition::handleCommand (engine, command))      // E-1: Preview, PreviewRecipe
        return;

    if (lock::handleCommand (engine, command))          // C-5 / C-11: Lock*, Unlock, Like*
        return;
}

} // namespace gapfill::hooks
