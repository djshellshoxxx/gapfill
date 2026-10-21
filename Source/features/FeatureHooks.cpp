#include "FeatureHooks.h"
#include "../dsp/FillEngine.h"
#include "audition/Audition.h"
#include "lock/LockCommands.h"
#include "lock/LockStage.h"
#include "fx/FxHooks.h"
#include "scale/ScaleHooks.h"
#include "context/ContextState.h"

namespace gapfill::hooks
{

Effective::ModulationInputs gatherModulationInputs (const ParamCache& /*params*/) noexcept
{
    Effective::ModulationInputs in;

    // C-1: in.personality = pi (params.personality);
    // C-2: in.macros = { pb (params.macrosOn), pv (params.macroActivity), ... };
    return in;
}

void gatherFeatureSettings (EngineSettings& s, const ParamCache& params) noexcept
{
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
