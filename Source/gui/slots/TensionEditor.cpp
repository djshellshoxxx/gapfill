// F-M21 curve editor popover (see TensionEditor.h).

#include "TensionEditor.h"

#include "../LookAndFeel.h"
#include "../../PluginProcessor.h"

namespace gapfill
{

namespace
{
    constexpr const char* kLaneNames[tension::kLanes] = { "Density", "Intensity", "Width", "FX Send" };
    constexpr const char* kShapeNames[4] = { "BUILD", "BUILD+DROP", "BREAKDOWN", "SWELL" };
    constexpr int kPad = 10;           // canvas inset so end points are not clipped
    constexpr int kPickRadius = 12;    // px
    constexpr int kHStep = 16;         // 1/64 of the length, in per-mille (spec s6)
    constexpr int kVStep = 10;         // 1 percent
}

TensionCurveEditor::TensionCurveEditor (GapFillAudioProcessor& p)
    : proc (p), curves (p.getContextState().tensionService().curves())
{
    setTitle ("Tension curve editor");
    setDescription ("Edit the Custom tension curve of one lane. Page Up and Page Down select a point, arrow keys move it "
                    "(Shift for ten times the step), Insert adds a point, Delete removes it.");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    for (int i = 0; i < tension::kLanes; ++i)
    {
        auto* b = laneButtons.add (new juce::TextButton (kLaneNames[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4021);
        b->setTitle (juce::String (kLaneNames[i]) + " lane");
        b->setTooltip (juce::String ("Edit the ") + kLaneNames[i] + " curve");
        b->setToggleState (i == 0, juce::dontSendNotification);
        b->onClick = [this, i] { setLane (i); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < 4; ++i)
    {
        auto* b = shapeButtons.add (new juce::TextButton (kShapeNames[i]));
        b->setTitle (juce::String ("Copy the ") + kShapeNames[i] + " shape into this lane");
        b->setTooltip ("Copy this built-in shape into the selected lane; then drag points to adjust it.");
        b->onClick = [this, i] { applyShape (i); };
        addAndMakeVisible (b);
    }

    resetButton.setTooltip ("Back to a flat line (no change) for the selected lane.");
    resetButton.onClick = [this] { resetCurrentLane(); };
    addAndMakeVisible (resetButton);

    status.setFont (fonts::ui (11.0f));
    status.setColour (juce::Label::textColourId, colours::textMuted);
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    setSize (kWidth, kHeight);
    refreshAnnouncement();
    startTimerHz (15);
}

TensionCurveEditor::~TensionCurveEditor()
{
    stopTimer();
}

void TensionCurveEditor::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto tabs = r.removeFromTop (26);
    const int w = tabs.getWidth() / tension::kLanes;

    for (auto* b : laneButtons)
        b->setBounds (tabs.removeFromLeft (w).reduced (2, 0));

    r.removeFromTop (4);
    canvas = r.removeFromTop (kCanvasHeight);
    r.removeFromTop (4);

    auto bottom = r.removeFromTop (26);
    const int bw = bottom.getWidth() / 5;

    for (auto* b : shapeButtons)
        b->setBounds (bottom.removeFromLeft (bw).reduced (2, 0));

    resetButton.setBounds (bottom.reduced (2, 0));
    r.removeFromTop (4);
    status.setBounds (r);
}

int TensionCurveEditor::xToPerMille (int px) const noexcept
{
    const auto w = juce::jmax (1, canvas.getWidth() - 2 * kPad);
    return juce::jlimit (0, 1000, (int) std::lround ((double) (px - canvas.getX() - kPad) * 1000.0 / (double) w));
}

int TensionCurveEditor::yToPerMille (int py) const noexcept
{
    const auto h = juce::jmax (1, canvas.getHeight() - 2 * kPad);
    return juce::jlimit (0, 1000, (int) std::lround ((double) (canvas.getBottom() - kPad - py) * 1000.0 / (double) h));
}

juce::Point<float> TensionCurveEditor::toPixels (const tension::TensionPoint& pt) const noexcept
{
    const auto w = (float) juce::jmax (1, canvas.getWidth() - 2 * kPad);
    const auto h = (float) juce::jmax (1, canvas.getHeight() - 2 * kPad);
    return { (float) canvas.getX() + (float) kPad + (float) pt.x * 0.001f * w,
             (float) canvas.getBottom() - (float) kPad - (float) pt.y * 0.001f * h };
}

void TensionCurveEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto cv = canvas.toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (cv, metrics::controlRadius);

    // 1/16 grid (every 4th line stronger = one beat of a 4-bar view), 1/4 horizontal.
    const auto inner = cv.reduced ((float) kPad);

    for (int i = 0; i <= 16; ++i)
    {
        g.setColour (colours::edge.withAlpha (i % 4 == 0 ? 0.9f : 0.4f));
        const float x = inner.getX() + inner.getWidth() * (float) i / 16.0f;
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
    }

    for (int i = 0; i <= 4; ++i)
    {
        g.setColour (colours::edge.withAlpha (i == 2 ? 0.95f : 0.4f));          // the middle line is "no change"
        const float y = inner.getY() + inner.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
    }

    const auto& l = curves.lane[lane];

    // The curve exactly as the audio thread evaluates it.
    juce::Path path;
    const int steps = juce::jmax (2, (int) inner.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const float ph = (float) i / (float) steps;
        const juce::Point<float> pt { inner.getX() + inner.getWidth() * ph,
                                      inner.getBottom() - inner.getHeight() * tension::evalLane (l, ph) };

        if (i == 0)
            path.startNewSubPath (pt);
        else
            path.lineTo (pt);
    }

    g.setColour (colours::accentWarm);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    for (int i = 0; i < l.numPoints; ++i)
    {
        const auto c = toPixels (l.pt[i]);
        const bool sel = i == selected;
        const float rad = sel ? 6.0f : 4.0f;
        g.setColour (colours::background);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
        g.setColour (sel ? colours::text : colours::accentWarm);
        g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, sel ? 2.5f : 1.5f);   // size + weight, not colour alone
    }

    if (lastPlayhead >= 0.0f)
    {
        g.setColour (colours::secondary);
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * lastPlayhead), cv.getY() + 1.0f, cv.getBottom() - 1.0f);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::text);
        g.drawRoundedRectangle (cv.reduced (1.0f), metrics::controlRadius, focusRingWidth);
    }
}

juce::String TensionCurveEditor::describeSelection() const
{
    const auto& l = curves.lane[lane];
    const int i = juce::jlimit (0, l.numPoints - 1, selected);
    static const int lengths[] = { 2, 4, 8, 16, 32 };
    const int idx = (int) std::lround (proc.getValueTreeState().getRawParameterValue ("fm21_bars")->load());
    const double barPos = (double) l.pt[i].x * 0.001 * (double) lengths[juce::jlimit (0, 4, idx)];

    return juce::String (kLaneNames[lane]) + " point " + juce::String (i + 1) + " of " + juce::String (l.numPoints)
         + ", bar " + juce::String (barPos + 1.0, 1) + ", " + juce::String (juce::roundToInt ((double) l.pt[i].y * 0.1)) + " percent";
}

void TensionCurveEditor::refreshAnnouncement()
{
    const auto text = describeSelection();

    if (getTitle() != text)
    {
        setTitle (text);

        if (auto* h = getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
    }

    status.setText (text + (proc.getValueTreeState().getRawParameterValue ("fm21_shape")->load() > 3.5f
                                ? juce::String()
                                : juce::String ("  |  editing switches Shape to Custom")),
                    juce::dontSendNotification);
}

void TensionCurveEditor::commit()
{
    for (auto& l : curves.lane)
        tension::normalise (l);

    proc.getContextState().tensionService().publishCurves (curves);

    // The drawn curve is only heard with Shape = Custom: make the edit audible.
    if (auto* shape = proc.getValueTreeState().getParameter ("fm21_shape"))
        if (proc.getValueTreeState().getRawParameterValue ("fm21_shape")->load() < 3.5f)
        {
            shape->beginChangeGesture();
            shape->setValueNotifyingHost (shape->convertTo0to1 ((float) tension::Custom));
            shape->endChangeGesture();
        }

    selected = juce::jlimit (0, curves.lane[lane].numPoints - 1, selected);
    refreshAnnouncement();
    repaint();
}

void TensionCurveEditor::setLane (int newLane)
{
    lane = juce::jlimit (0, tension::kLanes - 1, newLane);
    selected = 0;

    for (int i = 0; i < laneButtons.size(); ++i)
        laneButtons[i]->setToggleState (i == lane, juce::dontSendNotification);

    refreshAnnouncement();
    repaint();
}

void TensionCurveEditor::selectPoint (int index)
{
    selected = juce::jlimit (0, curves.lane[lane].numPoints - 1, index);
    refreshAnnouncement();
    repaint();
}

void TensionCurveEditor::addPointAt (int x, int y)
{
    const int idx = tension::edit::addPoint (curves.lane[lane], x, y);

    if (idx >= 0)
    {
        selected = idx;
        commit();
    }
}

bool TensionCurveEditor::removeSelected()
{
    if (! tension::edit::removePoint (curves.lane[lane], selected))
        return false;

    selected = juce::jlimit (0, curves.lane[lane].numPoints - 1, selected - 1);
    commit();
    return true;
}

void TensionCurveEditor::nudgeSelected (int dx, int dy)
{
    if (tension::edit::nudgePoint (curves.lane[lane], selected, dx, dy) >= 0)
        commit();
}

void TensionCurveEditor::applyShape (int shape)
{
    tension::edit::copyShape (curves.lane[lane], shape);
    selected = 0;
    commit();
}

void TensionCurveEditor::resetCurrentLane()
{
    tension::edit::resetLane (curves.lane[lane]);
    selected = 0;
    commit();
}

bool TensionCurveEditor::keyPressed (const juce::KeyPress& key)
{
    const int mult = key.getModifiers().isShiftDown() ? 10 : 1;
    const int code = key.getKeyCode();
    auto& l = curves.lane[lane];

    if (code == juce::KeyPress::leftKey)    { nudgeSelected (-kHStep * mult, 0); return true; }
    if (code == juce::KeyPress::rightKey)   { nudgeSelected (kHStep * mult, 0);  return true; }
    if (code == juce::KeyPress::upKey)      { nudgeSelected (0, kVStep * mult);  return true; }
    if (code == juce::KeyPress::downKey)    { nudgeSelected (0, -kVStep * mult); return true; }
    if (code == juce::KeyPress::pageUpKey)  { selectPoint (selected + 1); return true; }
    if (code == juce::KeyPress::pageDownKey){ selectPoint (selected - 1); return true; }
    if (code == juce::KeyPress::homeKey)    { selectPoint (0); return true; }
    if (code == juce::KeyPress::endKey)     { selectPoint (l.numPoints - 1); return true; }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        return removeSelected() || true;

    if (code == juce::KeyPress::insertKey)
    {
        // Into the widest gap, on the current curve.
        int bestSeg = 0, bestGap = 0;

        for (int i = 0; i + 1 < l.numPoints; ++i)
            if (l.pt[i + 1].x - l.pt[i].x > bestGap)
            {
                bestGap = l.pt[i + 1].x - l.pt[i].x;
                bestSeg = i;
            }

        const int x = (l.pt[bestSeg].x + l.pt[bestSeg + 1].x) / 2;
        addPointAt (x, (int) std::lround (tension::evalLane (l, (float) x * 0.001f) * 1000.0f));
        return true;
    }

    return false;
}

void TensionCurveEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;

    if (! canvas.contains (e.getPosition()))
        return;

    const auto& l = curves.lane[lane];
    int hit = -1;
    float bestD = (float) (kPickRadius * kPickRadius);

    for (int i = 0; i < l.numPoints; ++i)
    {
        const auto d = toPixels (l.pt[i]).getDistanceSquaredFrom (e.position);

        if (d <= bestD)
        {
            bestD = d;
            hit = i;
        }
    }

    if (hit < 0)
        return;

    selectPoint (hit);

    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        removeSelected();               // right click / Alt+click removes (end points are kept)
        return;
    }

    dragging = true;
}

void TensionCurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    if (tension::edit::movePoint (curves.lane[lane], selected, xToPerMille (e.x), yToPerMille (e.y)) >= 0)
        commit();
}

void TensionCurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (canvas.contains (e.getPosition()))
        addPointAt (xToPerMille (e.x), yToPerMille (e.y));
}

void TensionCurveEditor::timerCallback()
{
    const auto& svc = proc.getContextState().tensionService();
    const float next = svc.lastActive() ? svc.lastPhase() : -1.0f;

    if (std::abs (next - lastPlayhead) > 1.0e-4f)
    {
        lastPlayhead = next;
        repaint (canvas);
    }
}

} // namespace gapfill
