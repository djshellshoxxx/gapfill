// U-9: ParamInfo entries for the WP5 shaping parameters. One line each (<= 90 characters).

#include "../info/ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar shapingInfo
    {
        { "personality",       "Personality",      "Tilts which fills are chosen and nudges four macros; Off leaves everything alone", "", "" },
        { "captureLength",     "Capture Length",   "How much audio before the freeze point a latched Capture may use", "500-1500", "ms" },
        { "fillShape",         "Fill Shape",       "How a fill develops over its own length: level and brightness curve", "", "" },
        { "fillShapeAmount",   "Shape Amount",     "How strongly the Fill Shape is applied; 0 = off", "30-80", "%" },
        { "densityBreath",     "Density Breathing", "Swells Density up and down over a cycle of bars, locked to the song position", "", "" },
        { "densityBreathBars", "Breathing Bars",   "Length of one breathing cycle in bars", "2-4", "bars" },
        { "densityBreathDepth","Breathing Depth",  "Size of the Density swing; 100 % moves it up to half the range either side", "20-60", "%" },
        { "densityBreathCurve","Breathing Curve",  "Shape of the swing: smooth sine, straight ramp or on/off pulse", "", "" },
        { "phraseBars",        "Phrase Length",    "Length of your musical phrase; the last bar gets more fills. Off = ignore phrases", "4-8", "bars" },
        { "phraseBias",        "Phrase Bias",      "How much more likely a fill is in the last bar of the phrase; 0 = off", "30-70", "%" },
        { "phraseOffset",      "Phrase Offset",    "Shifts where the phrase starts, in bars", "0-3", "bars" },
        { "mistakes",          "Mistakes",         "Chance of one small human flaw per fill; 100 % = about one fill in five", "10-40", "%" },
        { "fillQuant",         "Quantise Length",  "Rounds fill length down to whole 1/4, 1/8, 1/16 or 1/32 notes while tempo is known", "", "" },
        { "modSync",           "Sync Mods",        "Locks tremolo, pan motion and scramble slices to the tempo (needs Sync on)", "", "" },
        { "bandLowType",       "Low Band Fill Type",  "Forces a fill type for gaps found in the low lane (Spectral mode); Auto = normal", "", "" },
        { "bandMidType",       "Mid Band Fill Type",  "Forces a fill type for gaps found in the mid lane (Spectral mode); Auto = normal", "", "" },
        { "bandHighType",      "High Band Fill Type", "Forces a fill type for gaps found in the high lane (Spectral mode); Auto = normal", "", "" },
        { "evolve",            "Evolve",           "Slowly drifts the fill character towards Surprise styles every 8, 16 or 32 bars", "", "" },
        { "fillBudget",        "Fills Per Bar",    "Most fills allowed to start in one bar; Off = no limit", "2-4", "" },
        { "quietZone",         "Quiet Zone",       "Part of the bar where no fill may start (a running fill continues)", "", "" },
    };
}

} // namespace gapfill::info
