#include "Catalog.h"
#include <cstring>

namespace gapfill::i18n
{
bool Catalog::placeholders (const juce::String& value, std::set<int>& indices)
{
    indices.clear();
    for (int i = 0; i < value.length(); ++i)
    {
        const auto ch = value[i];
        if ((ch == '{' || ch == '}') && i + 1 < value.length() && value[i + 1] == ch)
        {
            ++i;
            continue;
        }
        if (ch == '}')
            return false;
        if (ch != '{')
            continue;
        const auto end = value.indexOfChar (i + 1, '}');
        if (end < 0)
            return false;
        const auto number = value.substring (i + 1, end);
        if (number.isEmpty() || number.length() > 2 || ! number.containsOnly ("0123456789")
            || number.getIntValue() > 31 || (number.length() > 1 && number[0] == '0'))
            return false;
        indices.insert (number.getIntValue());
        i = end;
    }
    return true;
}

bool Catalog::load (const void* data, size_t bytes, juce::String& error, const Catalog* english)
{
    const auto* utf8 = static_cast<const char*> (data);
    if (utf8 == nullptr || bytes == 0 || bytes > maxCatalogBytes || std::memchr (utf8, 0, bytes) != nullptr
        || ! juce::CharPointer_UTF8::isValidString (utf8, (int) bytes))
    {
        error = "The language file must be valid UTF-8 and no larger than 256 KiB.";
        return false;
    }
    juce::var parsed;
    if (juce::JSON::parse (juce::String::fromUTF8 (utf8, (int) bytes), parsed).failed() || ! parsed.isObject())
    {
        error = "The language file is not a JSON object.";
        return false;
    }
    std::map<juce::String, juce::String> replacement;
    const auto& properties = parsed.getDynamicObject()->getProperties();
    for (int i = 0; i < properties.size(); ++i)
    {
        const auto key = properties.getName (i).toString();
        const auto& value = properties.getValueAt (i);
        std::set<int> translatedSlots, englishSlots;
        if (key.isEmpty() || key.length() > 128 || ! key.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
            || ! value.isString() || value.toString().isEmpty() || value.toString().length() > 8192
            || ! placeholders (value.toString(), translatedSlots))
        {
            error = "A language entry has an invalid key, value or placeholder.";
            return false;
        }
        if (english != nullptr && english->get (key).isNotEmpty()
            && (! placeholders (english->get (key), englishSlots) || translatedSlots != englishSlots))
        {
            error = "A translation must preserve all numbered placeholders.";
            return false;
        }
        replacement.emplace (key, value.toString());
    }
    strings.swap (replacement);                  // atomic validation: never retain partial data
    error.clear();
    return true;
}

bool Catalog::loadFile (const juce::File& file, juce::String& error, const Catalog* english)
{
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > (juce::int64) maxCatalogBytes)
    {
        error = "The language file is missing or larger than 256 KiB.";
        return false;
    }
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
    {
        error = "The language file could not be read.";
        return false;
    }
    return load (bytes.getData(), bytes.getSize(), error, english);
}

juce::String Catalog::get (const juce::String& key, const Catalog* english) const
{
    const auto found = strings.find (key);
    return found == strings.end() ? (english != nullptr ? english->get (key) : juce::String()) : found->second;
}

juce::String Catalog::expand (const juce::String& pattern, const juce::StringArray& arguments)
{
    std::set<int> slots;
    if (! placeholders (pattern, slots) || (! slots.empty() && *slots.rbegin() >= arguments.size()))
        return pattern;                         // invalid caller: safe original, no partial sentence
    juce::String result;
    size_t reserve = pattern.getNumBytesAsUTF8();
    for (const auto& argument : arguments)
        reserve += argument.getNumBytesAsUTF8();
    result.preallocateBytes (reserve + 1);
    for (int i = 0; i < pattern.length(); ++i)
    {
        const auto ch = pattern[i];
        if ((ch == '{' || ch == '}') && i + 1 < pattern.length() && pattern[i + 1] == ch)
        {
            result += juce::String::charToString (ch);
            ++i;
        }
        else if (ch == '{')
        {
            const auto end = pattern.indexOfChar (i + 1, '}');
            result += arguments[pattern.substring (i + 1, end).getIntValue()];
            i = end;
        }
        else
            result += juce::String::charToString (ch);
    }
    return result;
}
}
