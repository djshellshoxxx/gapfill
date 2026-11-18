#pragma once

#include <algorithm>
#include <array>

namespace gapfill::formant
{

/** Adult-average resonances used as a colour, not a speech synthesiser. */
inline const std::array<float, 3>& vowelFormants (int vowel) noexcept
{
    static constexpr std::array<std::array<float, 3>, 6> vowels {{
        {{ 0.0f, 0.0f, 0.0f }}, {{ 730.0f, 1090.0f, 2440.0f }},
        {{ 530.0f, 1840.0f, 2480.0f }}, {{ 270.0f, 2290.0f, 3010.0f }},
        {{ 570.0f, 840.0f, 2410.0f }}, {{ 300.0f, 870.0f, 2240.0f }}
    }};
    return vowels[(std::size_t) std::clamp (vowel, 0, 5)];
}

} // namespace gapfill::formant
