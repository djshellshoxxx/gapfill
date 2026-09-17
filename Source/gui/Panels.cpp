#include "Panels.h"

namespace gapfill
{

namespace
{
    /** Lays a row of equally sized children into `area`. */
    void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps, int gap = 6)
    {
        const auto n = (int) comps.size();

        if (n <= 0)
            return;

        const auto w = (area.getWidth() - gap * (n - 1)) / n;
        auto x = area.getX();

        for (auto* c : comps)
        {
            c->setBounds (x, area.getY(), w, area.getHeight());
            x += w + gap;
        }
    }
}

//==============================================================================
DetectionPanel::DetectionPanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Detection", colours::accent),
      mode (s, "detMode", "Mode",
            "TRANSIENT: attack-driven, for drums and percussive material.\n"
            "PHRASE: slower, finds musical pauses in vocals, melodies and pads.\n"
            "SPECTRAL: analyses low / mid / high independently so a busy kick "
            "does not hide the empty top end."),
      source (s, "detSource", "Detector Source",
              "MAIN: the track GapFill is on defines the timing.\n"
              "SIDECHAIN: the sidechain input defines WHEN there is space, while "
              "the fills are still built from this track's audio.\n"
              "HYBRID: sidechain defines the spaces but important events on this "
              "track are still protected."),
      maxFillDiv (s, "maxFillDiv", "Sync Division", "Tempo division used for Max Fill Length when Max Fill Sync is on."),
      sensitivity (s, "sensitivity", "Sensitivity",
                   "How easily a musical event is detected. Turn up for quiet or soft material, "
                   "down if GapFill is triggering on noise.", colours::accent),
      gapThreshold (s, "gapThresh", "Gap Thresh",
                    "How much quieter than the surrounding music a region must be before it counts "
                    "as space. Higher = GapFill accepts less silence as a gap.", colours::accent),
      minGap (s, "minGap", "Min Gap",
              "Spaces shorter than this never get a fill. Raise it to keep GapFill out of "
              "busy passages.", colours::accent),
      maxFill (s, "maxFill", "Max Fill",
               "Upper limit on how long a single generated fill may last.", colours::accent),
      gapRelease (s, "gapRelease", "Gap Confirm",
                  "How long GapFill waits before it is convinced an event has ended. "
                  "Short = reacts fast, long = only settled pauses count.", colours::accent),
      lookahead (s, "lookahead", "Lookahead",
                 "Lets GapFill see upcoming events so fills can end before them. "
                 "Reported to the host as plugin latency - set to 0 for zero-latency use.", colours::accent),
      protectHold (s, "protectHold", "Protect Hold",
                   "A safe region right after each detected hit where no fill may start.", colours::accentAmber),
      retrigger (s, "retrigGuard", "Retrig Guard",
                 "Minimum time between two detected events - stops one hit being counted twice.",
                 colours::accentAmber),
      maxFillSync (s, "maxFillSync", "SYNC MAX FILL", "Use a tempo division for Max Fill Length instead of milliseconds."),
      audition (s, "audition", "AUDITION DETECTOR",
                "Monitor exactly what the detector is listening to (the main track, or the sidechain "
                "when sidechain detection is active).", colours::accentAmber)
{
    for (auto* c : std::initializer_list<juce::Component*> { &mode, &source, &maxFillDiv, &sensitivity,
                                                             &gapThreshold, &minGap, &maxFill, &gapRelease,
                                                             &lookahead, &protectHold, &retrigger,
                                                             &maxFillSync, &audition })
        addAndMakeVisible (c);
}

void DetectionPanel::resized()
{
    auto b = contentArea();

    // Fixed-height furniture first so the knobs absorb whatever slack is left.
    auto toggles = b.removeFromBottom (24);
    b.removeFromBottom (4);
    auto divRow = b.removeFromBottom (30);
    b.removeFromBottom (6);

    layoutRow (b.removeFromTop (34), { &mode, &source });
    b.removeFromTop (6);

    const auto rowHeight = juce::jmax (48, (b.getHeight() - 4) / 2);
    layoutRow (b.removeFromTop (rowHeight), { &sensitivity, &gapThreshold, &minGap, &maxFill });
    b.removeFromTop (4);
    layoutRow (b.removeFromTop (rowHeight), { &gapRelease, &lookahead, &protectHold, &retrigger });

    maxFillDiv.setBounds (divRow.removeFromLeft (juce::jmax (90, divRow.getWidth() / 3)).reduced (2, 0));
    layoutRow (toggles, { &maxFillSync, &audition });
}

//==============================================================================
CreativePanel::CreativePanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Creative", colours::accentWarm),
      fill (s, "fill", "Fill",
            "The main amount control: how likely an eligible space is to receive a fill. "
            "0% = nothing at all, 100% = fill almost every usable space.", colours::accentWarm, true),
      intensity (s, "intensity", "Intensity",
                 "How hard each chosen fill is processed - pitch range, drive, crushing, "
                 "movement. Does not simply make things louder.", colours::accentWarm, true),
      complexity (s, "complexity", "Complexity",
                  "How elaborate a fill may become: one simple transformation at the bottom, "
                  "several stacked and evolving ones at the top.", colours::accentWarm, true),
      chaos (s, "chaos", "Chaos",
             "Variation and unpredictability in algorithm choice, timing, pitch and stereo "
             "movement. Stays musically bounded even at 100%.", colours::accentWarm, true),
      density (s, "density", "Density",
               "How many of the eligible spaces actually get used, independently of the wet level.",
               colours::accentWarm, true),
      fillType (s, "fillType", "Fill Type",
                "AUTO lets GapFill weigh every enabled algorithm against the gap length, the hit "
                "that opened it and the macros. Choosing a specific type forces it."),
      stutterDiv (s, "stutterDiv", "Stutter Div", "Subdivision used by the Stutter algorithm."),
      delayDiv (s, "delayDiv", "Delay Div", "Subdivision used by the Delay algorithm."),
      seed (s, "seed", "Seed",
            "The random seed. The same seed and the same transport position always produce the "
            "same fills, so a project plays back identically.", colours::accentGreen),
      width (s, "width", "Wet Width", "Stereo width of the generated material only. The dry signal is untouched.",
             colours::accentGreen),
      sync (s, "syncEnable", "TEMPO SYNC", "Derive stutter and delay timings from the host tempo."),
      pingPong (s, "pingPong", "PING PONG", "Alternate delay taps between left and right."),
      monoWet (s, "wetMono", "MONO WET", "Collapse the generated material to mono for maximum mono compatibility.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &fill, &intensity, &complexity, &chaos, &density,
                                                             &fillType, &stutterDiv, &delayDiv, &seed, &width,
                                                             &sync, &pingPong, &monoWet })
        addAndMakeVisible (c);
}

void CreativePanel::resized()
{
    auto b = contentArea();

    layoutRow (b.removeFromBottom (24), { &sync, &pingPong, &monoWet });
    b.removeFromBottom (5);

    layoutRow (b.removeFromBottom (34), { &fillType, &stutterDiv, &delayDiv });
    b.removeFromBottom (6);

    auto smallRow = b.removeFromBottom (juce::jlimit (46, 62, b.getHeight() / 3));
    b.removeFromBottom (6);

    // Seed and width sit beside each other, leaving the macros the whole top.
    layoutRow (smallRow, { &seed, &width });
    layoutRow (b, { &fill, &intensity, &complexity, &chaos, &density }, 8);
}

//==============================================================================
WeightsPanel::WeightsPanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Auto Algorithm Weights", colours::accentWarm), state (s)
{
    static const char* ids[numAlgos] =
    {
        "wReverse", "wStutter", "wDelay", "wGranular", "wFreeze", "wPitchRise", "wPitchFall",
        "wFilter", "wTexture", "wCrush", "wDistort", "wScramble", "wSpectral", "wComb"
    };

    for (int i = 0; i < numAlgos; ++i)
    {
        auto* bar = bars.add (new GapBar (s, ids[i], algoName ((Algo) i),
                                          juce::String ("How often AUTO may choose ") + algoName ((Algo) i)
                                            + ". Set to 0 to disable it completely.",
                                          colours::accentWarm));
        addAndMakeVisible (bar);
    }

    hint.setText ("Weights only apply when Fill Type is AUTO. 0% disables an algorithm.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colours::textDim);
    hint.setFont (juce::Font (juce::FontOptions (11.0f)));
    hint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hint);

    for (auto* b : { &allOn, &allOff, &drumsOnly, &texturesOnly })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    allOn.onClick = [this]
    {
        for (int i = 0; i < numAlgos; ++i)
            if (auto* p = state.getParameter (juce::String (ids[i])))
                p->setValueNotifyingHost (0.6f);
    };

    allOff.onClick = [this]
    {
        for (int i = 0; i < numAlgos; ++i)
            if (auto* p = state.getParameter (juce::String (ids[i])))
                p->setValueNotifyingHost (0.0f);
    };

    drumsOnly.onClick = [this]
    {
        applySet ({ "wReverse", "wStutter", "wDelay", "wCrush", "wDistort", "wScramble" });
    };

    texturesOnly.onClick = [this]
    {
        applySet ({ "wGranular", "wFreeze", "wSpectral", "wTexture", "wFilter", "wComb" });
    };
}

void WeightsPanel::applySet (const juce::StringArray& enabled)
{
    static const char* ids[numAlgos] =
    {
        "wReverse", "wStutter", "wDelay", "wGranular", "wFreeze", "wPitchRise", "wPitchFall",
        "wFilter", "wTexture", "wCrush", "wDistort", "wScramble", "wSpectral", "wComb"
    };

    for (int i = 0; i < numAlgos; ++i)
        if (auto* p = state.getParameter (juce::String (ids[i])))
            p->setValueNotifyingHost (enabled.contains (ids[i]) ? 0.75f : 0.0f);
}

void WeightsPanel::resized()
{
    auto b = contentArea();

    hint.setBounds (b.removeFromTop (16));
    b.removeFromTop (2);

    auto buttons = b.removeFromBottom (24);
    layoutRow (buttons, { &allOn, &allOff, &drumsOnly, &texturesOnly });
    b.removeFromBottom (6);

    const auto columnsCount = b.getWidth() > 540 ? 2 : 1;
    const auto perColumn = (bars.size() + columnsCount - 1) / columnsCount;
    const auto colWidth = b.getWidth() / columnsCount;
    const auto rowHeight = juce::jmax (26, b.getHeight() / juce::jmax (1, perColumn));

    for (int i = 0; i < bars.size(); ++i)
    {
        const auto col = i / perColumn;
        const auto row = i % perColumn;
        bars[i]->setBounds (b.getX() + col * colWidth, b.getY() + row * rowHeight,
                            colWidth - 10, rowHeight - 2);
    }
}

//==============================================================================
ProtectionPanel::ProtectionPanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Protection", colours::accentAmber),
      transientProtect (s, "protTrans", "Transient",
                        "Ducks generated material around strong detected attacks so the original hits "
                        "keep their impact.", colours::accentAmber),
      lowProtect (s, "protLow", "Low Protect",
                  "High-passes the generated material only. Set around 80-150 Hz to keep the kick and "
                  "sub region clean. Fully left = Off.", colours::accentAmber),
      centreProtect (s, "protCenter", "Center",
                     "Pushes generated material out of the stereo centre, protecting centred kick, "
                     "snare, bass and vocals. The dry signal is never altered.", colours::accentAmber),
      nextProtect (s, "protNext", "Next Event",
                   "Uses lookahead to fade the fill out smoothly before the next important event "
                   "arrives.", colours::accentAmber),
      sidechainProtect (s, "protSC", "Sidechain",
                        "When sidechain detection is active, strong sidechain hits force generated "
                        "material out of the way.", colours::accentAmber)
{
    for (auto* c : std::initializer_list<juce::Component*> { &transientProtect, &lowProtect, &centreProtect,
                                                             &nextProtect, &sidechainProtect })
        addAndMakeVisible (c);

    note.setText ("The original hit always wins.", juce::dontSendNotification);
    note.setColour (juce::Label::textColourId, colours::textDim);
    note.setFont (juce::Font (juce::FontOptions (11.0f)));
    note.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (note);
}

void ProtectionPanel::resized()
{
    auto b = contentArea();
    note.setBounds (b.removeFromBottom (16));

    const auto rowHeight = juce::jmax (58, (b.getHeight() - 6) / 2);
    layoutRow (b.removeFromTop (rowHeight), { &transientProtect, &lowProtect, &centreProtect });
    b.removeFromTop (6);
    layoutRow (b.removeFromTop (rowHeight), { &nextProtect, &sidechainProtect });
}

//==============================================================================
SpectralPanel::SpectralPanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Spectral Lanes", colours::accentGreen),
      xoverLowMid (s, "xoverLM", "Low / Mid", "Crossover between the low and mid detection lanes.", colours::accentGreen),
      xoverMidHigh (s, "xoverMH", "Mid / High", "Crossover between the mid and high detection lanes.", colours::accentGreen),
      solo (s, "bandSolo", "Solo / Audition",
            "Listen to one frequency lane on its own so you can hear exactly what that detector sees.")
{
    struct LaneDef { const char* on; const char* amt; const char* intensity; const char* prob; const char* name; };
    static const LaneDef defs[3] =
    {
        { "bandLowOn",  "bandLowAmt",  "bandLowInt",  "bandLowProb",  "LOW"  },
        { "bandMidOn",  "bandMidAmt",  "bandMidInt",  "bandMidProb",  "MID"  },
        { "bandHighOn", "bandHighAmt", "bandHighInt", "bandHighProb", "HIGH" },
    };

    for (int i = 0; i < 3; ++i)
    {
        auto& lane = lanes[i];
        const auto colour = colours::bandColour (i);

        lane.enabled = std::make_unique<GapToggle> (s, defs[i].on, defs[i].name,
                                                    juce::String ("Allow fills in the ") + defs[i].name + " lane.",
                                                    colour);
        lane.amount = std::make_unique<GapKnob> (s, defs[i].amt, "Amount",
                                                 "Level of fills placed in this lane.", colour);
        lane.intensity = std::make_unique<GapKnob> (s, defs[i].intensity, "Intensity",
                                                    "Processing strength for fills in this lane.", colour);
        lane.probability = std::make_unique<GapKnob> (s, defs[i].prob, "Prob",
                                                      "How likely a usable space in this lane is to be filled.", colour);

        addAndMakeVisible (*lane.enabled);
        addAndMakeVisible (*lane.amount);
        addAndMakeVisible (*lane.intensity);
        addAndMakeVisible (*lane.probability);
    }

    addAndMakeVisible (xoverLowMid);
    addAndMakeVisible (xoverMidHigh);
    addAndMakeVisible (solo);

    hint.setText ("Active in SPECTRAL detector mode. Each lane finds its own spaces.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colours::textDim);
    hint.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (hint);
}

void SpectralPanel::setBandConfidence (int band, float conf)
{
    if (band < 0 || band > 2)
        return;

    if (std::abs (conf - lanes[band].confidence) > 0.01f)
    {
        lanes[band].confidence = conf;
        repaint (lanes[band].meterBounds);
    }
}

void SpectralPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);

    for (int i = 0; i < 3; ++i)
    {
        auto bounds = lanes[i].meterBounds.toFloat();

        if (bounds.isEmpty())
            continue;

        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (bounds, 3.0f);

        auto filled = bounds.withWidth (bounds.getWidth() * juce::jlimit (0.0f, 1.0f, lanes[i].confidence));
        g.setColour (colours::bandColour (i).withAlpha (0.35f + 0.5f * lanes[i].confidence));
        g.fillRoundedRectangle (filled, 3.0f);

        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText (lanes[i].confidence > 0.55f ? "SPACE AVAILABLE" : "BUSY",
                    bounds.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
    }
}

void SpectralPanel::resized()
{
    auto b = contentArea();

    hint.setBounds (b.removeFromBottom (16));
    b.removeFromBottom (4);

    auto bottom = b.removeFromBottom (44);
    auto soloArea = bottom.removeFromRight (juce::jmin (150, bottom.getWidth() / 3));
    solo.setBounds (soloArea.reduced (4, 4));
    layoutRow (bottom, { &xoverLowMid, &xoverMidHigh });
    b.removeFromBottom (4);

    const auto laneHeight = juce::jmax (52, b.getHeight() / 3);

    for (int i = 0; i < 3; ++i)
    {
        auto lane = b.removeFromTop (laneHeight);
        auto left = lane.removeFromLeft (juce::jmin (110, lane.getWidth() / 4));

        lanes[i].enabled->setBounds (left.removeFromTop (22).reduced (0, 1));
        lanes[i].meterBounds = left.removeFromTop (16).reduced (0, 2);

        layoutRow (lane.reduced (4, 2), { lanes[i].amount.get(), lanes[i].intensity.get(),
                                          lanes[i].probability.get() });
    }
}

//==============================================================================
MixPanel::MixPanel (juce::AudioProcessorValueTreeState& s)
    : PanelBase ("Mix", colours::accentGreen),
      dry (s, "dry", "Dry", "Level of the original signal.", colours::accentGreen),
      wet (s, "wet", "Wet", "Level of the generated fills.", colours::accentWarm),
      output (s, "output", "Output", "Final output trim.", colours::accentGreen)
{
    for (auto* c : std::initializer_list<juce::Component*> { &dry, &wet, &output })
        addAndMakeVisible (c);

    for (auto* m : std::initializer_list<juce::Component*> { &inputMeter, &outputMeter, &wetMeter })
        addAndMakeVisible (m);

    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colours::textDim);
        l.setFont (juce::Font (juce::FontOptions (9.5f)));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    setupLabel (inLabel, "IN");
    setupLabel (wetLabel, "FILL");
    setupLabel (outLabel, "OUT");

    addAndMakeVisible (confidence);

    status.setColour (juce::Label::textColourId, colours::textDim);
    status.setFont (juce::Font (juce::FontOptions (11.0f)));
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);
}

void MixPanel::resetMeterPeaks()
{
    inputMeter.resetPeak();
    outputMeter.resetPeak();
    wetMeter.resetPeak();
}

void MixPanel::setLevels (float in, float out, float wetLevel)
{
    inputMeter.setLevel (in);
    outputMeter.setLevel (out);
    wetMeter.setLevel (wetLevel);
}

void MixPanel::setStatusText (const juce::String& text)
{
    if (status.getText() != text)
        status.setText (text, juce::dontSendNotification);
}

void MixPanel::resized()
{
    auto b = contentArea();

    // Meters on the far right.
    auto meterArea = b.removeFromRight (juce::jmin (92, juce::jmax (60, b.getWidth() / 4)));
    auto labels = meterArea.removeFromBottom (12);
    const auto mw = juce::jmax (12, meterArea.getWidth() / 3);

    inputMeter.setBounds (meterArea.getX() + 4, meterArea.getY(), mw - 8, meterArea.getHeight());
    wetMeter.setBounds (meterArea.getX() + mw + 4, meterArea.getY(), mw - 8, meterArea.getHeight());
    outputMeter.setBounds (meterArea.getX() + 2 * mw + 4, meterArea.getY(), mw - 8, meterArea.getHeight());

    inLabel.setBounds (labels.getX(), labels.getY(), mw, labels.getHeight());
    wetLabel.setBounds (labels.getX() + mw, labels.getY(), mw, labels.getHeight());
    outLabel.setBounds (labels.getX() + 2 * mw, labels.getY(), mw, labels.getHeight());

    b.removeFromRight (10);

    // Readouts in their own column so the knobs keep their full height.
    auto readouts = b.removeFromRight (juce::jlimit (150, 320, b.getWidth() / 2));
    confidence.setBounds (readouts.removeFromTop (18));
    readouts.removeFromTop (6);
    status.setBounds (readouts.removeFromTop (18));

    b.removeFromRight (10);

    layoutRow (b, { &dry, &wet, &output });
}

} // namespace gapfill
