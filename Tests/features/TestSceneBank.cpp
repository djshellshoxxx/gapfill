// N-VAL Scene Bank & Exchange (docs/dev/12 N-VAL).

#include "../support/LiveRig.h"

#include "features/bank/BankFile.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::scenes;
using namespace gapfill::bank;
using namespace gftest;

namespace
{
    Scene scene (const juce::String& name, std::initializer_list<std::pair<const char*, float>> values,
                 Quant q = Quant::Inherit, std::uint16_t scope = kDefaultScope)
    {
        Scene s;
        s.name = name;
        s.quant = q;
        s.scope = scope;

        for (const auto& [id, v] : values)
            s.params.push_back ({ (std::uint16_t) paramIndexOf (id), v });

        return s;
    }

    SceneBank sampleBank()
    {
        initTables();
        SceneBank b;
        b.setName ("Gig 2026-11");
        b.store (0, scene ("Verse", { { "fill", 0.55f }, { "chaos", 0.125f } }, Quant::Bar));
        b.store (1, scene ("Chorus", { { "fill", 0.9f }, { "wStutter", 1.0f } }, Quant::Phrase, ScopeCreative | ScopeWeights));
        b.store (2, scene ("Drop", { { "density", 0.0f } }, Quant::Immediate));
        return b;
    }

    const juce::String kCreated = "2026-10-21T12:00:00Z";

    juce::String wrap (const juce::String& body, const juce::String& attrs = "formatVersion=\"1\" name=\"X\"")
    {
        return "<GapBank " + attrs + ">" + body + "</GapBank>";
    }
}

GF_TEST (SceneBank, digest_is_a_real_sha256_known_answers)
{
    GF_EXPECT (sha256 ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    GF_EXPECT (sha256 ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    GF_EXPECT (sha256 ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
               == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

GF_TEST (SceneBank, file_round_trip_preserves_scenes_names_scopes_and_quantise)
{
    const auto bank = sampleBank();
    const auto text = exportToString (bank, {}, "0.0.1 beta", kCreated);

    const auto report = parseString (text);
    GF_EXPECT (report.status == ParseStatus::Ok);
    GF_EXPECT (! report.digestMismatch && report.digestPresent);
    GF_EXPECT (report.bankName == "Gig 2026-11");
    GF_EXPECT (report.accepted.size() == 3 && report.skipped.empty() && report.unknownIds == 0);

    for (int i = 0; i < 3; ++i)
    {
        const auto& a = report.accepted[(size_t) i];
        const auto& b = bank.at (i);
        GF_EXPECT (a.name == b.name && a.scope == b.scope && a.quant == b.quant && a.params.size() == b.params.size());

        for (size_t p = 0; p < a.params.size(); ++p)
        {
            GF_EXPECT (a.params[p].index == b.params[p].index);
            GF_EXPECT_NEAR (a.params[p].value01, b.params[p].value01, 1e-6);
        }
    }
}

GF_TEST (SceneBank, export_of_a_selection_and_no_paths_or_host_info)
{
    const auto bank = sampleBank();
    ExportOptions o;
    o.selection = { 2, 0 };
    const auto text = exportToString (bank, o, "0.0.1 beta", kCreated);
    const auto r = parseString (text);
    GF_EXPECT (r.accepted.size() == 2 && r.accepted[0].name == "Drop" && r.accepted[1].name == "Verse");

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    GF_EXPECT (! text.contains (home) && ! text.contains ("\\") && ! text.contains ("/home"));
    GF_EXPECT (! text.contains (juce::SystemStats::getComputerName()) || juce::SystemStats::getComputerName().isEmpty());
    GF_EXPECT (text.contains ("formatVersion=\"1\"") && text.contains ("stateVersion=\"3\""));
}

GF_TEST (SceneBank, import_rejects_oversize_malformed_and_newer_versions)
{
    juce::String huge = "<GapBank formatVersion=\"1\">";

    while (huge.length() < 300 * 1024)
        huge << "<Scene name=\"x\"/>";

    huge << "</GapBank>";
    GF_EXPECT (parseString (huge).status == ParseStatus::TooLarge);

    GF_EXPECT (parseString ("").status == ParseStatus::Malformed);
    GF_EXPECT (parseString ("not xml at all <<<").status == ParseStatus::Malformed);
    GF_EXPECT (parseString ("<Other formatVersion=\"1\"/>").status == ParseStatus::Malformed);
    GF_EXPECT (parseString ("<GapBank/>").status == ParseStatus::Malformed);                          // no version
    GF_EXPECT (parseString ("<GapBank formatVersion=\"0\"/>").status == ParseStatus::Malformed);

    const auto newer = parseString (wrap ("", "formatVersion=\"2\""));
    GF_EXPECT (newer.status == ParseStatus::NewerVersion && newer.message.contains ("newer GapFill"));

    GF_EXPECT (parseString (wrap ("")).status == ParseStatus::Empty);
}

GF_TEST (SceneBank, import_skips_a_bad_scene_and_keeps_the_rest)
{
    initTables();
    const auto text = wrap (
        "<Scene name=\"Good\" quant=\"bar\"><P id=\"fill\" v=\"0.5\"/></Scene>"
        "<Scene name=\"BadValue\"><P id=\"fill\" v=\"7\"/></Scene>"
        "<Scene name=\"BadText\"><P id=\"fill\" v=\"abc\"/></Scene>"
        "<Scene name=\"BadQuant\" quant=\"sometime\"/>"
        "<Scene name=\"Never\"><P id=\"dry\" v=\"0.5\"/></Scene>"
        "<Scene name=\"AlsoGood\"><P id=\"chaos\" v=\"0.25\"/></Scene>");

    const auto r = parseString (text);
    GF_EXPECT (r.status == ParseStatus::Ok);
    GF_EXPECT (r.accepted.size() == 2 && r.accepted[0].name == "Good" && r.accepted[1].name == "AlsoGood");
    GF_EXPECT (r.skipped.size() == 4);
}

GF_TEST (SceneBank, never_ids_are_rejected_unknown_ids_are_ignored_and_counted)
{
    initTables();
    for (const char* never : { "dry", "wet", "output", "bypass", "lookahead", "seed", "capture", "audition" })
    {
        const auto r = parseString (wrap ("<Scene name=\"S\"><P id=\"" + juce::String (never) + "\" v=\"0.5\"/></Scene>"));
        GF_EXPECT_MSG (r.status == ParseStatus::Empty && r.skipped.size() == 1, never);
    }

    const auto r = parseString (wrap ("<Scene name=\"S\"><P id=\"fill\" v=\"0.5\"/><P id=\"futureParam\" v=\"0.5\"/>"
                                      "<P id=\"anotherOne\" v=\"1\"/></Scene>"));
    GF_EXPECT (r.status == ParseStatus::Ok && r.unknownIds == 2 && r.accepted[0].params.size() == 1);
}

GF_TEST (SceneBank, more_than_128_parameters_or_64_scenes_are_refused)
{
    initTables();
    juce::String params;

    for (int i = 0; i < 130; ++i)
        params << "<P id=\"fill\" v=\"0.5\"/>";

    GF_EXPECT (parseString (wrap ("<Scene name=\"S\">" + params + "</Scene>")).status == ParseStatus::Empty);

    juce::String many;

    for (int i = 0; i < 70; ++i)
        many << "<Scene name=\"S" << i << "\"><P id=\"fill\" v=\"0.5\"/></Scene>";

    const auto r = parseString (wrap (many));
    GF_EXPECT (r.status == ParseStatus::Ok && r.accepted.size() == 64 && r.skipped.size() == 6);
}

GF_TEST (SceneBank, digest_mismatch_is_reported_as_a_warning_but_the_bank_is_usable)
{
    auto text = exportToString (sampleBank(), {}, "0.0.1 beta", kCreated);
    text = text.replace ("v=\"0.550000\"", "v=\"0.560000\"");
    const auto r = parseString (text);
    GF_EXPECT (r.status == ParseStatus::Ok && r.digestMismatch);
    GF_EXPECT (r.message.contains ("integrity"));
}

GF_TEST (SceneBank, replace_append_and_merge_are_atomic_and_undoable)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().assign (sampleBank());
    const auto before = ctl.getBank().canonicalPayload();

    const auto incoming = parseString (wrap ("<Scene name=\"New\"><P id=\"fill\" v=\"0.1\"/></Scene>"
                                             "<Scene name=\"Verse\"><P id=\"fill\" v=\"0.77\"/></Scene>", "formatVersion=\"1\" name=\"Other\""));
    GF_EXPECT (incoming.status == ParseStatus::Ok);

    // a rejected report never changes anything
    const auto bad = parseString ("garbage");
    GF_EXPECT (! applyImport (ctl, bad, ImportMode::Replace).ok);
    GF_EXPECT (ctl.getBank().canonicalPayload() == before);

    auto res = applyImport (ctl, incoming, ImportMode::MergeByName);
    GF_EXPECT (res.ok && res.added == 1 && res.replaced == 1);
    GF_EXPECT (ctl.getBank().size() == 4);

    GF_EXPECT (ctl.undoImport());
    GF_EXPECT (ctl.getBank().canonicalPayload() == before);
    GF_EXPECT (! ctl.undoImport());                                      // one step only

    res = applyImport (ctl, incoming, ImportMode::Append);
    GF_EXPECT (res.ok && res.added == 2 && ctl.getBank().size() == 5);
    ctl.undoImport();

    res = applyImport (ctl, incoming, ImportMode::Replace);
    GF_EXPECT (res.ok && ctl.getBank().size() == 2 && ctl.getBank().getName() == "Other");
    GF_EXPECT (ctl.undoImport() && ctl.getBank().canonicalPayload() == before);

    // import never changes live parameters
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("fill")), 35.0, 0.01);
}

GF_TEST (SceneBank, append_beyond_64_scenes_drops_the_excess_and_says_so)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    initTables();

    for (int i = 0; i < 62; ++i)
        ctl.getBank().store (i, scene ("S" + juce::String (i), { { "fill", 0.5f } }));

    const auto r = parseString (wrap ("<Scene name=\"a\"/><Scene name=\"b\"/><Scene name=\"c\"/>"));
    const auto res = applyImport (ctl, r, ImportMode::Append);
    GF_EXPECT (res.ok && res.added == 2 && res.dropped == 1 && ctl.getBank().size() == 64);
}

GF_TEST (SceneBank, export_refuses_overwrite_and_writes_atomically)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_bank_test");
    dir.createDirectory();
    const auto file = dir.getChildFile ("test.gapbank");
    file.deleteFile();

    const auto text = exportToString (sampleBank(), {}, "0.0.1 beta", kCreated);
    juce::String error;
    GF_EXPECT (writeFile (file, text, false, error));
    GF_EXPECT (! writeFile (file, "changed", false, error) && error.isNotEmpty());
    GF_EXPECT (file.loadFileAsString().contains ("GapBank"));                // untouched

    GF_EXPECT (writeFile (file, text, true, error));

    const auto r = parseFile (file);
    GF_EXPECT (r.status == ParseStatus::Ok && r.accepted.size() == 3);

    GF_EXPECT (parseFile (dir.getChildFile ("missing.gapbank")).status == ParseStatus::Malformed);

    // an unwritable target reports an error and leaves no temp file behind
    GF_EXPECT (! writeFile (dir.getChildFile ("nope/sub/x.gapbank"), text, true, error));
    file.deleteFile();
    dir.deleteRecursively();
}

GF_TEST (SceneBank, hostile_input_never_crashes_or_changes_state)
{
    GapFillAudioProcessor proc;
    auto& ctl = proc.getLiveControl().scenes();
    ctl.getBank().assign (sampleBank());
    const auto before = ctl.getBank().canonicalPayload();

    const auto good = exportToString (sampleBank(), {}, "0.0.1 beta", kCreated);
    juce::Random rnd (1234);

    for (int round = 0; round < 300; ++round)
    {
        auto text = good;

        // random byte-level damage
        for (int k = 0; k < 1 + round % 6; ++k)
        {
            const auto pos = rnd.nextInt (text.length());
            text = text.replaceSection (pos, 1, juce::String::charToString ((juce::juce_wchar) (32 + rnd.nextInt (95))));
        }

        if (round % 10 == 0)
            text = text.substring (0, rnd.nextInt (text.length()));

        const auto r = parseString (text);

        if (r.status != ParseStatus::Ok)
            GF_EXPECT (! applyImport (ctl, r, ImportMode::Replace).ok);
    }

    GF_EXPECT (ctl.getBank().canonicalPayload() == before);
}

GF_TEST (SceneBank, program_change_maps_to_scene_goto_through_the_processor)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    initTables();

    for (int i = 0; i < 5; ++i)
        live.scenes().getBank().store (i, scene ("S" + juce::String (i), { { "fill", 0.1f * (float) i } }));

    live.scenes().getBank().options.quant = Quant::Bar;
    live.bindings().setProgramChangeScenes (true, 0);

    prepareProcessor (proc, 48000.0, 256, false);
    TestPlayHead frozen;                                                 // playing, but parked at ppq 0.1: nothing reaches the bar
    frozen.ppq = 0.1;
    proc.setPlayHead (&frozen);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    midi.addEvent (juce::MidiMessage::programChange (1, 3), 10);
    proc.processBlock (work, midi);                                      // scanned: queued for the next block
    midi.clear();
    proc.processBlock (work, midi);                                      // executed: UiRequest posted
    proc.flushLatencyReportForTests();
    GF_EXPECT (live.scenes().getPendingIndex() == 3);

    midi.clear();
    midi.addEvent (juce::MidiMessage::programChange (1, 63), 10);       // out of range of a 5-scene bank: ignored
    proc.processBlock (work, midi);
    midi.clear();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (live.scenes().getPendingIndex() == 3);
    proc.setPlayHead (nullptr);
}

GF_TEST (SceneBank, command_bindings_drive_next_prev_and_cancel_through_the_processor)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    initTables();

    for (int i = 0; i < 3; ++i)
        live.scenes().getBank().store (i, scene ("S" + juce::String (i), { { "fill", 0.2f * (float) i } }));

    live.scenes().getBank().options.quant = Quant::Bar;

    auto bind = [&] (CommandType c, int cc)
    {
        commands::Binding b;
        b.cmd = c;
        b.number = (std::uint8_t) cc;
        live.bindings().add (b);
    };

    bind (CommandType::SceneNext, 10);
    bind (CommandType::ScenePrev, 11);
    bind (CommandType::SceneCancel, 12);

    prepareProcessor (proc, 48000.0, 256, false);
    TestPlayHead frozen;
    frozen.ppq = 0.1;
    proc.setPlayHead (&frozen);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;

    auto press = [&] (int cc)
    {
        work.clear();
        midi.clear();
        midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, 127), 0);
        proc.processBlock (work, midi);
        midi.clear();
        midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, 0), 0);        // re-arm
        proc.processBlock (work, midi);
        proc.flushLatencyReportForTests();
    };

    press (10);
    GF_EXPECT (live.scenes().getPendingIndex() == 0);
    press (10);
    press (10);
    GF_EXPECT (live.scenes().getPendingIndex() == 2);
    press (11);
    GF_EXPECT (live.scenes().getPendingIndex() == 1);
    press (12);
    GF_EXPECT (live.scenes().getPendingIndex() == -1);
    proc.setPlayHead (nullptr);
}
