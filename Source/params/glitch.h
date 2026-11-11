#pragma once

// F-G01 Glitch rack parameters (GLITCH tab). Appended as the GLITCH group at the
// permanent registry tail (after BATTLEV2); IDs are append-only.

#include <juce_audio_processors/juce_audio_processors.h>

#define GAPFILL_PARAMS_GLITCH(X) \
    X (glitchOn, "fg01_on") \
    X (glitchTarget, "fg01_target") \
    X (glitchMix, "fg01_mix") \
    X (glitchSwing, "fg01_swing") \
    X (glitchTimeVar, "fg01_timeVar") \
    X (glitchStutMode, "fg01_stutMode") \
    X (glitchStutDiv, "fg01_stutDiv") \
    X (glitchStutMs, "fg01_stutMs") \
    X (glitchStutRepeats, "fg01_stutRepeats") \
    X (glitchStutProb, "fg01_stutProb") \
    X (glitchStutRev, "fg01_stutRev") \
    X (glitchStutVar, "fg01_stutVar") \
    X (glitchStutPick, "fg01_stutPick") \
    X (glitchStutGate, "fg01_stutGate") \
    X (glitchStutDecay, "fg01_stutDecay") \
    X (glitchStutSmooth, "fg01_stutSmooth") \
    X (glitchStab, "fg01_stab") \
    X (glitchStabInf, "fg01_stabInf") \
    X (glitchStabDiv, "fg01_stabDiv") \
    X (glitchStabRepeats, "fg01_stabRepeats") \
    X (glitchStabDecay, "fg01_stabDecay") \
    X (glitchStabGate, "fg01_stabGate") \
    X (glitchStabRelease, "fg01_stabRelease") \
    X (glitchRevMode, "fg01_revMode") \
    X (glitchRevDiv, "fg01_revDiv") \
    X (glitchRevMs, "fg01_revMs") \
    X (glitchRevProb, "fg01_revProb") \
    X (glitchRevMix, "fg01_revMix") \
    X (glitchRevFade, "fg01_revFade") \
    X (glitchGateMode, "fg01_gateMode") \
    X (glitchGateDiv, "fg01_gateDiv") \
    X (glitchGateHz, "fg01_gateHz") \
    X (glitchGateLen, "fg01_gateLen") \
    X (glitchGateDepth, "fg01_gateDepth") \
    X (glitchGateAttack, "fg01_gateAttack") \
    X (glitchGateRelease, "fg01_gateRelease") \
    X (glitchGatePattern, "fg01_gatePattern") \
    X (glitchGateVar, "fg01_gateVar") \
    X (glitchGateStereo, "fg01_gateStereo") \
    X (glitchFiltMode, "fg01_filtMode") \
    X (glitchFiltSync, "fg01_filtSync") \
    X (glitchFiltDiv, "fg01_filtDiv") \
    X (glitchFiltHz, "fg01_filtHz") \
    X (glitchFiltMin, "fg01_filtMin") \
    X (glitchFiltMax, "fg01_filtMax") \
    X (glitchFiltDepth, "fg01_filtDepth") \
    X (glitchFiltRes, "fg01_filtRes") \
    X (glitchFiltShape, "fg01_filtShape") \
    X (glitchFiltStereo, "fg01_filtStereo") \
    X (glitchPhMix, "fg01_phMix") \
    X (glitchPhStages, "fg01_phStages") \
    X (glitchPhSync, "fg01_phSync") \
    X (glitchPhDiv, "fg01_phDiv") \
    X (glitchPhHz, "fg01_phHz") \
    X (glitchPhDepth, "fg01_phDepth") \
    X (glitchPhFb, "fg01_phFb") \
    X (glitchPhCentre, "fg01_phCentre") \
    X (glitchPhStereo, "fg01_phStereo") \
    X (glitchPolL, "fg01_polL") \
    X (glitchPolR, "fg01_polR") \
    X (glitchPanInv, "fg01_panInv") \
    X (glitchFlipMode, "fg01_flipMode") \
    X (glitchFlipDiv, "fg01_flipDiv") \
    X (glitchFlipProb, "fg01_flipProb")

namespace gapfill
{
void addGlitchParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);
void addGlitchPerformanceIds (juce::StringArray&);
} // namespace gapfill
