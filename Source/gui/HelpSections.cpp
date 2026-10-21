// Core help text. Each section registers itself with HelpRegistry; the order
// numbers are spaced so a feature can slot a section in between (features use
// 1000-8999, Troubleshooting and About stay last).

#include "HelpRegistry.h"
#include "LookAndFeel.h"   // versionString()

namespace gapfill
{

namespace
{
    const HelpRegistrar helpSection100 (100, "", [] (HelpWriter& w)
    {
        w.title ("GapFill - what it actually does");
        w.versionLine ("version " + versionString());
        w.body ("Most effects process everything they are given. GapFill does the opposite: it listens "
              "for the musical events in your audio, works out where the usable spaces between those "
              "events are, and puts generated ear candy only in those spaces - while actively "
              "protecting the original hits.");
        w.body ("The material for those fills is cut from your own recent audio, so what appears in the "
              "gaps still sounds like your track rather than a canned sample.");
        w.body ("Rule one inside GapFill is simple: the original hit is more important than the fill.");
    });

    const HelpRegistrar helpSection200 (200, "60-second quick start", [] (HelpWriter& w)
    {
        w.body ("1.  Insert GapFill on a drum loop, drum bus or percussion track and press play.");
        w.body ("2.  Watch the visualiser. White lines are detected events, the blue tint is space "
              "GapFill considers usable, the amber strip is protection, the bar underneath shows fills.");
        w.body ("3.  Turn FILL up from 0%. Around 25-35% you get occasional ear candy between hits.");
        w.body ("4.  Raise INTENSITY and COMPLEXITY for bigger, more elaborate fills.");
        w.body ("5.  If the original hits start to lose definition, raise TRANSIENT PROTECT.");
        w.body ("6.  If the kick or sub gets cluttered, set LOW PROTECT to around 100 Hz.");
        w.body ("7.  Nothing happening? Raise SENSITIVITY, raise GAP THRESHOLD, or lower MINIMUM GAP.");
    });

    const HelpRegistrar helpSection300 (300, "Reading the visualiser", [] (HelpWriter& w)
    {
        w.entry ("Waveform", "the incoming energy of the track, drawn around the centre line.");
        w.entry ("White vertical lines", "events the detector is confident about.");
        w.entry ("Blue tint", "regions currently eligible as space.");
        w.entry ("Blue curve", "gap confidence, from 0 (definitely busy) to 1 (clearly usable space). "
                             "GapFill does not use a simple on/off gate - this continuous value also "
                             "decides how loud, how long and how complex a fill may be.");
        w.entry ("Amber strip", "protection is active - a fill is being ducked, delayed or faded.");
        w.entry ("Coloured bar", "a fill is playing. The name of the algorithm is shown on the right. "
                               "In Spectral mode the colour tells you which lane it went into.");
        w.entry ("Right-hand strip", "the lookahead window: audio GapFill has already analysed but you "
                                   "have not heard yet.");
    });

    const HelpRegistrar helpSection400 (400, "Detection", [] (HelpWriter& w)
    {
        w.entry ("Mode", "TRANSIENT follows attacks and works best on drums, percussion, plucks and "
                       "rhythmic bass. PHRASE uses slower envelopes plus spectral change, so it "
                       "reacts to phrase endings instead of every consonant - use it for vocals, "
                       "melodies, guitars and pads. SPECTRAL analyses low, mid and high separately, "
                       "so a busy kick does not stop GapFill using the empty top end.");
        w.entry ("Detector Source", "MAIN listens to the track GapFill is on. SIDECHAIN uses the "
                                  "sidechain input purely for timing - the fills are still built from "
                                  "this track's audio. HYBRID takes the spaces from the sidechain but "
                                  "still protects important events on this track.");
        w.entry ("Sensitivity", "how easily an event is detected. Too low and quiet hits are missed; too "
                              "high and GapFill triggers on noise and tails.");
        w.entry ("Gap Threshold", "how far energy must fall, relative to the local musical reference "
                                "level, before a region counts as space. Higher values accept less "
                                "silence as a gap. Digital silence is never required.");
        w.entry ("Minimum Gap", "spaces shorter than this never get a fill. This is the main control for "
                              "keeping GapFill out of busy passages.");
        w.entry ("Max Fill", "the longest a single fill may last. Can be tempo-synced with SYNC MAX FILL.");
        w.entry ("Gap Confirm", "how long GapFill waits before deciding an event has really ended. Short "
                              "values react instantly; long values only accept settled pauses. Note "
                              "that the opposite direction is always instant - a new hit collapses the "
                              "gap immediately.");
        w.entry ("Protect Hold", "a safe region straight after each detected hit where no fill may start, "
                               "so the body of the hit is never covered.");
        w.entry ("Retrig Guard", "minimum time between two detected events, so one hit is not counted "
                               "several times.");
        w.entry ("Lookahead", "lets GapFill see what is coming, so fills can be faded out before the next "
                            "important event. This is reported to the host as plugin latency and the "
                            "dry path is delayed to match. Set to 0 ms for zero-latency work.");
        w.entry ("Audition Detector", "monitor exactly what the detector hears - the main track, or the "
                                    "sidechain when sidechain detection is active. Very useful for "
                                    "setting Sensitivity.");
    });

    const HelpRegistrar helpSection500 (500, "The five macros", [] (HelpWriter& w)
    {
        w.entry ("Fill", "the primary amount. It is a probability, not a wet/dry control: 0% generates "
                       "nothing, 10-25% gives occasional subtle candy, 25-50% is clearly audible while "
                       "the groove survives, 50-75% is active transformation and 75-100% fills nearly "
                       "every usable space.");
        w.entry ("Intensity", "how hard each fill is processed - pitch range, drive, crushing, filter "
                            "resonance, stereo movement. It is deliberately not a level control.");
        w.entry ("Complexity", "how elaborate a fill may become. Low means a single simple transformation. "
                             "High stacks up to three carefully chosen extra transformations and unlocks "
                             "the granular and spectral algorithms. It does not make anything louder.");
        w.entry ("Chaos", "variation and unpredictability: algorithm choice, timing, pitch, grain "
                        "behaviour and stereo movement. Even at 100% everything stays bounded.");
        w.entry ("Density", "how many of the eligible spaces actually get used. Separate from Wet, so you "
                          "can have loud but rare fills, or quiet but constant ones.");
    });

    const HelpRegistrar helpSection600 (600, "Fill types", [] (HelpWriter& w)
    {
        w.body ("AUTO weighs every enabled algorithm against the length of the gap, how hard the hit that "
              "opened it was, the macros and which algorithms were used recently. It will not choose "
              "something that cannot fit, and it avoids repeating itself unless Chaos is low.");
        w.entry ("Reverse", "plays a recent fragment backwards, typically swelling into the next hit.");
        w.entry ("Stutter", "repeats a short slice on a musical subdivision, optionally accelerating, "
                          "decelerating or stepping in pitch.");
        w.entry ("Delay", "a short echo pattern built from the material right before the gap, with "
                        "optional ping-pong. It always ends inside the gap.");
        w.entry ("Granular", "a cloud of short grains taken from the last second or so of audio, with "
                           "pitch, position, pan and reverse variation.");
        w.entry ("Freeze", "captures a short region and sustains it with a proper crossfaded loop.");
        w.entry ("Pitch Rise / Pitch Fall", "a fragment whose pitch glides up or down across the gap.");
        w.entry ("Filter Sweep", "animates a low-pass, high-pass or band-pass sweep across a captured slice.");
        w.entry ("Texture", "synthesises a short noise texture shaped by the track's own band energies, "
                          "so it belongs to the material rather than sounding pasted on.");
        w.entry ("Bit Crush", "controlled digital degradation - bit reduction plus sample-rate decimation.");
        w.entry ("Distort", "saturation and foldback applied to the fill only. The protected original hit "
                          "stays clean.");
        w.entry ("Scramble", "rearranges small slices of the captured material, keeping the timing musical "
                           "rather than purely random.");
        w.entry ("Spectral Smear", "an FFT smear that removes transients and leaves ghostly transitional "
                                 "material. Only one spectral fill runs at a time to bound CPU use.");
        w.entry ("Comb", "a short tuned resonance excited by the captured audio. It decays inside the gap "
                       "instead of ringing forever.");
        w.entry ("Tape Stop", "replays the hit that opened the gap while the virtual tape deck brakes to a "
                            "halt, losing speed, pitch and top end. Intensity sets how hard it brakes; "
                            "Chaos adds a little wow.");
        w.entry ("CAPTURE", "(Creative panel) freezes the fill source. While it is on, every fill is "
                          "built from the audio heard at the moment you pressed it, even as the track "
                          "moves on - detection, protection and the dry signal keep following the live "
                          "input. Turn it off to release. It needs half a second of audio before it can "
                          "latch, and it is automatable and MIDI-learnable for live performance.");
    });

    const HelpRegistrar helpSection650 (650, "Auto algorithm weights", [] (HelpWriter& w)
    {
        w.body ("Each algorithm has a weight from 0 to 100%. Zero disables it completely. The weights only "
              "matter when Fill Type is AUTO. The RHYTHMIC and TEXTURAL buttons are quick starting "
              "points: rhythmic favours reverses, stutters, delays and crushing, textural favours "
              "granular, freeze, spectral and comb.");
    });

    const HelpRegistrar helpSection700 (700, "Protection", [] (HelpWriter& w)
    {
        w.body ("This is what separates GapFill from a random glitch effect. All of it acts on the "
              "generated material only - the dry signal is never touched.");
        w.entry ("Transient", "ducks generated material around strong detected attacks so the original "
                            "hits keep their impact.");
        w.entry ("Low Protect", "high-passes the fills. Around 80-150 Hz keeps kick and sub clean while "
                              "everything above still gets treated. Fully left is Off.");
        w.entry ("Center", "pushes fills progressively out to the sides, protecting centred kick, snare, "
                         "bass and lead vocal. At high settings the wet material is almost entirely on "
                         "the sides.");
        w.entry ("Next Event", "uses lookahead to fade or duck the fill as the next important event "
                             "approaches, and at high settings fades it to silence exactly on the hit.");
        w.entry ("Sidechain", "when sidechain detection is active, strong sidechain hits force generated "
                            "material out of the way. Think event-aware masking rather than a static "
                            "sidechain compressor.");
    });

    const HelpRegistrar helpSection750 (750, "Spectral lanes", [] (HelpWriter& w)
    {
        w.body ("In SPECTRAL detector mode the signal is split with proper Linkwitz-Riley crossovers into "
              "LOW, MID and HIGH, and each lane finds its own spaces. A kick can keep the low lane busy "
              "while the high lane is wide open, and GapFill will place a high-frequency fill there "
              "without touching the kick's energy.");
        w.body ("Each lane has its own enable, fill amount, intensity and probability. Generated material "
              "is band-limited to the lane it was scheduled in. Use SOLO / AUDITION to listen to one "
              "lane on its own and hear exactly what that detector sees.");
    });

    const HelpRegistrar helpSection800 (800, "Sidechain routing", [] (HelpWriter& w)
    {
        w.body ("Route another track into GapFill's sidechain input in your DAW, then set Detector Source "
              "to SIDECHAIN or HYBRID. The sidechain decides WHEN there is space; the track GapFill is "
              "on still provides WHAT the fills are made of.");
        w.entry ("GapFill on bass, drums to sidechain", "manipulated bass fragments appear between the "
                                                      "drum hits.");
        w.entry ("GapFill on vocals, drums to sidechain", "vocal echoes, grains and reverses land "
                                                        "rhythmically around the drums.");
        w.entry ("GapFill on a synth, kick to sidechain", "generated synth material stays out of the kick.");
        w.body ("If the sidechain is selected but nothing is connected (or it has been silent for several "
              "seconds), GapFill falls back to main detection and the status line tells you so. Audio "
              "is never interrupted.");
    });

    const HelpRegistrar helpSection850 (850, "Seed, Random and Reset", [] (HelpWriter& w)
    {
        w.entry ("Seed", "GapFill's randomness is deterministic. With a running transport the decision for "
                       "a gap is derived from the seed and the musical position, so the same bar plays "
                       "back the same way every time and a reopened project sounds identical.");
        w.entry ("RANDOM", "generates a genuinely new creative setup on every press - new macros, new "
                         "algorithm weights and a new seed that is guaranteed to differ from the "
                         "previous one. Detector settings and mix levels are left alone.");
        w.entry ("RANDOM ALL", "also randomises detection and protection within safe musical ranges.");
        w.entry ("SURPRISE", "jumps to one of eight curated creative profiles - Subtle Sparkle, Drum "
                           "Edits, Glitch Storm, Dub Echoes, Ambient Haze, Pitch Bender, Industrial Grit "
                           "or Tape Breakdown - with fresh variation each time and never the same profile "
                           "twice in a row. Unlike RANDOM, the result is always coherent; detector, "
                           "protection and mix are untouched.");
        w.entry ("UNDO", "steps back through the last 32 whole-sound changes: RANDOM, RANDOM ALL, "
                       "SURPRISE, RESET, preset changes and file loads.");
        w.entry ("RESET", "returns every parameter to its default value.");
        w.body ("Randomising never touches Dry, Wet, Output, Lookahead or Bypass, so it cannot produce a "
              "dangerous level or an unexpected latency change.");
    });

    const HelpRegistrar helpSection900 (900, "Mix, metering and latency", [] (HelpWriter& w)
    {
        w.entry ("Dry / Wet", "independent levels for the original and the generated material.");
        w.entry ("Output", "final trim, -24 to +12 dB.");
        w.entry ("Wet Width / Mono Wet", "stereo width of the fills only. Mono Wet collapses them for "
                                       "maximum mono compatibility.");
        w.entry ("Meters", "input, fill and output levels, plus the live gap-confidence bar.");
        w.entry ("Latency", "equal to the Lookahead setting and reported to the host, which compensates "
                          "automatically. The dry path is delayed by the same amount, so the dry and "
                          "wet paths always stay sample-aligned and bypass stays in time.");
    });

    const HelpRegistrar helpSection950 (950, "Starting points by source", [] (HelpWriter& w)
    {
        w.entry ("Drum loop / drum bus", "Transient mode, Min Gap 50-80 ms, Fill 30-55%, Transient "
                                       "Protect 80%, Low Protect 80-120 Hz.");
        w.entry ("Drum & bass / breakcore", "Transient mode, Min Gap 25-40 ms, Fill 60-90%, Complexity "
                                          "55-85%, Chaos 40-70%, Transient Protect 85-90%.");
        w.entry ("Vocals", "Phrase mode, Min Gap 180-250 ms, Gap Confirm 150 ms, Fill 30-40%, Next Event "
                         "Protect 95%.");
        w.entry ("Bass / synth with drums in the sidechain", "Sidechain source, Low Protect 100-120 Hz, "
                                                           "Sidechain Protect 90%+.");
        w.entry ("Full stems or master", "Spectral mode, high lane only, Fill 20-35%, Low Protect on. "
                                       "Treat this as a creative effect rather than a mastering tool.");
    });

    const HelpRegistrar helpSection990 (990, "Options", [] (HelpWriter& w)
    {
        w.entry ("Reduced Motion", "disables knob/value easing, hover fades and short action flashes. "
                                 "Meters and the gap visualizer remain live because they communicate "
                                 "signal state.");
        w.entry ("Keyboard Navigation", "is off by default so opening GapFill does not capture DAW shortcuts. "
                                      "Enable it to use Tab / Shift+Tab between controls, arrow keys on "
                                      "sliders and menus, and Space/Return on buttons.");
        w.entry ("Hover Tooltips", "can be turned off globally without changing the sound.");
    });

    const HelpRegistrar helpSection9000 (9000, "Troubleshooting", [] (HelpWriter& w)
    {
        w.entry ("No fills at all", "Fill is above 0%? Are any algorithms enabled in the weights panel? Is "
                                  "Minimum Gap longer than the actual spaces? Try raising Sensitivity "
                                  "and Gap Threshold.");
        w.entry ("Fills cover the hits", "raise Transient Protect, Protect Hold and Next Event Protect, "
                                       "and add some Lookahead.");
        w.entry ("Too busy", "lower Density before lowering Fill - you keep the character but get fewer "
                           "events. Raising Minimum Gap also helps.");
        w.entry ("Muddy low end", "set Low Protect to 100-150 Hz, and consider Center Protect.");
        w.entry ("Triggering on noise or reverb tails", "lower Sensitivity, raise Retrig Guard, and lower "
                                                      "Gap Threshold so quiet tails still count as busy.");
        w.entry ("Everything changed after reopening the project", "check the Seed was saved - it is part "
                                                                 "of the plugin state and should restore "
                                                                 "automatically.");
        w.entry ("Preset file will not open", "GapFill only accepts valid .gapfill parameter-state files up to "
                                             "1 MB. A rejected file leaves the current sound unchanged and the "
                                             "error dialog explains the reason.");
        w.entry ("MIDI learn", "right-click a parameter and choose MIDI learn, then move a controller. "
                             "The status line shows the armed parameter and countdown. Learn cancels "
                             "automatically after 10 seconds, or right-click the same control again to cancel.");
    });

    const HelpRegistrar helpSection9100 (9100, "About", [] (HelpWriter& w)
    {
        w.body ("GapFill is a Circuit Drift Labs audio effect. Project documentation, source and release "
              "information live in the repository.");
        w.entry ("Formats", "VST3, CLAP and Standalone.");
        w.entry ("Build platforms", "Windows x64 and Linux x86_64. Release validation status is tracked "
                                  "in docs/VALIDATION.md in the repository.");
        w.entry ("Issue reports", "use the GitHub repository issue tracker. It is a project issue queue, "
                                "not a staffed support service.");
        w.body ("Circuit Drift Labs: https://djshellshoxxx.github.io/circuitdriftlabs/");
    });

}

} // namespace gapfill
