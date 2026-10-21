// U-9: help text for the WP10 routing and system parameters (wet-only output, MIDI output,
// sidechain-driven fills, co-op, quality mode). One line each (<= 90 characters).

#include "../info/ParamInfo.h"

namespace gapfill::info
{

namespace
{
    const ParamInfoRegistrar routingInfo
    {
        { "wetSolo",      "Wet Only Output",   "Main output carries only what GapFill generates; the Dry and Wet knobs are ignored", "", "" },

        { "midiOutMode",  "MIDI Out Mode",     "Send MIDI notes for detected gaps, for fills, or both; Off sends nothing", "", "" },
        { "midiOutNote",  "MIDI Out Note",     "Note for gaps; fills use the next note up (36 = C1 in the usual naming)", "36-60", "" },
        { "midiOutChan",  "MIDI Out Channel",  "MIDI channel for the gap and fill notes (10 is the usual drum channel)", "1-16", "" },

        { "scSnap",       "Sidechain Snap",    "Delays a fill start to a nearby sidechain hit so it lands on the external rhythm", "20-60", "%" },
        { "scGate",       "Sidechain Gate",    "Fill only while the sidechain is quiet (ducking) or only while it is active", "", "" },
        { "scThresh",     "Sidechain Threshold", "Level that separates a quiet sidechain from an active one; also the hit detector floor", "-40 to -20", "dB" },

        { "coopGroup",    "Co-op Group",       "Instances of GapFill in the same group take turns; Off shares nothing", "", "" },
        { "coopMode",     "Co-op Mode",        "Avoid overlap, take turns bar by bar, or follow other fills; live modes vary run to run", "", "" },

        { "qualityMode",  "Quality",           "Full is the reference sound; Balanced and Lite use less CPU; Auto steps down under load", "", "" },
    };
}

} // namespace gapfill::info
