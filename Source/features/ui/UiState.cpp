#include "UiState.h"
#include "../../Parameters.h"

namespace gapfill::ui
{

UiState::UiState()
{
    headerAb.setPerformanceIds (performanceParameterIDs());
}

void UiState::writeState (juce::ValueTree& root) const
{
    root.setProperty (viewModeProperty, (int) getViewMode(), nullptr);
}

void UiState::readState (const juce::ValueTree& root)
{
    headerAb.requestReset(); // host restore may occur off the message thread
    if (! root.hasProperty (viewModeProperty))
    {
        setViewMode (ViewMode::Advanced);          // saved before U-5: nothing changes for the user
        return;
    }

    const auto v = root.getProperty (viewModeProperty);

    if (v.isInt() || v.isInt64() || v.isDouble() || v.isString())
    {
        const int value = (int) v;

        if (value == (int) ViewMode::Simple || value == (int) ViewMode::Advanced)
            setViewMode ((ViewMode) value);
    }
}

} // namespace gapfill::ui
