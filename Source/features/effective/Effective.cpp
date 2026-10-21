#include "Effective.h"

namespace gapfill::Effective
{

void applyBlock (EngineSettings& s, const ModulationInputs& in, MacroOutputs& out)
{
    // Order is part of the contract (creative spec section 8): personality
    // first, then macros on top of it.
    applyPersonality (s, in.personality);
    applyMacros (s, in.macros, out);
}

void applyAtDecision (EngineSettings& sLocal, double ppq, const TransportInfo& ts)
{
    applyScene (sLocal, ppq, ts);
    applyMacroLfos (sLocal, ppq, ts);

    // Evolve slowly moves the base, breathing modulates density, phrase biases.
    applyEvolve (sLocal, ppq, ts);
    applyBreathing (sLocal, ppq, ts);
    applyPhrase (sLocal, ppq, ts);
}

} // namespace gapfill::Effective
