#include "../TestFramework.h"
#include "features/effective/Effective.h"
#include "features/modulation/Runtime.h"

using namespace gapfill;

GF_TEST (ModulationPipeline, opt_in_decision_targets_precede_tension_and_send_scales_local_fx_once)
{
    modulation::initTargets();
    modulation::Runtime runtime;
    for (auto& count : runtime.cfg.count) count = 0;
    runtime.cfg.count[0] = 3;
    runtime.cfg.map[0][0] = { 4, 0, 0, 0, .5f, .5f, 0 };
    runtime.cfg.map[0][1] = { 32, 0, 0, 0, .5f, .5f, 0 };
    runtime.cfg.map[0][2] = { 43, 0, 0, 0, .25f, .25f, 0 };
    EngineSettings base;
    base.density01 = .1f;
    base.fx.resonator.amount01 = .8f;
    base.fx.tapeExtra.amount01 = .5f;
    base.fx.tapeExtra.mix01 = 1;
    base.perf.tension.on = true;
    base.perf.tension.shape = tension::Build;
    base.perf.tension.depth[tension::Density] = .4f;
    base.perf.tension.depth[tension::FxSend] = 1;
    Effective::ModulationInputs inputs;
    inputs.macros.on = true;
    inputs.macros.extendedRuntime = &runtime;
    Effective::MacroOutputs outputs;
    Effective::applyBlock (base, inputs, outputs);
    GF_EXPECT (base.perf.extendedMacroRuntime == &runtime);
    GF_EXPECT_NEAR (base.density01, .1f, 1e-6f);
    auto decision = base;
    Effective::TransportInfo transport { true, 0, 4, 4, 120 };
    Effective::applyAtDecision (decision, 0, transport);
    GF_EXPECT_NEAR (decision.density01, .4f, 1e-6f);
    GF_EXPECT_NEAR (decision.perf.fxSend01, .125f, 1e-6f);
    GF_EXPECT_NEAR (decision.fx.resonator.amount01, .1f, 1e-6f);
    GF_EXPECT_NEAR (decision.fx.tapeExtra.amount01, .5f, 1e-6f);
    // Since 042ccfe the detailed Tape mix is latched raw at the decision; the voice applies the combined
    // send (perf.fxSend01 -> FillVoiceSetup::recipeReplaySend01, FillVoice.cpp) so a replayed recipe keeps
    // its recorded mix. The send itself is asserted above (.125); DetailedVoice tests cover the voice side.
    GF_EXPECT_NEAR (decision.fx.tapeExtra.mix01, 1.0f, 1e-6f);
    GF_EXPECT_NEAR (base.fx.resonator.amount01, .8f, 1e-6f);
}
