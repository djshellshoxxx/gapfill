#include "Effective.h"
#include "../macros/MacroEngine.h"

namespace gapfill::Effective
{

// C-2 / F-M23. Neutral (identity, outputs untouched) while macros.on is false or no runtime is attached.
void applyMacros (EngineSettings& s, const MacroState& st, MacroOutputs& out)
{
    if (! st.on || st.runtime == nullptr)
        return;

    const float knobs[macros::kNumMacros] = { st.activity * 0.01f, st.chaos * 0.01f, st.space * 0.01f, st.destruction * 0.01f };
    st.runtime->evaluateBlock (s, knobs, s.perf.macroOwnedMask);
    s.perf.macroRuntime = st.runtime;

    out.space01 = knobs[2];
    out.destruction01 = knobs[3];
}

// F-M23: LFO macros are re-evaluated at the PPQ of the decision (block-size independent).
void applyMacroLfos (EngineSettings& s, double ppq, const TransportInfo& ts)
{
    if (ts.valid && s.perf.macroRuntime != nullptr)
        s.perf.macroRuntime->evaluateDecision (s, ppq);
}

} // namespace gapfill::Effective
