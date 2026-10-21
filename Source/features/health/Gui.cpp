#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../../gui/SlotHost.h"
#include "../events/Diagnostics.h"
#include "../events/Replay.h"
#include "../explain/ExplainState.h"
#include "../telemetry/Telemetry.h"

namespace gapfill::health
{

namespace
{
    constexpr int kPanelWidth = 480;
    constexpr int kPanelMaxHeight = 560;
    constexpr int kGap = 6;

    bool isCritical (HealthCode c)   { return c == HealthCode::PanicActive || c == HealthCode::OverloadCritical; }

    bool isWarning (HealthCode c)
    {
        return c == HealthCode::NoSidechain || c == HealthCode::SidechainSilent || c == HealthCode::NoTransport
            || c == HealthCode::FreeTimingFallback || c == HealthCode::OverloadWarn;
    }

    DescribeContext contextFor (const GapFillAudioProcessor& proc)
    {
        const auto& ex = proc.getExplainState();
        DescribeContext ctx;
        ctx.cpu01 = ex.cpu.getCpu01();
        ctx.fallbackBpm = ex.fallbackBpm.load();
        return ctx;
    }

    /** Synthetic stand-in audio for a replay without a user file: decaying noise bursts every half second. */
    juce::AudioBuffer<float> syntheticAudio (double sampleRate, double seconds)
    {
        const auto n = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> b (2, juce::jmax (1, n));
        b.clear();
        juce::Random rng (1234);
        const auto burst = (int) (sampleRate * 0.12);

        for (int start = 0; start < n; start += (int) (sampleRate * 0.5))
            for (int i = 0; i < burst && start + i < n; ++i)
            {
                const auto env = std::exp (-6.0f * (float) i / (float) burst);
                const auto v = 0.6f * env * (rng.nextFloat() * 2.0f - 1.0f);
                b.setSample (0, start + i, v);
                b.setSample (1, start + i, v);
            }

        return b;
    }
}

//==============================================================================
FooterView computeFooterView (GapFillAudioProcessor& proc)
{
    auto& ex = proc.getExplainState();
    FooterView v;

    HealthCode code {};
    const bool haveHealth = highestVisible (ex.healthTracker.getMask(), ex.mutedHealth.load(), code);
    const auto& chip = ex.fixitController.getChip();
    const bool haveFixit = chip.kind != fixit::Chip::Kind::None && ex.showSuggestions.load();

    const auto& log = proc.getGapOutcomeLog();
    const bool haveGap = log.size() > 0 && proc.getVisualisationFifo().isEnabled();

    v.kind = selectFooter ({ haveHealth, haveFixit, haveGap });

    switch (v.kind)
    {
        case FooterKind::Health:
            v.text = describe (code, contextFor (proc)).message;
            v.healthIsCritical = isCritical (code);
            break;

        case FooterKind::Fixit:
            v.text = (chip.kind == fixit::Chip::Kind::Applied ? "Applied: " : "Suggestion: ") + chip.proposal.text;
            break;

        case FooterKind::LastGap:
            v.text = reasons::statusLine (log.get (0));
            break;

        case FooterKind::Nothing:
            break;
    }

    return v;
}

//==============================================================================
// Detail view

class DetailsPanel::Rows : public juce::Component
{
public:
    explicit Rows (juce::Component& owner) : panel (owner) {}

    void addHeader (const juce::String& text)
    {
        auto l = std::make_unique<juce::Label> ("", text);
        l->setFont (fonts::sectionHeader (11.0f));
        l->setColour (juce::Label::textColourId, colours::accent);
        l->setInterceptsMouseClicks (false, false);
        add (std::move (l), 20);
        plain << "\n" << text << "\n";
    }

    void addText (const juce::String& text, juce::Colour colour = colours::text, float size = 11.5f)
    {
        auto l = std::make_unique<juce::Label> ("", text);
        l->setFont (fonts::ui (size));
        l->setColour (juce::Label::textColourId, colour);
        l->setMinimumHorizontalScale (1.0f);
        l->setJustificationType (juce::Justification::topLeft);
        l->setInterceptsMouseClicks (false, false);
        wrapped.push_back ({ l.get(), size });
        add (std::move (l), 16);
        plain << text << "\n";
    }

    void addToggle (const juce::String& text, bool state, std::function<void (bool)> onChange)
    {
        auto t = std::make_unique<juce::ToggleButton> (text);
        t->setToggleState (state, juce::dontSendNotification);
        t->setColour (juce::ToggleButton::textColourId, colours::text);
        t->setColour (juce::ToggleButton::tickColourId, colours::accent);
        auto* raw = t.get();
        t->onClick = [raw, onChange] { onChange (raw->getToggleState()); };
        add (std::move (t), 24);
        plain << "[" << (state ? "x" : " ") << "] " << text << "\n";
    }

    void addButtons (std::vector<std::pair<juce::String, std::function<void()>>> buttons)
    {
        auto row = std::make_unique<juce::Component>();

        for (auto& b : buttons)
        {
            auto btn = std::make_unique<juce::TextButton> (b.first);
            auto fn = b.second;
            btn->onClick = [fn] { fn(); };
            row->addAndMakeVisible (*btn);
            buttonsOwned.push_back (std::move (btn));
            plain << "<" << b.first << "> ";
        }

        plain << "\n";
        auto* raw = row.get();
        buttonRows.push_back ({ raw, (int) buttons.size(), (int) buttonsOwned.size() - (int) buttons.size() });
        add (std::move (row), metrics::buttonHeight);
    }

    int layoutFor (int width)
    {
        int y = 0;

        for (auto& e : entries)
        {
            int h = e.height;

            for (auto& w : wrapped)
                if (w.label == e.comp.get())
                {
                    juce::AttributedString as;
                    as.append (w.label->getText(), fonts::ui (w.size));
                    juce::TextLayout tl;
                    tl.createLayout (as, (float) width);
                    h = juce::jmax (e.height, (int) std::ceil (tl.getHeight()) + 2);
                }

            e.comp->setBounds (0, y, width, h);

            for (auto& br : buttonRows)
                if (br.row == e.comp.get())
                {
                    int x = 0;

                    for (int i = 0; i < br.count; ++i)
                    {
                        auto* btn = buttonsOwned[(size_t) (br.first + i)].get();
                        const auto bw = juce::jmax (60, btn->getButtonText().length() * 8 + 24);
                        btn->setBounds (x, 0, bw, h);
                        x += bw + 8;
                    }
                }

            y += h + kGap;
        }

        return y;
    }

    juce::String plain;

private:
    struct Entry { std::unique_ptr<juce::Component> comp; int height; };
    struct Wrapped { juce::Label* label; float size; };
    struct ButtonRow { juce::Component* row; int count; int first; };

    void add (std::unique_ptr<juce::Component> c, int height)
    {
        addAndMakeVisible (*c);
        entries.push_back ({ std::move (c), height });
    }

    juce::Component& panel;
    std::vector<Entry> entries;
    std::vector<Wrapped> wrapped;
    std::vector<std::unique_ptr<juce::TextButton>> buttonsOwned;
    std::vector<ButtonRow> buttonRows;
};

DetailsPanel::DetailsPanel (GapFillAudioProcessor& p) : proc (p)
{
    setTitle ("Status details");
    setDescription ("Active health messages, the current suggestion, why gaps were skipped, latency and estimated CPU, and diagnostics.");
    addAndMakeVisible (viewport);
    viewport.setScrollBarsShown (true, false);
    rebuild();
    startTimerHz (2);
}

DetailsPanel::~DetailsPanel()
{
    stopTimer();
}

juce::String DetailsPanel::contentSignature() const
{
    auto& ex = proc.getExplainState();
    const auto& chip = ex.fixitController.getChip();
    return juce::String (ex.healthTracker.getMask()) + "|" + juce::String (ex.mutedHealth.load()) + "|" + juce::String ((int) chip.kind)
         + chip.proposal.text + "|" + juce::String ((int) ex.showSuggestions.load()) + "|"
         + events::describeRecording (proc) + "|" + juce::String ((int) (ex.cpu.getCpu01() * 100.0f))
         + "|" + juce::String (proc.getMeterState().latencySamples.load());
}

void DetailsPanel::timerCallback()
{
    // Rebuild only when something shown actually changed (keeps focus and scroll position).
    const auto now = contentSignature();

    if (now != signature)
        rebuild();
}

void DetailsPanel::rebuild()
{
    signature = contentSignature();
    auto& ex = proc.getExplainState();
    const auto scroll = viewport.getViewPositionY();

    auto r = std::make_unique<Rows> (*this);

    // ---- status ----------------------------------------------------------------
    r->addHeader ("STATUS");
    HealthTracker::Entry entries[numHealthCodes];
    const auto n = ex.healthTracker.list (entries, numHealthCodes, ex.mutedHealth.load());
    const auto now = juce::Time::getMillisecondCounter();

    if (n == 0)
        r->addText ("No problems detected.", colours::textMuted);

    for (int i = 0; i < n; ++i)
    {
        const auto d = describe (entries[i].code, contextFor (proc));
        const auto seconds = (int) ((std::uint32_t) (now - entries[i].onsetMs) / 1000);
        r->addText (d.message + (entries[i].muted ? "  (hidden from the footer)" : ""),
                    isCritical (entries[i].code) ? colours::clip : isWarning (entries[i].code) ? colours::warning : colours::text, 12.5f);
        r->addText (d.explanation + "\nWhat to do: " + d.action + "\nActive for " + juce::String (seconds) + " s.", colours::textMuted);

        if (isMutable (entries[i].code))
        {
            const auto code = entries[i].code;
            r->addToggle ("Hide this message from the footer", entries[i].muted,
                          [this, code] (bool hide) { proc.getExplainState().muteHealthCode (code, hide); });
        }
    }

    // ---- suggestion ------------------------------------------------------------
    r->addHeader ("SUGGESTION");
    const auto& chip = ex.fixitController.getChip();

    if (chip.kind == fixit::Chip::Kind::Suggestion && ex.showSuggestions.load())
    {
        r->addText (chip.proposal.text, colours::text, 12.5f);
        const bool confirm = chip.proposal.needsConfirm;

        if (confirm)
            r->addText ("This lowers a protection setting, so it needs an explicit confirmation.", colours::warning);

        const auto rule = chip.proposal.rule;
        r->addButtons ({ { confirm ? "CONFIRM AND APPLY" : "APPLY", [this, confirm] { proc.getExplainState().applyFixit (confirm); } },
                         { "NOT NOW", [this] { proc.getExplainState().dismissFixit(); } },
                         { "DON'T SUGGEST THIS", [this, rule] { proc.getExplainState().muteFixitRule (rule); } } });
    }
    else if (chip.kind == fixit::Chip::Kind::Applied)
    {
        r->addText ("Applied: " + chip.proposal.text, colours::text, 12.5f);
        r->addButtons ({ { "UNDO", [this] { proc.getExplainState().undoFixit(); } } });
    }
    else
    {
        r->addText (ex.showSuggestions.load() ? "No suggestions right now." : "Suggestions are switched off.", colours::textMuted);
    }

    r->addToggle ("Show suggestions", ex.showSuggestions.load(),
                  [this] (bool on) { proc.getExplainState().showSuggestions.store (on); });

    // ---- why so few fills --------------------------------------------------------
    r->addHeader ("WHY SO FEW FILLS?");
    std::uint32_t counts[numRejectReasons];

    for (int i = 0; i < numRejectReasons; ++i)
        counts[i] = proc.getMeterState().reasonCount[i].load (std::memory_order_relaxed);

    r->addText (reasons::whySoFewFillsText (counts, numRejectReasons), colours::text);

    // ---- performance -------------------------------------------------------------
    r->addHeader ("LATENCY AND CPU");
    r->addText (telemetry::describeTelemetry (proc.getLatencySamples(), juce::jmax (8000.0, ex.sampleRate.load()),
                                              ex.cpu.getCpu01(), ex.cpu.isEnabled()), colours::text);

    // ---- diagnostics ---------------------------------------------------------------
    r->addHeader ("DIAGNOSTICS");
    r->addText (events::describeRecording (proc), colours::text);
    r->addText (events::kPrivacyNote, colours::textMuted, 10.5f);

    std::vector<std::pair<juce::String, std::function<void()>>> buttons;
    buttons.push_back ({ events::isRecording (proc) ? "STOP" : "RECORD", [this] { events::toggleRecording (proc); rebuild(); } });
    buttons.push_back ({ "SAVE...", [this]
    {
        events::stopRecording (proc);
        chooser = std::make_shared<juce::FileChooser> ("Save GapFill event recording",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                           .getChildFile ("gapfill-session.gfevents"), "*.gfevents");
        auto keep = chooser;
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, keep] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      events::saveRecording (proc, fc.getResult().withFileExtension ("gfevents"));

                                  rebuild();
                              });
    } });

    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        buttons.push_back ({ "OPEN REPLAY...", [this]
        {
            chooser = std::make_shared<juce::FileChooser> ("Open a GapFill event recording",
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.gfevents");
            auto keep = chooser;
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [keep] (const juce::FileChooser& fc)
                                  {
                                      events::ReplayData data;
                                      const auto result = fc.getResult() == juce::File() ? juce::Result::fail ("") : events::loadFromFile (fc.getResult(), data);

                                      if (fc.getResult() == juce::File())
                                          return;

                                      juce::String message;

                                      if (result.failed())
                                      {
                                          message = "Could not open the recording: " + result.getErrorMessage();
                                      }
                                      else
                                      {
                                          const auto report = events::replay (data, syntheticAudio (data.sampleRate, (double) data.numBlocks * data.preparedBlockSize / data.sampleRate));
                                          const auto recorded = events::outcomesOf (data).size();
                                          message = "Replayed " + juce::String (report.blocksReplayed) + " blocks with built-in test audio.\n"
                                                  + "Recorded gaps: " + juce::String ((int) recorded) + ", replayed gaps: " + juce::String ((int) report.outcomes.size()) + ".\n"
                                                  + (report.firstDivergence < 0 ? "The outcomes are identical."
                                                                                : "First difference: " + report.divergenceText);
                                      }

                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Replay", message);
                                  });
        } });
    }

    r->addButtons (std::move (buttons));

    // ---- install ---------------------------------------------------------------------
    const auto width = kPanelWidth - viewport.getScrollBarThickness() - 2 * kGap - 4;
    const auto height = r->layoutFor (width);
    r->setSize (width, height);

    viewport.setViewedComponent (nullptr, false);
    rows = std::move (r);
    viewport.setViewedComponent (rows.get(), false);
    viewport.setViewPosition (0, scroll);
    setSize (kPanelWidth, juce::jlimit (160, kPanelMaxHeight, height + 2 * kGap));
}

juce::String DetailsPanel::getPlainText() const
{
    return rows != nullptr ? rows->plain : juce::String();
}

void DetailsPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (kGap));
}

void DetailsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::edge);
    g.drawRect (getLocalBounds(), 1);
}

//==============================================================================
FooterStatusComponent::FooterStatusComponent (GapFillAudioProcessor& p) : proc (p)
{
    setTitle ("Status");
    setDescription ("Shows the most important status message. Click for details.");

    statusButton.setTooltip ("Status details: health, suggestions, why gaps were skipped, latency and CPU, diagnostics");
    statusButton.onClick = [this] { showDetails(); };
    addAndMakeVisible (statusButton);

    applyButton.onClick = [this]
    {
        auto& ex = proc.getExplainState();

        if (ex.fixitController.getChip().proposal.needsConfirm)
            showDetails();                                  // R10 is popover-only and needs a confirm click
        else
            ex.applyFixit (false);

        update (true);
    };
    dismissButton.setTooltip ("Not now");
    dismissButton.setTitle ("Dismiss suggestion");
    dismissButton.onClick = [this] { proc.getExplainState().dismissFixit(); update (true); };
    undoButton.setTooltip ("Restore the previous values");
    undoButton.onClick = [this] { proc.getExplainState().undoFixit(); update (true); };

    for (auto* b : { &applyButton, &dismissButton, &undoButton })
        addChildComponent (*b);

    update (true);
    startTimerHz (15);
}

FooterStatusComponent::~FooterStatusComponent()
{
    stopTimer();
}

bool FooterStatusComponent::hitTest (int x, int)
{
    return x < getWidth() - reservedRight;      // the version marker area belongs to the editor
}

void FooterStatusComponent::timerCallback()
{
    update (false);
}

void FooterStatusComponent::update (bool force)
{
    auto next = computeFooterView (proc);
    const auto now = juce::Time::getMillisecondCounter();

    // The last-gap line changes with every gap: at most 4 updates per second.
    if (next.kind == FooterKind::LastGap && view.kind == FooterKind::LastGap && next.text != view.text
         && ! force && ! limiter.allow (now))
        return;

    if (next.kind == view.kind && next.text == view.text)
        return;

    view = next;
    const auto& chip = proc.getExplainState().fixitController.getChip();

    const bool suggestion = view.kind == FooterKind::Fixit && chip.kind == fixit::Chip::Kind::Suggestion;
    applyButton.setVisible (suggestion);
    applyButton.setButtonText (suggestion && chip.proposal.needsConfirm ? "REVIEW" : "APPLY");
    applyButton.setTitle (chip.proposal.spoken);
    dismissButton.setVisible (suggestion);
    undoButton.setVisible (view.kind == FooterKind::Fixit && chip.kind == fixit::Chip::Kind::Applied);

    resized();
    repaint();
}

void FooterStatusComponent::resized()
{
    auto b = getLocalBounds().withTrimmedRight (reservedRight).reduced (0, 1);
    statusButton.setBounds (b.removeFromLeft (58));
    b.removeFromLeft (8);

    auto buttonArea = b.removeFromRight (view.kind == FooterKind::Fixit ? 150 : 0);

    if (applyButton.isVisible())
    {
        dismissButton.setBounds (buttonArea.removeFromRight (28));
        buttonArea.removeFromRight (4);
        applyButton.setBounds (buttonArea.removeFromRight (72));
    }
    else
    {
        undoButton.setBounds (buttonArea.removeFromRight (72));
    }
}

void FooterStatusComponent::paint (juce::Graphics& g)
{
    if (view.kind == FooterKind::Nothing)
        return;

    auto area = getLocalBounds().withTrimmedRight (reservedRight).withTrimmedLeft (58 + 8);

    if (view.kind == FooterKind::Fixit)
        area.removeFromRight (150);

    // Glyph + text: the shape says what kind of message it is (not colour alone).
    auto glyph = area.removeFromLeft (16).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
    juce::Colour colour = colours::textMuted;
    juce::Path shape;

    if (view.kind == FooterKind::Health)
    {
        colour = view.healthIsCritical ? colours::clip : colours::warning;

        if (view.healthIsCritical)
            shape.addRectangle (glyph);                                       // square: critical
        else
            shape.addTriangle (glyph.getCentreX(), glyph.getY(), glyph.getRight(), glyph.getBottom(),
                               glyph.getX(), glyph.getBottom());              // triangle: warning
    }
    else if (view.kind == FooterKind::Fixit)
    {
        colour = colours::accent;
        shape.addEllipse (glyph);                                             // circle: suggestion
    }
    else
    {
        colour = colours::textMuted;
        shape.startNewSubPath (glyph.getCentreX(), glyph.getY());             // diamond: gap outcome
        shape.lineTo (glyph.getRight(), glyph.getCentreY());
        shape.lineTo (glyph.getCentreX(), glyph.getBottom());
        shape.lineTo (glyph.getX(), glyph.getCentreY());
        shape.closeSubPath();
    }

    g.setColour (colour);

    if (view.kind == FooterKind::LastGap)
        g.strokePath (shape, juce::PathStrokeType (1.2f));
    else
        g.fillPath (shape);

    g.setColour (view.kind == FooterKind::LastGap ? colours::textDim : colours::text);
    g.setFont (fonts::ui (11.0f));
    g.drawText (view.text, area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
}

void FooterStatusComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && view.kind != FooterKind::Nothing)
        showDetails();
}

void FooterStatusComponent::showDetails()
{
    auto panel = std::make_unique<DetailsPanel> (proc);
    auto* parent = getParentComponent();
    auto target = getScreenBounds().withWidth (58);

    juce::CallOutBox::launchAsynchronously (std::move (panel), target, parent);
}

//==============================================================================
namespace
{
    const SlotRegistrar footerStatus ({ Slot::FooterStatus, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<FooterStatusComponent> (proc);
        } });
}

} // namespace gapfill::health
