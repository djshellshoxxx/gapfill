#include "ParamInfo.h"

#include "../../Parameters.h"

namespace gapfill::info
{

ParamInfoRegistry& ParamInfoRegistry::get()
{
    static ParamInfoRegistry registry;
    return registry;
}

void ParamInfoRegistry::add (const ParamInfo& info)
{
    // A later registration of the same id replaces the earlier one (feature override).
    for (auto& e : entries)
        if (juce::String (e.id) == info.id)
        {
            e = info;
            return;
        }

    entries.push_back (info);
}

const ParamInfo* ParamInfoRegistry::find (const juce::String& id) const noexcept
{
    for (const auto& e : entries)
        if (id == e.id)
            return &e;

    return nullptr;
}

std::optional<float> ParamInfoRegistry::effectiveValue (const juce::String& id) const
{
    return effective != nullptr ? effective (id) : std::nullopt;
}

juce::String tooltipFor (const ParamInfo& i)
{
    juce::String text = juce::String (i.shortName) + " - " + i.oneLine;

    if (juce::String (i.typicalRange).isNotEmpty())
    {
        if (! text.endsWithChar ('.'))
            text << ".";

        text << " Typical: " << i.typicalRange;

        if (juce::String (i.unit).isNotEmpty())
            text << " " << i.unit;
    }

    return text;
}

juce::String tooltipFor (const juce::String& id, const juce::String& fallback)
{
    if (const auto* i = ParamInfoRegistry::get().find (id))
        return tooltipFor (*i);

    return fallback;
}

juce::StringArray idsMissingInfo()
{
    juce::StringArray missing;

    for (const auto& id : allParameterIDs())
        if (ParamInfoRegistry::get().find (id) == nullptr)
            missing.add (id);

    return missing;
}

} // namespace gapfill::info
