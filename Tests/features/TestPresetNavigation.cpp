#include "../support/TestRig.h"
#include "features/presetbrowser/PresetNavigation.h"
#include "features/presetbrowser/PresetFiles.h"
#include "features/presetbrowser/GlobalSettings.h"
#include "features/i18n/Strings.h"

#include <semaphore>
#include <chrono>

using namespace gapfill;
using namespace gapfill::presets;
using namespace gftest;

namespace
{
    struct NavigationFiles
    {
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("gapfill-nav-" + juce::Uuid().toString());
        std::shared_ptr<settings::GlobalSettings> preferences;

        NavigationFiles()
        {
            root.createDirectory();
            preferences = std::make_shared<settings::GlobalSettings> (root.getChildFile ("settings.xml"));
        }
        ~NavigationFiles() { preferences.reset(); root.deleteRecursively(); }
        std::vector<ScanRoot> roots() const { return { { "test", root } }; }
    };

    struct DelayedScan
    {
        ScanResult run()
        {
            if (calls.fetch_add (1) == 0)
            {
                entered.release();
                proceed.acquire();
            }
            return {};
        }
        void unblock() { if (! released.exchange (true)) proceed.release(); }
        std::binary_semaphore entered { 0 }, proceed { 0 };
        std::atomic<int> calls { 0 };
        std::atomic<bool> released { false };
    };

    struct UnblockOnExit
    {
        std::shared_ptr<DelayedScan> gate;
        ~UnblockOnExit() { gate->unblock(); }
    };

    void drainScans (PresetNavigation& navigation, GapFillAudioProcessor& proc)
    {
        const auto start = juce::Time::getMillisecondCounter();
        while (navigation.scanPendingForTests() && juce::Time::getMillisecondCounter() - start < 5000u)
        {
            navigation.pump (proc);
            juce::Thread::sleep (1);
        }
        GF_EXPECT (! navigation.scanPendingForTests());
    }
}

GF_TEST (PresetNavigation, a_newer_explicit_preset_choice_cancels_an_inflight_step)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    proc.selectPreset (0);
    auto& navigation = proc.getUiState().presetNavigation;
    const auto gate = std::make_shared<DelayedScan>();
    UnblockOnExit unblock { gate }; // also releases the worker if an assertion throws
    navigation.configureScanForTests ([gate] { return gate->run(); }, files.preferences);
    navigation.requestStep (1);
    navigation.pump (proc);
    GF_EXPECT (gate->entered.try_acquire_for (std::chrono::seconds (2)));
    proc.selectPreset (3);
    navigation.noteLoaded (factoryEntries()[3]);
    gate->unblock();
    drainScans (navigation, proc);
    GF_EXPECT (proc.getCurrentProgram() == 3 && navigation.displayName() == proc.getProgramName (3));
    GF_EXPECT (gate->calls.load() == 1);
}

GF_TEST (PresetNavigation, a_newer_file_choice_cancels_an_inflight_step)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto gate = std::make_shared<DelayedScan>();
    UnblockOnExit unblock { gate };
    navigation.configureScanForTests ([gate] { return gate->run(); }, files.preferences);
    navigation.requestStep (1);
    navigation.pump (proc);
    GF_EXPECT (gate->entered.try_acquire_for (std::chrono::seconds (2)));
    setParams (proc, { { "fill", 75.0f } });
    const auto file = files.root.getChildFile ("Newer.gapfill");
    navigation.noteFile (file, "Newer choice");
    gate->unblock();
    drainScans (navigation, proc);
    GF_EXPECT (navigation.currentFile() == file && navigation.displayName() == "Newer choice");
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == 75.0f);
}

GF_TEST (PresetNavigation, a_post_restore_step_requires_a_fresh_scan_generation)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto gate = std::make_shared<DelayedScan>();
    UnblockOnExit unblock { gate };
    navigation.configureScanForTests ([gate] { return gate->run(); }, files.preferences);
    navigation.requestStep (1);
    navigation.pump (proc);
    GF_EXPECT (gate->entered.try_acquire_for (std::chrono::seconds (2)));
    proc.selectPreset (3);
    navigation.requestReset();
    navigation.requestStep (1); // newer than reset: must survive while the old scan is discarded
    gate->unblock();
    drainScans (navigation, proc);
    GF_EXPECT (gate->calls.load() == 2 && proc.getCurrentProgram() == 4);
}

GF_TEST (PresetNavigation, panic_then_recover_before_the_timer_cancels_an_inflight_step)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);
    proc.selectPreset (3);
    auto& navigation = proc.getUiState().presetNavigation;
    const auto gate = std::make_shared<DelayedScan>();
    UnblockOnExit unblock { gate };
    navigation.configureScanForTests ([gate] { return gate->run(); }, files.preferences);
    navigation.requestStep (1);
    navigation.pump (proc);
    GF_EXPECT (gate->entered.try_acquire_for (std::chrono::seconds (2)));
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    audio.clear();
    proc.getLiveControl().panic().requestPanic();
    proc.processBlock (audio, midi);
    proc.getLiveControl().panic().requestRecover();
    proc.processBlock (audio, midi);
    GF_EXPECT (! proc.getLiveControl().panic().isPanicked());
    proc.flushLatencyReportForTests(); // consumes the retained PanicChanged1 acknowledgement
    gate->unblock();
    drainScans (navigation, proc);
    GF_EXPECT (proc.getCurrentProgram() == 3 && gate->calls.load() == 1);
}

GF_TEST (PresetNavigation, closed_editor_steps_factory_and_user_presets_with_one_undo_burst)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto user = files.root.getChildFile ("User.gapfill");
    setParams (proc, { { "fill", 73.0f } });
    juce::String error;
    GF_EXPECT (writeUserPreset (proc, user, { "User tone", "Ambient", "fixture", "" }, error));
    proc.selectPreset (proc.getNumPrograms() - 1);
    const auto beforeFill = proc.getValueTreeState().getRawParameterValue ("fill")->load();
    const auto depth = proc.getUndoDepth();
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 1000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user && navigation.displayName() == "User tone");
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == 73.0f);
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 2000, files.roots(), files.preferences));
    GF_EXPECT (proc.getCurrentProgram() == 0 && navigation.currentFile() == juce::File());
    GF_EXPECT (proc.getUndoDepth() == depth + 1);
    GF_EXPECT (proc.undo());
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == beforeFill);
    GF_EXPECT (proc.getActiveEditor() == nullptr);
}

GF_TEST (PresetNavigation, favourites_use_shared_user_identity_and_empty_favourites_fall_back)
{
    NavigationFiles files;
    GapFillAudioProcessor proc;
    auto& navigation = proc.getUiState().presetNavigation;
    const auto user = files.root.getChildFile ("Favourite.gapfill");
    juce::String error;
    GF_EXPECT (writeUserPreset (proc, user, { "Favourite user", "Ambient", "fixture", "" }, error));
    files.preferences->setInt (kKeyStepScope, 1);
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 1000, files.roots(), files.preferences));
    GF_EXPECT (navigation.status() == i18n::text ("No favourites yet - stepping all presets"));
    juce::String identity;
    for (const auto& entry : navigation.catalog().entries())
        if (entry.file == user)
            identity = entry.identity;
    GF_EXPECT (identity.isNotEmpty() && files.preferences->setFavourite (identity, true));
    GF_EXPECT (navigation.stepNowForTests (proc, 1, 2000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user && navigation.currentIndex (proc) >= proc.getNumPrograms());
    GF_EXPECT (navigation.stepNowForTests (proc, -1, 6000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user);
    files.preferences->setFavourite (navigation.catalog().entries()[0].identity, true);
    files.preferences->setFavourite (navigation.catalog().entries()[1].identity, true);
    proc.selectPreset (0);
    GF_EXPECT (navigation.stepNowForTests (proc, 2, 7000, files.roots(), files.preferences));
    GF_EXPECT (navigation.currentFile() == user); // coalesced requests keep every step, including favourites
    navigation.requestReset();
    navigation.pump (proc);
    GF_EXPECT (navigation.currentFile() == juce::File());
}
