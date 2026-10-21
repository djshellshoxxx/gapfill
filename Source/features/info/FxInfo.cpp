// U-9: help text for the WP8 voice effects (swell, cloud, ratchet, rate crush, tape destruction, freeze and
// scrub, per-band effect switches) and Fill Lock. One line each (<= 90 characters). With this file the
// Tests/param_info_allowlist.txt allow-list is empty.

#include "ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar fxInfo
    {
        { "fl01_lock",     "Fill Lock",         "Repeats the character of the latest fill in every gap that gets a fill", "", "" },

        { "swellAmount",   "Swell Amount",      "Chance that a fill becomes a reversed swell that lands on the next hit; 0 = off", "10-40", "%" },
        { "swellCurve",    "Swell Curve",       "Shape of the build-up: Exponential rises late, Linear evenly", "", "" },
        { "swellFilter",   "Swell Filter",      "How far the swell opens its low-pass filter while it rises", "40-80", "%" },
        { "swellTarget",   "Swell Target",      "Where the swell lands: the end of the gap, the next beat or the next bar", "", "" },

        { "cloudAmount",   "Cloud Amount",      "Chance that a Granular fill becomes a dense, smooth grain cloud; 0 = off", "10-40", "%" },
        { "cloudDensity",  "Cloud Density",     "Grains started per second; more grains sound smoother", "20-60", "grains/s" },
        { "cloudSize",     "Cloud Grain Size",  "Length of each grain; long grains blur together", "60-150", "ms" },
        { "cloudSpan",     "Cloud Span",        "How far back in the capture the grains may be taken from", "300-1000", "ms" },
        { "cloudJitter",   "Cloud Jitter",      "Randomises grain timing and start positions", "20-60", "%" },
        { "cloudPitch",    "Cloud Pitch Spread", "Random pitch change per grain; snaps to Scale Lock when it is on", "0-300", "cents" },
        { "cloudSpread",   "Cloud Stereo Spread", "Scatters the grains between left and right", "30-80", "%" },

        { "ratAmount",     "Ratchet Amount",    "Chance that a fill is replaced by a rolling, repeated hit; 0 = off", "10-40", "%" },
        { "ratDiv",        "Ratchet Division",  "Spacing of the repeats as a tempo division", "1/16-1/8", "" },
        { "ratRepeats",    "Ratchet Repeats",   "Number of repeats in each ratchet", "3-8", "" },
        { "ratAccel",      "Ratchet Accel",     "Positive speeds the repeats up, negative slows them down", "-50-50", "%" },
        { "ratGate",       "Ratchet Gate",      "How much of each repeat sounds before it is cut", "50-90", "%" },

        { "rcAmount",      "Crush Amount",      "Chance that a fill is bit- and rate-crushed in time with the music; 0 = off", "10-50", "%" },
        { "rcBits",        "Crush Bits",        "Bit depth the sound is reduced to; fewer bits are grittier", "4-10", "bits" },
        { "rcRate",        "Crush Rate",        "Keeps every Nth sample; higher values sound more aliased", "2-12", "" },
        { "rcHold",        "Crush Hold",        "Free, or holds each sample for a tempo division so the crush is rhythmic", "", "" },
        { "rcTone",        "Crush Tone",        "Tilts the crushed sound darker (negative) or brighter (positive)", "-30-30", "%" },

        { "tdAmount",      "Tape Destruction",  "How much of the tape damage is applied to fills; 0 = off", "10-50", "%" },
        { "tdWow",         "Tape Wow",          "Slow pitch wobble, like a worn capstan", "10-40", "%" },
        { "tdFlutter",     "Tape Flutter",      "Fast pitch flutter, like a damaged tape path", "10-40", "%" },
        { "tdSat",         "Tape Saturation",   "Warm tape-style saturation", "10-40", "%" },
        { "tdDropout",     "Tape Dropouts",     "Random short level dips, like worn oxide", "0-30", "%" },
        { "tdSplice",      "Tape Splices",      "Random small jumps in the playback position, like a bad edit", "0-30", "%" },

        { "scrubOn",       "Scrub",             "Freezes the last second of audio so you can scrub through it", "", "" },
        { "scrubPos",      "Scrub Position",    "Where in the frozen audio the scrub head plays", "", "%" },
        { "scrubWidth",    "Scrub Width",       "Length of the audio window under the scrub head", "40-200", "ms" },

        { "bandLowFx",     "Low Band Effects",  "Applies the effects to fills in the low band; off keeps them clean", "", "" },
        { "bandMidFx",     "Mid Band Effects",  "Applies the effects to fills in the mid band; off keeps them clean", "", "" },
        { "bandHighFx",    "High Band Effects", "Applies the effects to fills in the high band; off keeps them clean", "", "" },
    };
}

} // namespace gapfill::info
