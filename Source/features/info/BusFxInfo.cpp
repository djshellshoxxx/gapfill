// U-9: help text for the WP9 bus / spectral effects (resonator, frequency-shifted delay, dub, formant,
// spectral freeze, convolution). One line each (<= 90 characters).

#include "ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar busFxInfo
    {
        { "resAmount",    "Resonator Amount",  "How much of the tuned resonator is mixed into the fills; 0 = off", "10-40", "%" },
        { "resNote",      "Resonator Note",    "Pitch the resonator rings at, as a MIDI note (60 = middle C)", "36-72", "" },
        { "resMode",      "Resonator Mode",    "Manual uses the note; Scale degrees follows the detected or chosen scale", "", "" },
        { "resDecay",     "Resonator Decay",   "How long the resonator keeps ringing after each fill", "30-70", "%" },
        { "resDamp",      "Resonator Damping", "Darkens the ring: higher values roll off the highs faster", "20-60", "%" },
        { "resSpread",    "Resonator Spread",  "Detunes the resonator voices apart for a wider, richer ring", "10-50", "%" },

        { "fsdAmount",    "FS Delay Amount",   "How much of the frequency-shifted delay is mixed in; 0 = off", "10-40", "%" },
        { "fsdShift",     "FS Delay Shift",    "Frequency shift added on every repeat; negative shifts down", "-100-100", "Hz" },
        { "fsdTime",      "FS Delay Time",     "Delay time as a tempo division (falls back to a free tempo without a transport)", "1/8-1/4", "" },
        { "fsdFeedback",  "FS Delay Feedback", "How much of each repeat feeds back, so the shift keeps climbing", "30-60", "%" },
        { "fsdDamp",      "FS Delay Damping",  "Darkens the repeats so they fade into the background", "20-50", "%" },
        { "fsdStereo",    "FS Delay Stereo",   "Spreads the repeats between left and right", "20-60", "%" },

        { "dubAmount",    "Dub Amount",        "How much of the dub echo network is mixed in; 0 = off", "10-40", "%" },
        { "dubDiv",       "Dub Division",      "Echo time as a tempo division", "1/8-1/4", "" },
        { "dubFeedback",  "Dub Feedback",      "How long the echoes keep repeating; very high values self-oscillate gently", "40-70", "%" },
        { "dubLow",       "Dub Low Cut",       "Removes low frequencies from the echoes so they stay out of the bass", "80-200", "Hz" },
        { "dubHigh",      "Dub High Cut",      "Rolls off the highs of the echoes; lower is darker and more tape-like", "2000-6000", "Hz" },
        { "dubMove",      "Dub Move",          "Slowly modulates the echo time for a drifting, tape-like movement", "10-40", "%" },
        { "dubSat",       "Dub Saturation",    "Adds warm saturation to the repeats", "10-40", "%" },
        { "dubPing",      "Dub Ping-Pong",     "Bounces the echoes between left and right", "", "" },

        { "fmtAmount",    "Formant Amount",    "How much of the formant-shifted sound is mixed into the fills; 0 = off", "20-60", "%" },
        { "fmtShift",     "Formant Shift",     "Moves the vocal-tract character up or down without changing the pitch", "-6-+6", "st" },
        { "fmtTilt",      "Formant Tilt",      "Tilts the spectrum: positive is brighter, negative is darker", "-30-+30", "%" },
        { "fmtVowel",     "Formant Vowel",     "Pushes the sound toward a vowel; Shift only leaves the vowel alone", "", "" },

        { "sfzAmount",    "Freeze Amount",     "How much of the spectral freeze is mixed into the fills; 0 = off", "20-60", "%" },
        { "sfzLength",    "Freeze Length",     "How much of the sound is captured and held", "400-1500", "ms" },
        { "sfzDecay",     "Freeze Decay",      "How quickly the frozen sound fades away", "30-70", "%" },
        { "sfzSmear",     "Freeze Smear",      "Blurs the frozen spectrum for a smoother, more pad-like hold", "20-60", "%" },
        { "sfzMorph",     "Freeze Morph",      "Glides the frozen spectrum toward the live input", "0-50", "%" },
        { "sfzPitch",     "Freeze Pitch",      "Transposes the frozen sound", "-12-+12", "st" },
        { "sfzWidth",     "Freeze Width",      "Stereo width of the frozen sound", "30-80", "%" },

        { "cvAmount",     "Convolution Amount", "How much of the transient-triggered reverb is mixed in; 0 = off", "10-40", "%" },
        { "cvSize",       "Convolution Size",  "Length of the reverb tail built from the hit that opened the gap", "60-250", "ms" },
        { "cvDamp",       "Convolution Damping", "Darkens the tail so it sits behind the music", "20-60", "%" },
    };
}

} // namespace gapfill::info
