#pragma once

// Untrusted-XML entry point for every file or host blob GapFill parses (presets, recipes,
// rating library, preset metadata, settings, host state text). Rejects, before JUCE's
// recursive XmlDocument runs:
//   * invalid UTF-8: JUCE's parser can loop forever on it (found by fuzzing: a 77-byte
//     recipe with 0xFF bytes hung XmlDocument::skipNextWhiteSpace, i.e. the message thread);
//   * a processing instruction (<?...?>) after the root element starts: JUCE's
//     readChildElements skips it as whitespace, then rewinds and loops forever (fuzzing:
//     `<a><?x?>b</a>`). GapFill files only use one at the start (the XML declaration);
//   * DTD/entity declarations and nesting deeper than 64 (state::xmlWithinLimits);
//   * files over the caller's byte cap, checked before reading them.
// Returns nullptr on rejection; callers keep their own semantic validation and messages.

#include "XmlLimits.h"

#include <juce_core/juce_core.h>

#include <cstring>
#include <limits>
#include <string_view>
#include <memory>

namespace gapfill::state
{

/** True when every "<?" lies before the root element's start tag. */
inline bool processingInstructionsOnlyInProlog (std::string_view text) noexcept
{
    bool inRoot = false;

    for (std::size_t i = 0; i + 1 < text.size(); ++i)
    {
        if (text[i] != '<')
            continue;

        const char next = text[i + 1];

        if (next == '?')
        {
            if (inRoot)
                return false;
        }
        else if (next != '!')
        {
            inRoot = true;
        }
    }

    return true;
}

inline bool xmlTextIsSafe (const juce::String& text) noexcept
{
    // strlen, not getNumBytesAsUTF8(): the latter walks code points and reads past the end of
    // a string holding invalid UTF-8 (heap overflow found by the fuzzer).
    const auto* utf8 = text.toRawUTF8();
    const auto bytes = std::strlen (utf8);
    return bytes <= (size_t) std::numeric_limits<int>::max()
        && juce::CharPointer_UTF8::isValidString (utf8, (int) bytes)
        && processingInstructionsOnlyInProlog ({ utf8, bytes })
        && xmlWithinLimits ({ utf8, bytes });
}

inline std::unique_ptr<juce::XmlElement> parseUntrustedXml (const juce::String& text)
{
    return xmlTextIsSafe (text) ? juce::XmlDocument::parse (text) : nullptr;
}

inline std::unique_ptr<juce::XmlElement> parseUntrustedXml (const juce::File& file, juce::int64 maxBytes)
{
    if (! file.existsAsFile() || file.getSize() > maxBytes)
        return nullptr;

    return parseUntrustedXml (file.loadFileAsString());
}

} // namespace gapfill::state
