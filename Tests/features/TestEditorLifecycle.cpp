// W-01: the normal suite launches a fresh child whose actual main thread runs
// JUCE's public event loop. A test worker coordinates 200 editor lifetimes via
// callSync(), while separate threads process audio and automate every parameter.
// Timer/AsyncUpdater delivery and callbacks queued across destruction are exercised.
// A green Linux run does not establish the historical Windows crash's root cause.

#include "../support/MessageLoopTestRunner.h"
#include "../support/TestRig.h"

#include "PluginEditor.h"

#include <atomic>
#include <iostream>
#include <semaphore>
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

        std::binary_semaphore timerDelivered { 0 }, asyncDelivered { 0 };
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
        const auto input = drumLoop (sampleRate, 120.0, 2);
        ThreadReport report;
        const auto started = juce::Time::getMillisecondCounterHiRes();

        std::jthread audio ([&] (std::stop_token stop)
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
            }
        });

        std::jthread automation ([&] (std::stop_token stop)
        {
            juce::Random rng (99);

            while (! stop.stop_requested())
                for (auto* parameter : params)
                {
                    if (stop.stop_requested())
                        break;

                    parameter->setValueNotifyingHost (rng.nextFloat() < 0.2f
                        ? (rng.nextBool() ? 0.0f : 1.0f) : rng.nextFloat());
                    report.paramWrites.fetch_add (1, std::memory_order_relaxed);
                }
        });

        int expiredCallbacks = 0;
        std::binary_semaphore afterDestroyDelivered { 0 };

        for (int round = 0; round < rounds; ++round)
        {
            GF_EXPECT (juce::MessageManager::callSync ([&]
            {
                host.editor.reset (host.proc->createEditor());
                host.editor->setSize (1100, 700);
                host.editor->setSize (1020, 620);
                host.probe->requestTimer();
            }));

            // Await actual timer dispatch; while the 50 ms probe is pending the
            // editor/processor's shorter-period timers can execute on the live loop.
            host.probe->awaitTimer();

            GF_EXPECT (juce::MessageManager::callSync ([&]
            {
                if (round % 2 == 0)
                    host.proc->selectPreset (round % (int) getFactoryPresets().size());
                else
                    host.proc->setStateInformation (saved.getData(), (int) saved.getSize());

                host.probe->requestAsync();
            }));
            host.probe->awaitAsync();

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
            afterDestroyDelivered.acquire();
        }

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
    const bool finished = child.waitForProcessToFinish (600000);

    if (! finished)
        child.kill();

    const auto output = child.readAllProcessOutput();
    std::cout << output << std::endl;
    GF_EXPECT_MSG (finished, "message-loop lifecycle child exceeded its ten-minute deadline");
    GF_EXPECT_MSG (child.getExitCode() == 0, output);
    GF_EXPECT_MSG (output.contains ("1 passed, 0 failed"), "the child executed exactly the lifecycle stress case");
}
