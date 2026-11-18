// Regression tests for parser findings from the libFuzzer campaign (docs/qa/FUZZING.md).
// Each input is the minimised reproducer; before the fix it hung or crashed.

#include "../TestFramework.h"

#include "features/commands/ControllerProfile.h"
#include "features/history/RecipeFile.h"
#include "features/i18n/Catalog.h"
#include "features/state/SafeJson.h"
#include "features/state/SafeXml.h"
#include "features/taste/RatingStore.h"
#include "PluginProcessor.h"

#include <juce_core/juce_core.h>

using namespace gapfill;

namespace
{
    // fuzz recipe timeout-e44b5aae: invalid UTF-8 (0xFF runs) inside XML-like text. JUCE's
    // XmlDocument looped forever in skipNextWhiteSpace on it, hanging the message thread.
    const unsigned char kInvalidUtf8Recipe[] = { 0x3c, 0x54, 0x46, 0x2d, 0x3e, 0x3f, 0x22, 0x38, 0x3c, 0x3f, 0x78, 0x6d, 0x6c, 0x20, 0xcc, 0xc0, 0x87, 0x96, 0x6c, 0x20, 0x76, 0x65, 0x22, 0x55, 0x54, 0x46, 0x2d, 0x39, 0x22, 0x3f, 0x3e, 0x76, 0x67, 0x22, 0x55, 0x54, 0x46, 0x52, 0x30, 0xff, 0x3c, 0x67, 0x22, 0x55, 0x54, 0x46, 0x52, 0xff, 0x3c, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3c, 0x3f, 0x0a, 0x3c, 0x47, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x46, 0x52, 0x22, 0x73, 0x78, 0x65, 0x65, 0x64, 0xff };

    juce::String rawText (const unsigned char* data, size_t size)
    {
        return juce::String::fromUTF8 (reinterpret_cast<const char*> (data), (int) size);
    }
}

GF_TEST (FuzzRegressions, invalid_utf8_xml_is_rejected_before_juce_parses_it)
{
    const auto text = rawText (kInvalidUtf8Recipe, sizeof (kInvalidUtf8Recipe));
    GF_EXPECT (! state::xmlTextIsSafe (text));
    GF_EXPECT (state::parseUntrustedXml (text) == nullptr);

    const auto recipe = history::parseRecipeText (text);
    GF_EXPECT (recipe.error.isNotEmpty());
}

GF_TEST (FuzzRegressions, invalid_utf8_rating_library_is_rejected_and_left_empty)
{
    auto file = juce::File::createTempFile ("ratings.xml");
    file.replaceWithData (kInvalidUtf8Recipe, sizeof (kInvalidUtf8Recipe));
    taste::RatingStore store (file);
    GF_EXPECT (! store.load());
    GF_EXPECT (store.entries().empty());
    file.deleteFile();
}

GF_TEST (FuzzRegressions, safe_xml_accepts_ordinary_utf8_documents)
{
    const auto xml = state::parseUntrustedXml (juce::String::fromUTF8 ("<GFRECIPE name=\"Caf\xc3\xa9\"/>"));
    GF_EXPECT (xml != nullptr && xml->getStringAttribute ("name") == juce::String::fromUTF8 ("Caf\xc3\xa9"));
}

GF_TEST (FuzzRegressions, processing_instruction_inside_an_element_is_rejected)
{
    // fuzz recipe timeout (round 2): JUCE skips "<?...?>" as whitespace in element content,
    // then rewinds to it and loops forever. Minimal form of the reproducer:
    GF_EXPECT (state::parseUntrustedXml (juce::String ("<a><?x?>b</a>")) == nullptr);
    GF_EXPECT (history::parseRecipeText (juce::String ("<GFRECIPE><?x?>b</GFRECIPE>")).error.isNotEmpty());

    // The XML declaration before the root stays accepted, as do comments in content.
    GF_EXPECT (state::parseUntrustedXml (juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<a><!-- c -->b</a>")) != nullptr);
}

GF_TEST (FuzzRegressions, host_state_with_a_processing_instruction_in_content_is_ignored)
{
    GapFillAudioProcessor proc;
    juce::MemoryBlock before;
    proc.getStateInformation (before);

    // Same binary framing as AudioProcessor::copyXmlToBinary: magic, length, UTF-8, NUL.
    const juce::String xml ("<GapFill><?x?>b</GapFill>");
    juce::MemoryOutputStream out;
    out.writeInt (0x21324356);
    out.writeInt ((int) xml.getNumBytesAsUTF8() + 1);
    out.write (xml.toRawUTF8(), xml.getNumBytesAsUTF8() + 1);
    proc.setStateInformation (out.getData(), (int) out.getDataSize());   // hung before the fix

    juce::MemoryBlock after;
    proc.getStateInformation (after);
    GF_EXPECT (after == before);
}

GF_TEST (FuzzRegressions, json_integer_overflow_and_deep_nesting_are_rejected_before_juce_parses)
{
    // fuzz profile crash-ee13a7b9 (UBSan): a 600-digit integer made juce::JSON overflow int64.
    const auto longNumber = juce::String ("{\"name\":\"x\",\"version\":1,\"bindings\":[")
                          + juce::String::repeatedString ("2", 600) + "]}";
    GF_EXPECT (! state::jsonWithinLimits ({ longNumber.toRawUTF8(), longNumber.getNumBytesAsUTF8() }));
    const auto parsed = commands::parseProfile (longNumber);
    GF_EXPECT (! parsed.ok && parsed.error.isNotEmpty());

    // 18 digits still fit; digits inside strings are text, not numbers.
    const juce::String fits ("{\"a\":123456789012345678,\"b\":\"12345678901234567890123\"}");
    GF_EXPECT (state::jsonWithinLimits ({ fits.toRawUTF8(), fits.getNumBytesAsUTF8() }));

    // JUCE recurses per nesting level: 100k levels would overflow the stack.
    const auto deep = juce::String::repeatedString ("[", 100000) + juce::String::repeatedString ("]", 100000);
    i18n::Catalog catalog;
    juce::String error;
    GF_EXPECT (! catalog.load (deep.toRawUTF8(), deep.getNumBytesAsUTF8(), error) && error.isNotEmpty());
}

GF_TEST (FuzzRegressions, built_in_controller_profiles_still_parse)
{
    // The guard must not reject real profiles: every built-in re-parses from its own JSON.
    GF_EXPECT (! commands::builtInProfiles().empty());
    for (const auto& info : commands::builtInProfiles())
        GF_EXPECT (commands::parseProfile (commands::profileToJson (info.profile)).ok);
}
