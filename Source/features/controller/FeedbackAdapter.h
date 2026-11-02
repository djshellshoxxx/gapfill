#pragma once
#include "FeedbackCore.h"
#include "../commands/ControllerProfile.h"
#include "../../dsp/SpscQueue.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill::controller
{
/** Owns the enabled-only message timer and the message->audio queue. The sink
    remains owned by FeedbackEngine so legacy device and test seams stay valid. */
class FeedbackAdapter final : private juce::Timer
{
public:
    explicit FeedbackAdapter (commands::FeedbackEngine&);
    ~FeedbackAdapter() override;
    bool configure (const commands::Profile&, juce::AudioProcessorValueTreeState&, MidiLearnManager*,
                    std::function<commands::FeedbackState()>);
    void disable();
    void sinkChanged();
    void enableHostOutput (bool);
    void resync();
    void tick (double nowMs);
    bool isConfigured() const noexcept { return configured; }
    const commands::Profile& profile() const noexcept { return configuration; }
    int pop (OutEvent*, int maximum) noexcept;
    bool takeDeviceLost() noexcept { return deviceLost.exchange (false, std::memory_order_acq_rel); }
private:
    void timerCallback() override;
    void updateTimer();
    static bool send (void*, OutEvent) noexcept;
    struct Queued { OutEvent event; std::uint64_t generation = 0; };
    commands::FeedbackEngine& owner;
    commands::Profile configuration;
    FeedbackCore core;
    std::array<juce::RangedAudioParameter*, kMaxParameterBindings> parameters {};
    std::array<int, commands::kMaxFeedbackRules> indicatorOutput {};
    std::size_t parameterCount = 0;
    MidiLearnManager* midiLearn = nullptr;
    std::function<commands::FeedbackState()> source;
    bool configured = false, hostOutput = false;
    double nextDevicePoll = -1.0;
    SpscQueue<Queued, kOutputQueueCapacity> output;
    std::atomic<std::uint64_t> generation { 1 };
    std::atomic<bool> deviceLost { false };
};
}
