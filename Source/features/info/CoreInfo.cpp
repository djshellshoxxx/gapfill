// U-9: help text for the core parameters (the ~66 that shipped before the feature groups).
// One line each (<= 90 characters); the long explanations stay in the Help view.

#include "ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar coreInfo
    {
        // ---- detection ----
        { "detMode",      "Detector Mode",   "Transient follows hits, Phrase finds musical pauses, Spectral splits low/mid/high", "", "" },
        { "detSource",    "Detector Source", "What defines when there is space: this track, the sidechain, or both", "", "" },
        { "sensitivity",  "Sensitivity",     "How easily a musical event is detected; lower it if noise triggers events", "40-70", "%" },
        { "gapThresh",    "Gap Threshold",   "How much quieter than the music a region must be to count as space", "40-70", "%" },
        { "minGap",       "Min Gap",         "Spaces shorter than this never get a fill", "40-120", "ms" },
        { "maxFill",      "Max Fill",        "Upper limit on the length of a single generated fill", "250-800", "ms" },
        { "maxFillSync",  "Max Fill Sync",   "Use a tempo division instead of milliseconds for the maximum fill length", "", "" },
        { "maxFillDiv",   "Max Fill Division", "Tempo division used for the maximum fill length when sync is on", "1/8-1/2", "" },
        { "gapRelease",   "Gap Confirm",     "How long GapFill waits before it trusts that an event has ended", "50-150", "ms" },
        { "protectHold",  "Protect Hold",    "Safe region after each detected hit where no fill may start", "20-80", "ms" },
        { "retrigGuard",  "Retrigger Guard", "Minimum time between two detected events, so one hit is not counted twice", "20-60", "ms" },
        { "lookahead",    "Lookahead",       "Lets fills end before upcoming events; reported to the host as latency", "0-60", "ms" },
        { "audition",     "Audition Detector", "Listen to exactly what the detector hears (main track or sidechain)", "", "" },

        // ---- creative macros ----
        { "fill",         "Fill",            "The main amount: how likely an eligible gap is to receive a fill", "20-60", "%" },
        { "intensity",    "Intensity",       "How hard each chosen fill is processed; not simply louder", "30-70", "%" },
        { "complexity",   "Complexity",      "How elaborate a fill may become, from one simple effect to stacked ones", "20-60", "%" },
        { "chaos",        "Chaos",           "Variation in algorithm choice, timing, pitch and stereo movement", "10-40", "%" },
        { "density",      "Density",         "How often an eligible gap gets filled", "40-70", "%" },
        { "fillType",     "Fill Type",       "Auto weighs every enabled algorithm; a specific type forces it", "", "" },
        { "seed",         "Seed",            "Same seed and transport position always give the same fills", "0-9999", "" },
        { "syncEnable",   "Tempo Sync",      "Derive stutter and delay timings from the host tempo", "", "" },
        { "stutterDiv",   "Stutter Division", "Subdivision used by the Stutter algorithm", "1/16-1/8", "" },
        { "delayDiv",     "Delay Division",  "Subdivision used by the Delay algorithm", "1/16-1/4", "" },
        { "pingPong",     "Ping Pong",       "Alternate delay taps between left and right", "", "" },
        { "wetMono",      "Mono Wet",        "Collapse the generated material to mono for mono compatibility", "", "" },
        { "width",        "Wet Width",       "Stereo width of the generated material only; the dry signal is untouched", "80-140", "%" },

        // ---- auto-mode algorithm weights ----
        { "wReverse",     "Weight Reverse",    "How often Auto may choose Reverse; 0 disables it", "0-100", "%" },
        { "wStutter",     "Weight Stutter",    "How often Auto may choose Stutter; 0 disables it", "0-100", "%" },
        { "wDelay",       "Weight Delay",      "How often Auto may choose Delay; 0 disables it", "0-100", "%" },
        { "wGranular",    "Weight Granular",   "How often Auto may choose Granular; 0 disables it", "0-100", "%" },
        { "wFreeze",      "Weight Freeze",     "How often Auto may choose Freeze; 0 disables it", "0-100", "%" },
        { "wPitchRise",   "Weight Pitch Rise", "How often Auto may choose Pitch Rise; 0 disables it", "0-100", "%" },
        { "wPitchFall",   "Weight Pitch Fall", "How often Auto may choose Pitch Fall; 0 disables it", "0-100", "%" },
        { "wFilter",      "Weight Filter",     "How often Auto may choose Filter; 0 disables it", "0-100", "%" },
        { "wTexture",     "Weight Texture",    "How often Auto may choose Texture; 0 disables it", "0-100", "%" },
        { "wCrush",       "Weight Crush",      "How often Auto may choose Bit Crush; 0 disables it", "0-100", "%" },
        { "wDistort",     "Weight Distort",    "How often Auto may choose Distort; 0 disables it", "0-100", "%" },
        { "wScramble",    "Weight Scramble",   "How often Auto may choose Scramble; 0 disables it", "0-100", "%" },
        { "wSpectral",    "Weight Spectral",   "How often Auto may choose Spectral Smear; 0 disables it", "0-100", "%" },
        { "wComb",        "Weight Comb",       "How often Auto may choose Comb; 0 disables it", "0-100", "%" },
        { "wTapeStop",    "Weight Tape Stop",  "How often Auto may choose Tape Stop; 0 disables it", "0-100", "%" },

        // ---- protection ----
        { "protTrans",    "Transient Protect", "Keeps fills from covering the attack of the next hit", "40-80", "%" },
        { "protLow",      "Low Protect",       "Frequency below which the original signal is never replaced", "80-150", "Hz" },
        { "protCenter",   "Center Protect",    "Keeps the centre of the stereo image (vocals, kick) clean of fills", "0-50", "%" },
        { "protNext",     "Next Event Protect", "Ends fills before the next predicted event", "50-90", "%" },
        { "protSC",       "Sidechain Protect", "Protects the space where the sidechain input is active", "50-90", "%" },

        // ---- spectral lanes ----
        { "xoverLM",      "Low/Mid Crossover",  "Frequency that separates the low lane from the mid lane", "120-300", "Hz" },
        { "xoverMH",      "Mid/High Crossover", "Frequency that separates the mid lane from the high lane", "2500-5000", "Hz" },
        { "bandLowOn",    "Low Band On",       "Allow fills in the low lane", "", "" },
        { "bandMidOn",    "Mid Band On",       "Allow fills in the mid lane", "", "" },
        { "bandHighOn",   "High Band On",      "Allow fills in the high lane", "", "" },
        { "bandLowAmt",   "Low Fill Amount",   "How much fill the low lane receives", "50-100", "%" },
        { "bandMidAmt",   "Mid Fill Amount",   "How much fill the mid lane receives", "70-100", "%" },
        { "bandHighAmt",  "High Fill Amount",  "How much fill the high lane receives", "70-100", "%" },
        { "bandLowInt",   "Low Intensity",     "How hard fills in the low lane are processed", "30-60", "%" },
        { "bandMidInt",   "Mid Intensity",     "How hard fills in the mid lane are processed", "30-60", "%" },
        { "bandHighInt",  "High Intensity",    "How hard fills in the high lane are processed", "30-60", "%" },
        { "bandLowProb",  "Low Probability",   "Chance that the low lane takes part in a fill", "30-60", "%" },
        { "bandMidProb",  "Mid Probability",   "Chance that the mid lane takes part in a fill", "60-90", "%" },
        { "bandHighProb", "High Probability",  "Chance that the high lane takes part in a fill", "70-100", "%" },
        { "bandSolo",     "Band Solo",         "Listen to one lane of the generated material on its own", "", "" },

        // ---- mix ----
        { "dry",          "Dry",               "Level of the original signal", "100", "%" },
        { "wet",          "Wet",               "Level of the generated fills", "50-100", "%" },
        { "output",       "Output",            "Final output gain", "-6 to +3", "dB" },
        { "bypass",       "Bypass",            "Pass the signal through unchanged; latency compensation stays correct", "", "" },
        { "capture",      "Capture",           "Freeze the current source material and build fills from it", "", "" },
    };
}

} // namespace gapfill::info
