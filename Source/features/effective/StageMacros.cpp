#include "Effective.h"
#include "../macros/MacroEngine.h"
#include "../modulation/Runtime.h"

namespace gapfill::Effective
{

// C-2 / F-M23. Neutral (identity, outputs untouched) while macros.on is false or no runtime is attached.
void applyMacros (EngineSettings& s, const MacroState& st, MacroOutputs& out)
{
    if (! st.on) return;
    if (st.extendedRuntime != nullptr)
    {
        auto& runtime = *st.extendedRuntime;
        runtime.inputs = st.extendedInputs;
        runtime.inputs.knob01[0] = st.activity * .01f;
        runtime.inputs.knob01[1] = st.chaos * .01f;
        runtime.inputs.knob01[2] = st.space * .01f;
        runtime.inputs.knob01[3] = st.destruction * .01f;
        runtime.claimOwnership (s);
        s.perf.extendedMacroRuntime = &runtime;
        s.perf.macroRuntime = nullptr;
        s.perf.macroFxSend01 = 1;
        out.space01 = runtime.inputs.knob01[2];
        out.destruction01 = runtime.inputs.knob01[3];
        return;
    }
    if (st.runtime == nullptr) return;

    const float knobs[macros::kNumMacros] = { st.activity * 0.01f, st.chaos * 0.01f, st.space * 0.01f, st.destruction * 0.01f };
    st.runtime->evaluateBlock (s, knobs, s.perf.macroOwnedMask);
    s.perf.macroRuntime = st.runtime;

    out.space01 = knobs[2];
    out.destruction01 = knobs[3];
}

// F-M23: LFO macros are re-evaluated at the PPQ of the decision (block-size independent).
void applyMacroLfos (EngineSettings& s, double ppq, const TransportInfo& ts)
{
    if (s.perf.extendedMacroRuntime != nullptr)
    {
        const modulation::DecisionInputs at { ppq, ts.valid, ts.outputPos, ts.sampleRate, ts.energy01,
                                              (std::uint64_t) (std::uint32_t) s.seed, ts.tsNum, ts.tsDen };
        s.perf.macroFxSend01 = s.perf.extendedMacroRuntime->evaluateDecision (s, at).fxSend01;
        return;
    }
    if (ts.valid && s.perf.macroRuntime != nullptr)
        s.perf.macroRuntime->evaluateDecision (s, ppq);
}

} // namespace gapfill::Effective
