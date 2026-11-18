#pragma once

// F-L20 per-voice spectral-envelope settings. Latched at fill start. This is
// separate from the shipped fmt* bus card; its neutral path performs no work.

namespace gapfill::formant
{

struct FormantSetup
{
    bool enabled = false;
    float amount01 = 0.0f;
    float shiftSt = 0.0f;
    float tiltDbPerOct = 0.0f;
    int vowel = 0;                    // Off, A, E, I, O, U
    float preserve01 = 1.0f;
    int detail = 1;                   // Low/Medium/High: 0.5/1/2 ms lifter
};

} // namespace gapfill::formant
