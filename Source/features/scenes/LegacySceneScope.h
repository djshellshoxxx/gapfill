#pragma once

// Frozen from released e89aecd SceneModel.cpp and its parameter groups.
// Do not expand these lists when new scope membership is added.
namespace gapfill::scenes::legacy
{
inline constexpr const char* scopeIds[8] =
{
    "fill,intensity,complexity,chaos,density,fillType",
    "wReverse,wStutter,wDelay,wGranular,wFreeze,wPitchRise,wPitchFall,wFilter,wTexture,wCrush,"
    "wDistort,wScramble,wSpectral,wComb,wTapeStop",
    "swellAmount,swellCurve,swellFilter,swellTarget,cloudAmount,cloudDensity,cloudSize,cloudSpan,"
    "cloudJitter,cloudPitch,cloudSpread,ratAmount,ratDiv,ratRepeats,ratAccel,ratGate,rcAmount,"
    "rcBits,rcRate,rcHold,rcTone,tdAmount,tdWow,tdFlutter,tdSat,tdDropout,tdSplice,resAmount,"
    "resNote,resMode,resDecay,resDamp,resSpread,fsdAmount,fsdShift,fsdTime,fsdFeedback,fsdDamp,"
    "fsdStereo,dubAmount,dubDiv,dubFeedback,dubLow,dubHigh,dubMove,dubSat,dubPing,fmtAmount,"
    "fmtShift,fmtTilt,fmtVowel,sfzAmount,sfzLength,sfzDecay,sfzSmear,sfzMorph,sfzPitch,sfzWidth,"
    "cvAmount,cvSize,cvDamp,scrubOn,scrubPos,scrubWidth,bandLowFx,bandMidFx,bandHighFx",
    "scaleRoot,scaleType,scaleAmount,scaleFollow,fx03_window,fx03_minConf,fx03_hold,phraseBars,"
    "phraseBias,phraseOffset,densityBreath,densityBreathBars,densityBreathDepth,densityBreathCurve,"
    "fm21_on,fm21_bars,fm21_start,fm21_shape,fm21_end,fm21_depthDensity,fm21_depthIntensity,"
    "fm21_depthWidth,fm21_depthFx,fl09_amount,fl09_grid,fl09_hold,fl10_mode,fl10_strength,"
    "fl10_calls,fl10_replies,energyAdapt,energyTarget,battleEvery,fillBudget,quietZone",
    "macroActivity,macroChaos,macroSpace,macroDestruction,macrosOn",
    "detMode,detSource,sensitivity,gapThresh,minGap,maxFill,maxFillSync,maxFillDiv,gapRelease,"
    "protectHold,retrigGuard",
    "protTrans,protLow,protCenter,protNext,protSC",
    "xoverLM,xoverMH,bandLowOn,bandMidOn,bandHighOn,bandLowAmt,bandMidAmt,bandHighAmt,bandLowInt,"
    "bandMidInt,bandHighInt,bandLowProb,bandMidProb,bandHighProb",
};
} // namespace gapfill::scenes::legacy
