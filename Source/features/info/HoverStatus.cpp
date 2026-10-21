#include "HoverStatus.h"

#include "../../gui/Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace gapfill::info
{

HoverStatus::HoverStatus (juce::AudioProcessorValueTreeState& s, StatusBoard& b) : state (s), board (b) {}

HoverStatus::~HoverStatus()
{
    board.clear (StatusKind::Hover);
}

juce::String HoverStatus::parameterIdFor (const juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
    {
        const auto& props = c->getProperties();

        if (props.contains (kParamIdProperty))
            return props[kParamIdProperty].toString();
    }

    return {};
}

void HoverStatus::update (const juce::MouseEvent* ev, bool force)
{
    if (ev == nullptr)
        return;

    const auto* c = ev->eventComponent;

    // During a drag the mouse can leave the knob; keep showing the dragged parameter.
    if (ev->mouseWasDraggedSinceMouseDown())
    {
        dragging = true;
        c = lastComponent != nullptr ? lastComponent : c;
    }
    else if (dragging)
    {
        dragging = false;
        force = true;
    }

    refresh (c, juce::Time::getMillisecondCounter(), force);
}

void HoverStatus::refresh (const juce::Component* c, juce::uint32 nowMs, bool force)
{
    const auto id = parameterIdFor (c);

    if (id.isEmpty())
    {
        lastComponent = nullptr;
        board.clear (StatusKind::Hover);
        return;
    }

    lastComponent = c;

    if (! force && ! limiter.allow (nowMs))
        return;

    board.set (StatusKind::Hover, describeParameter (state, id));
}

} // namespace gapfill::info
