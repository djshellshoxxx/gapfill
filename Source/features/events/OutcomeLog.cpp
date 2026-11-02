#include "Replay.h"

#include <algorithm>

namespace gapfill::events
{
namespace detail
{
OutcomeKey outcomeKeyAt (const ReplayData& data, size_t index)
{
    const auto& record = data.records[index];
    OutcomeKey key { (record.flags & 1) != 0, (std::uint8_t) (record.index & 0xFF), record.pos };
    key.detailed = (record.flags & kOutcomeHasDetails) != 0;
    if (key.detailed)
        outcomeDetailsAt (data, index, key.recipe, key.extras);
    return key;
}
}

std::vector<OutcomeKey> outcomesOf (const ReplayData& data)
{
    std::vector<OutcomeKey> outcomes;
    for (size_t i = 0; i < data.records.size(); ++i)
        if (data.records[i].type == (std::uint8_t) RecType::Outcome)
            outcomes.push_back (detail::outcomeKeyAt (data, i));
    return outcomes;
}

int findFirstDivergence (const std::vector<OutcomeKey>& expected, const std::vector<OutcomeKey>& actual) noexcept
{
    const auto count = std::min (expected.size(), actual.size());
    for (size_t i = 0; i < count; ++i)
        if (! (expected[i] == actual[i]))
            return (int) i;
    return expected.size() == actual.size() ? -1 : (int) count;
}
} // namespace gapfill::events
