#include "Strings.h"
#include "Catalog.h"
#include "CatalogData.h"
#include "../presetbrowser/GlobalSettings.h"
#include "../info/ParamInfo.h"
#include <cstring>

namespace gapfill::i18n
{
namespace
{
    class Service final : public juce::ChangeBroadcaster
    {
    public:
        Service()
        {
            juce::String error;
            for (const auto& locale : data::languages)
                if (juce::String (locale.code) == "en")
                    english.load (locale.bytes, locale.size, error);
            for (const auto& entry : english.entries())
                if (! entry.first.startsWith ("locale."))
                    byEnglish.emplace (entry.second, entry.first);
            // Exact registered presentation aliases only. Arbitrary unknown or
            // user text is never interpreted as a sentence to be translated.
            for (const auto& metadata : info::ParamInfoRegistry::get().all())
                parameterTooltips.emplace (info::tooltipFor (metadata), metadata);
            select (settings::GlobalSettings::shared()->getString (settings::GlobalSettings::keyLanguage, "en"), false);
        }

        juce::String lookup (const juce::String& key) const
        {
            const auto translated = selected.get (key, &english);
            if (translated.isNotEmpty())
                return translated;
            return key;
        }

        juce::String translate (const juce::String& source) const
        {
            if (language == "en" || source.isEmpty())
                return source;
            const auto key = byEnglish.find (source);
            if (key != byEnglish.end())
                return lookup (key->second);
            const auto tooltip = parameterTooltips.find (source);
            if (tooltip == parameterTooltips.end())
                return source;
            const auto& metadata = tooltip->second;
            const auto title = translate (metadata.shortName);
            const auto body = translate (metadata.oneLine);
            const juce::String range (metadata.typicalRange), unit (metadata.unit);
            if (range.isEmpty())
                return Catalog::expand (translate ("{0} - {1}"), { title, body });
            const auto pattern = juce::String (metadata.oneLine).endsWithChar ('.')
                ? (unit.isEmpty() ? "{0} - {1} Typical: {2}" : "{0} - {1} Typical: {2} {3}")
                : (unit.isEmpty() ? "{0} - {1}. Typical: {2}" : "{0} - {1}. Typical: {2} {3}");
            return Catalog::expand (translate (pattern), { title, body, range, unit });
        }

        bool select (const juce::String& code, bool persist)
        {
            for (const auto& locale : data::languages)
                if (code == locale.code)
                {
                    Catalog candidate;
                    juce::String error;
                    if (! candidate.load (locale.bytes, locale.size, error, &english))
                        return false;
                    if (code != language)
                    {
                        selected = std::move (candidate);
                        language = code;
                        ++generation;
                        sendChangeMessage();
                    }
                    if (persist)
                        settings::GlobalSettings::shared()->setString (settings::GlobalSettings::keyLanguage, code);
                    return true;
                }
            return false;
        }

        Catalog english, selected;
        std::map<juce::String, juce::String> byEnglish;
        std::map<juce::String, info::ParamInfo> parameterTooltips;
        juce::String language = "en";
        std::uint64_t generation = 0;
    };

    Service& service()
    {
        static Service instance;              // first touched by the message-thread editor/options path
        return instance;
    }
}

juce::String tr (const juce::String& key) { return service().lookup (key); }
juce::String text (const juce::String& english) { return service().translate (english); }
juce::String format (const juce::String& englishPattern, std::initializer_list<juce::String> arguments)
{
    juce::StringArray values;
    for (const auto& argument : arguments)
        values.add (argument);
    return Catalog::expand (text (englishPattern), values);
}
juce::String errorText (const juce::String& english)
{
    struct Pattern { const char* prefix; const char* suffix; const char* sentence; };
    // File services also feed CLI/serialization callers, so their errors remain
    // canonical English. Translate only at a presentation sink, never filenames.
    static constexpr Pattern patterns[] {
        { "Couldn't create the folder ", "", "Couldn't create the folder {0}" },
        { "Couldn't write ", "", "Couldn't write {0}" },
        { "Couldn't open ", " for writing.", "Couldn't open {0} for writing." },
        { "Couldn't create a WAV writer for ", "", "Couldn't create a WAV writer for {0}" },
        { "Writing ", " failed (is the disk full?).", "Writing {0} failed (is the disk full?)." },
        { "The file ", " was not written completely.", "The file {0} was not written completely." }
    };
    for (const auto& pattern : patterns)
        if (english.startsWith (pattern.prefix) && english.endsWith (pattern.suffix))
            return format (pattern.sentence, { english.substring ((int) std::strlen (pattern.prefix),
                              english.length() - (int) std::strlen (pattern.suffix)) });
    return text (english);
}
juce::String currentLanguage() { return service().language; }
std::uint64_t languageGeneration() { return service().generation; }
bool setLanguage (const juce::String& code, bool persist) { return service().select (code, persist); }
juce::ChangeBroadcaster& languageChanges() { return service(); }
std::vector<Language> availableLanguages()
{
    std::vector<Language> result;
    for (const auto& locale : data::languages)
    {
        Catalog catalog;
        juce::String error;
        if (catalog.load (locale.bytes, locale.size, error, &service().english))
        {
            const auto name = catalog.get ("locale.nativeName");
            result.push_back ({ locale.code, name.isNotEmpty() ? name : juce::String (locale.code) });
        }
    }
    return result;
}
}
