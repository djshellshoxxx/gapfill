#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "MidiLearn.h"
#include "features/commands/ControllerProfile.h"

using namespace gapfill;
using namespace gftest;
namespace
{
    struct Processor : juce::AudioProcessor
    {
        const juce::String getName() const override { return "Controller test"; }
        void prepareToPlay (double, int) override {} void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0; }
        bool acceptsMidi() const override { return true; } bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; } bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; } void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; } void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {} void setStateInformation (const void*, int) override {}
    };
    struct Rig
    {
        Processor processor;
        juce::AudioProcessorValueTreeState state { processor, nullptr, "Test", layout() };
        MidiLearnManager learn;
        static juce::AudioProcessorValueTreeState::ParameterLayout layout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout result;
            for (const auto* id : { "wet", "fill" })
                result.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, id, 0.0f, 1.0f, 0.5f));
            return result;
        }
        Rig() { learn.connect (state); }
        float value() const { return state.getParameter ("wet")->getValue(); }
        float expected (int cc) const
        { auto* p = state.getParameter ("wet"); return p->convertTo0to1 (p->convertFrom0to1 ((float) cc / 127.0f)); }
        void cc (int channel, int number, int value)
        {
            juce::MidiBuffer midi; midi.addEvent (juce::MidiMessage::controllerEvent (channel, number, value), 0);
            { AllocGuard guard; learn.processMidi (midi); GF_EXPECT (guard.count() == 0); }
            learn.applyPendingChanges();
        }
    };
}
GF_TEST (MidiLearnExtended, channel_filter_and_legacy_omni_jump_use_the_actual_apvts_path)
{
    Rig rig;
    GF_EXPECT (rig.learn.configureMapping ("wet", 74, {2, controller::TakeoverMode::Jump}));
    rig.cc (1, 74, 127); GF_EXPECT_NEAR (rig.value(), 0.5, 1.0e-6);
    rig.cc (2, 74, 127); GF_EXPECT_NEAR (rig.value(), 1.0, 1.0e-6);
    rig.learn.setMapping ("wet", 74); // established API resets to legacy Omni/Jump
    rig.cc (1, 74, 0); GF_EXPECT_NEAR (rig.value(), 0.0, 1.0e-6);
}
GF_TEST (MidiLearnExtended, pickup_crosses_then_external_automation_rearms_and_midimap_bytes_stay_legacy)
{
    Rig rig;
    GF_EXPECT (rig.learn.configureMapping ("wet", 74, {0, controller::TakeoverMode::Pickup}));
    const auto before = rig.learn.toValueTree().toXmlString();
    rig.cc (1, 74, 10); GF_EXPECT_NEAR (rig.value(), 0.5, 1.0e-6);
    rig.cc (1, 74, 100); GF_EXPECT_NEAR (rig.value(), rig.expected (100), 1.0e-6);
    rig.state.getParameter ("wet")->setValueNotifyingHost (0.2f);
    rig.cc (1, 74, 90); GF_EXPECT_NEAR (rig.value(), 0.2, 1.0e-6);
    rig.cc (1, 74, 20); GF_EXPECT_NEAR (rig.value(), rig.expected (20), 1.0e-6);
    // Even an external excursion back to the exact last MIDI value re-arms it.
    rig.state.getParameter ("wet")->setValueNotifyingHost (0.9f);
    rig.state.getParameter ("wet")->setValueNotifyingHost (20.0f / 127.0f);
    rig.cc (1, 74, 80); GF_EXPECT_NEAR (rig.value(), rig.expected (20), 1.0e-6);
    GF_EXPECT (rig.learn.toValueTree().toXmlString() == before);
    rig.learn.restoreFromValueTree (rig.learn.toValueTree());
    GF_EXPECT (rig.learn.getMappingOptions ("wet").channel == 0 && rig.learn.getMappingOptions ("wet").takeover == controller::TakeoverMode::Jump);
}
GF_TEST (MidiLearnExtended, duplicate_learning_refuses_to_steal_and_explicit_replace_is_required)
{
    Rig rig; rig.learn.setMapping ("fill", 74);
    GF_EXPECT (! rig.learn.configureMapping ("wet", 74, {}));
    GF_EXPECT (rig.learn.getMappedCC ("fill") == 74 && rig.learn.getMappedCC ("wet") == -1);
    rig.learn.startLearning ("wet"); rig.cc (1, 74, 80);
    GF_EXPECT (rig.learn.getMappedCC ("fill") == 74 && rig.learn.getMappedCC ("wet") == -1);
    GF_EXPECT (rig.learn.getMappingStatus().containsIgnoreCase ("already"));
    GF_EXPECT (rig.learn.configureMapping ("wet", 74, {}, true));
    GF_EXPECT (rig.learn.getMappedCC ("fill") == -1 && rig.learn.getMappedCC ("wet") == 74);
}
GF_TEST (MidiLearnExtended, command_conflicts_and_exclusive_learning_are_message_thread_only)
{
    Rig rig; int calls = 0, starts = 0;
    rig.learn.setCommandConflictHandler ([&] (int cc, int channel, bool replace)
    { ++calls; return cc == 74 && (channel == 0 || channel == 2) && ! replace ? juce::String ("CC 74 already controls Panic") : juce::String(); });
    rig.learn.setLearnStartHook ([&] { ++starts; });
    GF_EXPECT (! rig.learn.configureMapping ("wet", 74, {}));
    GF_EXPECT (rig.learn.configureMapping ("wet", 74, {1, controller::TakeoverMode::Jump}));
    const auto before = calls;
    rig.cc (1, 74, 100); GF_EXPECT (calls == before);
    rig.learn.startLearning ("fill"); GF_EXPECT (starts == 1);
    rig.cc (2, 74, 50); GF_EXPECT (! rig.learn.isLearning() && rig.learn.getMappedCC ("fill") == -1);
}
GF_TEST (MidiLearnExtended, monitor_and_echo_receipts_are_independent_of_mapping_and_coalesce_latest)
{
    Rig rig; rig.cc (3, 74, 42);
    const auto message = rig.learn.lastInput();
    GF_EXPECT (message.sequence != 0 && message.status == 0xb2 && message.data1 == 74 && message.data2 == 42);
    std::uint8_t channel = 0;
    GF_EXPECT (rig.learn.takeReceivedCC (74, channel) && channel == 3);
    GF_EXPECT (! rig.learn.takeReceivedCC (74, channel));
}

namespace
{
    struct FeedbackDevice : commands::MidiSink
    {
        std::vector<juce::MidiMessage> sent;
        bool present = true, returned = false;
        bool send (const juce::MidiMessage& event) override
        { if (! present) return false; sent.push_back (event); return true; }
        bool isConnected() override { return present; }
        bool tryReconnect() override { if (returned) present = true; return present; }
    };
    commands::Profile knobProfile()
    {
        commands::Profile profile;
        profile.name = "Knobs"; profile.extended = true; profile.parameterFeedback = true;
        profile.params.push_back ({"wet", 74, 2, controller::TakeoverMode::Pickup});
        return profile;
    }
}
GF_TEST (ControllerFeedbackAdapter, actual_parameter_feedback_suppresses_input_even_on_a_different_transmit_channel)
{
    Rig rig; commands::FeedbackEngine feedback;
    auto device = std::make_unique<FeedbackDevice>(); auto* observed = device.get();
    feedback.setSink (std::move (device));
    auto profile = knobProfile(); profile.txChannel = 3;
    GF_EXPECT (feedback.configureExtended (profile, rig.state, &rig.learn, {}));
    feedback.tickExtendedForTests (0.0);
    GF_EXPECT (observed->sent.size() == 1 && observed->sent[0].getChannel() == 3 && observed->sent[0].getControllerValue() == 64);
    rig.cc (2, 74, 100); // Receipt exists even though this parameter is not mapped.
    rig.state.getParameter ("wet")->setValueNotifyingHost (0.9f);
    feedback.tickExtendedForTests (100.0); feedback.tickExtendedForTests (349.0);
    GF_EXPECT (observed->sent.size() == 1);
    feedback.tickExtendedForTests (350.0);
    GF_EXPECT (observed->sent.size() == 2 && observed->sent.back().getControllerValue() == 114);
}
GF_TEST (ControllerFeedbackAdapter, device_loss_is_sticky_and_return_resyncs_current_state_within_500ms)
{
    Rig rig; commands::FeedbackEngine feedback;
    auto device = std::make_unique<FeedbackDevice>(); auto* observed = device.get();
    feedback.setSink (std::move (device));
    GF_EXPECT (feedback.configureExtended (knobProfile(), rig.state, &rig.learn, {}));
    feedback.tickExtendedForTests (0.0); observed->present = false;
    feedback.tickExtendedForTests (100.0);
    GF_EXPECT (! feedback.isConnected() && feedback.takeDeviceLost() && ! feedback.takeDeviceLost());
    rig.state.getParameter ("wet")->setValueNotifyingHost (0.25f); observed->returned = true;
    feedback.tickExtendedForTests (200.0);
    GF_EXPECT (feedback.isConnected() && observed->sent.size() == 2 && observed->sent.back().getControllerValue() == 32);
}
GF_TEST (ControllerFeedbackAdapter, plugin_queue_is_bounded_budgeted_and_stale_generations_never_escape)
{
    Rig rig; commands::FeedbackEngine feedback;
    GF_EXPECT (feedback.configureExtended (knobProfile(), rig.state, &rig.learn, {}));
    feedback.setHostOutputEnabled (true);
    for (int i = 0; i < 600; ++i)
    { rig.state.getParameter ("wet")->setValueNotifyingHost ((i % 2) == 0 ? 0.2f : 0.8f); feedback.tickExtendedForTests (i * 40.0); }
    std::array<controller::OutEvent, 7> output {};
    int total = 0;
    for (int i = 0; i < 40; ++i)
    {
        AllocGuard guard;
        const auto count = feedback.popOutput (output.data(), (int) output.size());
        GF_EXPECT (count >= 0 && count <= 7 && guard.count() == 0); total += count;
    }
   #if GAPFILL_MIDI_OUT
    GF_EXPECT (total == controller::kOutputQueueCapacity);
   #else
    GF_EXPECT (total == 0);
   #endif
    feedback.tickExtendedForTests (30000.0); feedback.disableExtended();
    GF_EXPECT (feedback.popOutput (output.data(), 7) == 0);
}
