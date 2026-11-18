#pragma once

#include <cstddef>
#include <string_view>

namespace gapfill::state
{

// A non-recursive resource check before JUCE constructs an XML/ValueTree graph.
// This is not an XML validator: JUCE still checks syntax and the state loader
// still checks semantics. DTDs are not part of the saved-state/preset format.
// No allocation, framework dependency or mutation; linear time, constant space.
inline bool xmlWithinLimits (std::string_view text, std::size_t maxDepth = 64) noexcept
{
    std::size_t depth = 0, position = 0;
    bool seenElement = false;

    while ((position = text.find ('<', position)) != std::string_view::npos)
    {
        const auto remaining = text.substr (position);
        const auto skip = [&] (std::string_view terminator, std::size_t prefix)
        {
            const auto end = text.find (terminator, position + prefix);
            if (end == std::string_view::npos)
                return false;
            position = end + terminator.size();
            return true;
        };

        if (remaining.substr (0, 4) == "<!--")
        {
            if (! skip ("-->", 4)) return false;
            continue;
        }
        if (remaining.substr (0, 9) == "<![CDATA[")
        {
            if (depth == 0 || ! skip ("]]>", 9)) return false;
            continue;
        }
        if (remaining.substr (0, 2) == "<?")
        {
            if (! skip ("?>", 2)) return false;
            continue;
        }
        if (remaining.size() < 3 || remaining[1] == '!')
            return false; // Includes DTDs and entity declarations/expansion.

        const bool closing = remaining[1] == '/';
        char quote = 0;
        auto end = position + (closing ? 2 : 1);
        for (; end < text.size(); ++end)
        {
            const char c = text[end];
            if (quote != 0)
            {
                if (c == quote) quote = 0;
            }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '<') return false;
            else if (c == '>') break;
        }
        if (end == text.size()) return false;

        if (closing)
        {
            if (depth == 0) return false;
            --depth;
        }
        else
        {
            seenElement = true;
            if (depth >= maxDepth) return false;
            if (text[end - 1] != '/') ++depth;
        }
        position = end + 1;
    }
    return seenElement && depth == 0;
}

} // namespace gapfill::state
