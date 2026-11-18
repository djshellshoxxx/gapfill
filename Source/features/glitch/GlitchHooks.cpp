#include "GlitchHooks.h"

#include <algorithm>

namespace gapfill::glitch
{
namespace
{
    float pct01 (const std::atomic<float>* a) noexcept { return std::clamp (pv (a) * 0.01f, 0.0f, 1.0f); }

    template <typename Enum>
    Enum choice (const std::atomic<float>* a, int count) noexcept
    {
        return (Enum) std::clamp (pi (a), 0, count - 1);
    }

    int division (const std::atomic<float>* a) noexcept { return std::clamp (pi (a), 0, kNumDivs - 1); }
}

Settings gatherSettings (const ParamCache& p) noexcept
{
    Settings s;
    s.on = pb (p.glitchOn);
    s.target = choice<Target> (p.glitchTarget, 2);
    s.mix01 = pct01 (p.glitchMix);
    s.seed = pi (p.seed);
    s.swing01 = pct01 (p.glitchSwing);
    s.timeVar01 = pct01 (p.glitchTimeVar);

    s.stutMode = choice<ClockMode> (p.glitchStutMode, 3);
    s.stutDiv = division (p.glitchStutDiv);
    s.stutMs = std::clamp (pv (p.glitchStutMs), 10.0f, 2000.0f);
    s.stutRepeats = std::clamp (pi (p.glitchStutRepeats), 1, 16);
    s.stutProb01 = pct01 (p.glitchStutProb);
    s.stutReverse01 = pct01 (p.glitchStutRev);
    s.stutVar01 = pct01 (p.glitchStutVar);
    s.stutPick01 = pct01 (p.glitchStutPick);
    s.stutGate01 = std::clamp (pct01 (p.glitchStutGate), 0.05f, 1.0f);
    s.stutDecay01 = pct01 (p.glitchStutDecay);
    s.stutSmoothMs = std::clamp (pv (p.glitchStutSmooth), 0.5f, 20.0f);

    s.stab = pb (p.glitchStab);
    s.stabInfinite = pb (p.glitchStabInf);
    s.stabDiv = division (p.glitchStabDiv);
    s.stabRepeats = std::clamp (pi (p.glitchStabRepeats), 1, 32);
    s.stabDecay01 = pct01 (p.glitchStabDecay);
    s.stabGate01 = std::clamp (pct01 (p.glitchStabGate), 0.05f, 1.0f);
    s.stabReleaseMs = std::clamp (pv (p.glitchStabRelease), 1.0f, 500.0f);

    s.revMode = choice<ClockMode> (p.glitchRevMode, 3);
    s.revDiv = division (p.glitchRevDiv);
    s.revMs = std::clamp (pv (p.glitchRevMs), 20.0f, 4000.0f);
    s.revProb01 = pct01 (p.glitchRevProb);
    s.revMix01 = pct01 (p.glitchRevMix);
    s.revFadeMs = std::clamp (pv (p.glitchRevFade), 1.0f, 50.0f);

    s.gateMode = choice<ClockMode> (p.glitchGateMode, 3);
    s.gateDiv = division (p.glitchGateDiv);
    s.gateHz = std::clamp (pv (p.glitchGateHz), 0.5f, 64.0f);
    s.gateLen01 = std::clamp (pct01 (p.glitchGateLen), 0.05f, 1.0f);
    s.gateDepth01 = pct01 (p.glitchGateDepth);
    s.gateAttackMs = std::clamp (pv (p.glitchGateAttack), 0.1f, 20.0f);
    s.gateReleaseMs = std::clamp (pv (p.glitchGateRelease), 0.1f, 50.0f);
    s.gatePattern = choice<GatePattern> (p.glitchGatePattern, (int) GatePattern::Count);
    s.gateVar01 = pct01 (p.glitchGateVar);
    s.gateStereo01 = pct01 (p.glitchGateStereo);

    s.filtMode = choice<FilterMode> (p.glitchFiltMode, 4);
    s.filtSync = pb (p.glitchFiltSync);
    s.filtDiv = division (p.glitchFiltDiv);
    s.filtHz = std::clamp (pv (p.glitchFiltHz), 0.01f, 20.0f);
    s.filtMinHz = std::clamp (pv (p.glitchFiltMin), 20.0f, 20000.0f);
    s.filtMaxHz = std::clamp (pv (p.glitchFiltMax), 20.0f, 20000.0f);
    s.filtDepth01 = pct01 (p.glitchFiltDepth);
    s.filtRes01 = pct01 (p.glitchFiltRes);
    s.filtShape = choice<LfoShape> (p.glitchFiltShape, (int) LfoShape::Count);
    s.filtStereoDeg = std::clamp (pv (p.glitchFiltStereo), 0.0f, 180.0f);

    s.phMix01 = pct01 (p.glitchPhMix);
    s.phStages = kPhaserStageChoices[std::clamp (pi (p.glitchPhStages), 0, kNumPhaserStageChoices - 1)];
    s.phSync = pb (p.glitchPhSync);
    s.phDiv = division (p.glitchPhDiv);
    s.phHz = std::clamp (pv (p.glitchPhHz), 0.01f, 10.0f);
    s.phDepth01 = pct01 (p.glitchPhDepth);
    s.phFeedback = std::clamp (pv (p.glitchPhFb) * 0.01f, -0.95f, 0.95f);
    s.phCentreHz = std::clamp (pv (p.glitchPhCentre), 50.0f, 5000.0f);
    s.phStereoDeg = std::clamp (pv (p.glitchPhStereo), 0.0f, 180.0f);

    s.polLeft = pb (p.glitchPolL);
    s.polRight = pb (p.glitchPolR);
    s.panInvert01 = pct01 (p.glitchPanInv);
    s.flipMode = choice<FlipMode> (p.glitchFlipMode, 5);
    s.flipDiv = division (p.glitchFlipDiv);
    s.flipProb01 = pct01 (p.glitchFlipProb);
    return s;
}

} // namespace gapfill::glitch
