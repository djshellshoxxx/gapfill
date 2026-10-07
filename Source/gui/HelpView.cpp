#include "HelpView.h"

namespace gapfill
{

namespace
{
    juce::Font titleFont()   { return juce::Font (juce::FontOptions (20.0f)).boldened(); }
    juce::Font headingFont() { return juce::Font (juce::FontOptions (14.5f)).boldened(); }
    juce::Font bodyFont()    { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font termFont()    { return juce::Font (juce::FontOptions (13.0f)).boldened(); }
}

//==============================================================================
HelpContent::HelpContent()
{
    auto title = [this] (const juce::String& s)
    {
        text.append ("\n" + s + "\n", titleFont(), colours::text);
    };

    auto heading = [this] (const juce::String& s)
    {
        text.append ("\n" + s + "\n", headingFont(), colours::accent);
    };

    auto body = [this] (const juce::String& s)
    {
        text.append (s + "\n", bodyFont(), colours::text.withAlpha (0.86f));
    };

    auto entry = [this] (const juce::String& term, const juce::String& description)
    {
        text.append ("  " + term + "  ", termFont(), colours::accentWarm);
        text.append (description + "\n", bodyFont(), colours::text.withAlpha (0.86f));
    };

    auto versionLine = [this] (const juce::String& s)
    {
        text.append (s + "\n", fonts::mono (10.0f), colours::textMuted);
    };

    text.setLineSpacing (3.0f);

    //--------------------------------------------------------------------------
    title ("GapFill - what it actually does");
    versionLine ("version " + versionString());
    body ("Most effects process everything they are given. GapFill does the opposite: it listens "
          "for the musical events in your audio, works out where the usable spaces between those "
          "events are, and puts generated ear candy only in those spaces - while actively "
          "protecting the original hits.");
    body ("The material for those fills is cut from your own recent audio, so what appears in the "
          "gaps still sounds like your track rather than a canned sample.");
    body ("Rule one inside GapFill is simple: the original hit is more important than the fill.");

    //--------------------------------------------------------------------------
    heading ("60-second quick start");
    body ("1.  Insert GapFill on a drum loop, drum bus or percussion track and press play.");
    body ("2.  Watch the visualiser. White lines are detected events, the blue tint is space "
          "GapFill considers usable, the amber strip is protection, the bar underneath shows fills.");
    body ("3.  Turn FILL up from 0%. Around 25-35% you get occasional ear candy between hits.");
    body ("4.  Raise INTENSITY and COMPLEXITY for bigger, more elaborate fills.");
    body ("5.  If the original hits start to lose definition, raise TRANSIENT PROTECT.");
    body ("6.  If the kick or sub gets cluttered, set LOW PROTECT to around 100 Hz.");
    body ("7.  Nothing happening? Raise SENSITIVITY, raise GAP THRESHOLD, or lower MINIMUM GAP.");

    //--------------------------------------------------------------------------
    heading ("Reading the visualiser");
    entry ("Waveform", "the incoming energy of the track, drawn around the centre line.");
    entry ("White vertical lines", "events the detector is confident about.");
    entry ("Blue tint", "regions currently eligible as space.");
    entry ("Blue curve", "gap confidence, from 0 (definitely busy) to 1 (clearly usable space). "
                         "GapFill does not use a simple on/off gate - this continuous value also "
                         "decides how loud, how long and how complex a fill may be.");
    entry ("Amber strip", "protection is active - a fill is being ducked, delayed or faded.");
    entry ("Coloured bar", "a fill is playing. The name of the algorithm is shown on the right. "
                           "In Spectral mode the colour tells you which lane it went into.");
    entry ("Right-hand strip", "the lookahead window: audio GapFill has already analysed but you "
                               "have not heard yet.");

    //--------------------------------------------------------------------------
    heading ("Detection");
    entry ("Mode", "TRANSIENT follows attacks and works best on drums, percussion, plucks and "
                   "rhythmic bass. PHRASE uses slower envelopes plus spectral change, so it "
                   "reacts to phrase endings instead of every consonant - use it for vocals, "
                   "melodies, guitars and pads. SPECTRAL analyses low, mid and high separately, "
                   "so a busy kick does not stop GapFill using the empty top end.");
    entry ("Detector Source", "MAIN listens to the track GapFill is on. SIDECHAIN uses the "
                              "sidechain input purely for timing - the fills are still built from "
                              "this track's audio. HYBRID takes the spaces from the sidechain but "
                              "still protects important events on this track.");
    entry ("Sensitivity", "how easily an event is detected. Too low and quiet hits are missed; too "
                          "high and GapFill triggers on noise and tails.");
    entry ("Gap Threshold", "how far energy must fall, relative to the local musical reference "
                            "level, before a region counts as space. Higher values accept less "
                            "silence as a gap. Digital silence is never required.");
    entry ("Minimum Gap", "spaces shorter than this never get a fill. This is the main control for "
                          "keeping GapFill out of busy passages.");
    entry ("Max Fill", "the longest a single fill may last. Can be tempo-synced with SYNC MAX FILL.");
    entry ("Gap Confirm", "how long GapFill waits before deciding an event has really ended. Short "
                          "values react instantly; long values only accept settled pauses. Note "
                          "that the opposite direction is always instant - a new hit collapses the "
                          "gap immediately.");
    entry ("Protect Hold", "a safe region straight after each detected hit where no fill may start, "
                           "so the body of the hit is never covered.");
    entry ("Retrig Guard", "minimum time between two detected events, so one hit is not counted "
                           "several times.");
    entry ("Lookahead", "lets GapFill see what is coming, so fills can be faded out before the next "
                        "important event. This is reported to the host as plugin latency and the "
                        "dry path is delayed to match. Set to 0 ms for zero-latency work.");
    entry ("Audition Detector", "monitor exactly what the detector hears - the main track, or the "
                                "sidechain when sidechain detection is active. Very useful for "
                                "setting Sensitivity.");

    //--------------------------------------------------------------------------
    heading ("The five macros");
    entry ("Fill", "the primary amount. It is a probability, not a wet/dry control: 0% generates "
                   "nothing, 10-25% gives occasional subtle candy, 25-50% is clearly audible while "
                   "the groove survives, 50-75% is active transformation and 75-100% fills nearly "
                   "every usable space.");
    entry ("Intensity", "how hard each fill is processed - pitch range, drive, crushing, filter "
                        "resonance, stereo movement. It is deliberately not a level control.");
    entry ("Complexity", "how elaborate a fill may become. Low means a single simple transformation. "
                         "High stacks up to three carefully chosen extra transformations and unlocks "
                         "the granular and spectral algorithms. It does not make anything louder.");
    entry ("Chaos", "variation and unpredictability: algorithm choice, timing, pitch, grain "
                    "behaviour and stereo movement. Even at 100% everything stays bounded.");
    entry ("Density", "how many of the eligible spaces actually get used. Separate from Wet, so you "
                      "can have loud but rare fills, or quiet but constant ones.");

    //--------------------------------------------------------------------------
    heading ("Fill types");
    body ("AUTO weighs every enabled algorithm against the length of the gap, how hard the hit that "
          "opened it was, the macros and which algorithms were used recently. It will not choose "
          "something that cannot fit, and it avoids repeating itself unless Chaos is low.");
    entry ("Reverse", "plays a recent fragment backwards, typically swelling into the next hit.");
    entry ("Stutter", "repeats a short slice on a musical subdivision, optionally accelerating, "
                      "decelerating or stepping in pitch.");
    entry ("Delay", "a short echo pattern built from the material right before the gap, with "
                    "optional ping-pong. It always ends inside the gap.");
    entry ("Granular", "a cloud of short grains taken from the last second or so of audio, with "
                       "pitch, position, pan and reverse variation.");
    entry ("Freeze", "captures a short region and sustains it with a proper crossfaded loop.");
    entry ("Pitch Rise / Pitch Fall", "a fragment whose pitch glides up or down across the gap.");
    entry ("Filter Sweep", "animates a low-pass, high-pass or band-pass sweep across a captured slice.");
    entry ("Texture", "synthesises a short noise texture shaped by the track's own band energies, "
                      "so it belongs to the material rather than sounding pasted on.");
    entry ("Bit Crush", "controlled digital degradation - bit reduction plus sample-rate decimation.");
    entry ("Distort", "saturation and foldback applied to the fill only. The protected original hit "
                      "stays clean.");
    entry ("Scramble", "rearranges small slices of the captured material, keeping the timing musical "
                       "rather than purely random.");
    entry ("Spectral Smear", "an FFT smear that removes transients and leaves ghostly transitional "
                             "material. Only one spectral fill runs at a time to bound CPU use.");
    entry ("Comb", "a short tuned resonance excited by the captured audio. It decays inside the gap "
                   "instead of ringing forever.");
    entry ("Tape Stop", "replays the hit that opened the gap while the virtual tape deck brakes to a "
                        "halt, losing speed, pitch and top end. Intensity sets how hard it brakes; "
                        "Chaos adds a little wow.");
    entry ("CAPTURE", "(Creative panel) freezes the fill source. While it is on, every fill is "
                      "built from the audio heard at the moment you pressed it, even as the track "
                      "moves on - detection, protection and the dry signal keep following the live "
                      "input. Turn it off to release. It needs half a second of audio before it can "
                      "latch, and it is automatable and MIDI-learnable for live performance.");

    //--------------------------------------------------------------------------
    heading ("Auto algorithm weights");
    body ("Each algorithm has a weight from 0 to 100%. Zero disables it completely. The weights only "
          "matter when Fill Type is AUTO. The RHYTHMIC and TEXTURAL buttons are quick starting "
          "points: rhythmic favours reverses, stutters, delays and crushing, textural favours "
          "granular, freeze, spectral and comb.");

    //--------------------------------------------------------------------------
    heading ("Protection");
    body ("This is what separates GapFill from a random glitch effect. All of it acts on the "
          "generated material only - the dry signal is never touched.");
    entry ("Transient", "ducks generated material around strong detected attacks so the original "
                        "hits keep their impact.");
    entry ("Low Protect", "high-passes the fills. Around 80-150 Hz keeps kick and sub clean while "
                          "everything above still gets treated. Fully left is Off.");
    entry ("Center", "pushes fills progressively out to the sides, protecting centred kick, snare, "
                     "bass and lead vocal. At high settings the wet material is almost entirely on "
                     "the sides.");
    entry ("Next Event", "uses lookahead to fade or duck the fill as the next important event "
                         "approaches, and at high settings fades it to silence exactly on the hit.");
    entry ("Sidechain", "when sidechain detection is active, strong sidechain hits force generated "
                        "material out of the way. Think event-aware masking rather than a static "
                        "sidechain compressor.");

    //--------------------------------------------------------------------------
    heading ("Spectral lanes");
    body ("In SPECTRAL detector mode the signal is split with proper Linkwitz-Riley crossovers into "
          "LOW, MID and HIGH, and each lane finds its own spaces. A kick can keep the low lane busy "
          "while the high lane is wide open, and GapFill will place a high-frequency fill there "
          "without touching the kick's energy.");
    body ("Each lane has its own enable, fill amount, intensity and probability. Generated material "
          "is band-limited to the lane it was scheduled in. Use SOLO / AUDITION to listen to one "
          "lane on its own and hear exactly what that detector sees.");

    //--------------------------------------------------------------------------
    heading ("Sidechain routing");
    body ("Route another track into GapFill's sidechain input in your DAW, then set Detector Source "
          "to SIDECHAIN or HYBRID. The sidechain decides WHEN there is space; the track GapFill is "
          "on still provides WHAT the fills are made of.");
    entry ("GapFill on bass, drums to sidechain", "manipulated bass fragments appear between the "
                                                  "drum hits.");
    entry ("GapFill on vocals, drums to sidechain", "vocal echoes, grains and reverses land "
                                                    "rhythmically around the drums.");
    entry ("GapFill on a synth, kick to sidechain", "generated synth material stays out of the kick.");
    body ("If the sidechain is selected but nothing is connected (or it has been silent for several "
          "seconds), GapFill falls back to main detection and the status line tells you so. Audio "
          "is never interrupted.");

    //--------------------------------------------------------------------------
    heading ("Seed, Random and Reset");
    entry ("Seed", "GapFill's randomness is deterministic. With a running transport the decision for "
                   "a gap is derived from the seed and the musical position, so the same bar plays "
                   "back the same way every time and a reopened project sounds identical.");
    entry ("RANDOM", "generates a genuinely new creative setup on every press - new macros, new "
                     "algorithm weights and a new seed that is guaranteed to differ from the "
                     "previous one. Detector settings and mix levels are left alone.");
    entry ("RANDOM ALL", "also randomises detection and protection within safe musical ranges.");
    entry ("SURPRISE", "jumps to one of eight curated creative profiles - Subtle Sparkle, Drum "
                       "Edits, Glitch Storm, Dub Echoes, Ambient Haze, Pitch Bender, Industrial Grit "
                       "or Tape Breakdown - with fresh variation each time and never the same profile "
                       "twice in a row. Unlike RANDOM, the result is always coherent; detector, "
                       "protection and mix are untouched.");
    entry ("UNDO", "steps back through the last 32 whole-sound changes: RANDOM, RANDOM ALL, "
                   "SURPRISE, RESET, preset changes and file loads.");
    entry ("RESET", "returns every parameter to its default value.");
    body ("Randomising never touches Dry, Wet, Output, Lookahead or Bypass, so it cannot produce a "
          "dangerous level or an unexpected latency change.");

    //--------------------------------------------------------------------------
    heading ("Mix, metering and latency");
    entry ("Dry / Wet", "independent levels for the original and the generated material.");
    entry ("Output", "final trim, -24 to +12 dB.");
    entry ("Wet Width / Mono Wet", "stereo width of the fills only. Mono Wet collapses them for "
                                   "maximum mono compatibility.");
    entry ("Meters", "input, fill and output levels, plus the live gap-confidence bar.");
    entry ("Latency", "equal to the Lookahead setting and reported to the host, which compensates "
                      "automatically. The dry path is delayed by the same amount, so the dry and "
                      "wet paths always stay sample-aligned and bypass stays in time.");

    //--------------------------------------------------------------------------
    heading ("Starting points by source");
    entry ("Drum loop / drum bus", "Transient mode, Min Gap 50-80 ms, Fill 30-55%, Transient "
                                   "Protect 80%, Low Protect 80-120 Hz.");
    entry ("Drum & bass / breakcore", "Transient mode, Min Gap 25-40 ms, Fill 60-90%, Complexity "
                                      "55-85%, Chaos 40-70%, Transient Protect 85-90%.");
    entry ("Vocals", "Phrase mode, Min Gap 180-250 ms, Gap Confirm 150 ms, Fill 30-40%, Next Event "
                     "Protect 95%.");
    entry ("Bass / synth with drums in the sidechain", "Sidechain source, Low Protect 100-120 Hz, "
                                                       "Sidechain Protect 90%+.");
    entry ("Full stems or master", "Spectral mode, high lane only, Fill 20-35%, Low Protect on. "
                                   "Treat this as a creative effect rather than a mastering tool.");

    //--------------------------------------------------------------------------
    heading ("Options");
    entry ("Reduced Motion", "disables knob/value easing, hover fades and short action flashes. "
                             "Meters and the gap visualizer remain live because they communicate "
                             "signal state.");
    entry ("Keyboard Navigation", "is off by default so opening GapFill does not capture DAW shortcuts. "
                                  "Enable it to use Tab / Shift+Tab between controls, arrow keys on "
                                  "sliders and menus, and Space/Return on buttons.");
    entry ("Hover Tooltips", "can be turned off globally without changing the sound.");

    //--------------------------------------------------------------------------
    heading ("Troubleshooting");
    entry ("No fills at all", "Fill is above 0%? Are any algorithms enabled in the weights panel? Is "
                              "Minimum Gap longer than the actual spaces? Try raising Sensitivity "
                              "and Gap Threshold.");
    entry ("Fills cover the hits", "raise Transient Protect, Protect Hold and Next Event Protect, "
                                   "and add some Lookahead.");
    entry ("Too busy", "lower Density before lowering Fill - you keep the character but get fewer "
                       "events. Raising Minimum Gap also helps.");
    entry ("Muddy low end", "set Low Protect to 100-150 Hz, and consider Center Protect.");
    entry ("Triggering on noise or reverb tails", "lower Sensitivity, raise Retrig Guard, and lower "
                                                  "Gap Threshold so quiet tails still count as busy.");
    entry ("Everything changed after reopening the project", "check the Seed was saved - it is part "
                                                             "of the plugin state and should restore "
                                                             "automatically.");
    entry ("Preset file will not open", "GapFill only accepts valid .gapfill parameter-state files up to "
                                         "1 MB. A rejected file leaves the current sound unchanged and the "
                                         "error dialog explains the reason.");
    entry ("MIDI learn", "right-click a parameter and choose MIDI learn, then move a controller. "
                         "The status line shows the armed parameter and countdown. Learn cancels "
                         "automatically after 10 seconds, or right-click the same control again to cancel.");

    //--------------------------------------------------------------------------
    heading ("About");
    body ("GapFill is a Circuit Drift Labs audio effect. Project documentation, source and release "
          "information live in the repository.");
    entry ("Formats", "VST3, CLAP and Standalone.");
    entry ("Build platforms", "Windows x64 and Linux x86_64. Release validation status is tracked "
                              "in docs/VALIDATION.md in the repository.");
    entry ("Issue reports", "use the GitHub repository issue tracker. It is a project issue queue, "
                            "not a staffed support service.");
    body ("Circuit Drift Labs: https://djshellshoxxx.github.io/circuitdriftlabs/");

    text.append ("\n", bodyFont(), colours::text);
}

void HelpContent::rebuildLayout (float width)
{
    if (std::abs (width - lastWidth) < 1.0f)
        return;

    lastWidth = width;
    layout.createLayout (text, juce::jmax (120.0f, width));
}

void HelpContent::resized()
{
    rebuildLayout ((float) getWidth() - 32.0f);
}

void HelpContent::paint (juce::Graphics& g)
{
    layout.draw (g, juce::Rectangle<float> (16.0f, 10.0f,
                                            (float) getWidth() - 32.0f,
                                            (float) getHeight() - 20.0f));
}

//==============================================================================
HelpView::HelpView()
{
    setOpaque (false);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

void HelpView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Matches the options overlay: the shared panel radius and the house shadow,
    // rather than a radius of its own.
    GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);

    g.setColour (juce::Colour (0xf20b0d11));
    g.fillRoundedRectangle (b, metrics::panelRadius);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.0f);

    g.setColour (colours::accent);
    g.setFont (juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.18f));
    g.drawText ("HELP", b.removeFromTop (34.0f).withTrimmedLeft (18.0f).toNearestInt(),
                juce::Justification::centredLeft);
}

void HelpView::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (34);
    closeButton.setBounds (header.removeFromRight (90).reduced (8, 5));

    viewport.setBounds (b.reduced (8, 4));

    const auto contentWidth = juce::jmax (200, viewport.getMaximumVisibleWidth());
    content.setSize (contentWidth, 100);
    content.setSize (contentWidth, content.idealHeight());
}

} // namespace gapfill
