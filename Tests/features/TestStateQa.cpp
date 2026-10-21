// State / preset compatibility QA (docs/audits/2026-10-21-qa-report.md §3).
//
//   * every checked-in state fixture loads, parameters stay inside their ranges and the result renders finite audio,
//   * a fully populated session (every parameter moved, scenes, macros, bindings, history) round-trips
//     save -> load -> save byte-identically, and stays identical on repeated cycles,
//   * random garbage, truncations, byte flips and hostile numbers never crash and never leave a parameter
//     out of range or non-finite.

#include "../support/TestRig.h"

#include <cmath>
#include <iostream>
#include <random>

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::String stateText (const juce::MemoryBlock& b)
    {
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (b.getData(), (int) b.getSize()))
            return xml->toString (juce::XmlElement::TextFormat().singleLine());

        return {};
    }

    juce::MemoryBlock saveState (GapFillAudioProcessor& p)
    {
        juce::MemoryBlock b;
        p.getStateInformation (b);
        return b;
    }

    void loadState (GapFillAudioProcessor& p, const juce::MemoryBlock& b)
    {
        p.setStateInformation (b.getData(), (int) b.getSize());
    }

    /** First difference between two saved states, as readable context ("" when identical). */
    juce::String firstDifference (const juce::MemoryBlock& a, const juce::MemoryBlock& b)
    {
        if (a == b)
            return {};

        const auto ta = stateText (a), tb = stateText (b);
        int i = 0;

        while (i < ta.length() && i < tb.length() && ta[i] == tb[i])
            ++i;

        return "first difference at char " + juce::String (i) + ": [" + ta.substring (juce::jmax (0, i - 70), i + 90) + "] vs ["
             + tb.substring (juce::jmax (0, i - 70), i + 90) + "]";
    }

    /** Every parameter finite and inside its range; the processor still renders finite audio. */
    void expectSane (GapFillAudioProcessor& p, const juce::String& what)
    {
        for (auto* par : p.getParameters())
        {
            const auto v = par->getValue();
            GF_EXPECT_MSG (std::isfinite (v) && v >= 0.0f && v <= 1.0f,
                           what + ": parameter " + par->getName (64) + " normalised value " + juce::String (v));
        }

        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer midi;

        for (int i = 0; i < 4; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 256; ++s)
                    b.setSample (ch, s, 0.5f * std::sin (0.05f * (float) (s + 256 * i)));

            p.processBlock (b, midi);
            GF_EXPECT_MSG (allFinite (b), what + ": non-finite output after load");
        }
    }

    /** Moves everything that has state: all parameters, scenes, history, MIDI map. */
    void populate (GapFillAudioProcessor& p, unsigned seed)
    {
        std::mt19937 g (seed);
        auto u = [&] { return (float) (g() & 0xFFFF) / 65535.0f; };

        for (auto* par : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (par))
                if (withId->paramID != "bypass")
                    par->setValueNotifyingHost (u());

        p.getMidiLearnManager().setMapping ("wet", 74);
        p.getMidiLearnManager().setMapping ("fill", 21);

        auto& scenes = p.getLiveControl().scenes();
        scenes.saveCurrent (0, "Verse");
        p.randomise (true);
        scenes.saveCurrent (1, "Drop \"quoted\" & <tag>");
        p.surprise();

        // history / recipe state: render real audio so fills happen
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (p, drumLoop (48000.0, 150.0, 2), 512, 48000.0, o);
        p.drainFillEvents();
    }

    juce::MemoryBlock wrapText (const juce::String& xmlText)
    {
        juce::MemoryBlock out;
        const auto utf8 = xmlText.toRawUTF8();
        const auto len = (juce::uint32) std::strlen (utf8);
        const juce::uint32 magic = juce::ByteOrder::swapIfBigEndian ((juce::uint32) 0x21324356);
        const juce::uint32 size = juce::ByteOrder::swapIfBigEndian (len);
        out.append (&magic, 4);
        out.append (&size, 4);
        out.append (utf8, (size_t) len + 1);
        return out;
    }

}

GF_TEST (StateQa, every_fixture_loads_with_sane_parameters_and_renders_finite_audio)
{
    const auto dir = testDataDir().getChildFile ("fixtures");
    const auto files = dir.findChildFiles (juce::File::findFiles, false, "state_*.bin");
    GF_EXPECT (files.size() >= 20);

    for (const auto& f : files)
    {
        juce::MemoryBlock b;
        GF_EXPECT (f.loadFileAsData (b));

        GapFillAudioProcessor p;
        prepareProcessor (p, 48000.0, 256, false);
        loadState (p, b);
        expectSane (p, f.getFileName());

        // loading a fixture never makes the next save unstable
        const auto s1 = saveState (p);
        GapFillAudioProcessor q;
        prepareProcessor (q, 48000.0, 256, false);
        loadState (q, s1);
        const auto s2 = saveState (q);
        GF_EXPECT_MSG (s1 == s2, "fixture " + f.getFileName() + " does not save-load-save to identical bytes; " + firstDifference (s1, s2));
    }
}

GF_TEST (StateQa, populated_session_round_trip_is_byte_identical_and_stable)
{
    GapFillAudioProcessor a;
    prepareProcessor (a, 48000.0, 512, false);
    populate (a, 11);

    const auto s1 = saveState (a);
    GF_EXPECT (s1.getSize() > 1000);

    GapFillAudioProcessor b;
    prepareProcessor (b, 48000.0, 512, false);
    loadState (b, s1);
    const auto s2 = saveState (b);
    GF_EXPECT_MSG (s1 == s2, "save -> load -> save differs; " + firstDifference (s1, s2));

    // again, into a processor that already holds other state, and onto itself
    GapFillAudioProcessor c;
    prepareProcessor (c, 48000.0, 512, false);
    populate (c, 99);
    loadState (c, s2);
    GF_EXPECT_MSG (saveState (c) == s1, "loading into a populated processor leaves residue; " + firstDifference (s1, saveState (c)));
    loadState (c, s1);
    loadState (c, s1);
    GF_EXPECT (saveState (c) == s1);
    expectSane (c, "populated");

    // state saved after processing audio equals the state saved before it (audio never edits the state)
    const auto before = saveState (a);
    renderOffline (a, drumLoop (48000.0, 150.0, 1), 300, 48000.0);
    const auto after = saveState (a);
    GF_EXPECT_MSG (before == after, "processing audio changed the saved state");
}

GF_TEST (StateQa, garbage_truncated_and_flipped_blobs_never_crash_or_corrupt_parameters)
{
    GapFillAudioProcessor src;
    prepareProcessor (src, 48000.0, 512, false);
    populate (src, 3);
    const auto good = saveState (src);
    const auto text = stateText (good);
    GF_EXPECT (text.length() > 1000);

    GapFillAudioProcessor p;
    prepareProcessor (p, 48000.0, 256, false);
    std::mt19937 g (2026);
    auto below = [&] (int n) { return n <= 1 ? 0 : (int) (g() % (unsigned) n); };

    int rejected = 0, accepted = 0;
    auto feed = [&] (const juce::MemoryBlock& blob, const juce::String& what)
    {
        const auto before = saveState (p);
        loadState (p, blob);
        expectSane (p, what);
        const auto after = saveState (p);
        (before == after ? rejected : accepted)++;
    };

    // 1. random bytes of random length, half of them with a valid binary-XML header
    for (int i = 0; i < 150; ++i)
    {
        juce::MemoryBlock blob;
        const int len = below(4096);

        for (int k = 0; k < len; ++k)
        {
            const auto byte = (juce::uint8) (g() & 0xFF);
            blob.append (&byte, 1);
        }

        if (i % 2 == 0 && len > 8)
        {
            const juce::uint32 magic = 0x21324356, size = (juce::uint32) (len - 8);
            blob.copyFrom (&magic, 0, 4);
            blob.copyFrom (&size, 4, 4);
        }

        feed (blob, "random blob #" + juce::String (i));
    }

    // 2. every prefix at ~100 cut points of a real state (text-level truncation)
    for (int i = 1; i <= 100; ++i)
        feed (wrapText (text.substring (0, text.length() * i / 101)), "truncated " + juce::String (i) + "%");

    // 3. raw binary truncation
    for (int i = 0; i < 60; ++i)
    {
        juce::MemoryBlock cut (good.getData(), (size_t) below ((int) good.getSize()));
        feed (cut, "binary cut #" + juce::String (i));
    }

    // 4. byte flips / deletions / hostile numbers in the XML text
    const char* hostile[] = { "nan", "inf", "-inf", "1e999", "-1e999", "99999999999999999999", "-7", "0x7fffffff", "", "NaN" };

    for (int i = 0; i < 300; ++i)
    {
        auto t = text;
        const auto at = below (t.length());

        switch (i % 4)
        {
            case 0: t = t.replaceSection (at, 1, juce::String::charToString ((juce::juce_wchar) (32 + below (95)))); break;
            case 1: t = t.replaceSection (at, juce::jmin (below (40), t.length() - at), {}); break;
            case 2:
            {
                // replace a quoted number with a hostile one
                const auto q = t.indexOfChar (at, '"');

                if (q >= 0)
                {
                    const auto e = t.indexOfChar (q + 1, '"');

                    if (e > q)
                        t = t.replaceSection (q + 1, e - q - 1, hostile[below (10)]);
                }
                break;
            }
            default: t = t.replaceSection (at, 0, t.substring (below (t.length()), juce::jmin (t.length(), below (t.length()) + 200))); break;
        }

        feed (wrapText (t), "text mutation #" + juce::String (i));
    }

    // 5. a valid state still loads after all of that
    loadState (p, good);
    GF_EXPECT_MSG (saveState (p) == good, "a good state no longer restores exactly after garbage");
    std::cout << "  garbage corpus: " << accepted << " accepted partially, " << rejected << " rejected unchanged" << std::endl;
}

GF_TEST (StateQa, oversized_subtree_text_and_deep_nesting_are_bounded)
{
    GapFillAudioProcessor src;
    prepareProcessor (src, 48000.0, 512, false);
    populate (src, 5);
    auto text = stateText (saveState (src));

    // a 3 MB attribute, deep nesting, thousands of siblings
    GapFillAudioProcessor p;
    prepareProcessor (p, 48000.0, 256, false);

    {
        auto big = text.replaceFirstOccurrenceOf ("</GAPFILL_STATE>", "<Scenes bank=\"" + juce::String::repeatedString ("x", 3 * 1024 * 1024) + "\"/></GAPFILL_STATE>");
        loadState (p, wrapText (big));
        expectSane (p, "3 MB attribute");
    }

    {
        juce::String nest;
        const int depth = 2000;

        for (int i = 0; i < depth; ++i)
            nest << "<L>";

        for (int i = 0; i < depth; ++i)
            nest << "</L>";

        loadState (p, wrapText (text.replaceFirstOccurrenceOf ("</GAPFILL_STATE>", nest + "</GAPFILL_STATE>")));
        expectSane (p, "deep nesting");
    }

    {
        juce::String many;

        for (int i = 0; i < 20000; ++i)
            many << "<PARAM id=\"p" << i << "\" value=\"1\"/>";

        loadState (p, wrapText (text.replaceFirstOccurrenceOf ("</GAPFILL_STATE>", many + "</GAPFILL_STATE>")));
        expectSane (p, "20000 siblings");
    }
}

GF_TEST (StateQa, beta1_parameter_ids_and_host_indices_are_unchanged_and_every_locked_id_exists)
{
    // The 66 parameters of 0.0.1 beta in host-index order (from the beta release). Hosts store automation by ID and
    // some by index: neither may move.
    static const char* const beta1[] = {
        "detMode", "detSource", "sensitivity", "gapThresh", "minGap", "maxFill", "maxFillSync", "maxFillDiv", "gapRelease",
        "protectHold", "retrigGuard", "lookahead", "audition", "fill", "intensity", "complexity", "chaos", "density",
        "fillType", "seed", "syncEnable", "stutterDiv", "delayDiv", "pingPong", "wetMono", "width", "wReverse", "wStutter",
        "wDelay", "wGranular", "wFreeze", "wPitchRise", "wPitchFall", "wFilter", "wTexture", "wCrush", "wDistort",
        "wScramble", "wSpectral", "wComb", "wTapeStop", "protTrans", "protLow", "protCenter", "protNext", "protSC",
        "xoverLM", "xoverMH", "bandLowOn", "bandMidOn", "bandHighOn", "bandLowAmt", "bandMidAmt", "bandHighAmt",
        "bandLowInt", "bandMidInt", "bandHighInt", "bandLowProb", "bandMidProb", "bandHighProb", "bandSolo", "dry", "wet",
        "output", "bypass", "capture" };

    GapFillAudioProcessor p;
    const auto& params = p.getParameters();
    GF_EXPECT (params.size() > 66);

    for (int i = 0; i < 66; ++i)
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
        GF_EXPECT_MSG (withId != nullptr && withId->paramID == beta1[i],
                       "host index " + juce::String (i) + " is no longer " + beta1[i]);
    }

    // every id written in tools/param_ids/*.lock still exists; no duplicates
    juce::StringArray runtime;

    for (auto* par : params)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (par))
            runtime.add (withId->paramID);

    juce::StringArray dedupe (runtime);
    dedupe.removeDuplicates (false);
    GF_EXPECT_MSG (dedupe.size() == runtime.size(), "duplicate parameter IDs");

    const auto lockDir = testDataDir().getParentDirectory().getChildFile ("tools").getChildFile ("param_ids");
    int locked = 0;

    for (const auto& f : lockDir.findChildFiles (juce::File::findFiles, false, "*.lock"))
        for (auto line : juce::StringArray::fromLines (f.loadFileAsString()))
        {
            line = line.trim();

            if (line.isEmpty() || line.startsWithChar ('#'))
                continue;

            ++locked;
            GF_EXPECT_MSG (runtime.contains (line), f.getFileName() + ": locked parameter ID no longer exists: " + line);
        }

    GF_EXPECT_MSG (locked >= 200, "lock files were not found / read (" + juce::String (locked) + " ids)");
}

GF_TEST (StateQa, a_restored_groove_and_a_pending_reset_are_what_the_next_save_writes)
{
    // set-state followed directly by get-state (what host state tests do, with no audio in between) must be
    // reproducible even for state the audio thread adopts asynchronously.
    GapFillAudioProcessor a;
    prepareProcessor (a, 48000.0, 512, false);
    setParams (a, { { "fl09_hold", 1.0f } });
    groove::GrooveSnapshot snap;

    for (int i = 0; i < groove::kSlots; ++i)
        snap.offsetSteps[i] = 0.02f * (float) (i % 3);

    snap.confidence = 0.62f;
    snap.valid = true;
    a.getContextState().grooveState().requestLoad (snap, false);
    renderOffline (a, drumLoop (48000.0, 150.0, 1), 512, 48000.0);          // the audio thread adopts and publishes it
    const auto withGroove = saveState (a);
    GF_EXPECT_MSG (stateText (withGroove).contains ("<Groove"), "a held groove must be saved");

    GapFillAudioProcessor b;
    prepareProcessor (b, 48000.0, 512, false);
    loadState (b, withGroove);
    GF_EXPECT_MSG (saveState (b) == withGroove, "restored groove not saved back before audio ran; " + firstDifference (withGroove, saveState (b)));

    // a state without a Groove subtree resets it, and the very next save says so
    GapFillAudioProcessor fresh;
    prepareProcessor (fresh, 48000.0, 512, false);
    setParams (fresh, { { "fl09_hold", 1.0f } });
    const auto noGroove = saveState (fresh);
    loadState (a, noGroove);
    GF_EXPECT_MSG (! stateText (saveState (a)).contains ("<Groove"), "a restore that carries no groove must clear the previous one immediately");
}
