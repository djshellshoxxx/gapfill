// F-L11 MIDI output from gap / fill events (docs/dev/12 F-L11).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"

#include "features/midiout/MidiOut.h"

#include <cmath>
#include <cstring>
#include <map>

using namespace gapfill;
using namespace gftest;
using namespace gapfill::midiout;

namespace
{
    constexpr double kSr = 48000.0;

    struct Ev { int offset; bool on; int note; int chan; int vel; };

    std::vector<Ev> eventsOf (const juce::MidiBuffer& buf)
    {
        std::vector<Ev> v;

        for (const auto meta : buf)
        {
            const auto m = meta.getMessage();

            if (m.isNoteOn() || m.isNoteOff())
                v.push_back ({ meta.samplePosition, m.isNoteOn(), m.getNoteNumber(), m.getChannel(), m.getVelocity() });
        }

        return v;
    }

    Settings settings (int mode, int note = 36, int chan = 10, int offset = 0)
    {
        Settings s;
        s.mode = mode;
        s.note = note;
        s.channel = chan;
        s.offsetSamples = offset;
        return s;
    }

    /** (type, output position, value, length): value is the confidence (gaps) or velocity (fills). */
    struct Tr { Transition::Type type; std::int64_t pos; int value; int length; };

    TransitionLog logOf (std::initializer_list<Tr> items)
    {
        TransitionLog l;
        for (auto& t : items)
            l.push (t.type, t.pos, (std::uint8_t) t.value, (std::int32_t) t.length);
        return l;
    }

    // one host block of 512 samples, one chunk
    std::vector<Ev> runBlock (MidiOutEmitter& e, const Settings& s, const TransitionLog& log, juce::MidiBuffer& midi,
                              std::int64_t outStart = 0, int block = 512)
    {
        e.beginBlock (s, block);
        e.consume (log, outStart, 0, block);
        e.finish (midi);
        return eventsOf (midi);
    }
}

GF_TEST (MidiOut, off_emits_nothing_and_leaves_the_buffer_alone)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 90), 10);
    const auto log = logOf ({ { Transition::GapOpen, 100, 255, 0 }, { Transition::GapClose, 300, 0, 0 } });
    const auto ev = runBlock (e, settings (ModeOff), log, midi);
    GF_EXPECT (ev.empty());

    int n = 0;
    for (const auto m : midi) { juce::ignoreUnused (m); ++n; }
    GF_EXPECT (n == 1);                                            // the incoming CC is untouched when the feature is off
}

GF_TEST (MidiOut, gap_notes_pair_on_the_configured_note_and_channel)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    const auto log = logOf ({ { Transition::GapOpen, 100, 255, 0 }, { Transition::GapClose, 300, 0, 0 } });
    const auto ev = runBlock (e, settings (ModeGaps, 40, 3), log, midi);

    GF_EXPECT (ev.size() == 2);
    GF_EXPECT (ev[0].on && ev[0].offset == 100 && ev[0].note == 40 && ev[0].chan == 3 && ev[0].vel == 127);
    GF_EXPECT (! ev[1].on && ev[1].offset == 300 && ev[1].note == 40 && ev[1].chan == 3);
    GF_EXPECT (! e.hasSoundingNotes());
}

GF_TEST (MidiOut, velocity_follows_the_gap_confidence_with_a_minimum_of_one)
{
    for (auto [conf, expected] : { std::pair { 0, 1 }, std::pair { 1, 1 }, std::pair { 128, 64 }, std::pair { 255, 127 } })
    {
        MidiOutEmitter e;
        juce::MidiBuffer midi;
        const auto ev = runBlock (e, settings (ModeGaps), logOf ({ { Transition::GapOpen, 10, conf, 0 } }), midi);
        GF_EXPECT_MSG (ev.size() == 1 && ev[0].vel == expected, "conf " + juce::String (conf));
    }
}

GF_TEST (MidiOut, fills_use_the_next_note_and_the_fill_velocity)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    const auto log = logOf ({ { Transition::GapOpen, 20, 200, 0 }, { Transition::FillStart, 50, 90, 400 },
                              { Transition::FillEnd, 450, 0, 0 }, { Transition::GapClose, 460, 0, 0 } });

    const auto fills = runBlock (e, settings (ModeFills, 36), log, midi);
    GF_EXPECT (fills.size() == 2);
    GF_EXPECT (fills[0].on && fills[0].note == 37 && fills[0].vel == 90 && fills[0].offset == 50);
    GF_EXPECT (! fills[1].on && fills[1].note == 37 && fills[1].offset == 450);

    MidiOutEmitter both;
    juce::MidiBuffer m2;
    const auto all = runBlock (both, settings (ModeBoth, 36), log, m2);
    GF_EXPECT (all.size() == 4);
    GF_EXPECT (all[0].note == 36 && all[1].note == 37 && all[2].note == 37 && all[3].note == 36);   // sorted by time
}

GF_TEST (MidiOut, chunk_offsets_and_the_output_alignment_offset_are_applied_and_clamped)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    e.beginBlock (settings (ModeGaps, 36, 10, 100), 512);
    // second chunk of the block: host sample 256 is output position 1000
    e.consume (logOf ({ { Transition::GapOpen, 1100, 255, 0 }, { Transition::GapClose, 1150, 0, 0 } }), 1000, 256, 256);
    e.finish (midi);
    const auto ev = eventsOf (midi);
    GF_EXPECT (ev.size() == 2 && ev[0].offset == 256 + 100 + 100 && ev[1].offset == 256 + 150 + 100);

    // clamped into the block at both ends
    MidiOutEmitter late, early;
    juce::MidiBuffer a, b;
    late.beginBlock (settings (ModeGaps, 36, 10, 5000), 512);
    late.consume (logOf ({ { Transition::GapOpen, 10, 255, 0 } }), 0, 0, 512);
    late.finish (a);
    GF_EXPECT (eventsOf (a).size() == 1 && eventsOf (a)[0].offset == 511);
    early.beginBlock (settings (ModeGaps, 36, 10, -5000), 512);
    early.consume (logOf ({ { Transition::GapOpen, 10, 255, 0 } }), 0, 0, 512);
    early.finish (b);
    GF_EXPECT (eventsOf (b).size() == 1 && eventsOf (b)[0].offset == 0);
}

GF_TEST (MidiOut, never_more_than_sixteen_events_per_block_and_no_note_off_is_dropped)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    e.beginBlock (settings (ModeFills), 512);
    int ons = 0;

    for (int chunk = 0; chunk < 6; ++chunk)
    {
        TransitionLog l;

        for (int k = 0; k < 8; ++k)
        {
            l.push (Transition::FillStart, chunk * 80 + k * 4, 100, 10);
            l.push (Transition::FillEnd, chunk * 80 + k * 4 + 2, 0, 0);
        }

        e.consume (l, 0, 0, 512);
    }

    e.finish (midi);
    const auto ev = eventsOf (midi);
    GF_EXPECT (ev.size() > 0);

    for (auto& x : ev)
        ons += x.on ? 1 : -1;

    GF_EXPECT_MSG (ons == e.getFillsSounding(), "on/off balance " + juce::String (ons));
    GF_EXPECT (e.getDroppedCount() > 0);

    int onCount = 0;
    for (auto& x : ev) onCount += x.on ? 1 : 0;
    GF_EXPECT (onCount <= kMaxEventsPerBlock);
}

GF_TEST (MidiOut, sounding_notes_get_their_note_off_on_flush_mode_note_and_channel_changes)
{
    // flush (panic, bypass, transport stop / jump, release)
    {
        MidiOutEmitter e;
        juce::MidiBuffer m1, m2;
        runBlock (e, settings (ModeBoth, 36, 10), logOf ({ { Transition::GapOpen, 10, 255, 0 }, { Transition::FillStart, 20, 100, 9999 } }), m1);
        GF_EXPECT (e.hasSoundingNotes());
        e.requestFlush();
        const auto ev = runBlock (e, settings (ModeBoth, 36, 10), {}, m2);
        GF_EXPECT (ev.size() == 2);
        GF_EXPECT (! ev[0].on && ! ev[1].on && ev[0].offset == 0 && ev[1].offset == 0);
        GF_EXPECT (! e.hasSoundingNotes());
    }

    // switching the mode off ends the notes first, with the OLD note and channel
    {
        MidiOutEmitter e;
        juce::MidiBuffer m1, m2;
        runBlock (e, settings (ModeGaps, 36, 10), logOf ({ { Transition::GapOpen, 10, 255, 0 } }), m1);
        const auto ev = runBlock (e, settings (ModeOff, 50, 2), {}, m2);
        GF_EXPECT (ev.size() == 1 && ! ev[0].on && ev[0].note == 36 && ev[0].chan == 10);
    }

    // changing note or channel while a note sounds ends it first
    {
        MidiOutEmitter e;
        juce::MidiBuffer m1, m2, m3;
        runBlock (e, settings (ModeGaps, 36, 10), logOf ({ { Transition::GapOpen, 10, 255, 0 } }), m1);
        const auto ev = runBlock (e, settings (ModeGaps, 40, 10), {}, m2);
        GF_EXPECT (ev.size() == 1 && ! ev[0].on && ev[0].note == 36);
        runBlock (e, settings (ModeGaps, 40, 10), logOf ({ { Transition::GapOpen, 10, 255, 0 } }), m3);
        juce::MidiBuffer m4;
        const auto ev2 = runBlock (e, settings (ModeGaps, 40, 11), {}, m4);
        GF_EXPECT (ev2.size() == 1 && ! ev2[0].on && ev2[0].chan == 10);
    }
}

GF_TEST (MidiOut, random_sequences_never_leave_a_note_hanging)
{
    juce::Random rng (11);
    MidiOutEmitter e;
    std::map<int, int> balance;       // channel * 128 + note -> on minus off

    for (int blockIndex = 0; blockIndex < 1000; ++blockIndex)
    {
        const auto mode = rng.nextInt (4);
        Settings s = settings (mode, 30 + rng.nextInt (3), 1 + rng.nextInt (3));
        TransitionLog log;
        int pos = 0;

        for (int k = 0; k < rng.nextInt (6); ++k)
        {
            pos += rng.nextInt (60);
            const auto t = (Transition::Type) rng.nextInt (4);
            log.push (t, pos, (std::uint8_t) (1 + rng.nextInt (127)), 100);
        }

        if (rng.nextInt (7) == 0)
            e.requestFlush();

        juce::MidiBuffer midi;
        e.beginBlock (s, 512);
        e.consume (log, 0, 0, 512);
        e.finish (midi);

        for (auto& x : eventsOf (midi))
            balance[x.chan * 128 + x.note] += x.on ? 1 : -1;
    }

    // quiesce
    juce::MidiBuffer last;
    e.requestFlush();
    e.beginBlock (settings (ModeOff), 512);
    e.finish (last);

    for (auto& x : eventsOf (last))
        balance[x.chan * 128 + x.note] += x.on ? 1 : -1;

    for (auto& [key, value] : balance)
        GF_EXPECT_MSG (value == 0, "note " + juce::String (key) + " balance " + juce::String (value));
}

GF_TEST (MidiOut, output_is_sorted_and_does_not_echo_the_incoming_midi_when_on)
{
    MidiOutEmitter e;
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 90), 10);
    const auto log = logOf ({ { Transition::FillStart, 300, 80, 100 }, { Transition::GapOpen, 100, 255, 0 }, { Transition::GapClose, 200, 0, 0 } });
    const auto ev = runBlock (e, settings (ModeBoth), log, midi);

    for (size_t i = 1; i < ev.size(); ++i)
        GF_EXPECT (ev[i - 1].offset <= ev[i].offset);

    for (const auto m : midi)
        GF_EXPECT (! m.getMessage().isController());
}

GF_TEST (MidiOut, the_declaration_follows_the_build_option)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.producesMidi() == (GAPFILL_MIDI_OUT != 0));
    GF_EXPECT (proc.acceptsMidi());
    GF_EXPECT (! proc.isMidiEffect());
}

GF_TEST (MidiOut, parameters_are_registered_neutral_and_never_randomised)
{
    GapFillAudioProcessor proc;
    auto& v = proc.getValueTreeState();
    GF_EXPECT (v.getRawParameterValue ("midiOutMode")->load() == 0.0f);
    GF_EXPECT (v.getRawParameterValue ("midiOutNote")->load() == 36.0f);
    GF_EXPECT (v.getRawParameterValue ("midiOutChan")->load() == 10.0f);

    for (auto* id : { "midiOutMode", "midiOutNote", "midiOutChan" })
    {
        GF_EXPECT (performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
}

//==============================================================================
namespace
{
    struct ProcRun
    {
        juce::AudioBuffer<float> out;
        std::vector<Ev> events;
        bool ccSurvived = false;
        long allocations = 0;
    };

    ProcRun runProcessor (std::initializer_list<std::pair<const char*, float>> params, bool guardAllocations = false,
                          int block = 512, int bars = 4)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        prepareProcessor (proc, kSr, block, false);
        TestPlayHead ph;
        ph.bpm = 150.0;
        proc.setPlayHead (&ph);

        const auto in = drumLoop (kSr, 150.0, bars);
        ProcRun r;
        r.out.setSize (2, in.getNumSamples());
        r.out.clear();
        juce::AudioBuffer<float> work (2, block);
        juce::MidiBuffer midi;
        midi.ensureSize (2048);                                        // what the JUCE wrappers do

        for (int pos = 0, idx = 0; pos + block <= in.getNumSamples(); pos += block, ++idx)
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 90), 0);   // a learn CC arrives every block

            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, in, ch, pos, block);

            {
                // Only the plug-in call is guarded: the harness itself (vectors, copies) may allocate.
                std::unique_ptr<AllocGuard> guard;

                if (guardAllocations && idx >= 30)
                    guard = std::make_unique<AllocGuard>();

                proc.processBlock (work, midi);

                if (guard != nullptr)
                    r.allocations += guard->count();
            }

            for (int ch = 0; ch < 2; ++ch)
                r.out.copyFrom (ch, pos, work, ch, 0, block);

            for (auto e : eventsOf (midi))
            {
                e.offset += pos;
                r.events.push_back (e);
            }

            for (const auto m : midi)
                r.ccSurvived = r.ccSurvived || m.getMessage().isController();

            ph.advance (block, kSr);
        }

        proc.setPlayHead (nullptr);
        return r;
    }
}

GF_TEST (MidiOut, plugin_off_emits_nothing_and_keeps_the_incoming_cc)
{
    const auto r = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f } });
    GF_EXPECT (r.events.empty());
    GF_EXPECT (r.ccSurvived);
}

GF_TEST (MidiOut, plugin_gap_and_fill_notes_pair_up_and_do_not_change_the_audio)
{
    const auto off = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f } });
    const auto on = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f }, { "midiOutMode", 3.0f } });

    GF_EXPECT (std::memcmp (off.out.getReadPointer (0), on.out.getReadPointer (0), sizeof (float) * (size_t) off.out.getNumSamples()) == 0);
    GF_EXPECT (! on.ccSurvived);
    GF_EXPECT (on.events.size() > 4);

    std::map<int, int> balance;
    int gapOns = 0, fillOns = 0;

    for (size_t i = 0; i < on.events.size(); ++i)
    {
        const auto& e = on.events[i];
        GF_EXPECT (e.chan == 10);
        balance[e.note] += e.on ? 1 : -1;
        gapOns += e.on && e.note == 36 ? 1 : 0;
        fillOns += e.on && e.note == 37 ? 1 : 0;

        if (i > 0)
            GF_EXPECT (on.events[i - 1].offset <= e.offset);
    }

    GF_EXPECT (gapOns > 0 && fillOns > 0);
    GF_EXPECT (balance[36] >= 0 && balance[36] <= 1);                  // at most the last gap is still open
    GF_EXPECT (balance[37] >= 0 && balance[37] <= 2);
}

GF_TEST (MidiOut, plugin_block_size_does_not_move_the_notes)
{
    const auto a = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f }, { "midiOutMode", 3.0f } }, false, 128);
    const auto b = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f }, { "midiOutMode", 3.0f } }, false, 512);
    GF_EXPECT (a.events.size() > 4);

    // block 128 and 512 differ only at the very end of the render (the last partial block is dropped by the helper)
    const auto n = juce::jmin (a.events.size(), b.events.size());
    GF_EXPECT (n > 4);

    for (size_t i = 0; i + 2 < n; ++i)
        GF_EXPECT_MSG (a.events[i].offset == b.events[i].offset && a.events[i].note == b.events[i].note && a.events[i].on == b.events[i].on,
                       "event " + juce::String ((int) i));
}

GF_TEST (MidiOut, plugin_audio_thread_does_not_allocate_with_a_presized_host_buffer)
{
    const auto r = runProcessor ({ { "fill", 100.0f }, { "density", 100.0f }, { "midiOutMode", 3.0f } }, true);
    GF_EXPECT (r.events.size() > 4);
    GF_EXPECT_MSG (r.allocations == 0, "allocations " + juce::String ((int) r.allocations));
}
