#include "Tour.h"

#include "../../gui/HelpRegistry.h"
#include "../../gui/Knob.h"
#include "../../gui/LookAndFeel.h"

namespace gapfill::tour
{

std::vector<TourStep> defaultSteps (bool auditionAvailable)
{
    std::vector<TourStep> steps;

    steps.push_back ({ "visualiser", "See the gaps",
                       "Put GapFill on a drum bus or full mix and press play. The picture shows the gaps it finds.",
                       { "visualiser" }, false });

    steps.push_back ({ "fill", "Make some fills",
                       "Raise Fill, or press SURPRISE for a new sound. Load example sets up a good starting point.",
                       { "fill", "surprise" }, true });

    if (auditionAvailable)
        steps.push_back ({ "audition", "Hear a fill any time",
                           "Press AUDITION to hear a fill any time, without waiting for a gap.",
                           { "audition" }, false });
    else
        steps.push_back ({ "undo", "Try anything safely",
                           "UNDO and the arrows next to the preset name let you try anything safely. "
                           "Every Surprise, Random or preset change can be undone.",
                           { "undo", "presets" }, false });

    return steps;
}

bool& shownInProcess()
{
    static bool shown = false;
    return shown;
}

//==============================================================================
TourModel::TourModel (settings::GlobalSettings& s, int n) : settingsFile (s), count (juce::jmax (1, n)) {}

bool TourModel::shouldAutoShow (bool alreadyShownInProcess) const
{
    if (alreadyShownInProcess)
        return false;

    // Cannot read or cannot persist the flag: err on the side of NOT showing (never loop).
    if (! settingsFile.isUsable() || ! settingsFile.isWritable())
        return false;

    if (settingsFile.getBool (kKeySuppress, false))
        return false;

    return settingsFile.getInt (kKeyDoneVersion, 0) <= 0;
}

bool TourModel::shouldShowWhatsNewHint() const
{
    if (! settingsFile.isUsable() || ! settingsFile.isWritable() || settingsFile.getBool (kKeySuppress, false))
        return false;

    const auto done = settingsFile.getInt (kKeyDoneVersion, 0);
    return done > 0 && done < kTourVersion;
}

void TourModel::skip()
{
    settingsFile.setInt (kKeyDoneVersion, kTourVersion);
    settingsFile.flush();
}

void TourModel::finish()
{
    skip();
}

void TourModel::dontShowAgain()
{
    settingsFile.setBool (kKeySuppress, true);
    settingsFile.setInt (kKeyDoneVersion, kTourVersion);
    settingsFile.flush();
}

//==============================================================================
void TourOverlay::Card::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    GapFillLookAndFeel::drawShadow (g, b, metrics::panelRadius);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, metrics::panelRadius);
    g.setColour (colours::accent);
    g.drawRoundedRectangle (b.reduced (0.5f), metrics::panelRadius, 1.5f);
}

TourOverlay::TourOverlay (std::vector<TourStep> s, TourModel& m) : steps (std::move (s)), model (m)
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);

    addAndMakeVisible (card);

    for (auto* l : { &title, &body, &status })
    {
        l->setInterceptsMouseClicks (false, false);
        card.addAndMakeVisible (*l);
    }

    title.setFont (fonts::sectionHeader (12.0f));
    title.setColour (juce::Label::textColourId, colours::text);
    body.setFont (fonts::ui (12.5f));
    body.setColour (juce::Label::textColourId, colours::text);
    body.setJustificationType (juce::Justification::topLeft);
    body.setMinimumHorizontalScale (1.0f);
    status.setFont (fonts::ui (11.0f));
    status.setColour (juce::Label::textColourId, colours::success);

    for (auto* b : { &backButton, &nextButton, &skipButton, &dontShowButton, &exampleButton })
    {
        b->setWantsKeyboardFocus (true);
        card.addAndMakeVisible (*b);
    }

    backButton.setTitle ("Back");
    nextButton.setTitle ("Next");
    skipButton.setTitle ("Skip tour");
    dontShowButton.setTitle ("Don't show the tour again");
    exampleButton.setTitle ("Load example");
    exampleButton.setTooltip ("Loads the Drum Gap Glitch preset. Undo goes back.");

    backButton.onClick = [this] { model.back(); refresh(); };
    nextButton.onClick = [this]
    {
        if (model.isLast())
        {
            model.finish();
            close (0);
            return;
        }

        model.next();
        refresh();
    };
    skipButton.onClick = [this] { model.skip(); close (0); };
    dontShowButton.onClick = [this] { model.dontShowAgain(); close (0); };
    exampleButton.onClick = [this] { if (onLoadExample != nullptr) onLoadExample(); };

    refresh();
}

void TourOverlay::parentHierarchyChanged()
{
    if (getParentComponent() != nullptr && isShowing())
        nextButton.grabKeyboardFocus();
}

void TourOverlay::close (int)
{
    if (closing)
        return;

    closing = true;
    setVisible (false);

    if (onClosed != nullptr)
        onClosed();
}

void TourOverlay::setExampleStatus (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

void TourOverlay::refresh()
{
    const auto& step = getStep();
    const auto n = model.numSteps();
    const auto label = "Quick tour, step " + juce::String (model.index() + 1) + " of " + juce::String (n);

    card.setTitle (label);
    card.setDescription (step.title + ". " + step.body);
    title.setText (label.toUpperCase() + "  -  " + step.title.toUpperCase(), juce::dontSendNotification);
    body.setText (step.body, juce::dontSendNotification);
    status.setText ({}, juce::dontSendNotification);

    backButton.setEnabled (model.index() > 0);
    nextButton.setButtonText (model.isLast() ? "DONE" : "NEXT");
    exampleButton.setVisible (step.offersExample);

    resized();
    repaint();

    if (isShowing())
        nextButton.grabKeyboardFocus();
}

std::vector<juce::Rectangle<int>> TourOverlay::getHighlights() const
{
    std::vector<juce::Rectangle<int>> out;

    if (findTarget == nullptr)
        return out;

    for (const auto& id : getStep().targetIds)
        if (auto* target = findTarget (id))
            if (target->isShowing() && ! target->getBounds().isEmpty())
                out.push_back (getLocalArea (target, target->getLocalBounds()).expanded (4).getIntersection (getLocalBounds()));

    return out;
}

void TourOverlay::resized()
{
    const auto cardW = juce::jmax (200, juce::jmin (360, getWidth() - 48));
    const auto cardH = 170;
    const auto highlights = getHighlights();

    juce::Rectangle<int> cardBounds (cardW, cardH);

    if (highlights.empty())
    {
        cardBounds.setCentre (getLocalBounds().getCentre());
    }
    else
    {
        auto target = highlights.front();

        for (const auto& h : highlights)
            target = target.getUnion (h);

        // Put the card on the side of the target with the most room, never on top of it.
        const auto above = target.getY();
        const auto below = getHeight() - target.getBottom();
        const auto left = target.getX();
        const auto right = getWidth() - target.getRight();
        const auto best = juce::jmax (juce::jmax (above, below), juce::jmax (left, right));

        if (best == below && below >= cardH + 16)
            cardBounds.setPosition (target.getCentreX() - cardW / 2, target.getBottom() + 12);
        else if (best == above && above >= cardH + 16)
            cardBounds.setPosition (target.getCentreX() - cardW / 2, target.getY() - cardH - 12);
        else if (right >= left && right >= cardW + 16)
            cardBounds.setPosition (target.getRight() + 12, target.getCentreY() - cardH / 2);
        else
            cardBounds.setPosition (target.getX() - cardW - 12, target.getCentreY() - cardH / 2);

        cardBounds.setPosition (juce::jlimit (8, juce::jmax (8, getWidth() - cardW - 8), cardBounds.getX()),
                                juce::jlimit (8, juce::jmax (8, getHeight() - cardH - 8), cardBounds.getY()));
    }

    card.setBounds (cardBounds);

    auto b = card.getLocalBounds().reduced (12);
    title.setBounds (b.removeFromTop (20));
    auto buttons = b.removeFromBottom (26);
    status.setBounds (b.removeFromBottom (16));
    body.setBounds (b);

    skipButton.setBounds (buttons.removeFromLeft (52));
    buttons.removeFromLeft (4);
    dontShowButton.setBounds (buttons.removeFromLeft (juce::jmin (130, buttons.getWidth() / 2)));
    nextButton.setBounds (buttons.removeFromRight (56));
    buttons.removeFromRight (4);
    backButton.setBounds (buttons.removeFromRight (52));
    exampleButton.setBounds (buttons.removeFromRight (juce::jmin (104, buttons.getWidth())));
}

void TourOverlay::paint (juce::Graphics& g)
{
    juce::Path scrim;
    scrim.addRectangle (getLocalBounds().toFloat());

    const auto highlights = getHighlights();

    for (const auto& h : highlights)
        scrim.addRoundedRectangle (h.toFloat(), 6.0f);

    scrim.setUsingNonZeroWinding (false);
    g.setColour (juce::Colours::black.withAlpha (0.58f));
    g.fillPath (scrim);

    // Outline plus the step counter on the card: the highlight is not colour-only.
    g.setColour (colours::accent);

    for (const auto& h : highlights)
        g.drawRoundedRectangle (h.toFloat(), 6.0f, 2.0f);
}

bool TourOverlay::hitTest (int x, int y)
{
    if (card.getBounds().contains (x, y))
        return true;

    for (const auto& h : getHighlights())
        if (h.contains (x, y))
            return false;                       // the user may try the highlighted control

    return true;
}

bool TourOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        model.skip();                           // Esc counts as Skip
        close (0);
        return true;
    }

    if (key == juce::KeyPress::rightKey) { nextButton.onClick(); return true; }
    if (key == juce::KeyPress::leftKey)  { backButton.onClick(); return true; }
    return false;
}

//==============================================================================
namespace
{
    const HelpRegistrar helpTour (1010, "Quick tour", [] (HelpWriter& w)
    {
        w.body ("The first time GapFill opens for you, a three-step quick tour points at the visualiser, Fill and SURPRISE, and "
                "the way to hear or undo a fill. Skip it at any time with Esc. FILE > Show quick tour plays it again. "
                "\"Load example\" applies the Drum Gap Glitch preset as one Undo step.");
    });
}

} // namespace gapfill::tour
