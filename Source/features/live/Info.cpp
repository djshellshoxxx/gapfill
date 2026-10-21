// U-9: help text for the live-control parameters (macros, hold performance, safety, Beat Battle).
// One line each (<= 90 characters).

#include "../info/ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar liveInfo
    {
        { "macroActivity",    "Macro Activity",    "One knob for how busy the fills are: more fills, denser and more often", "30-70", "%" },
        { "macroChaos",       "Macro Chaos",       "One knob for how unpredictable the fills are: more variation and surprise", "20-60", "%" },
        { "macroSpace",       "Macro Space",       "One knob for how roomy the fills sound: wetter, wider and longer tails", "20-60", "%" },
        { "macroDestruction", "Macro Destruction", "One knob for how damaged the fills sound: crush, grit and tape wear", "0-40", "%" },
        { "macrosOn",         "Macros On",         "Lets the four macros drive their target settings; off leaves every target alone", "", "" },
        { "wetSafety",        "Wet Safety",        "Soft limiter and DC guard on the wet signal so live tweaks cannot blast the speakers", "", "" },
        { "battleEvery",      "Beat Battle",       "Every N bars the engine swaps the fill settings with a rival set to keep it lively", "", "" },
        { "fm10_stutter",     "Hold Stutter",      "While on, holds a stutter of the last moment of sound (MIDI or the pads)", "", "" },
        { "fm10_freeze",      "Hold Freeze",       "While on, freezes the last moment of sound into a sustained wash", "", "" },
        { "fm10_reverse",     "Hold Reverse",      "While on, plays the last moment of sound backwards", "", "" },
        { "fm10_tape",        "Hold Tape Stop",    "While on, slows the last moment of sound to a stop like a tape machine", "", "" },
        { "fm10_mode",        "Hold Mode",         "Momentary holds only while pressed; Latch toggles on each press", "", "" },
        { "fm10_level",       "Hold Level",        "Loudness of the hold sound relative to the fills", "70-100", "%" },
        { "fm10_dryDuck",     "Hold Dry Duck",     "Lowers the dry signal while a hold is active so the hold stands out", "0-60", "%" },
        { "fm10_protect",     "Hold Respects Protection", "Keeps holds from covering hits that Protection would normally keep clear", "", "" },
        { "fm10_timeout",     "Hold Timeout",      "A latched hold releases by itself after this many seconds", "10-30", "s" },
    };
}

} // namespace gapfill::info
