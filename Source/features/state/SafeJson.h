#pragma once

// Untrusted-JSON entry point (controller profiles, translation catalogs). Rejects, before
// juce::JSON::parse runs:
//   * an integer digit run longer than 18 digits outside a string: JUCE accumulates it in an
//     int64 (`intValue * 10 + digit`) before it looks for '.'/'e', which is signed overflow
//     (undefined behaviour; found by fuzzing a controller profile);
//   * nesting deeper than 64 arrays/objects: JUCE's parser recurses per level.
// Linear time, constant space, no allocation. Callers keep their own schema validation.

#include <juce_core/juce_core.h>

#include <cstddef>
#include <string_view>

namespace gapfill::state
{

inline bool jsonWithinLimits (std::string_view text, int maxDepth = 64) noexcept
{
    int depth = 0, digits = 0;
    bool inString = false, escaped = false;

    for (const char c : text)
    {
        if (inString)
        {
            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  inString = false;
            continue;
        }

        if (c >= '0' && c <= '9')
        {
            if (++digits > 18)
                return false;
            continue;
        }

        digits = 0;

        if (c == '"')
            inString = true;
        else if (c == '[' || c == '{')
        {
            if (++depth > maxDepth)
                return false;
        }
        else if (c == ']' || c == '}')
            --depth;
    }

    return true;
}

/** juce::JSON::parse behind jsonWithinLimits; a rejected text yields a failed Result. */
inline juce::Result parseUntrustedJson (const juce::String& text, juce::var& result)
{
    if (! jsonWithinLimits ({ text.toRawUTF8(), text.getNumBytesAsUTF8() }))
        return juce::Result::fail ("JSON nesting or a number is outside the supported limits");

    return juce::JSON::parse (text, result);
}

} // namespace gapfill::state
