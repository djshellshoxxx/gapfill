#include "Battle.h"

#include <cmath>

namespace gapfill::battle
{

const char* statusText (Status s) noexcept
{
    switch (s)
    {
        case Status::Off:         return "Battle: off";
        case Status::NeedsStyles: return "Battle: store two different styles first";
        case Status::NoTransport: return "Battle: needs transport and time signature";
        case Status::StyleA:      return "Battle: A";
        case Status::StyleB:      return "Battle: B";
    }

    return "Battle: off";
}

int styleForPosition (double ppq, int tsNum, int tsDen, int everyBars) noexcept
{
    if (everyBars <= 0)
        return -1;

    const auto barLen = scenes::quantiser::barLength (tsNum, tsDen);
    const auto bar = (long long) std::floor (ppq / barLen + 1.0e-9);
    const auto window = (bar >= 0 ? bar : bar - (everyBars - 1)) / everyBars;           // floor division, also before PPQ 0
    return (int) (((window % 2) + 2) % 2);
}

//==============================================================================
int BattleRuntime::styleAt (double ppq, bool ppqValid, int tsNum, int tsDen) const noexcept
{
    if (everyBars <= 0 || ! haveStyles || ! ppqValid || tsNum <= 0 || tsDen <= 0)
        return -1;

    return styleForPosition (ppq, tsNum, tsDen, everyBars);
}

void BattleRuntime::adopt (const BattleConfig& c) noexcept
{
    haveStyles = c.differ != 0;

    auto fill = [] (scenes::OverlayView& v, const std::uint16_t* idx, const float* real, int n)
    {
        v.clear();
        v.active = true;
        v.immediate = true;
        v.n = std::min<int> (n, scenes::kMaxOverlayEntries);

        for (int i = 0; i < v.n; ++i)
        {
            v.refs[i] = scenes::overlayTargetFor (idx[i]);
            v.real[i] = real[i];
        }
    };

    fill (a, c.idxA, c.realA, c.nA);
    fill (b, c.idxB, c.realB, c.nB);
}

void BattleRuntime::applyAt (EngineSettings& s, double ppq, bool ppqValid, int tsNum, int tsDen) const noexcept
{
    const auto which = styleAt (ppq, ppqValid, tsNum, tsDen);

    if (which == 0)
        a.applyTo (s);
    else if (which == 1)
        b.applyTo (s);
}

//==============================================================================
bool BattleModel::hasStyle (int which) const noexcept
{
    return which >= 0 && which < 2 && style[which].set;
}

bool BattleModel::stylesDiffer() const noexcept
{
    if (! style[0].set || ! style[1].set)
        return false;

    const auto& a = style[0].params;
    const auto& b = style[1].params;

    if (a.size() != b.size())
        return true;

    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].index != b[i].index || ! juce::exactlyEqual (a[i].real, b[i].real))
            return true;

    return false;
}

bool BattleModel::setStyleFromState (int which, juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& state)
{
    if (which < 0 || which > 1)
        return false;

    scenes::initTables();
    Style s;
    s.set = true;

    for (const auto& id : scenes::idsForScope (kBattleScope))
    {
        auto* p = apvts.getParameter (id);

        if (p == nullptr)
            continue;

        float real = p->convertFrom0to1 (p->getDefaultValue());
        const auto node = state.getChildWithProperty ("id", id);

        if (node.isValid())
        {
            const auto v = (float) node.getProperty ("value", real);

            if (std::isfinite (v))
                real = p->convertFrom0to1 (p->convertTo0to1 (v));           // clamp / snap to the legal range
        }

        s.params.push_back ({ (std::uint16_t) scenes::paramIndexOf (id), real });
    }

    if (s.params.size() > (size_t) scenes::kMaxOverlayEntries)
        return false;

    style[which] = std::move (s);
    publish();
    return true;
}

bool BattleModel::setStyleFromCurrent (int which, juce::AudioProcessorValueTreeState& apvts)
{
    return setStyleFromState (which, apvts, apvts.copyState());
}

void BattleModel::clear()
{
    style[0] = {};
    style[1] = {};
    publish();
}

void BattleModel::publish()
{
    BattleConfig c;
    c.differ = stylesDiffer() ? 1 : 0;

    auto copy = [] (const Style& s, std::uint16_t* idx, float* real, std::uint16_t& n)
    {
        n = (std::uint16_t) std::min<size_t> (s.params.size(), scenes::kMaxOverlayEntries);

        for (int i = 0; i < n; ++i)
        {
            idx[i] = s.params[(size_t) i].index;
            real[i] = s.params[(size_t) i].real;
        }
    };

    copy (style[0], c.idxA, c.realA, c.nA);
    copy (style[1], c.idxB, c.realB, c.nB);
    pub.publish (c);
}

} // namespace gapfill::battle
