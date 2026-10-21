#include "Presets.h"
#include <algorithm>

namespace gapfill
{

namespace
{
    constexpr float kAuto = 0.0f;   // fillType index for AUTO

    float algoChoice (Algo a) { return (float) ((int) a + 1); }
}

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets =
    {
        { "Init", "Plain defaults - a good neutral starting point.", "Utility", {} },

        { "Subtle Ear Candy",
          "Occasional short reverses and filtered echoes that never fight the groove.",
          "Subtle",
          { { "fill", 18.0f }, { "intensity", 28.0f }, { "complexity", 15.0f },
            { "chaos", 12.0f }, { "density", 45.0f }, { "fillType", kAuto },
            { "minGap", 90.0f }, { "maxFill", 260.0f }, { "sensitivity", 48.0f },
            { "protTrans", 75.0f }, { "protNext", 85.0f }, { "protLow", 90.0f },
            { "wet", 62.0f }, { "wGranular", 20.0f }, { "wSpectral", 0.0f },
            { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wComb", 0.0f } } },

        { "Drum Gap Glitch",
          "Classic between-the-hits glitching for loops and drum buses.",
          "Glitch",
          { { "fill", 52.0f }, { "intensity", 58.0f }, { "complexity", 48.0f },
            { "chaos", 35.0f }, { "density", 70.0f }, { "minGap", 55.0f },
            { "maxFill", 320.0f }, { "sensitivity", 58.0f }, { "gapThresh", 58.0f },
            { "protTrans", 80.0f }, { "protNext", 85.0f }, { "protLow", 80.0f },
            { "stutterDiv", (float) Division::ThirtySecond },
            { "wStutter", 95.0f }, { "wReverse", 80.0f }, { "wScramble", 70.0f },
            { "wSpectral", 0.0f }, { "wFreeze", 10.0f } } },

        { "DnB Micro Fills",
          "Fast micro-edits between breakbeat hits with the attacks left intact.",
          "Rhythmic",
          { { "fill", 62.0f }, { "intensity", 62.0f }, { "complexity", 55.0f },
            { "chaos", 40.0f }, { "density", 78.0f }, { "minGap", 35.0f },
            { "maxFill", 190.0f }, { "sensitivity", 66.0f }, { "gapThresh", 62.0f },
            { "retrigGuard", 18.0f }, { "protectHold", 22.0f }, { "gapRelease", 35.0f },
            { "lookahead", 30.0f }, { "protTrans", 90.0f }, { "protNext", 92.0f },
            { "protLow", 110.0f }, { "stutterDiv", (float) Division::ThirtySecond },
            { "delayDiv", (float) Division::ThirtySecond },
            { "wStutter", 100.0f }, { "wReverse", 85.0f }, { "wCrush", 55.0f },
            { "wScramble", 75.0f }, { "wFreeze", 0.0f }, { "wSpectral", 0.0f } } },

        { "Breakcore Destroyer",
          "Everything on, everywhere - still keeps the original hits punching through.",
          "Destructive",
          { { "fill", 92.0f }, { "intensity", 88.0f }, { "complexity", 86.0f },
            { "chaos", 78.0f }, { "density", 95.0f }, { "minGap", 22.0f },
            { "maxFill", 420.0f }, { "sensitivity", 72.0f }, { "gapThresh", 72.0f },
            { "retrigGuard", 14.0f }, { "protectHold", 16.0f }, { "gapRelease", 22.0f },
            { "protTrans", 85.0f }, { "protNext", 70.0f }, { "protLow", 70.0f },
            { "stutterDiv", (float) Division::ThirtySecond },
            { "wCrush", 85.0f }, { "wDistort", 80.0f }, { "wScramble", 95.0f },
            { "wStutter", 100.0f }, { "wPitchRise", 70.0f }, { "wPitchFall", 70.0f } } },

        { "Ghost Reverses",
          "Reverse swells rising into each hit.",
          "Ambient",
          { { "fill", 45.0f }, { "intensity", 40.0f }, { "complexity", 22.0f },
            { "chaos", 18.0f }, { "density", 60.0f }, { "fillType", algoChoice (Algo::Reverse) },
            { "minGap", 110.0f }, { "maxFill", 600.0f }, { "lookahead", 90.0f },
            { "protTrans", 70.0f }, { "protNext", 100.0f }, { "wet", 70.0f } } },

        { "Industrial Debris",
          "Metallic comb resonances and crushed shrapnel in the spaces.",
          "Destructive",
          { { "fill", 58.0f }, { "intensity", 78.0f }, { "complexity", 62.0f },
            { "chaos", 48.0f }, { "density", 66.0f }, { "minGap", 70.0f },
            { "maxFill", 480.0f }, { "protLow", 120.0f }, { "protTrans", 78.0f },
            { "wComb", 95.0f }, { "wCrush", 85.0f }, { "wDistort", 70.0f },
            { "wTexture", 70.0f }, { "wReverse", 30.0f }, { "wDelay", 30.0f },
            { "wGranular", 45.0f }, { "wFreeze", 25.0f } } },

        { "Vocal Ghosts",
          "Phrase-aware granular and reverse tails after vocal lines.",
          "Ambient",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 38.0f }, { "intensity", 45.0f },
            { "complexity", 52.0f }, { "chaos", 30.0f }, { "density", 55.0f },
            { "minGap", 180.0f }, { "maxFill", 900.0f }, { "sensitivity", 42.0f },
            { "gapThresh", 45.0f }, { "gapRelease", 160.0f }, { "protectHold", 90.0f },
            { "retrigGuard", 140.0f }, { "lookahead", 110.0f },
            { "protTrans", 60.0f }, { "protNext", 95.0f },
            { "wGranular", 95.0f }, { "wReverse", 80.0f }, { "wSpectral", 70.0f },
            { "wFreeze", 60.0f }, { "wStutter", 15.0f }, { "wCrush", 0.0f },
            { "wDistort", 0.0f }, { "wScramble", 10.0f }, { "wet", 65.0f } } },

        { "Vocal Delay Fills",
          "Rhythmic ping-pong echoes that appear only where the singer breathes.",
          "Dub",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 46.0f }, { "intensity", 42.0f },
            { "complexity", 26.0f }, { "chaos", 16.0f }, { "density", 60.0f },
            { "fillType", algoChoice (Algo::Delay) },
            { "minGap", 200.0f }, { "maxFill", 1200.0f }, { "gapRelease", 150.0f },
            { "protectHold", 80.0f }, { "lookahead", 120.0f },
            { "delayDiv", (float) Division::EighthD }, { "pingPong", 1.0f },
            { "protNext", 100.0f }, { "protTrans", 55.0f }, { "wet", 58.0f } } },

        { "Bass Between Kicks",
          "Sidechain the drums in: fills appear only where the kick is not.",
          "Utility",
          { { "detSource", (float) DetSource::Sidechain }, { "fill", 50.0f },
            { "intensity", 45.0f }, { "complexity", 38.0f }, { "chaos", 22.0f },
            { "density", 65.0f }, { "minGap", 90.0f }, { "maxFill", 380.0f },
            { "protLow", 110.0f }, { "protSC", 95.0f }, { "protNext", 90.0f },
            { "lookahead", 60.0f }, { "wGranular", 40.0f }, { "wSpectral", 15.0f },
            { "wComb", 0.0f }, { "wCrush", 20.0f }, { "wet", 70.0f } } },

        { "Granular Spaces",
          "Wide granular clouds filling the longer gaps.",
          "Ambient",
          { { "fill", 55.0f }, { "intensity", 52.0f }, { "complexity", 72.0f },
            { "chaos", 42.0f }, { "density", 58.0f }, { "fillType", algoChoice (Algo::Granular) },
            { "minGap", 160.0f }, { "maxFill", 1100.0f }, { "lookahead", 100.0f },
            { "protNext", 88.0f }, { "protCenter", 35.0f }, { "width", 140.0f },
            { "wet", 66.0f } } },

        { "Trance Transitions",
          "Long filtered and pitched risers between phrases.",
          "Rhythmic",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 42.0f }, { "intensity", 66.0f },
            { "complexity", 58.0f }, { "chaos", 26.0f }, { "density", 50.0f },
            { "minGap", 260.0f }, { "maxFill", 1800.0f }, { "maxFillSync", 1.0f },
            { "maxFillDiv", (float) Division::Half }, { "gapRelease", 180.0f },
            { "lookahead", 140.0f }, { "protNext", 95.0f }, { "protLow", 100.0f },
            { "wPitchRise", 100.0f }, { "wFilter", 95.0f }, { "wSpectral", 70.0f },
            { "wFreeze", 55.0f }, { "wStutter", 25.0f }, { "wCrush", 0.0f },
            { "wScramble", 10.0f }, { "width", 130.0f } } },

        { "Trip Hop Dust",
          "Quiet, dusty debris in the spaces of a slow beat.",
          "Subtle",
          { { "fill", 26.0f }, { "intensity", 32.0f }, { "complexity", 34.0f },
            { "chaos", 24.0f }, { "density", 42.0f }, { "minGap", 140.0f },
            { "maxFill", 520.0f }, { "sensitivity", 45.0f }, { "gapThresh", 45.0f },
            { "protLow", 140.0f }, { "protTrans", 70.0f }, { "protCenter", 45.0f },
            { "wTexture", 90.0f }, { "wCrush", 60.0f }, { "wReverse", 55.0f },
            { "wGranular", 55.0f }, { "wSpectral", 35.0f }, { "wet", 48.0f } } },

        { "Spectral High Fill",
          "Leaves the kick alone and drops glitches into the empty top end.",
          "Glitch",
          { { "detMode", (float) DetMode::Spectral }, { "fill", 55.0f },
            { "intensity", 55.0f }, { "complexity", 50.0f }, { "chaos", 34.0f },
            { "density", 72.0f }, { "minGap", 45.0f }, { "maxFill", 300.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 1.0f },
            { "bandHighProb", 100.0f }, { "bandHighAmt", 100.0f }, { "bandHighInt", 62.0f },
            { "xoverMH", 2800.0f }, { "protLow", 120.0f }, { "protTrans", 78.0f } } },

        { "Spectral Mid Mutation",
          "Mutates the midrange while lows and highs carry on untouched.",
          "Glitch",
          { { "detMode", (float) DetMode::Spectral }, { "fill", 58.0f },
            { "intensity", 62.0f }, { "complexity", 68.0f }, { "chaos", 45.0f },
            { "density", 70.0f }, { "minGap", 70.0f }, { "maxFill", 520.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 1.0f }, { "bandHighOn", 0.0f },
            { "bandMidProb", 100.0f }, { "bandMidInt", 70.0f },
            { "xoverLM", 220.0f }, { "xoverMH", 4200.0f },
            { "wSpectral", 80.0f }, { "wGranular", 80.0f }, { "wScramble", 70.0f },
            { "protLow", 130.0f } } },

        { "Sparse Texture",
          "Rare, synthesised textures shaped by the track's own spectrum.",
          "Ambient",
          { { "fill", 20.0f }, { "intensity", 38.0f }, { "complexity", 40.0f },
            { "chaos", 30.0f }, { "density", 28.0f }, { "fillType", algoChoice (Algo::Texture) },
            { "minGap", 200.0f }, { "maxFill", 800.0f }, { "protCenter", 55.0f },
            { "protNext", 92.0f }, { "width", 150.0f }, { "wet", 55.0f } } },

        { "Extreme Chaos",
          "Maximum variation. Bounded, but only just.",
          "Destructive",
          { { "fill", 100.0f }, { "intensity", 95.0f }, { "complexity", 95.0f },
            { "chaos", 100.0f }, { "density", 100.0f }, { "minGap", 18.0f },
            { "maxFill", 700.0f }, { "sensitivity", 78.0f }, { "gapThresh", 80.0f },
            { "retrigGuard", 10.0f }, { "protectHold", 10.0f }, { "gapRelease", 15.0f },
            { "protTrans", 55.0f }, { "protNext", 45.0f }, { "protLow", 60.0f },
            { "wet", 85.0f } } },

        // ---- U-10a / F-S10: genre-inspired performance presets (append-only: indices above never move) ----
        { "Breaks Chop Shop",
          "Snare-roll and stutter edits between break hits.",
          "Rhythmic",
          { { "fill", 58.0f }, { "intensity", 60.0f }, { "complexity", 50.0f }, { "chaos", 38.0f },
            { "density", 72.0f }, { "minGap", 50.0f }, { "maxFill", 280.0f }, { "sensitivity", 62.0f },
            { "gapThresh", 58.0f }, { "retrigGuard", 20.0f }, { "protTrans", 82.0f }, { "protNext", 88.0f },
            { "syncEnable", 1.0f }, { "stutterDiv", (float) Division::Sixteenth },
            { "wStutter", 100.0f }, { "wScramble", 80.0f }, { "wReverse", 55.0f },
            { "wSpectral", 0.0f }, { "wFreeze", 0.0f } } },

        { "Dub Echo Throwbacks",
          "Tempo-synced echoes thrown from the last hit.",
          "Dub",
          { { "fill", 45.0f }, { "intensity", 55.0f }, { "complexity", 28.0f }, { "chaos", 15.0f },
            { "density", 55.0f }, { "minGap", 140.0f }, { "maxFill", 900.0f }, { "syncEnable", 1.0f },
            { "delayDiv", (float) Division::EighthD }, { "pingPong", 1.0f },
            { "wDelay", 100.0f }, { "wFilter", 70.0f }, { "wComb", 40.0f }, { "wReverse", 30.0f },
            { "wStutter", 0.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f },
            { "lookahead", 60.0f }, { "protNext", 100.0f }, { "wet", 75.0f } } },

        { "Trance Riser Fills",
          "Pitch risers and reverse swells into the downbeat.",
          "Rhythmic",
          { { "fill", 55.0f }, { "intensity", 66.0f }, { "complexity", 42.0f }, { "chaos", 22.0f },
            { "density", 70.0f }, { "minGap", 120.0f }, { "maxFill", 700.0f }, { "lookahead", 90.0f },
            { "protNext", 100.0f }, { "wPitchRise", 100.0f }, { "wReverse", 90.0f }, { "wFilter", 60.0f },
            { "wStutter", 40.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wSpectral", 0.0f } } },

        { "Ambient Drift Fills",
          "Slow grains and spectral haze in the spaces.",
          "Ambient",
          { { "fill", 35.0f }, { "intensity", 35.0f }, { "complexity", 70.0f }, { "chaos", 30.0f },
            { "density", 50.0f }, { "minGap", 220.0f }, { "maxFill", 1400.0f }, { "lookahead", 120.0f },
            { "protTrans", 70.0f }, { "protNext", 100.0f }, { "protLow", 140.0f },
            { "wGranular", 100.0f }, { "wSpectral", 80.0f }, { "wFreeze", 70.0f }, { "wTexture", 80.0f },
            { "wStutter", 0.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wScramble", 0.0f }, { "wet", 70.0f } } },

        { "Liquid DnB Rollers",
          "Smooth micro-rolls that keep the amen groove intact.",
          "Rhythmic",
          { { "fill", 50.0f }, { "intensity", 50.0f }, { "complexity", 45.0f }, { "chaos", 28.0f },
            { "density", 74.0f }, { "minGap", 32.0f }, { "maxFill", 170.0f }, { "sensitivity", 68.0f },
            { "retrigGuard", 16.0f }, { "protectHold", 20.0f }, { "gapRelease", 32.0f }, { "lookahead", 28.0f },
            { "syncEnable", 1.0f }, { "stutterDiv", (float) Division::ThirtySecond },
            { "protTrans", 92.0f }, { "protNext", 94.0f }, { "protLow", 110.0f },
            { "wStutter", 100.0f }, { "wReverse", 70.0f }, { "wScramble", 55.0f }, { "wFilter", 40.0f },
            { "wCrush", 0.0f }, { "wDistort", 0.0f } } },

        { "House Groove Edits",
          "Light filter sweeps and short reverses that do not move the groove.",
          "Subtle",
          { { "fill", 30.0f }, { "intensity", 38.0f }, { "complexity", 25.0f }, { "chaos", 14.0f },
            { "density", 55.0f }, { "minGap", 100.0f }, { "maxFill", 300.0f }, { "syncEnable", 1.0f },
            { "stutterDiv", (float) Division::Sixteenth }, { "protTrans", 80.0f }, { "protNext", 90.0f },
            { "wFilter", 100.0f }, { "wReverse", 80.0f }, { "wDelay", 50.0f }, { "wStutter", 40.0f },
            { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wSpectral", 0.0f }, { "wFreeze", 0.0f } } },

        { "Hip-Hop Dust Chops",
          "Dusty vinyl-style stutters and tape stops after the snare.",
          "Glitch",
          { { "fill", 40.0f }, { "intensity", 48.0f }, { "complexity", 30.0f }, { "chaos", 24.0f },
            { "density", 56.0f }, { "minGap", 90.0f }, { "maxFill", 360.0f }, { "syncEnable", 1.0f },
            { "stutterDiv", (float) Division::Eighth }, { "protTrans", 78.0f }, { "protNext", 88.0f },
            { "wStutter", 90.0f }, { "wTapeStop", 85.0f }, { "wCrush", 45.0f }, { "wReverse", 40.0f },
            { "wFilter", 50.0f }, { "wSpectral", 0.0f }, { "wGranular", 0.0f } } },

        { "Techno Rumble Gaps",
          "Dark comb rings and distortion rumbling between kicks.",
          "Destructive",
          { { "fill", 55.0f }, { "intensity", 62.0f }, { "complexity", 52.0f }, { "chaos", 36.0f },
            { "density", 68.0f }, { "minGap", 80.0f }, { "maxFill", 420.0f }, { "protLow", 150.0f },
            { "protTrans", 85.0f }, { "protNext", 82.0f },
            { "wComb", 100.0f }, { "wDistort", 80.0f }, { "wFilter", 70.0f }, { "wScramble", 40.0f },
            { "wTexture", 40.0f }, { "wSpectral", 0.0f }, { "wFreeze", 30.0f } } },

        // ---- U-10b: Dub and Utility ----
        { "Dub Siren Throws",
          "Long triplet echoes with filtered tails; sparse and deep.",
          "Dub",
          { { "fill", 40.0f }, { "intensity", 50.0f }, { "complexity", 30.0f }, { "chaos", 18.0f },
            { "density", 45.0f }, { "minGap", 180.0f }, { "maxFill", 1100.0f }, { "syncEnable", 1.0f },
            { "delayDiv", (float) Division::EighthT }, { "pingPong", 1.0f }, { "width", 130.0f },
            { "wDelay", 100.0f }, { "wFilter", 80.0f }, { "wReverse", 40.0f }, { "wFreeze", 30.0f },
            { "wStutter", 0.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wScramble", 0.0f },
            { "lookahead", 80.0f }, { "protNext", 100.0f }, { "protLow", 130.0f }, { "wet", 70.0f } } },

        { "Dub Tape Spin Outs",
          "Tape stops and slow-motion echoes that drag the groove into space.",
          "Dub",
          { { "fill", 38.0f }, { "intensity", 52.0f }, { "complexity", 26.0f }, { "chaos", 20.0f },
            { "density", 50.0f }, { "minGap", 160.0f }, { "maxFill", 800.0f }, { "syncEnable", 1.0f },
            { "delayDiv", (float) Division::QuarterD }, { "pingPong", 1.0f },
            { "wTapeStop", 100.0f }, { "wDelay", 90.0f }, { "wFilter", 60.0f }, { "wPitchFall", 60.0f },
            { "wStutter", 0.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f },
            { "lookahead", 60.0f }, { "protNext", 100.0f }, { "wet", 72.0f } } },

        { "Sidechain Kick Gaps",
          "Detects the kick on the sidechain input; fills only where it is not.",
          "Utility",
          { { "detSource", (float) DetSource::Sidechain }, { "fill", 48.0f }, { "intensity", 45.0f },
            { "complexity", 35.0f }, { "chaos", 20.0f }, { "density", 62.0f }, { "minGap", 80.0f },
            { "maxFill", 360.0f }, { "protSC", 100.0f }, { "protNext", 85.0f }, { "protLow", 120.0f },
            { "lookahead", 40.0f }, { "wet", 70.0f } } },

        { "Hybrid Safe Bus",
          "Sidechain finds the space, the bus keeps its own hits protected. Conservative.",
          "Utility",
          { { "detSource", (float) DetSource::Hybrid }, { "fill", 32.0f }, { "intensity", 38.0f },
            { "complexity", 28.0f }, { "chaos", 15.0f }, { "density", 52.0f }, { "minGap", 90.0f },
            { "maxFill", 320.0f }, { "protTrans", 85.0f }, { "protNext", 90.0f }, { "protSC", 85.0f },
            { "wet", 60.0f } } },

        // ---- U-10c: Subtle ----
        { "Whisper Reverses",
          "Quiet reverse swells that melt into the next hit.",
          "Subtle",
          { { "fill", 22.0f }, { "intensity", 25.0f }, { "complexity", 12.0f }, { "chaos", 8.0f },
            { "density", 40.0f }, { "fillType", algoChoice (Algo::Reverse) }, { "minGap", 110.0f },
            { "maxFill", 450.0f }, { "lookahead", 70.0f }, { "protTrans", 80.0f }, { "protNext", 100.0f },
            { "wet", 50.0f } } },

        { "Soft Filter Breaths",
          "Slow filter movements in the pauses. Barely there, always musical.",
          "Subtle",
          { { "fill", 20.0f }, { "intensity", 28.0f }, { "complexity", 10.0f }, { "chaos", 10.0f },
            { "density", 42.0f }, { "fillType", algoChoice (Algo::Filter) }, { "minGap", 120.0f },
            { "maxFill", 380.0f }, { "protTrans", 80.0f }, { "protNext", 95.0f }, { "wet", 52.0f } } },

        // ---- U-10d: Starting Points (first settings to try; personalities, macros and Lock join this group as they ship) ----
        { "Start Here - Drum Bus",
          "A balanced first setting for a drum bus: audible, musical and safe.",
          "Starting Points",
          { { "fill", 42.0f }, { "intensity", 46.0f }, { "complexity", 36.0f }, { "chaos", 22.0f },
            { "density", 62.0f }, { "minGap", 70.0f }, { "maxFill", 320.0f }, { "protTrans", 80.0f },
            { "protNext", 86.0f }, { "protLow", 110.0f } } },

        { "Start Here - Full Mix",
          "Gentle fills for a full mix: long gaps only, hits and low end protected.",
          "Starting Points",
          { { "fill", 28.0f }, { "intensity", 34.0f }, { "complexity", 24.0f }, { "chaos", 14.0f },
            { "density", 48.0f }, { "minGap", 130.0f }, { "maxFill", 500.0f }, { "sensitivity", 45.0f },
            { "protTrans", 85.0f }, { "protNext", 92.0f }, { "protLow", 130.0f }, { "wet", 60.0f } } },

        { "Start Here - Vocals",
          "Phrase detection for a vocal track: fills only in the real pauses.",
          "Starting Points",
          { { "detMode", (float) DetMode::Phrase }, { "fill", 34.0f }, { "intensity", 38.0f },
            { "complexity", 32.0f }, { "chaos", 18.0f }, { "density", 52.0f }, { "minGap", 190.0f },
            { "maxFill", 900.0f }, { "gapRelease", 150.0f }, { "protectHold", 80.0f }, { "retrigGuard", 130.0f },
            { "lookahead", 100.0f }, { "protNext", 95.0f }, { "wet", 60.0f } } },

        { "Try One Algorithm - Reverse",
          "Only reverse fills, so you can hear what one algorithm does.",
          "Starting Points",
          { { "fill", 50.0f }, { "intensity", 45.0f }, { "complexity", 20.0f }, { "chaos", 12.0f },
            { "density", 65.0f }, { "fillType", algoChoice (Algo::Reverse) }, { "minGap", 90.0f },
            { "maxFill", 500.0f }, { "lookahead", 70.0f }, { "protNext", 100.0f } } },

        { "Try One Algorithm - Stutter",
          "Only stutter fills, so you can hear what one algorithm does.",
          "Starting Points",
          { { "fill", 50.0f }, { "intensity", 45.0f }, { "complexity", 20.0f }, { "chaos", 12.0f },
            { "density", 65.0f }, { "fillType", algoChoice (Algo::Stutter) }, { "minGap", 70.0f },
            { "maxFill", 300.0f }, { "syncEnable", 1.0f }, { "stutterDiv", (float) Division::Sixteenth },
            { "protTrans", 85.0f } } },

        { "Try Spectral Lanes - Highs",
          "Spectral detection with fills only in the top lane; lows and mids carry on.",
          "Starting Points",
          { { "detMode", (float) DetMode::Spectral }, { "fill", 50.0f }, { "intensity", 50.0f },
            { "complexity", 40.0f }, { "chaos", 25.0f }, { "density", 65.0f }, { "minGap", 60.0f },
            { "maxFill", 300.0f }, { "bandLowOn", 0.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 1.0f },
            { "bandHighProb", 100.0f }, { "protLow", 120.0f } } },

        // ---- U-10e: round out the categories ----
        { "Stutter Gate Chops",
          "Tight 1/32 stutter gates with a few reverses; keeps the beat's pulse.",
          "Glitch",
          { { "fill", 56.0f }, { "intensity", 55.0f }, { "complexity", 40.0f }, { "chaos", 32.0f },
            { "density", 74.0f }, { "minGap", 45.0f }, { "maxFill", 240.0f }, { "syncEnable", 1.0f },
            { "stutterDiv", (float) Division::ThirtySecond }, { "protTrans", 85.0f }, { "protNext", 88.0f },
            { "wStutter", 100.0f }, { "wReverse", 40.0f }, { "wScramble", 50.0f }, { "wCrush", 20.0f },
            { "wSpectral", 0.0f }, { "wFreeze", 0.0f } } },

        { "Half-Time Gap Rolls",
          "Slower 1/8 rolls and delays that give a straight beat a half-time feel.",
          "Rhythmic",
          { { "fill", 46.0f }, { "intensity", 48.0f }, { "complexity", 34.0f }, { "chaos", 20.0f },
            { "density", 60.0f }, { "minGap", 110.0f }, { "maxFill", 420.0f }, { "syncEnable", 1.0f },
            { "stutterDiv", (float) Division::Eighth }, { "delayDiv", (float) Division::Eighth },
            { "protTrans", 82.0f }, { "protNext", 90.0f },
            { "wStutter", 85.0f }, { "wDelay", 80.0f }, { "wTapeStop", 50.0f }, { "wReverse", 50.0f },
            { "wCrush", 0.0f }, { "wDistort", 0.0f } } },

        { "Crushed Gap Debris",
          "Bit-crushed and distorted shards in every gap; hits stay punchy.",
          "Destructive",
          { { "fill", 62.0f }, { "intensity", 72.0f }, { "complexity", 56.0f }, { "chaos", 44.0f },
            { "density", 70.0f }, { "minGap", 60.0f }, { "maxFill", 360.0f }, { "protTrans", 88.0f },
            { "protNext", 85.0f }, { "protLow", 120.0f },
            { "wCrush", 100.0f }, { "wDistort", 90.0f }, { "wScramble", 60.0f }, { "wComb", 40.0f },
            { "wSpectral", 0.0f } } },

        { "Shimmer Tail Wash",
          "Rising shimmer and grain washes that bloom in long gaps.",
          "Ambient",
          { { "fill", 32.0f }, { "intensity", 40.0f }, { "complexity", 62.0f }, { "chaos", 26.0f },
            { "density", 48.0f }, { "minGap", 240.0f }, { "maxFill", 1600.0f }, { "lookahead", 130.0f },
            { "protNext", 100.0f }, { "protLow", 130.0f }, { "width", 150.0f },
            { "wPitchRise", 80.0f }, { "wGranular", 90.0f }, { "wSpectral", 70.0f }, { "wFreeze", 60.0f },
            { "wStutter", 0.0f }, { "wCrush", 0.0f }, { "wDistort", 0.0f }, { "wScramble", 0.0f }, { "wet", 68.0f } } },
    };

    return presets;
}

//==============================================================================
void resetToDefaults (juce::AudioProcessorValueTreeState& state)
{
    for (const auto& id : allParameterIDs())
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    }
}

void applyPreset (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& presets = getFactoryPresets();

    if (index < 0 || index >= (int) presets.size())
        return;

    resetToDefaults (state);

    for (const auto& v : presets[(size_t) index].values)
    {
        if (auto* p = state.getParameter (v.id))
            p->setValueNotifyingHost (p->convertTo0to1 (v.value));
    }
}

//==============================================================================
int randomiseParameters (juce::AudioProcessorValueTreeState& state,
                         bool everything,
                         juce::Random& random)
{
    const auto ids = everything ? randomisableParameterIDs() : creativeParameterIDs();

    for (const auto& id : ids)
    {
        auto* p = state.getParameter (id);

        if (p == nullptr)
            continue;

        if (id == "seed")
            continue;   // handled below so that the value is always new

        // Keep the macros in a musically useful window rather than letting them
        // land anywhere; the result should be playable, not broken.
        float normalised;

        if (id == "fill")
            normalised = random.nextFloat() * 0.75f + 0.2f;
        else if (id == "intensity" || id == "complexity" || id == "density")
            normalised = random.nextFloat() * 0.85f + 0.1f;
        else if (id == "chaos")
            normalised = random.nextFloat() * 0.9f;
        else if (id == "minGap" || id == "maxFill" || id == "gapRelease"
                  || id == "protectHold" || id == "retrigGuard")
            normalised = random.nextFloat() * 0.8f + 0.1f;
        else if (id.startsWith ("prot"))
            normalised = random.nextFloat() * 0.6f + 0.35f;   // stay protective
        else
            normalised = random.nextFloat();

        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    }

    // Guarantee at least a few algorithms remain available in Auto mode.
    if (auto* fillType = state.getParameter ("fillType"))
    {
        const auto isAuto = fillType->getValue() <= 0.001f;

        if (isAuto)
        {
            int enabled = 0;

            for (int i = 0; i < numAlgos; ++i)
                if (auto* w = state.getParameter (algoWeightID ((Algo) i)))
                    if (w->getValue() > 0.05f)
                        ++enabled;

            if (enabled < 3)
            {
                for (const char* id : { "wReverse", "wStutter", "wDelay" })
                    if (auto* w = state.getParameter (id))
                        w->setValueNotifyingHost (0.6f + random.nextFloat() * 0.4f);
            }
        }
    }

    // A fresh seed every time, so pressing Random twice never repeats.
    int newSeed = 1;

    if (auto* seedParam = state.getParameter ("seed"))
    {
        const auto current = (int) std::lround (seedParam->convertFrom0to1 (seedParam->getValue()));

        do { newSeed = random.nextInt ({ 0, 10000 }); }
        while (newSeed == current);

        seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) newSeed));
    }

    return newSeed;
}

//==============================================================================
namespace
{
    struct Range { float lo, hi; };

    /** A coherent creative direction: macro windows plus the algorithms that
        carry it. Everything not listed gets a low background weight so Auto
        always has a fallback for gaps the core algorithms do not fit. */
    struct SurpriseProfile
    {
        const char* name;
        Range fill, intensity, complexity, chaos, density;
        std::vector<Algo> core;
        std::vector<int> divisions;   // candidate stutter/delay divisions
        Range width;
    };

    const std::vector<SurpriseProfile>& surpriseProfiles()
    {
        using D = Division;
        static const std::vector<SurpriseProfile> profiles =
        {
            { "Subtle Sparkle",  { 0.30f, 0.50f }, { 0.25f, 0.45f }, { 0.20f, 0.40f }, { 0.05f, 0.25f }, { 0.40f, 0.60f },
              { Algo::Reverse, Algo::Delay, Algo::Filter },
              { (int) D::Eighth, (int) D::Sixteenth, (int) D::EighthD }, { 110.0f, 150.0f } },
            { "Drum Edits",      { 0.55f, 0.80f }, { 0.45f, 0.70f }, { 0.35f, 0.60f }, { 0.20f, 0.45f }, { 0.60f, 0.85f },
              { Algo::Stutter, Algo::Reverse, Algo::Scramble, Algo::TapeStop },
              { (int) D::Sixteenth, (int) D::ThirtySecond, (int) D::SixteenthT }, { 80.0f, 120.0f } },
            { "Glitch Storm",    { 0.70f, 0.95f }, { 0.65f, 0.90f }, { 0.60f, 0.90f }, { 0.55f, 0.85f }, { 0.75f, 1.00f },
              { Algo::Stutter, Algo::Crush, Algo::Scramble, Algo::Distort },
              { (int) D::ThirtySecond, (int) D::SixteenthT, (int) D::Sixteenth }, { 100.0f, 180.0f } },
            { "Dub Echoes",      { 0.40f, 0.65f }, { 0.35f, 0.60f }, { 0.25f, 0.50f }, { 0.10f, 0.35f }, { 0.45f, 0.70f },
              { Algo::Delay, Algo::Filter, Algo::Comb },
              { (int) D::EighthD, (int) D::QuarterT, (int) D::Eighth }, { 130.0f, 190.0f } },
            { "Ambient Haze",    { 0.40f, 0.70f }, { 0.30f, 0.55f }, { 0.55f, 0.85f }, { 0.20f, 0.45f }, { 0.40f, 0.65f },
              { Algo::Granular, Algo::Freeze, Algo::Spectral, Algo::Texture },
              { (int) D::Quarter, (int) D::Eighth, (int) D::QuarterD }, { 140.0f, 200.0f } },
            { "Pitch Bender",    { 0.45f, 0.75f }, { 0.50f, 0.80f }, { 0.35f, 0.65f }, { 0.25f, 0.55f }, { 0.50f, 0.75f },
              { Algo::PitchRise, Algo::PitchFall, Algo::TapeStop, Algo::Reverse },
              { (int) D::Sixteenth, (int) D::Eighth }, { 90.0f, 140.0f } },
            { "Industrial Grit", { 0.55f, 0.85f }, { 0.70f, 0.95f }, { 0.40f, 0.70f }, { 0.30f, 0.60f }, { 0.60f, 0.90f },
              { Algo::Distort, Algo::Crush, Algo::Comb, Algo::Stutter },
              { (int) D::Sixteenth, (int) D::ThirtySecond }, { 70.0f, 110.0f } },
            { "Tape Breakdown",  { 0.50f, 0.80f }, { 0.45f, 0.75f }, { 0.30f, 0.55f }, { 0.15f, 0.40f }, { 0.55f, 0.80f },
              { Algo::TapeStop, Algo::Reverse, Algo::Filter, Algo::Delay },
              { (int) D::Eighth, (int) D::Sixteenth, (int) D::EighthT }, { 90.0f, 130.0f } },
        };

        return profiles;
    }

    void setReal (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float pick (juce::Random& random, Range r) { return r.lo + random.nextFloat() * (r.hi - r.lo); }
}

int getNumSurpriseProfiles() noexcept
{
    return (int) surpriseProfiles().size();
}

const SurpriseTargets& surpriseTargets (int profile) noexcept
{
    // Built once from surpriseProfiles() (the table Surprise itself reads); never mutated afterwards.
    static const std::vector<SurpriseTargets> targets = []
    {
        std::vector<SurpriseTargets> out;

        for (const auto& p : surpriseProfiles())
        {
            SurpriseTargets t;
            t.name = p.name;
            t.fill       = 0.5f * (p.fill.lo + p.fill.hi);
            t.intensity  = 0.5f * (p.intensity.lo + p.intensity.hi);
            t.complexity = 0.5f * (p.complexity.lo + p.complexity.hi);
            t.chaos      = 0.5f * (p.chaos.lo + p.chaos.hi);
            t.density    = 0.5f * (p.density.lo + p.density.hi);

            for (int i = 0; i < numAlgos; ++i)
                t.weight[i] = std::find (p.core.begin(), p.core.end(), (Algo) i) != p.core.end() ? 0.80f : 0.06f;

            t.stutterDivision = p.divisions.empty() ? 0 : p.divisions.front();
            out.push_back (t);
        }

        return out;
    }();

    return targets[(size_t) juce::jlimit (0, (int) targets.size() - 1, profile)];
}

int surpriseParameters (juce::AudioProcessorValueTreeState& state,
                        juce::Random& random,
                        int previousProfile,
                        juce::String& name)
{
    const auto& profiles = surpriseProfiles();
    const auto count = (int) profiles.size();

    auto index = random.nextInt (count);

    if (index == previousProfile)
        index = (index + 1 + random.nextInt (count - 1)) % count;

    const auto& prof = profiles[(size_t) index];
    name = prof.name;

    setReal (state, "fill",       100.0f * pick (random, prof.fill));
    setReal (state, "intensity",  100.0f * pick (random, prof.intensity));
    setReal (state, "complexity", 100.0f * pick (random, prof.complexity));
    setReal (state, "chaos",      100.0f * pick (random, prof.chaos));
    setReal (state, "density",    100.0f * pick (random, prof.density));
    setReal (state, "width",      pick (random, prof.width));
    setReal (state, "fillType",   kAuto);

    for (int i = 0; i < numAlgos; ++i)
    {
        const auto isCore = std::find (prof.core.begin(), prof.core.end(), (Algo) i) != prof.core.end();
        setReal (state, algoWeightID ((Algo) i),
                 isCore ? 65.0f + random.nextFloat() * 35.0f : random.nextFloat() * 12.0f);
    }

    const auto div = [&] { return (float) prof.divisions[(size_t) random.nextInt ((int) prof.divisions.size())]; };
    setReal (state, "stutterDiv", div());
    setReal (state, "delayDiv",   div());
    setReal (state, "pingPong",   random.nextBool() ? 1.0f : 0.0f);

    if (auto* seedParam = state.getParameter ("seed"))
    {
        const auto current = (int) std::lround (seedParam->convertFrom0to1 (seedParam->getValue()));
        int newSeed;

        do { newSeed = random.nextInt ({ 0, 10000 }); }
        while (newSeed == current);

        seedParam->setValueNotifyingHost (seedParam->convertTo0to1 ((float) newSeed));
    }

    return index;
}

} // namespace gapfill
