#include "../support/ScopedTestThread.h"
#include "../support/TestSync.h"
#include "../support/TestTimeScale.h"
// W-01: the normal suite launches a fresh child whose actual main thread runs
// JUCE's public event loop. A test worker coordinates 200 editor lifetimes via
// callSync(), while separate threads process audio and automate every parameter.
// Timer/AsyncUpdater delivery and callbacks queued across destruction are exercised.
// A green Linux run does not establish the historical Windows crash's root cause.

#include "../support/MessageLoopTestRunner.h"
#include "../support/TestRig.h"

#include "PluginEditor.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct ThreadReport
    {
        std::atomic<long> blocks { 0 }, paramWrites { 0 }, nonFinite { 0 };
    };

    class DispatchProbe final : private juce::Timer, private juce::AsyncUpdater
    {
    public:
        ~DispatchProbe() override
        {
            stopTimer();
            cancelPendingUpdate();
        }

        void requestTimer() { startTimer (50); }
        void requestAsync() { triggerAsyncUpdate(); }
        void awaitTimer() { timerDelivered.acquire(); }
        void awaitAsync() { asyncDelivered.acquire(); }

        int timerCallbacks = 0, asyncCallbacks = 0;
        bool wrongThread = false;

    private:
        void timerCallback() override
        {
            stopTimer();
            wrongThread = wrongThread || ! juce::MessageManager::getInstance()->isThisTheMessageThread();
            ++timerCallbacks;
            timerDelivered.release();
        }

        void handleAsyncUpdate() override
        {
            wrongThread = wrongThread || ! juce::MessageManager::getInstance()->isThisTheMessageThread();
            ++asyncCallbacks;
            asyncDelivered.release();
        }

        gftest::TestSemaphore timerDelivered { 0 }, asyncDelivered { 0 };
    };

    struct HostSession
    {
        ~HostSession()
        {
            juce::MessageManager::callSync ([&]
            {
                editor.reset();
                probe.reset();

                if (proc != nullptr)
                {
                    proc->setPlayHead (nullptr);
                    proc->releaseResources();
                }

                proc.reset();
            });
        }

        TestPlayHead playHead;
        std::unique_ptr<GapFillAudioProcessor> proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        std::unique_ptr<DispatchProbe> probe;
    };

    void runLifecycleChild()
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256, rounds = 200;
        auto* manager = juce::MessageManager::getInstance();
        GF_EXPECT (! manager->isThisTheMessageThread());
        HostSession host;
        std::vector<juce::AudioProcessorParameter*> params;
        juce::MemoryBlock saved;

        ThreadReport report;
        // Stall detector: a hang guard stricter than the total deadline. Every phase of every
        // cycle stamps progress; one phase stuck for two minutes (scaled for sanitizers) ends the
        // child with the phase name, instead of waiting out the total deadline silently.
        std::atomic<double> lastProgressMs { juce::Time::getMillisecondCounterHiRes() };
        std::atomic<const char*> phase { "start" };
        std::atomic<int> currentRound { 0 };
        const auto progress = [&] (const char* name)
        {
            phase.store (name);
            lastProgressMs.store (juce::Time::getMillisecondCounterHiRes());
        };

        gftest::ScopedTestThread watchdog ([&] (gftest::ScopedTestThread::StopToken stop)
        {
            const double limitMs = 120000.0 * gftest::kDeadlineScale;

            while (! stop.stop_requested())
            {
                if (juce::Time::getMillisecondCounterHiRes() - lastProgressMs.load() > limitMs)
                {
                    std::cout << "  editor lifecycle stalled: cycle=" << currentRound.load() << " phase=" << phase.load()
                              << " blocks=" << report.blocks.load() << " writes=" << report.paramWrites.load() << std::endl;
                    std::_Exit (3);
                }

                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
        });

        progress ("create-processor");
        GF_EXPECT (juce::MessageManager::callSync ([&]
        {
            host.proc = std::make_unique<GapFillAudioProcessor>();
            host.proc->setPlayHead (&host.playHead);
            host.proc->setPlayConfigDetails (2, 2, sampleRate, blockSize);
            host.proc->prepareToPlay (sampleRate, blockSize);
            host.proc->getStateInformation (saved);
            host.probe = std::make_unique<DispatchProbe>();

            for (auto* p : host.proc->getParameters())
                params.push_back (p);
        }));

        GF_EXPECT (params.size() > 100);
        std::cout << "  editor lifecycle: message thread serviced the first callSync" << std::endl;
        progress ("start-workers");
        const auto input = drumLoop (sampleRate, 120.0, 2);
        const auto started = juce::Time::getMillisecondCounterHiRes();

        gftest::ScopedTestThread audio ([&] (gftest::ScopedTestThread::StopToken stop)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            int position = 0;

            while (! stop.stop_requested())
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, input.getSample (ch, (position + i) % input.getNumSamples()));

                position = (position + blockSize) % input.getNumSamples();
                midi.clear();
                host.proc->processBlock (buffer, midi);
                host.playHead.advance (blockSize, sampleRate);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        if (! std::isfinite (buffer.getSample (ch, i)))
                            report.nonFinite.fetch_add (1, std::memory_order_relaxed);

                report.blocks.fetch_add (1, std::memory_order_relaxed);
                // Unpaced spinning on <= 3 cores (macOS runners, sanitizer slowdown) starved the
                // message thread under test past the deadline. Yield the core once per block.
                std::this_thread::yield();
            }
        });

        gftest::ScopedTestThread automation ([&] (gftest::ScopedTestThread::StopToken stop)
        {
            juce::Random rng (99);

            while (! stop.stop_requested())
            {
                for (auto* parameter : params)
                {
                    if (stop.stop_requested())
                        break;

                    parameter->setValueNotifyingHost (rng.nextFloat() < 0.2f
                        ? (rng.nextBool() ? 0.0f : 1.0f) : rng.nextFloat());
                    report.paramWrites.fetch_add (1, std::memory_order_relaxed);
                }

                std::this_thread::yield(); // once per sweep of every parameter; see the audio worker
            }
        });

        int expiredCallbacks = 0;
        gftest::TestSemaphore afterDestroyDelivered { 0 };

        for (int round = 0; round < rounds; ++round)
        {
            currentRound.store (round);
            progress ("create-editor");
            // Eight short progress lines stay below the child's pipe capacity.
            // Leave the full stress load unchanged; deadlines are in the parent and the stall detector.
            if (round % 25 == 0)
                std::cout << "  editor cycle=" << round
                          << " elapsed=" << (juce::Time::getMillisecondCounterHiRes() - started) * 0.001
                          << "s blocks=" << report.blocks.load() << " writes=" << report.paramWrites.load()
                          << std::endl;
            GF_EXPECT (juce::MessageManager::callSync ([&]
            {
                host.editor.reset (host.proc->createEditor());
                host.editor->setSize (1100, 700);
                host.editor->setSize (1020, 620);
                host.probe->requestTimer();
            }));

            // Await actual timer dispatch; while the 50 ms probe is pending the
            // editor/processor's shorter-period timers can execute on the live loop.
            progress ("await-timer");
            host.probe->awaitTimer();
            progress ("preset-or-state");

            GF_EXPECT (juce::MessageManager::callSync ([&]
            {
                if (round % 2 == 0)
                    host.proc->selectPreset (round % (int) getFactoryPresets().size());
                else
                    host.proc->setStateInformation (saved.getData(), (int) saved.getSize());

                host.probe->requestAsync();
            }));
            progress ("await-async");
            host.probe->awaitAsync();
            progress ("destroy-editor");

            GF_EXPECT (juce::MessageManager::callSync ([&]
            {
                juce::Component::SafePointer<juce::Component> safe (host.editor.get());
                juce::MessageManager::callAsync ([safe, &expiredCallbacks, &afterDestroyDelivered]
                {
                    if (safe == nullptr)
                        ++expiredCallbacks;

                    afterDestroyDelivered.release();
                });
                host.editor.reset();
            }));
            progress ("await-after-destroy");
            afterDestroyDelivered.acquire();
        }

        watchdog.request_stop();
        watchdog.join();

        audio.request_stop();
        automation.request_stop();
        audio.join();
        automation.join();
        const double elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - started) * 0.001;

        GF_EXPECT (report.blocks.load() > rounds);
        GF_EXPECT (report.paramWrites.load() >= (long) params.size() * rounds);
        GF_EXPECT_MSG ((double) report.paramWrites.load() / elapsedSeconds >= 10000.0,
                       "the host-automation worker sustained at least 10 kHz of parameter writes");
        GF_EXPECT (report.nonFinite.load() == 0);
        GF_EXPECT (expiredCallbacks == rounds);
        GF_EXPECT (host.probe->timerCallbacks == rounds && host.probe->asyncCallbacks == rounds);
        GF_EXPECT (! host.probe->wrongThread);
        std::cout << "  editor cycles=" << rounds << " blocks=" << report.blocks.load()
                  << " writes=" << report.paramWrites.load() << " writes/sec="
                  << (double) report.paramWrites.load() / elapsedSeconds
                  << " timer callbacks=" << host.probe->timerCallbacks
                  << " async callbacks=" << host.probe->asyncCallbacks << std::endl;
    }
}

GF_TEST (EditorLifecycle, editor_create_destroy_while_automating_and_processing)
{
    if (messageLoopTestMode())
    {
        runLifecycleChild();
        return;
    }

    GF_EXPECT (juce::MessageManager::getInstance()->isThisTheMessageThread());
    juce::ChildProcess child;
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    GF_EXPECT (child.start (juce::StringArray { executable, "--message-loop", "EditorLifecycle" }));

    // Deadline contains hangs in the isolated host without using sleeps for thread
    // synchronisation or leaving the normal suite's message loop in a quit state.
    // The child's own stall detector fails a stuck phase sooner; this caps the total.
    const bool finished = child.waitForProcessToFinish (600000 * gftest::kDeadlineScale);

    if (! finished)
        child.kill();

    const auto output = child.readAllProcessOutput();
    std::cout << output << std::endl;
    GF_EXPECT_MSG (finished, "message-loop lifecycle child exceeded its total deadline (ten minutes, x4 under sanitizers)");
    GF_EXPECT_MSG (child.getExitCode() == 0, output);
    GF_EXPECT_MSG (output.contains ("1 passed, 0 failed"), "the child executed exactly the lifecycle stress case");
}
