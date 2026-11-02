#include "../info/ParamInfo.h"
namespace gapfill::harmonic
{
namespace
{
    const info::ParamInfoRegistrar pitchSyncInfo {
        { "fx01_mode", "Pitch Sync Mode", "Opt-in pitch-synchronous Granular; Off uses the original Harmonic controls", "", "" },
        { "fx01_intervals", "Pitch Sync Intervals", "Harmony interval set; Scale uses the current known key", "", "" },
        { "fx01_voices", "Pitch Sync Voices", "Number of pitch-synchronous harmony layers; sampled at fill start", "1-4", "" },
        { "fx01_motion", "Pitch Sync Motion", "Slow source movement and pitch variation; sampled at fill start", "0-30", "%" },
        { "fx01_scatter", "Pitch Sync Scatter", "Seeded source-epoch and onset variation; sampled at fill start", "0-20", "%" },
        { "fx01_tone", "Pitch Sync Tone", "Darker to brighter pitch-synchronous harmony layers", "-30 to +30", "%" },
        { "fx01_snap", "Pitch Sync Key Snap", "Move interval pitches toward the current known key; unknown key is unchanged", "0-100", "%" }
    };
}
}
