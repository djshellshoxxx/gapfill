#pragma once

#include "Targets.h"

namespace gapfill::modulation
{
inline constexpr int kControlCellSamples = 32;

struct ContextBase
{
    TargetBase targets;
    float energyAdapt = 0, scaleAmount = 0, grooveAmount = 0;
};

struct WidthValue { bool active = false; float amount01 = 1; };

/** Audio-thread-owned config and already-pinned inputs. No mutable oscillator,
    RNG, smoothing clock, allocation or publication inside evaluation methods. */
struct Runtime
{
    ExtendedConfig cfg = extendedFromLegacy (macros::defaultConfig());
    ModInputs inputs;
    bool available[kTargetCount];
    mutable float effective01[kTargetCount] {}; ///< Audio-owned readout cache; LiveControl publishes atomics.
    mutable bool active[kTargetCount] {};
    void resetReadout() noexcept;

    Runtime() noexcept;
    bool hasTarget (int target) const noexcept;
    void claimOwnership (EngineSettings&) const noexcept;
    TargetOutputs evaluateDecision (EngineSettings&, const DecisionInputs&) const noexcept;
    TargetOutputs evaluateContinuous (EngineSettings&, const DecisionInputs&) const noexcept;
    ContextBase projectContextTargets (EngineSettings&, const DecisionInputs&) const noexcept;
    static void restoreContextBase (EngineSettings&, const ContextBase&) noexcept;
    WidthValue widthAt (const DecisionInputs&) const noexcept;

    /** Query only after preflight pins configuration, sources and scene projection. */
    bool needsControlGrid() const noexcept;
    static int samplesUntilControlBoundary (std::int64_t outputPos, int maxSamples) noexcept;
    /** Caller obtains Energy at the returned exact sample, not at the host cut. */
    static DecisionInputs controlCellMidpoint (DecisionInputs, double samplesPerBeat) noexcept;

private:
    struct Values { float value[kTargetCount] {}; bool active[kTargetCount] {}; };
    enum class Projection { Decision, Continuous, Context };
    Values valuesAt (const DecisionInputs&) const noexcept;
    TargetOutputs project (EngineSettings&, const Values&, Projection) const noexcept;
};
/** V3 only: settings must contain raw FX. Calls preserve their old schemas1/2 path. */
void applyCombinedEffectSend (EngineSettings&, float send01) noexcept;
} // namespace gapfill::modulation
