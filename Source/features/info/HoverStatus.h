#pragma once

// U-9: writes "Name value (base -> effective)" to the status board while a control is
// hovered or dragged. Attach with `editor.addMouseListener (&hover, true)`.
//
// Finds the parameter by walking up from the component under the mouse to the first
// one carrying the `gapfillParamId` property (set by every Gap* control). Updates are
// limited to 20 Hz; leaving the control (or the editor) clears the line at once.

#include "StatusBoard.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce { class AudioProcessorValueTreeState; }

namespace gapfill::info
{

class HoverStatus : public juce::MouseListener
{
public:
    HoverStatus (juce::AudioProcessorValueTreeState&, StatusBoard&);
    ~HoverStatus() override;

    void mouseEnter (const juce::MouseEvent& ev) override { update (&ev, false); }
    void mouseMove (const juce::MouseEvent& ev) override { update (&ev, false); }
    void mouseDrag (const juce::MouseEvent& ev) override { update (&ev, false); }
    void mouseUp (const juce::MouseEvent& ev) override   { update (&ev, true); }
    void mouseExit (const juce::MouseEvent&) override    { board.clear (StatusKind::Hover); dragging = false; }

    /** Parameter id of `c` or its nearest ancestor that edits one; empty if none. */
    static juce::String parameterIdFor (const juce::Component* c);

    /** Test seam: simulate the pointer being over `c` at time `nowMs`. */
    void hoverForTests (const juce::Component* c, juce::uint32 nowMs) { refresh (c, nowMs, true); }

private:
    void update (const juce::MouseEvent*, bool force);
    void refresh (const juce::Component*, juce::uint32 nowMs, bool force);

    juce::AudioProcessorValueTreeState& state;
    StatusBoard& board;
    UpdateLimiter limiter { 50 };
    bool dragging = false;
    const juce::Component* lastComponent = nullptr;
};

} // namespace gapfill::info
