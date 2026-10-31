#include "../info/ParamInfo.h"

namespace gapfill::formant
{
namespace
{
    const info::ParamInfoRegistrar formantInfo {
        { "fl20_amount", "Voice Formant Amount", "Per-fill envelope warp; zero leaves existing fills unchanged", "0-50", "%" },
        { "fl20_shift", "Voice Formant Shift", "Moves resonant character without changing pitch; sampled at fill start", "-7 to +7", "st" },
        { "fl20_tilt", "Voice Formant Tilt", "Brightness slope of the resonant envelope, sampled at fill start", "-3 to +3", "dB/oct" },
        { "fl20_vowel", "Voice Formant Vowel", "Adds vowel-like resonances as a colour, not speech", "", "" },
        { "fl20_preserve", "Voice Formant Preserve", "Original envelope versus vowel shape; irrelevant with Vowel Off", "50-100", "%" },
        { "fl20_detail", "Voice Formant Detail", "Low, Medium or High spectral-envelope resolution", "", "" }
    };
}
} // namespace gapfill::formant
