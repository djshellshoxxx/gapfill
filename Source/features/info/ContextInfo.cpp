// U-9: help text for the WP7 musical-context parameters (Scale Lock, key detection, energy,
// groove, call and response, taste, tension, arrangement). One line each (<= 90 characters).

#include "ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar contextInfo
    {
        { "scaleRoot",    "Scale Key",        "The song's tonic; pitched fills stay on notes of the chosen scale around it", "", "" },
        { "scaleType",    "Scale",            "Keeps pitched fills in key; Off leaves every pitch decision untouched", "", "" },
        { "scaleAmount",  "Scale Amount",     "How often a pitch decision is snapped to the scale; 100 = always", "70-100", "%" },
        { "scaleFollow",  "Auto Key",         "Use the key heard in the music instead of the Key and Scale set by hand", "", "" },
        { "fx03_window",  "Key Window",       "How many seconds of music the key estimate averages; longer is steadier", "8-16", "s" },
        { "fx03_minConf", "Key Confidence",   "How sure the detector must be before its key is used; below it your manual key stays", "30-60", "%" },
        { "fx03_hold",    "Lock Key",         "Freeze the detected key and save it with the project for repeatable renders", "", "" },

        { "energyAdapt",  "Energy Follow",    "Fills follow how busy the music is: more when sparse, fewer but bolder when busy", "20-60", "%" },
        { "energyTarget", "Energy Target",    "Whether the energy follow steers density, intensity or both", "", "" },

        { "fl09_amount",  "Groove",           "Stutter repeats and delay taps follow the swing of the music; needs host tempo", "30-70", "%" },
        { "fl09_grid",    "Groove Grid",      "The note grid the swing is measured against: eighths or sixteenths", "", "" },
        { "fl09_hold",    "Groove Hold",      "Freeze the learned groove so it no longer changes", "", "" },

        { "fl10_mode",     "Call Mode",       "Answer GapFill's last fills with a different (Complement) or similar (Echo) kind of fill", "", "" },
        { "fl10_strength", "Call Strength",   "How strongly the response is steered; it only nudges choices", "40-80", "%" },
        { "fl10_calls",    "Call Length",     "How many of GapFill's own fills are listened to before answering", "2-4", "fills" },
        { "fl10_replies",  "Response Length", "How many fills answer once a clear pattern was heard", "1-3", "fills" },

        { "fx05_on",       "Learn My Taste",  "Learn which fills you like or skip, on this computer only, and tilt Auto choices", "", "" },
        { "fx05_strength", "Taste Strength",  "How strongly your taste tilts the choice of fill; it never forces one", "30-70", "%" },

        { "fm21_on",             "Tension",           "Shape density, intensity, width and effects across a multi-bar build and release", "", "" },
        { "fm21_bars",           "Tension Bars",      "Length of the tension curve in bars", "4-16", "bars" },
        { "fm21_start",          "Tension Start",     "The bar, counted from the song start, where the curve begins", "0-64", "bar" },
        { "fm21_shape",          "Tension Shape",     "Build, Build+Drop, Breakdown, Swell or Custom curves saved with the project", "", "" },
        { "fm21_end",            "Tension End",       "After the last bar: repeat, keep the last value, or return to your knobs", "", "" },
        { "fm21_depthDensity",   "Tension Density",   "How far the curve moves density from your knob value; 0 leaves it alone", "20-60", "%" },
        { "fm21_depthIntensity", "Tension Intensity", "How far the curve moves intensity from your knob value; 0 leaves it alone", "20-50", "%" },
        { "fm21_depthWidth",     "Tension Width",     "How far the curve moves stereo width from your knob value; 0 leaves it alone", "0-40", "%" },
        { "fm21_depthFx",        "Tension FX",        "How much effect amount is pulled down at the low end of the curve", "0-50", "%" },

        { "fx04_on",          "Arrangement",      "Learn the sections of the song from the audio and let the fills follow them", "", "" },
        { "fx04_calm",        "Arrangement Calm", "Thin the fills out in sections that are quiet compared with the rest of the song", "30-70", "%" },
        { "fx04_lift",        "Arrangement Lift", "More and longer fills just before an expected section change and in builds", "30-70", "%" },
        { "fx04_settle",      "Arrangement Settle", "Hold fills back right after a new section starts so the downbeat lands", "10-50", "%" },
        { "fx04_sensitivity", "Arrangement Sensitivity", "How readily a change in the music counts as a new section", "40-60", "%" },

        { "hgAmount", "Harmonic Amount", "How often Granular fills become stacked harmonies of the source pitch", "30-100", "%" },
        { "hgVoices", "Harmonic Voices", "How many harmony layers play: unison, octave, fifth, third and more", "2-4", "voices" },
        { "hgMotion", "Harmonic Motion", "How much the grains drift through the source and detune", "20-50", "%" },
        { "hgTone",   "Harmonic Tone",   "Darker to brighter harmony layers", "", "%" },
    };
}

} // namespace gapfill::info
