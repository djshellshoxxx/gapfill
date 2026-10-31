#pragma once

// Startup-resolved scene destinations outside the effect-card table. Setters
// are bounded arithmetic; each projection runs only at its owning DSP stage.
namespace gapfill
{
struct EngineSettings;
struct AnalysisSettings;
namespace Effective { struct ModulationInputs; }
namespace performanceTargets
{
int indexForId (const char*) noexcept;
void apply (EngineSettings&, int index, float real) noexcept;
void applyAnalysis (AnalysisSettings&, int index, float real) noexcept;
void applyModulation (Effective::ModulationInputs&, int index, float real) noexcept;
}
}
