#include "../TestFramework.h"
#include "dsp/RecipeExtras.h"
#include "dsp/EventLogs.h"
#include "features/lock/LockState.h"
#include "features/commands/RecipeCommandTargets.h"
#include "features/abcompare/RecipeAB.h"
#include "features/history/RecipeCodec.h"
#include "features/history/RecipeFile.h"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <iostream>

using namespace gapfill;
using namespace gftest;

namespace
{
    RecipeExtras details (std::uint64_t seed = 0x123456789ABCDEF0ull)
    {
        RecipeExtras extras;
        extras.harmonic.version = 1;
        extras.harmonic.outcome = harmonic::psola::Outcome::Used;
        extras.harmonic.intervals = 3;
        extras.harmonic.voices = 4;
        extras.harmonic.f0Hz = 220.0f;
        extras.harmonic.clarity = 0.9f;
        extras.tape.version = 1;
        extras.tape.chosen = 1;
        extras.tape.subseed = seed;
        extras.tape.values = { 7000, 3000, 2500, 3000, 2000, 6000, 1000, 2000, 10000 };
        return extras;
    }
}

GF_TEST (RecipeExtras, fixed_size_canonical_envelope_roundtrips_both_feature_decisions)
{
    static_assert (sizeof (FillRecipe) == 16 && sizeof (RecipeExtras) == 48);
    const auto source = details();
    std::array<std::uint8_t, kRecipeExtrasBytes> bytes {};
    GF_EXPECT (isValid (source) && hasRecipeExtras (source) && encodeRecipeExtras (source, bytes));
    GF_EXPECT (bytes[0] == 1 && bytes[1] == 3 && bytes[2] == 0 && bytes[3] == 0);
    GF_EXPECT (bytes[4] == 1 && bytes[5] == 1 && bytes[6] == 3 && bytes[7] == 4);
    GF_EXPECT (bytes[16] == 1 && bytes[17] == 1 && bytes[18] == 0xF0 && bytes[25] == 0x12);
    RecipeExtras decoded;
    GF_EXPECT (decodeRecipeExtras (bytes, decoded) && decoded == source);
    const RecipeExtras absent;
    GF_EXPECT (isValid (absent) && ! hasRecipeExtras (absent));
    GF_EXPECT (encodeRecipeExtras (absent, bytes) && bytes[1] == 0);
    GF_EXPECT (decodeRecipeExtras (bytes, decoded) && decoded == absent);
}

GF_TEST (RecipeExtras, malformed_unknown_truncated_and_invalid_payloads_reject_atomically)
{
    const auto source = details();
    std::array<std::uint8_t, kRecipeExtrasBytes> valid {};
    GF_EXPECT (encodeRecipeExtras (source, valid));
    for (const auto offset : { 0, 1, 2, 3, 4, 16 })
    {
        auto bytes = valid;
        bytes[(size_t) offset] = 255;
        RecipeExtras destination = source;
        GF_EXPECT (! decodeRecipeExtras (bytes, destination) && destination == source);
    }
    auto bytes = valid;
    bytes[1] = 1; // Tape payload without declared presence
    RecipeExtras destination = source;
    GF_EXPECT (! decodeRecipeExtras (bytes, destination) && destination == source);
    GF_EXPECT (! decodeRecipeExtras (std::span (valid).first (43), destination) && destination == source);
    auto bad = source;
    bad.tape.values[0] = 10001;
    const auto before = bytes;
    GF_EXPECT (! encodeRecipeExtras (bad, bytes) && bytes == before);
}

GF_TEST (RecipeExtras, history_snapshots_and_recipe_commands_keep_metadata_with_the_same_generation)
{
    RecipeHistory history;
    FillRecipe recipe;
    recipe.seed = 42;
    const auto first = details(), second = details (17);
    history.push (recipe, {}, first);
    recipe.seed = 43;
    history.push (recipe, {}, second);
    GF_EXPECT (history.get (0).seed == 43 && history.getExtras (0) == second);
    GF_EXPECT (history.get (1).seed == 42 && history.getExtras (1) == first);
    commands::RecipeTargetSnapshot snapshot;
    snapshot.count = history.size(); snapshot.generation = history.getGeneration();
    for (int i = 0; i < snapshot.count; ++i)
    { snapshot.recipes[(size_t) i] = history.get (i); snapshot.extras[(size_t) i] = history.getExtras (i); }
    commands::RecipeCommandTargets targets;
    targets.publish (snapshot); targets.adopt();
    Command command;
    GF_EXPECT (targets.resolve (CommandType::PreviewRecipe, 1, snapshot.generation, command));
    GF_EXPECT (command.recipe.seed == 42 && command.extras == first && command.index == 0);
    history.clear();
    GF_EXPECT (history.getGeneration() != snapshot.generation && ! hasRecipeExtras (history.getExtras (0)));
    const FillRecipe restored[] { recipe };
    history.restore (restored, 1);
    GF_EXPECT (! hasRecipeExtras (history.getExtras (0))); // old states never inherit stale decisions
}

GF_TEST (RecipeExtras, lock_and_recipe_ab_preserve_the_selected_sound_decisions)
{
    LockState lock;
    FillRecipe recipe;
    const auto extra = details();
    lock.noteStarted (recipe, extra);
    lock.lockLatest();
    GF_EXPECT (lock.extras == extra);
    lock.clear();
    GF_EXPECT (! hasRecipeExtras (lock.extras));
    lock.setExplicit (recipe, extra);
    lock.resetRuntime();
    GF_EXPECT (lock.extras == extra && ! hasRecipeExtras (lock.lastNaturalExtras));
    abcompare::RecipeAB ab;
    recipe.flags = FillRecipe::flagCloud | FillRecipe::flagSwell | FillRecipe::flagFromLock | FillRecipe::flagPreview;
    ab.setSlot (0, recipe, extra);
    ab.setSlot (1, recipe);
    Command command;
    GF_EXPECT (ab.next (command, 0) && command.extras == extra);
    GF_EXPECT (command.recipe.flags == (FillRecipe::flagCloud | FillRecipe::flagSwell));
    GF_EXPECT (ab.next (command, 0) && ! hasRecipeExtras (command.extras));
}

GF_TEST (RecipeExtras, optional_portable_metadata_roundtrips_and_invalid_extras_preserve_the_base)
{
    FillRecipe recipe;
    recipe.seed = 42;
    const auto extra = details();
    const auto text = history::recipeFileText (recipe, "Portable fixture", extra);
    const auto decoded = history::parseRecipeText (text);
    GF_EXPECT (decoded.ok && decoded.recipe.seed == 42 && decoded.extras == extra);
    auto xml = juce::parseXML (text);
    GF_EXPECT (xml != nullptr && xml->getStringAttribute ("extras").length() == 60);
    xml->setAttribute ("extras", "malformed");
    const auto bad = history::parseRecipeText (xml->toString());
    GF_EXPECT (bad.ok && bad.recipe.seed == 42 && ! hasRecipeExtras (bad.extras));
    const auto legacy = history::recipeFileText (recipe, "Portable fixture");
    GF_EXPECT (! juce::parseXML (legacy)->hasAttribute ("extras"));
    GF_EXPECT (history::parseRecipeText (legacy).ok && ! hasRecipeExtras (history::parseRecipeText (legacy).extras));
    RecipeExtras untouched = extra;
    GF_EXPECT (! recipecodec::decodeExtras (juce::String::repeatedString ("A", 6000), untouched) && untouched == extra);
}

GF_TEST (RecipeExtras, escaped_history_and_lock_xml_stay_within_the_declared_bounded_caps)
{
    FillRecipe recipe;
    const auto base = recipecodec::encode (recipe);
    const auto extra = recipecodec::encodeExtras (details());
    GF_EXPECT (base.length() == 24 && extra.length() == 60);
    juce::ValueTree history ("History");
    size_t writerSize = 0;
    for (int i = 0; i < 16; ++i)
    {
        juce::ValueTree row ("R");
        row.setProperty ("b", base, nullptr);
        row.setProperty ("x", extra, nullptr);
        history.appendChild (row, nullptr);
        if (i == 7)
            writerSize = history.toXmlString().getNumBytesAsUTF8();
    }
    GF_EXPECT (writerSize <= 1024 && history.toXmlString().getNumBytesAsUTF8() <= 2048);
    juce::ValueTree lock ("Lock");
    lock.setProperty ("v", 1, nullptr);
    lock.setProperty ("valid", 1, nullptr);
    lock.setProperty ("recipe", base, nullptr);
    lock.setProperty ("extras", extra, nullptr);
    GF_EXPECT (lock.toXmlString().getNumBytesAsUTF8() <= 1024);
    std::cout << "  XML bytes: History8=" << writerSize << ", History16="
              << history.toXmlString().getNumBytesAsUTF8() << ", Lock="
              << lock.toXmlString().getNumBytesAsUTF8() << '\n';
}
