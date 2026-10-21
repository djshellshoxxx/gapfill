// Completeness audit of 2026-10-21 (docs/audits/2026-10-21-completeness-audit.md): regression tests for the wiring gaps
// that audit fixed. Each test names the gap it closes.

#include "../support/TestRig.h"

#include <set>

#include "PluginEditor.h"
#include "features/context/ContextState.h"
#include "features/fx/FxHooks.h"
#include "features/fx/FxSettings.h"
#include "features/health/Health.h"
#include "features/midiout/MidiOutState.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/tension/Tension.h"
#include "gui/SlotHost.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    void collect (juce::Component& root, std::vector<juce::Component*>& out)
    {
        for (auto* c : root.getChildren())
        {
            out.push_back (c);
            collect (*c, out);
        }
    }

    template <typename T>
    T* findByText (juce::Component& root, const juce::String& text)
    {
        std::vector<juce::Component*> all;
        collect (root, all);

        for (auto* c : all)
            if (auto* b = dynamic_cast<T*> (c))
                if (b->getButtonText() == text)
                    return b;

        return nullptr;
    }

    std::unique_ptr<juce::Component> makeSlotComponent (Slot slot, int order, GapFillAudioProcessor& proc)
    {
        for (const auto& f : getSlotFactories (slot))
            if (f.order == order)
                return f.make (proc);

        return nullptr;
    }

    float rawParam (GapFillAudioProcessor& p, const char* id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    void setNormalised (GapFillAudioProcessor& p, const char* id, float v01)
    {
        p.getValueTreeState().getParameter (id)->setValueNotifyingHost (v01);
    }
}

//==============================================================================
// X-1: commands 24 / 25 had a name in the command map but no handler.
GF_TEST (Audit, spectral_capture_and_release_commands_have_a_handler)
{
    GapFillAudioProcessor proc;
    Command capture, release, other;
    capture.type = CommandType::SpectralCapture;
    release.type = CommandType::SpectralRelease;
    other.type = CommandType::Preview;

    GF_EXPECT (proc.getFxChain().handleCommand (capture));
    GF_EXPECT (proc.getFxChain().handleCommand (release));
    GF_EXPECT (! proc.getFxChain().handleCommand (other));

    // And through the real command path: posted from the message thread, drained on the audio thread.
    proc.postCommand (capture);
    const auto in = drumLoop (48000.0, 120.0, 1);
    const auto out = renderOffline (proc, in, 512, 48000.0);
    GF_EXPECT (out.out.getNumSamples() == in.getNumSamples());
}

//==============================================================================
// F-L11: the Options value midiOutOffsetMs had a setter but no persistence.
GF_TEST (Audit, midi_out_offset_is_saved_with_the_project)
{
    GapFillAudioProcessor a;
    GF_EXPECT (! midiout::serialisePerf (a).isValid());                    // default writes nothing

    a.setMidiOutOffsetMs (37.0);
    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT_NEAR (b.getMidiOutOffsetMs(), 37.0, 1.0e-9);

    juce::ValueTree bad ("Perf");
    bad.setProperty ("midiOutOffsetMs", 500, nullptr);
    GF_EXPECT (! midiout::validatePerf (bad));
    bad.setProperty ("midiOutOffsetMs", "abc", nullptr);
    GF_EXPECT (! midiout::validatePerf (bad));
    bad.setProperty ("midiOutOffsetMs", -100, nullptr);
    GF_EXPECT (midiout::validatePerf (bad));
    GF_EXPECT (midiout::validatePerf (juce::ValueTree ("Perf")));          // absent = default

    midiout::resetPerf (b);
    GF_EXPECT (b.getMidiOutOffsetMs() == 0.0);
}

//==============================================================================
// F-M17: presets, Reset, Undo and file load must keep the wet-only switch; a DAW session restores it.
GF_TEST (Audit, wet_only_is_preserved_by_presets_reset_undo_and_file_load)
{
    GapFillAudioProcessor p;
    GF_EXPECT (routingParameterIDs().contains ("wetSolo"));
    setNormalised (p, "wetSolo", 1.0f);

    p.selectPreset (3);
    GF_EXPECT (rawParam (p, "wetSolo") > 0.5f);

    p.resetAllParameters();
    GF_EXPECT (rawParam (p, "wetSolo") > 0.5f);

    // Undo: the snapshot was taken with wet-only OFF, the current state is ON.
    GapFillAudioProcessor u;
    u.selectPreset (2);
    setNormalised (u, "wetSolo", 1.0f);
    GF_EXPECT (u.undo());
    GF_EXPECT (rawParam (u, "wetSolo") > 0.5f);

    // File load: a preset file written with wet-only OFF does not switch it off.
    GapFillAudioProcessor f;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("gapfill-audit-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".gapfill");
    juce::String error;
    GF_EXPECT (f.saveParameterStateToFile (file, error));
    setNormalised (f, "wetSolo", 1.0f);
    GF_EXPECT (f.loadParameterStateFromFile (file, error));
    GF_EXPECT (rawParam (f, "wetSolo") > 0.5f);
    file.deleteFile();

    // A DAW session restore still applies the stored value.
    juce::MemoryBlock block;
    p.getStateInformation (block);
    GapFillAudioProcessor s;
    s.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT (rawParam (s, "wetSolo") > 0.5f);
}

//==============================================================================
// F-M21: the FX-send lane was published but no effect card read it.
GF_TEST (Audit, effect_send_scales_every_amount_and_is_identity_at_one)
{
    fx::FxSettings f;
    f.swell.amount01 = 0.8f;    f.cloud.amount01 = 0.6f;   f.ratchet.amount01 = 0.4f;
    f.rcrush.amount01 = 0.5f;   f.tape.amount01 = 0.2f;    f.resonator.amount01 = 0.9f;
    f.fsdelay.amount01 = 0.3f;  f.dub.amount01 = 0.7f;     f.formant.amount01 = 0.1f;
    f.spfreeze.amount01 = 1.0f; f.convo.amount01 = 0.5f;

    const auto before = f;
    fx::applyEffectSend (f, 1.0f);
    GF_EXPECT (std::memcmp (&before, &f, sizeof (f)) == 0);

    fx::applyEffectSend (f, 0.5f);
    GF_EXPECT_NEAR (f.swell.amount01, 0.4f, 1.0e-6f);
    GF_EXPECT_NEAR (f.spfreeze.amount01, 0.5f, 1.0e-6f);
    GF_EXPECT_NEAR (f.convo.amount01, 0.25f, 1.0e-6f);
    GF_EXPECT (f.anyBusFx() && f.anyVoiceFx());

    fx::applyEffectSend (f, -3.0f);                                         // never negative
    GF_EXPECT (f.swell.amount01 == 0.0f && f.convo.amount01 == 0.0f && ! f.anyBusFx() && ! f.anyVoiceFx());
}

GF_TEST (Audit, tension_fx_lane_is_neutral_when_off_and_matches_the_decision_stage_when_on)
{
    context::ContextState ctx;
    EngineSettings s;
    s.transportValid = true;
    s.samplesPerBeat = 24000.0;
    s.perf.tsNum = 4;
    s.perf.tsDen = 4;

    GF_EXPECT (ctx.tensionFxSend (s, 480) == 1.0f);                         // Tension off

    s.perf.tension.on = true;
    s.perf.tension.shape = tension::Build;
    s.perf.tension.bars = 8;
    s.perf.tension.depth[tension::Density] = 0.0f;
    s.perf.tension.depth[tension::Intensity] = 0.0f;
    s.perf.tension.depth[tension::FxSend] = 1.0f;

    float first = 1.0f, last = 0.0f;

    for (const double bar : { 0.0, 4.0, 7.5 })
    {
        s.ppqAtBlockStart = bar * 4.0;
        const auto blockValue = ctx.tensionFxSend (s, 0);

        auto decision = s;
        tension::applyDecision (decision, s.ppqAtBlockStart, true, 4, 4);
        GF_EXPECT_NEAR (blockValue, decision.perf.fxSend01, 1.0e-6f);       // same law as the decision stage

        if (bar == 0.0)
            first = blockValue;

        last = blockValue;
    }

    GF_EXPECT (first < 0.5f && last > first);                               // a build opens the effects up

    s.perf.tension.depth[tension::FxSend] = 0.0f;
    s.perf.tension.depth[tension::Density] = 0.5f;
    GF_EXPECT (ctx.tensionFxSend (s, 480) == 1.0f);                         // lane depth 0 = neutral
}

//==============================================================================
// E-2 / N-USE-b: FreeTimingFallback was never fed.
GF_TEST (Audit, free_timing_fallback_reaches_the_health_mask)
{
    using health::HealthCode;
    const auto in = drumLoop (48000.0, 120.0, 3);

    GapFillAudioProcessor noHost;
    TestPlayHead none;
    none.provideInfo = false;
    RenderOptions opts;
    opts.playHead = &none;
    renderOffline (noHost, in, 512, 48000.0, opts);
    GF_EXPECT ((noHost.getExplainState().healthMonitor.getMask() & health::bitOf (HealthCode::FreeTimingFallback)) != 0);

    GapFillAudioProcessor withHost;
    renderOffline (withHost, in, 512, 48000.0);                             // default: playing 120 bpm transport
    GF_EXPECT ((withHost.getExplainState().healthMonitor.getMask() & health::bitOf (HealthCode::FreeTimingFallback)) == 0);

    GapFillAudioProcessor syncOff;
    setParams (syncOff, { { "syncEnable", 0.0f } });
    renderOffline (syncOff, in, 512, 48000.0, opts);
    GF_EXPECT ((syncOff.getExplainState().healthMonitor.getMask() & health::bitOf (HealthCode::FreeTimingFallback)) == 0);
}

//==============================================================================
// UI: components that shared a slot order had a link-order dependent position.
GF_TEST (Audit, slot_orders_are_unique_within_each_slot)
{
    for (int i = 0; i < (int) Slot::Count; ++i)
    {
        std::set<int> seen;

        for (const auto& f : getSlotFactories ((Slot) i))
            GF_EXPECT_MSG (seen.insert (f.order).second, "slot " + juce::String (i) + " order " + juce::String (f.order) + " used twice");
    }
}

GF_TEST (Audit, options_extra_rows_scroll_and_never_overlap)
{
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    editor.setSize (1020, 620);

    auto& options = editor.getOptionsViewForTests();
    auto& content = options.getExtraContent();
    GF_EXPECT (content.getNumChildComponents() >= 7);                       // scale, access, tempo, calibration, MIDI out, trigger, controllers

    int bottom = 0;
    int total = 0;

    for (auto* c : content.getChildren())
    {
        GF_EXPECT_MSG (c->getHeight() >= 40, c->getTitle());
        GF_EXPECT_MSG (c->getY() >= bottom, "overlap above " + c->getTitle());
        bottom = c->getBottom();
        total += c->getHeight();
    }

    GF_EXPECT (content.getHeight() >= total);                               // the viewport scrolls the rest
    GF_EXPECT (options.getExtraViewport().getHeight() == editor.getOptionsExtraBoundsForTests().getHeight());
}

GF_TEST (Audit, file_menu_entry_points_exist_while_the_editor_does)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.getUiState().editorActions.openHistory == nullptr);

    {
        GapFillAudioProcessorEditor editor (proc);
        editor.setSize (1020, 620);
        GF_EXPECT (proc.getUiState().editorActions.openHistory != nullptr);
        GF_EXPECT (proc.getUiState().editorActions.exportLatestFill != nullptr);
        GF_EXPECT (proc.getUiState().editorActions.auditionNow != nullptr);
    }

    GF_EXPECT (proc.getUiState().editorActions.openHistory == nullptr);
    GF_EXPECT (proc.getUiState().editorActions.exportLatestFill == nullptr);
    GF_EXPECT (proc.getUiState().editorActions.auditionNow == nullptr);
}

GF_TEST (Audit, live_safety_card_has_a_large_panic_button_and_drives_panic)
{
    GapFillAudioProcessor proc;
    auto card = makeSlotComponent (Slot::PerformanceTab, 50, proc);
    GF_EXPECT (card != nullptr);

    if (card == nullptr)
        return;

    card->setSize (600, 60);
    auto* panic = findByText<juce::TextButton> (*card, "PANIC  Ctrl+.");
    GF_EXPECT (panic != nullptr);

    if (panic != nullptr)
    {
        GF_EXPECT (panic->getHeight() >= 32);                               // N-USE-a: large hit target
        GF_EXPECT (! proc.getLiveControl().panic().isPanicked());
        const auto in = drumLoop (48000.0, 120.0, 1);
        RenderOptions opts;
        opts.onBlock = [panic] (int blockIndex, int) { if (blockIndex == 4) panic->onClick(); };
        renderOffline (proc, in, 512, 48000.0, opts);                       // the request is consumed by the next audio block
        GF_EXPECT (proc.getLiveControl().panic().isPanicked());
    }

    GF_EXPECT (findByText<juce::TextButton> (*card, "LAST GOOD SCENE") != nullptr);
}

GF_TEST (Audit, battle_card_stores_two_styles_and_reports_status)
{
    GapFillAudioProcessor proc;
    auto card = makeSlotComponent (Slot::PerformanceTab, 250, proc);
    GF_EXPECT (card != nullptr);

    if (card == nullptr)
        return;

    card->setSize (600, 52);
    auto* a = findByText<juce::TextButton> (*card, "STORE A");
    auto* b = findByText<juce::TextButton> (*card, "STORE B");
    auto* clear = findByText<juce::TextButton> (*card, "CLEAR");
    GF_EXPECT (a != nullptr && b != nullptr && clear != nullptr);

    if (a == nullptr || b == nullptr || clear == nullptr)
        return;

    auto& battles = proc.getLiveControl().battles();
    GF_EXPECT (! battles.hasStyle (0) && ! battles.hasStyle (1));

    setParams (proc, { { "density", 20.0f } });
    a->onClick();
    setParams (proc, { { "density", 80.0f } });
    b->onClick();

    GF_EXPECT (battles.hasStyle (0) && battles.hasStyle (1) && battles.stylesDiffer());
    clear->onClick();
    GF_EXPECT (! battles.hasStyle (0) && ! battles.hasStyle (1));
}

GF_TEST (Audit, calibration_options_write_the_global_settings)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("gapfill-audit-settings-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
    settings::GlobalSettings::useFileForTests (file);

    {
        GapFillAudioProcessor proc;
        auto row = makeSlotComponent (Slot::OptionsExtra, 270, proc);
        GF_EXPECT (row != nullptr);

        if (row != nullptr)
        {
            std::vector<juce::Component*> all;
            collect (*row, all);
            juce::Slider* slider = nullptr;
            juce::ToggleButton* toggle = nullptr;

            for (auto* c : all)
            {
                if (auto* s = dynamic_cast<juce::Slider*> (c))
                    slider = s;

                if (auto* t = dynamic_cast<juce::ToggleButton*> (c))
                    toggle = t;
            }

            GF_EXPECT (slider != nullptr && toggle != nullptr);

            if (slider != nullptr && toggle != nullptr)
            {
                GF_EXPECT (slider->getMinimum() == 4.0 && slider->getMaximum() == 15.0);
                GF_EXPECT (! toggle->getToggleState());                     // auto-apply is off by default
                slider->setValue (12.0, juce::sendNotificationSync);
                toggle->setToggleState (true, juce::dontSendNotification);
                toggle->onClick();                                          // what a click does after the state flips
                GF_EXPECT (settings::GlobalSettings::shared()->getInt ("calibDurationSec", 8) == 12);
                GF_EXPECT (settings::GlobalSettings::shared()->getBool ("calibAutoApply", false));
            }
        }
    }

    settings::GlobalSettings::useFileForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                  .getChildFile ("gapfill-audit-settings-reset.xml"));
    file.deleteFile();
}

GF_TEST (Audit, macro_editor_exposes_smoothing_and_the_lfo)
{
    GapFillAudioProcessor proc;
    auto editor = makeSlotComponent (Slot::PerformanceTab, 300, proc);
    GF_EXPECT (editor != nullptr);

    if (editor == nullptr)
        return;

    std::vector<juce::Component*> all;
    collect (*editor, all);

    bool smoothing = false, lfoShape = false, lfoDepth = false;

    for (auto* c : all)
    {
        smoothing = smoothing || c->getTitle() == "Smoothing";
        lfoShape = lfoShape || c->getTitle() == "LFO shape";
        lfoDepth = lfoDepth || c->getTitle() == "LFO depth";
    }

    GF_EXPECT (smoothing && lfoShape && lfoDepth);

    // Editing the LFO row reaches the model.
    for (auto* c : all)
        if (auto* box = dynamic_cast<juce::ComboBox*> (c))
            if (box->getTitle() == "LFO shape")
                box->setSelectedId (4, juce::sendNotificationSync);               // Square

    GF_EXPECT (proc.getLiveControl().macroMappings().get().lfo[0].shape == 3);
}

GF_TEST (Audit, scrub_window_follows_capture_length)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "captureLength", 500.0f }, { "scrubOn", 1.0f } });
    EngineSettings s;
    ParamCache cache;
    cache.connect (proc.getValueTreeState());
    fx::gatherFx (s, cache);
    GF_EXPECT_NEAR (s.fx.scrub.windowMs, 500.0f, 1.0e-3f);
}

//==============================================================================
// docs/dev/12 C9: every parameter added after the 66 core ids is excluded from Random, Random All and Surprise.
GF_TEST (Audit, feature_parameters_are_never_randomised)
{
    GapFillAudioProcessor p;
    const auto ids = allParameterIDs();
    GF_EXPECT (ids.size() > 66);

    // Put every feature parameter on a value that is not its default, remember it, then shake the sound three ways.
    std::vector<std::pair<juce::String, float>> before;

    for (int i = 66; i < ids.size(); ++i)
    {
        auto* prm = p.getValueTreeState().getParameter (ids[i]);
        const float v = prm->getDefaultValue() < 0.5f ? 0.8f : 0.2f;
        prm->setValueNotifyingHost (v);
        before.emplace_back (ids[i], prm->getValue());
    }

    for (int round = 0; round < 3; ++round)
    {
        p.randomise (true);
        p.randomise (false);
        p.surprise();
    }

    for (const auto& b : before)
        GF_EXPECT_MSG (p.getValueTreeState().getParameter (b.first)->getValue() == b.second, b.first + " was changed by Random / Surprise");
}

//==============================================================================
// docs/dev/02 F0-1: performance state is excluded from Undo snapshots and the header A/B.
GF_TEST (Audit, undo_and_ab_never_flip_performance_state)
{
    GapFillAudioProcessor p;
    setParams (p, { { "density", 30.0f } });
    p.randomise (false);                                                    // pushes an Undo snapshot with density 30, lock off
    setNormalised (p, "fl01_lock", 1.0f);
    setNormalised (p, "battleEvery", 0.5f);
    const auto battle = rawParam (p, "battleEvery");

    GF_EXPECT (p.undo());
    GF_EXPECT_NEAR (rawParam (p, "density"), 30.0f, 0.01f);                // the sound is restored
    GF_EXPECT (rawParam (p, "fl01_lock") > 0.5f);                            // the performance switches are not
    GF_EXPECT (rawParam (p, "battleEvery") == battle);

    // The header A/B swap uses the same helper.
    const auto snapshot = p.getValueTreeState().copyState().createCopy();
    setParams (p, { { "density", 70.0f } });
    setNormalised (p, "fl01_lock", 0.0f);
    p.replaceParameterStateKeepingPerformance (snapshot);
    GF_EXPECT_NEAR (rawParam (p, "density"), 30.0f, 0.01f);
    GF_EXPECT (rawParam (p, "fl01_lock") < 0.5f);
}
