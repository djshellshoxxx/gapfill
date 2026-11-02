#pragma once

// F-P04: bounded data-only language catalog. No GUI, processor or audio state.
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace gapfill::i18n
{
inline constexpr size_t maxCatalogBytes = 256 * 1024;

class Catalog
{
public:
    /** UTF-8 JSON object of string keys/values. Invalid input leaves this catalog unchanged. */
    bool load (const void* utf8, size_t bytes, juce::String& error, const Catalog* english = nullptr);
    bool loadFile (const juce::File&, juce::String& error, const Catalog* english = nullptr);
    juce::String get (const juce::String& key, const Catalog* english = nullptr) const;
    const std::map<juce::String, juce::String>& entries() const noexcept { return strings; }
    static bool placeholders (const juce::String&, std::set<int>& indices);
    static juce::String expand (const juce::String&, const juce::StringArray& arguments);

private:
    std::map<juce::String, juce::String> strings;
};
}
