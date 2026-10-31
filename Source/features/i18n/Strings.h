#pragma once

// F-P04. All translation/service functions are MESSAGE THREAD ONLY. They must
// never be called from a processor callback, DSP stage or serialization path.
#include <juce_events/juce_events.h>
#include <initializer_list>
#include <vector>

namespace gapfill::i18n
{
struct Language { juce::String code, name; };

/** Stable catalog key; missing translations use the embedded English table. */
juce::String tr (const juce::String& key);
/** Translate a complete known English string. Unknown/user strings stay verbatim. */
juce::String text (const juce::String& english);
/** Complete-sentence template; arguments are inserted verbatim, never translated. */
juce::String format (const juce::String& englishPattern, std::initializer_list<juce::String> arguments);
juce::String currentLanguage();
std::uint64_t languageGeneration();
std::vector<Language> availableLanguages();
bool setLanguage (const juce::String& code, bool persist = true);
juce::ChangeBroadcaster& languageChanges();

/** Tests/previews restore the prior language even if an assertion throws. Never writes preferences. */
class ScopedLanguage
{
public:
    explicit ScopedLanguage (const juce::String& code) : previous (currentLanguage()) { setLanguage (code, false); }
    ~ScopedLanguage() { setLanguage (previous, false); }
private:
    juce::String previous;
};
}
