// U-1 Fill History and recipes (docs/dev/05-SPEC-USABILITY-VALUE.md U-1).

#include "../support/RecipeRigs.h"

#include "PluginEditor.h"
#include "features/history/Describe.h"
#include "features/history/RecipeCodec.h"
#include "features/history/RecipeFile.h"

using namespace gapfill;
using namespace gftest;
using namespace recipetest;

namespace
{
    FillRecipe recipeWith (int algo, int band = -1, int lengthMs = 180, int division = 0xFF, std::uint32_t seed = 7)
    {
        FillRecipe r;
        r.algo = (std::uint8_t) algo;
        r.band = (std::int8_t) band;
        r.lengthMs = (std::uint16_t) lengthMs;
        r.lengthClass = 1;
        r.division = (std::uint8_t) division;
        r.seed = seed;
        return r;
    }

    /** Runs a processor over a drum loop until it has started fills. */
    void playFills (ProcRun& run, int bars = 4)
    {
        const auto input = drumLoop (kSr, 150.0, bars);

        for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
            run.step (input);

        run.proc.drainFillEvents();
    }
}

GF_TEST (History, history_rolls_after_eight)
{
    RecipeHistory h;
    const auto g0 = h.getGeneration();

    for (int i = 0; i < 11; ++i)
        h.push (recipeWith (i % numAlgos, -1, 100 + i, 0xFF, (std::uint32_t) i));

    GF_EXPECT (h.size() == 8);
    GF_EXPECT (h.get (0).seed == 10 && h.get (7).seed == 3);
    GF_EXPECT_MSG (h.getGeneration() != g0, "the generation counter moves whenever the contents do");
}

GF_TEST (History, history_order_newest_first)
{
    RecipeHistory h;
    h.push (recipeWith (1, -1, 100, 0xFF, 1), { 1000, 4800 });
    h.push (recipeWith (2, -1, 100, 0xFF, 2), { 9000, 4800 });

    GF_EXPECT (h.get (0).seed == 2 && h.get (1).seed == 1);
    GF_EXPECT (h.getSpan (0).startPos == 9000 && h.getSpan (1).startPos == 1000);
    GF_EXPECT (h.getSpan (0).isKnown());

    h.restore (&h.get (0), 0);
    GF_EXPECT (h.size() == 0);
}

GF_TEST (History, history_excludes_previews)
{
    ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });
    const auto input = drumLoop (kSr, 150.0, 4);

    for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
    {
        if (i % 25 == 20)
            run.post (CommandType::Preview);

        run.step (input);
    }

    run.proc.drainFillEvents();
    const auto& h = run.proc.getRecipeHistory();
    GF_EXPECT (h.size() > 0);
    GF_EXPECT (h.size() == juce::jmin (8, run.proc.getFillEngine().getFillsStarted()));

    for (int i = 0; i < h.size(); ++i)
        GF_EXPECT ((h.get (i).flags & FillRecipe::flagPreview) == 0);

    // Natural fills carry their span for export.
    GF_EXPECT (h.getSpan (0).isKnown() && h.getSpan (0).lengthSamples > 0);
}

GF_TEST (History, history_survives_editor_recreate)
{
    ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });
    playFills (run);
    const auto before = run.proc.getRecipeHistory().size();
    GF_EXPECT (before > 0);
    const auto first = run.proc.getRecipeHistory().get (0);

    for (int round = 0; round < 3; ++round)
    {
        GapFillAudioProcessorEditor editor (run.proc);
        editor.setSize (1100, 700);
        GF_EXPECT (run.proc.getRecipeHistory().size() == before);
    }

    GF_EXPECT (std::memcmp (&first, &run.proc.getRecipeHistory().get (0), sizeof (FillRecipe)) == 0);
}

GF_TEST (History, history_roundtrip_state)
{
    ProcRun a ({ { "fill", 90.0f }, { "density", 90.0f } });
    playFills (a);
    const auto n = a.proc.getRecipeHistory().size();
    GF_EXPECT (n > 1);

    juce::MemoryBlock blob;
    a.proc.getStateInformation (blob);

    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr && xml->getChildByName ("History") != nullptr);
    GF_EXPECT_MSG (xml->getChildByName ("History")->createDocument ({}).length() <= 1024, "within the 1 KiB cap");

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getRecipeHistory().size() == n);

    for (int i = 0; i < n; ++i)
    {
        const auto x = a.proc.getRecipeHistory().get (i);
        const auto y = b.getRecipeHistory().get (i);
        GF_EXPECT (std::memcmp (&x, &y, sizeof (FillRecipe)) == 0);
        GF_EXPECT_MSG (! b.getRecipeHistory().getSpan (i).isKnown(), "positions of an earlier session are not restored");
    }
}

GF_TEST (History, history_bad_entry_dropped)
{
    ProcRun a ({ { "fill", 90.0f }, { "density", 90.0f } });
    playFills (a);
    const auto n = a.proc.getRecipeHistory().size();
    GF_EXPECT (n > 2);

    juce::MemoryBlock blob;
    a.proc.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr);

    auto* hist = xml->getChildByName ("History");
    GF_EXPECT (hist != nullptr);
    hist->getChildElement (1)->setAttribute ("b", "AAAAAAAAAAAAAAAAAAAAAAAA");        // wrong checksum

    juce::MemoryBlock corrupted;
    juce::AudioProcessor::copyXmlToBinary (*xml, corrupted);

    GapFillAudioProcessor b;
    b.setStateInformation (corrupted.getData(), (int) corrupted.getSize());
    GF_EXPECT_MSG (b.getRecipeHistory().size() == n - 1, "only the bad entry is dropped");

    const auto first = a.proc.getRecipeHistory().get (0);
    const auto restored = b.getRecipeHistory().get (0);
    GF_EXPECT (std::memcmp (&first, &restored, sizeof (FillRecipe)) == 0);
}

GF_TEST (History, audition_recipe_plays_selected)
{
    EngineRig rig;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    auto wanted = recipeWith ((int) Algo::Stutter, -1, 300);
    wanted.modifierMask = 0x04;                  // Distort only
    rig.engine.requestPreviewRecipe (wanted, 100);
    rig.step (input, 40);

    GF_EXPECT (rig.engine.isPreviewActive());
    GF_EXPECT (previewAlgoOf (rig.engine.getPreviewStatusCode()) == (int) Algo::Stutter);

    const auto events = drainEvents (rig.events);
    GF_EXPECT (events.size() == 1 && events[0].type == FillEventType::Preview);
    GF_EXPECT (events[0].recipe.algo == (std::uint8_t) Algo::Stutter);
    GF_EXPECT (events[0].recipe.modifierMask == 0x04);
    GF_EXPECT ((events[0].recipe.flags & FillRecipe::flagPreview) != 0);
    GF_EXPECT (events[0].recipe.flags == FillRecipe::flagPreview);
    GF_EXPECT_NEAR (events[0].lengthSamples, 0.3 * kSr, 2.0);

    // The replay never touches the gap state or the history.
    GF_EXPECT (rig.engine.getFillsStarted() == 0);

    // An invalid recipe is refused, with the "couldn't build" status.
    auto bad = wanted;
    bad.algo = 99;
    rig.engine.requestPreviewRecipe (bad, 100);
    rig.step (input, 41);
    GF_EXPECT (previewStatusOf (rig.engine.getPreviewStatusCode()) == PreviewStatus::RejectedBuild);
}

GF_TEST (History, lock_this_sets_selected_recipe)
{
    ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });
    playFills (run);
    GF_EXPECT (run.proc.getRecipeHistory().size() > 2);

    // What the strip does for "Lock this" on row 2: LockRecipe first, then engage the parameter.
    const auto selected = run.proc.getRecipeHistory().get (2);
    Command c;
    c.type = CommandType::LockRecipe;
    c.recipe = selected;
    GF_EXPECT (run.proc.postCommand (c));
    setParams (run.proc, { { "fillLock", 1.0f } });

    const auto quiet = steadyNoise();
    run.blockIndex = 0;
    run.step (quiet);

    const auto& lock = run.proc.getFillEngine().getLockState();
    GF_EXPECT (lock.valid && lock.explicitSet && lock.engaged);
    GF_EXPECT (lock.recipe.algo == selected.algo && lock.recipe.band == selected.band
               && lock.recipe.lengthMs == selected.lengthMs && lock.recipe.modifierMask == selected.modifierMask);

    run.proc.drainFillEvents();
    GF_EXPECT (run.proc.getRecipes().lockMirror.valid
               && run.proc.getRecipes().lockMirror.recipe.algo == selected.algo);
}

GF_TEST (History, recipe_file_roundtrip)
{
    auto r = recipeWith ((int) Algo::PitchRise, 2, 420, 4, 99);
    r.pitchSemis = 7;
    r.modifierMask = 0x0A;

    const auto text = history::recipeFileText (r, "My: fill/1");
    GF_EXPECT (text.getNumBytesAsUTF8() <= 1024);

    const auto parsed = history::parseRecipeText (text);
    GF_EXPECT_MSG (parsed.ok, parsed.error);
    GF_EXPECT (std::memcmp (&r, &parsed.recipe, sizeof (FillRecipe)) == 0);
    GF_EXPECT_MSG (parsed.name == "My_ fill_1", "path separators and reserved characters are replaced: " + parsed.name);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill-recipe-test");
    dir.deleteRecursively();
    const auto file = dir.getChildFile ("sub").getChildFile ("a.gfrecipe");

    juce::String error;
    GF_EXPECT (history::writeRecipeFile (file, r, "name", error));
    GF_EXPECT (file.existsAsFile() && file.getSize() <= 1024);

    const auto back = history::readRecipeFile (file);
    GF_EXPECT (back.ok && std::memcmp (&r, &back.recipe, sizeof (FillRecipe)) == 0);
    dir.deleteRecursively();

    GF_EXPECT (! history::writeRecipeFile (dir.getChildFile ("x.gfrecipe"), recipeWith (99), "bad", error));
    GF_EXPECT (error.isNotEmpty());
}

GF_TEST (History, recipe_file_rejects_oversize_malformed_out_of_range)
{
    const auto good = history::recipeFileText (recipeWith (1), "x");
    GF_EXPECT (history::parseRecipeText (good).ok);

    // oversize
    const auto big = history::parseRecipeText (good + juce::String::repeatedString (" ", 5000));
    GF_EXPECT (! big.ok && big.error.isNotEmpty());

    // malformed / wrong root / missing attribute / bad base64 / bad checksum
    GF_EXPECT (! history::parseRecipeText ("<GFRECIPE").ok);
    GF_EXPECT (! history::parseRecipeText ("not xml at all").ok);
    GF_EXPECT (! history::parseRecipeText ("<OTHER recipe=\"x\"/>").ok);
    GF_EXPECT (! history::parseRecipeText ("<GFRECIPE name=\"n\"/>").ok);
    GF_EXPECT (! history::parseRecipeText ("<GFRECIPE recipe=\"@@@@@@@@@@@@@@@@@@@@@@@@\"/>").ok);
    GF_EXPECT (! history::parseRecipeText ("<GFRECIPE recipe=\"AAAAAAAAAAAAAAAAAAAAAAAA\"/>").ok);
    GF_EXPECT (! history::parseRecipeText ("").ok);

    // out of range fields behind a valid checksum
    FillRecipe bad = recipeWith (1);
    bad.band = 5;
    std::uint8_t bytes[FillRecipe::serialisedBytes];
    toBytes (bad, bytes);
    juce::MemoryOutputStream raw;
    raw.write (bytes, sizeof (bytes));
    raw.writeByte ((char) recipecodec::checksum (bytes));
    const auto text = "<GFRECIPE recipe=\"" + juce::Base64::toBase64 (raw.getData(), raw.getDataSize()) + "\"/>";
    GF_EXPECT (! history::parseRecipeText (text).ok);

    // a missing file and a file over the cap, through the file reader
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill-recipe-test2");
    dir.createDirectory();
    GF_EXPECT (! history::readRecipeFile (dir.getChildFile ("none.gfrecipe")).ok);

    auto huge = dir.getChildFile ("huge.gfrecipe");
    huge.replaceWithText (good + juce::String::repeatedString (" ", 6000));
    const auto result = history::readRecipeFile (huge);
    GF_EXPECT (! result.ok && result.error.contains ("too large"));
    dir.deleteRecursively();
}

GF_TEST (History, describe_recipe_all_algorithms)
{
    for (int a = 0; a < numAlgos; ++a)
    {
        const auto text = history::describeRecipe (recipeWith (a, -1, 180), 120.0);
        GF_EXPECT_MSG (text.startsWith (algoName ((Algo) a)), text);
        GF_EXPECT (text.endsWith ("180 ms"));
        GF_EXPECT (history::familyLetter (a) != '?');
    }

    GF_EXPECT (history::describeRecipe (recipeWith ((int) Algo::Stutter, 2, 180, (int) Division::ThirtySecond), 120.0)
                 == "Stutter 1/32 - High - 180 ms");
    GF_EXPECT (history::describeRecipe (recipeWith ((int) Algo::Delay, 0, 90, (int) Division::EighthD), 90.0)
                 == "Delay 1/8. - Low - 90 ms");
    GF_EXPECT (history::describeRecipe (recipeWith ((int) Algo::Reverse), 0.0) == "Reverse - 180 ms");
    GF_EXPECT (history::familyLetter (99) == '?');
}

GF_TEST (History, recipe_division_matches_the_tempo)
{
    EngineSettings s;
    s.stutterSeconds = 0.125f;
    s.samplesPerBeat = 0.5 * kSr;                                  // 120 bpm
    GF_EXPECT (history::recipeDivision ((int) Algo::Stutter, s, kSr) == (std::uint8_t) Division::Sixteenth);

    s.delaySeconds = 0.5f * 0.75f;                                 // dotted eighth... 0.375 s at 120 bpm
    GF_EXPECT (history::recipeDivision ((int) Algo::Delay, s, kSr) == (std::uint8_t) Division::EighthD);

    s.stutterSeconds = 0.0901f;                                    // matches nothing within 3 %
    GF_EXPECT (history::recipeDivision ((int) Algo::Stutter, s, kSr) == FillRecipe::noDivision);
    GF_EXPECT (history::recipeDivision ((int) Algo::Granular, s, kSr) == FillRecipe::noDivision);
}

GF_TEST (History, history_queue_overflow_counts_drops)
{
    FillEventQueue q;
    FillEvent e;
    e.type = FillEventType::Started;
    const int pushed = FillEventQueue::capacity + 20;

    for (int i = 0; i < pushed; ++i)
    {
        e.pos = i;
        q.push (e);
    }

    GF_EXPECT (q.getDroppedCount() > 0);
    GF_EXPECT_MSG ((int) q.getDroppedCount() + q.getNumReady() == pushed, "every event is either queued or counted as dropped");

    const auto kept = drainEvents (q);
    GF_EXPECT (kept.front().pos == 0);        // the OLDEST are kept, the newest dropped (single producer)
}

/** Manual review aid: GAPFILL_SNAPSHOT_DIR=<dir> writes PNGs of the editor with the recipe drawer closed and open. */
GF_TEST (History, snapshot_for_manual_review_when_requested)
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("GAPFILL_SNAPSHOT_DIR", {});

    if (dir.isEmpty())
        return;

    ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });
    playFills (run);

    for (const auto& size : { juce::Point<int> (1020, 620), juce::Point<int> (1280, 760) })
    {
        GapFillAudioProcessorEditor editor (run.proc);
        editor.setSize (size.x, size.y);

        for (int open = 0; open < 2; ++open)
        {
            if (open == 1)
                for (auto* c : editor.getChildren())
                    if (c->getTitle() == "Fill recipes")
                        for (auto* h : c->getChildren())
                            if (h->getTitle() == "Recipes drawer")
                                dynamic_cast<juce::Button*> (h)->setToggleState (true, juce::sendNotificationSync);

            auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            juce::File out = juce::File (dir).getChildFile ("editor_" + juce::String (size.x) + (open == 1 ? "_open.png" : "_closed.png"));
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat png;
            png.writeImageToStream (image, stream);
        }
    }
}

GF_TEST (History, recipe_drawer_is_hosted_clicks_pass_through_and_survives_reopen)
{
    GapFillAudioProcessor proc;

    for (int round = 0; round < 3; ++round)
    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (1100, 700);

        juce::Component* drawer = nullptr;

        for (auto* c : editor.getChildren())
            if (c->getTitle() == "Fill recipes")
                drawer = c;

        GF_EXPECT_MSG (drawer != nullptr, "the recipe drawer is a direct child of the editor (VisualiserOverlay slot)");

        if (drawer == nullptr)
            return;

        GF_EXPECT (drawer->isVisible() && drawer->getWidth() > 600 && drawer->getHeight() > 60);

        juce::Button* handle = nullptr;

        for (auto* c : drawer->getChildren())
            if (c->getTitle() == "Recipes drawer")
                handle = dynamic_cast<juce::Button*> (c);

        GF_EXPECT (handle != nullptr);

        if (handle == nullptr)
            return;

        // Closed: everything outside the little handle belongs to the visualiser underneath.
        GF_EXPECT (! drawer->hitTest (drawer->getWidth() / 2, 10));
        GF_EXPECT (! drawer->hitTest (drawer->getWidth() / 2, drawer->getHeight() - 30));
        GF_EXPECT (handle->getBounds().getWidth() > 40);

        handle->setToggleState (true, juce::sendNotificationSync);
        GF_EXPECT_MSG (drawer->hitTest (drawer->getWidth() / 2, drawer->getHeight() - 30), "open: the drawer takes its own clicks");
        GF_EXPECT (! drawer->hitTest (drawer->getWidth() / 2, 10));

        // Every visible button has a name for screen readers.
        for (auto* c : drawer->getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (c))
                if (b->isVisible())
                    GF_EXPECT_MSG (b->getTitle().isNotEmpty() || b->getButtonText().isNotEmpty(), "button without a name");
    }
}
