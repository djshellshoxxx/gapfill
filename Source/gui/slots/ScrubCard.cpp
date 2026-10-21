// F-M19 Freeze & Scrub card of the Effects section; see ScrubCard.h.

#include "ScrubCard.h"
#include "../HelpRegistry.h"

namespace gapfill::fxgui
{

namespace
{
    constexpr int kPad = 8;
    constexpr int kBarRow = 40;
    constexpr int kBarMaxWidth = 220;
    constexpr int kToggleWidth = 96;
}

ScrubCardView::ScrubCardView (GapFillAudioProcessor& p)
    : proc (p),
      toggle (p.getValueTreeState(), "scrubOn", "SCRUB",
              "Freeze the last second of sound and loop a short window of it. Move Position to scan through it. "
              "Switching it off fades out; the sound is a snapshot taken when you switch it on."),
      position (p.getValueTreeState(), "scrubPos", "Position",
                "Where in the frozen second the looping window sits. Automatable and MIDI-learnable.",
                colours::secondary),
      width (p.getValueTreeState(), "scrubWidth", "Width",
             "Length of the looping window.", colours::secondary)
{
    setTitle ("Freeze and scrub");
    addAndMakeVisible (toggle);

    fold.setButtonText ("-");
    fold.setTitle ("Scrub fold");
    fold.setTooltip ("Fold or unfold the Position and Width controls.");
    fold.onClick = [this] { setFolded (! folded); };
    addChildComponent (fold);

    addChildComponent (position);
    addChildComponent (width);

    refresh();
    startTimerHz (10);
}

ScrubCardView::~ScrubCardView()
{
    stopTimer();
}

bool ScrubCardView::isScrubOn() const
{
    if (auto* raw = proc.getValueTreeState().getRawParameterValue ("scrubOn"))
        return raw->load() > 0.5f;

    return false;
}

int ScrubCardView::getPreferredHeight() const
{
    return bodyWanted() ? headerHeight + kBarRow + kPad : headerHeight;
}

void ScrubCardView::setFolded (bool shouldFold)
{
    folded = shouldFold;
    updateVisibility();
}

void ScrubCardView::refresh()
{
    if (bodyWanted() != lastBody || fold.isVisible() != isScrubOn())
        updateVisibility();
}

void ScrubCardView::updateVisibility()
{
    const bool on = isScrubOn();
    lastBody = bodyWanted();

    fold.setVisible (on);
    fold.setButtonText (folded ? "+" : "-");
    position.setVisible (lastBody);
    width.setVisible (lastBody);

    resized();
    repaint();

    if (onHeightChanged != nullptr)
        onHeightChanged();
}

void ScrubCardView::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight);

    header.removeFromLeft (kPad);
    toggle.setBounds (header.removeFromLeft (kToggleWidth).reduced (0, 1));
    fold.setBounds (header.removeFromRight (28).reduced (2));

    const auto barW = juce::jlimit (60, kBarMaxWidth, (getWidth() - 2 * kPad - 16) / 2);
    position.setBounds (kPad, headerHeight, barW, kBarRow - 4);
    width.setBounds (kPad + barW + 16, headerHeight, barW, kBarRow - 4);
}

void ScrubCardView::paint (juce::Graphics& g)
{
    g.setColour (colours::edge);
    g.fillRect (0, 0, getWidth(), 1);

    const auto hintX = kPad + kToggleWidth + 8;

    if (! isScrubOn() && getWidth() > hintX + 60)
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::label (11.0f));
        g.drawText ("Freeze and scrub the last second", hintX, 0, getWidth() - hintX - kPad, headerHeight,
                    juce::Justification::centredLeft, true);
    }
}

//==============================================================================
namespace
{
    const HelpRegistrar helpScrub (4070, "Freeze and Scrub", [] (HelpWriter& w)
    {
        w.body ("SCRUB freezes a snapshot of the last second of sound (while Capture is on, the captured sound) and loops a short window of it "
                "with smooth loop points. It lives in the Effects section (Creative tab, EFFECTS drawer): Position and Width appear once SCRUB is on. "
                "Position moves the window through the snapshot: it glides rather than jumping, and its speed is limited "
                "so scanning stays clean. Position can be automated or driven from a controller.");
        w.body ("Switching SCRUB off fades out over about 8 ms. Panic (and a transport stop) also fades it out; switch SCRUB off and on again to restart it. "
                "The scrub sound follows the same protection of hits as fills.");
    });
}

} // namespace gapfill::fxgui
