#include "HelpRegistry.h"

#include <algorithm>

namespace gapfill
{

namespace
{
    std::vector<HelpSection>& storage()
    {
        static std::vector<HelpSection> sections;   // append-only at start-up
        return sections;
    }
}

void HelpRegistry::add (int order, juce::String title, juce::String body)
{
    add (order, std::move (title), [text = std::move (body)] (HelpWriter& w)
    {
        for (const auto& line : juce::StringArray::fromLines (text))
            if (line.isNotEmpty())
                w.body (line);
    });
}

void HelpRegistry::add (int order, juce::String title, std::function<void (HelpWriter&)> write)
{
    storage().push_back ({ order, std::move (title), std::move (write) });
}

std::vector<HelpSection> HelpRegistry::getSections()
{
    auto copy = storage();
    std::stable_sort (copy.begin(), copy.end(),
                      [] (const HelpSection& a, const HelpSection& b)
                      {
                          // Orders are meant to be unique; the title tie-break only
                          // keeps the result independent of static-init order.
                          return a.order != b.order ? a.order < b.order : a.title < b.title;
                      });
    return copy;
}

int HelpRegistry::getNumSections()
{
    return (int) storage().size();
}

} // namespace gapfill
