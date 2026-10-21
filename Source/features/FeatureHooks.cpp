#include "FeatureHooks.h"
#include "../dsp/FillEngine.h"
#include "fx/FxHooks.h"

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
    //   gatherLock (s, params);          // C-5
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

    fx::gatherFx (s, params);           // WP8: swell, cloud, ratchet, rcrush, tapedestr, bandfx (scrub: F-M19)
}

void dispatchCommand (FillEngine& /*engine*/, const Command& /*command*/) noexcept
{
    // switch (command.type) { case CommandType::Preview: ...; }
}

} // namespace gapfill::hooks
