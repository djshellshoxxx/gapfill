#include "SceneController.h"

#include <cmath>

namespace gapfill::scenes
{

SceneController::SceneController (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    initTables();
    initOverlayTargets();
}

void SceneController::publishOverlay (Overlay::Kind kind)
{
    Overlay o;
    o.kind = kind;
    o.id = pendingId;
    pub.publish (o);
}

bool SceneController::request (int index)
{
    if (index < 0 || index >= bank.size())
    {
        status = "Scene " + juce::String (index + 1) + " does not exist";
        return false;
    }

    const auto& scene = bank.at (index);
    std::vector<ResolvedParam> entries;
    juce::String why;

    if (! resolveScene (apvts, scene, entries, why))
    {
        status = "Scene \"" + scene.name + "\" is invalid (" + why + ") and was not applied";
        return false;
    }

    if (++idCounter == 0)
        ++idCounter;

    pendingId = idCounter;
    pendingIndex = index;
    resolved = std::move (entries);
    baseline01.clear();

    Overlay o;
    o.kind = Overlay::Set;
    o.id = pendingId;
    o.quant = (std::uint8_t) (scene.quant == Quant::Inherit ? bank.options.quant : scene.quant);
    o.waitForTransport = bank.options.immediateWithoutTransport ? 0 : 1;
    pendingQuant = o.quant;
    pendingWait = o.waitForTransport;
    o.n = (std::uint16_t) resolved.size();

    for (size_t i = 0; i < resolved.size(); ++i)
    {
        o.idx[i] = resolved[i].index;
        o.real[i] = resolved[i].real;

        auto* p = apvts.getParameter (paramIdAt (resolved[i].index));
        baseline01.push_back (p != nullptr ? p->getValue() : 0.0f);
    }

    pub.publish (o);
    status = "Pending: " + scene.name;
    return true;
}

bool SceneController::gotoScene (int index)
{
    return request (index);
}

int SceneController::clampStep (int from, int delta) const
{
    const auto n = bank.size();

    if (n <= 0)
        return -1;

    if (from < 0)
        return delta > 0 ? 0 : (bank.options.wrap ? n - 1 : 0);

    auto t = from + delta;

    if (t >= n)
        t = bank.options.wrap ? 0 : n - 1;
    else if (t < 0)
        t = bank.options.wrap ? n - 1 : 0;

    return t;
}

bool SceneController::next()
{
    const auto base = pendingIndex >= 0 ? pendingIndex : bank.current;
    const auto target = clampStep (base, +1);

    if (target < 0)
        return false;

    if (target == pendingIndex || (pendingIndex < 0 && target == bank.current))
        return true;                      // clamped at the end: nothing to do

    return request (target);
}

bool SceneController::prev()
{
    const auto base = pendingIndex >= 0 ? pendingIndex : bank.current;
    const auto target = clampStep (base, -1);

    if (target < 0)
        return false;

    if (target == pendingIndex || (pendingIndex < 0 && target == bank.current))
        return true;

    return request (target);
}

void SceneController::cancel()
{
    if (pendingId == 0)
        return;

    Overlay o;
    o.kind = Overlay::Cancel;
    o.id = pendingId;
    pub.publish (o);

    pendingIndex = -1;
    pendingId = 0;
    resolved.clear();
    baseline01.clear();
    status = "Scene change cancelled";
}

bool SceneController::gotoLastGood()
{
    return lastGood >= 0 && request (lastGood);
}

bool SceneController::saveCurrent (int index, const juce::String& name, std::uint16_t scope)
{
    return bank.store (index, captureScene (apvts, name, scope));
}

void SceneController::onApplied (std::uint32_t requestId)
{
    if (requestId == 0)
        return;

    if (requestId != pendingId)
    {
        Overlay o;
        o.kind = Overlay::Clear;
        o.id = requestId;
        pub.publish (o);
        return;
    }

    committing = true;

    for (const auto& r : resolved)
    {
        if (auto* p = apvts.getParameter (paramIdAt (r.index)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (r.real));
            p->endChangeGesture();
        }
    }

    committing = false;

    bank.current = pendingIndex;
    lastGood = pendingIndex;
    status = "Scene: " + (pendingIndex >= 0 && pendingIndex < bank.size() ? bank.at (pendingIndex).name : juce::String());

    Overlay o;
    o.kind = Overlay::Clear;
    o.id = requestId;
    pub.publish (o);

    pendingIndex = -1;
    pendingId = 0;
    resolved.clear();
    baseline01.clear();
    ++commits;
}

void SceneController::poll()
{
    if (pendingId == 0 || committing || resolved.empty())
        return;

    bool changed = false;

    for (size_t i = 0; i < resolved.size();)
    {
        auto* p = apvts.getParameter (paramIdAt (resolved[i].index));

        if (p != nullptr && ! juce::exactlyEqual (p->getValue(), baseline01[i]))
        {
            resolved.erase (resolved.begin() + (std::ptrdiff_t) i);
            baseline01.erase (baseline01.begin() + (std::ptrdiff_t) i);
            changed = true;
        }
        else
        {
            ++i;
        }
    }

    if (changed)
    {
        Overlay o;
        o.kind = Overlay::Set;
        o.id = pendingId;
        o.quant = pendingQuant;
        o.waitForTransport = pendingWait;
        o.n = (std::uint16_t) resolved.size();

        for (size_t i = 0; i < resolved.size(); ++i)
        {
            o.idx[i] = resolved[i].index;
            o.real[i] = resolved[i].real;
        }

        pub.publish (o);
    }
}

void SceneController::replaceBank (const SceneBank& newBank)
{
    cancel();
    bank.assign (newBank);

    if (bank.current >= bank.size())
        bank.current = -1;

    if (lastGood >= bank.size())
        lastGood = -1;
}

void SceneController::rememberForUndo()
{
    undoBank.assign (bank);
    hasUndo = true;
}

bool SceneController::undoImport()
{
    if (! hasUndo)
        return false;

    replaceBank (undoBank);
    hasUndo = false;
    return true;
}

void SceneController::reset()
{
    cancel();
    bank.clear();
    lastGood = -1;
    hasUndo = false;
    status = {};
}

} // namespace gapfill::scenes
