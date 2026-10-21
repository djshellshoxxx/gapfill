#include "Effective.h"

namespace gapfill::Effective
{

// C-2 plugs in here. Neutral (identity, outputs untouched) while macros.on is false.
void applyMacros (EngineSettings&, const MacroState&, MacroOutputs&)
{
}

// F-M23 plugs in here (LFO macros sampled at the decision PPQ). Identity while no runtime is attached.
void applyMacroLfos (EngineSettings&, double, const TransportInfo&)
{
}

} // namespace gapfill::Effective
