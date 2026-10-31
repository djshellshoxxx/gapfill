#include "Strings.h"
#include "Catalog.h"
#include "CatalogData.h"
#include "../presetbrowser/GlobalSettings.h"

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
            return key == byEnglish.end() ? source : lookup (key->second);
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
