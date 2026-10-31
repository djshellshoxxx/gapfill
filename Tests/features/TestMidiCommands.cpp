#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "features/commands/CommandMap.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    struct MidiCommandRig
    {
        GapFillAudioProcessor proc;
        juce::AudioBuffer<float> audio { 2, 512 };
        juce::MidiBuffer midi;

        MidiCommandRig()
        {
            setParams (proc, { { "fill", 0.0f }, { "captureLength", 250.0f } });
            prepareProcessor (proc, 48000.0, 512, false);
            midi.ensureSize (4096);
        }

        void bind (CommandType type, int cc, int historyIndex = 0)
        {
            commands::Binding binding;
            binding.cmd = type;
            binding.type = commands::MsgType::CC;
            binding.number = (std::uint8_t) cc;
            binding.arg = (std::int16_t) historyIndex;
            GF_EXPECT (proc.getLiveControl().bindings().add (binding) == commands::BindingModel::AddResult::Ok);
        }

        void block (int cc = -1, int value = 127)
        {
            audio.clear();
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    audio.setSample (channel, sample, 0.2f);
            midi.clear();
            if (cc >= 0)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 17);
            {
                AllocGuard guard;
                proc.processBlock (audio, midi);
                GF_EXPECT (guard.count() == 0);
            }
        }

        void trigger (int cc)
        {
            block (cc);
            block(); // the established one-block MIDI command delay
        }

        int recorded (CommandType type)
        {
            int count = 0;
            for (const auto& r : proc.getExplainState().recorder.snapshot().records)
                count += r.type == (std::uint8_t) events::RecType::Command && r.flags == (std::uint8_t) type;
            return count;
        }
    };

    FillRecipe recipe (std::uint32_t seed)
    {
        FillRecipe r;
        r.algo = 8;
        r.band = 1;
        r.lengthMs = 123;
        r.pitchSemis = -7;
        r.modifierMask = 3;
        r.seed = seed;
        return r;
    }
}

GF_TEST (MidiCommands, production_capture_commands_reach_the_latch_once_after_one_block)
{
    MidiCommandRig rig;
    rig.bind (CommandType::Recapture, 90);
    rig.bind (CommandType::ReleaseCapture, 91);
    for (int i = 0; i < 32; ++i)
        rig.block();
    setParams (rig.proc, { { "capture", 1.0f } });
    rig.block();
    auto& latch = rig.proc.getShaping().capture;
    GF_EXPECT (latch.isHeld() && latch.getSnapshotCount() == 0);
    rig.proc.getExplainState().recorder.start ("<s/>", 48000.0, 512, "test", 2);
    rig.block (90);
    GF_EXPECT (rig.recorded (CommandType::Recapture) == 0 && ! latch.isPending());
    rig.block();
    GF_EXPECT (rig.recorded (CommandType::Recapture) == 1);
    for (int i = 0; i < 8; ++i)
        rig.block();
    GF_EXPECT (latch.getSnapshotCount() == 1 && latch.getMaxCopyCount() <= CaptureLatch::kMaxCopyPerBlock);
    rig.block (91);
    GF_EXPECT (! latch.isSuppressed());
    rig.block();
    GF_EXPECT (latch.isSuppressed() && ! latch.isHeld());
    GF_EXPECT (rig.recorded (CommandType::ReleaseCapture) == 1);
    GF_EXPECT (rig.proc.postCommand ({ CommandType::ReleaseCapture }));
    rig.block();
    GF_EXPECT (rig.recorded (CommandType::ReleaseCapture) == 2); // GUI drain never calls the MIDI sink twice
    GF_EXPECT (rig.proc.getActiveEditor() == nullptr);
}

GF_TEST (MidiCommands, production_ratings_receive_the_exact_history_recipe_and_learning_gate)
{
    MidiCommandRig rig;
    rig.bind (CommandType::SkipRecipe, 92);
    rig.bind (CommandType::LikeLatest, 93);
    rig.bind (CommandType::SkipLatest, 94);
    auto& context = rig.proc.getContextState();
    auto& taste = context.tasteService();
    const juce::TemporaryFile libraryFile (".xml");
    taste.useLibraryFile (libraryFile.getFile());
    const auto historyRecipe = recipe (0xDEADBEEFu);
    rig.proc.getRecipeHistory().push (historyRecipe);
    rig.proc.getExplainState().recorder.start ("<s/>", 48000.0, 512, "test", 2);
    rig.trigger (92);
    rig.proc.flushLatencyReportForTests(); // real processor timer uses fx05_on (default off)
    GF_EXPECT (taste.counts().skips == 0 && rig.recorded (CommandType::SkipRecipe) == 1);
    setParams (rig.proc, { { "fx05_on", 1.0f } });
    rig.block (92, 0);
    rig.trigger (92);
    rig.proc.flushLatencyReportForTests();
    GF_EXPECT (taste.counts().skips == 1 && taste.store().entries().size() == 1);
    std::uint8_t expected[FillRecipe::serialisedBytes], actual[FillRecipe::serialisedBytes];
    toBytes (historyRecipe, expected);
    toBytes (taste.store().entries()[0].recipe, actual);
    GF_EXPECT (std::memcmp (expected, actual, sizeof expected) == 0);

    FillEvent latest;
    latest.type = FillEventType::Started;
    latest.recipe = recipe (0x12345678u);
    context.onFillEvent (latest);
    rig.trigger (93);
    rig.proc.flushLatencyReportForTests();
    GF_EXPECT (taste.counts().likes == 1 && taste.counts().skips == 1);
    rig.trigger (94);
    rig.proc.flushLatencyReportForTests();
    GF_EXPECT (taste.counts().likes == 0 && taste.counts().skips == 2);
    GF_EXPECT (rig.recorded (CommandType::LikeLatest) == 1 && rig.recorded (CommandType::SkipLatest) == 1);
    GF_EXPECT (rig.proc.getActiveEditor() == nullptr);
}

GF_TEST (MidiCommands, production_recipe_commands_reject_changed_or_missing_history)
{
    MidiCommandRig rig;
    rig.bind (CommandType::SkipRecipe, 95);
    rig.proc.getRecipeHistory().push (recipe (1));
    rig.proc.getExplainState().recorder.start ("<s/>", 48000.0, 512, "test", 2);
    rig.block (95);
    rig.proc.getRecipeHistory().push (recipe (2)); // replace history between scan and dispatch
    rig.block();
    GF_EXPECT (rig.recorded (CommandType::SkipRecipe) == 0);
    const auto dropped = rig.proc.getLiveControl().getDroppedCommandEvents();
    GF_EXPECT (dropped > 0);
    rig.block (95, 0);
    rig.proc.getRecipeHistory().clear();
    rig.trigger (95);
    GF_EXPECT (rig.recorded (CommandType::SkipRecipe) == 0);
    GF_EXPECT (rig.proc.getLiveControl().getDroppedCommandEvents() > dropped);
}
