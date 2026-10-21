#include "Fixit.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>

namespace gapfill::fixit
{

using health::HealthCode;
using R = RejectReason;

const char* ruleName (RuleId r) noexcept
{
    switch (r)
    {
        case RuleId::FillOff:     return "fill_off";
        case RuleId::TooShort:    return "too_short";
        case RuleId::TooSoon:     return "too_soon";
        case RuleId::DensityRoll: return "density_roll";
        case RuleId::NoVoice:     return "no_voice";
        case RuleId::NoAlgo:      return "no_algo";
        case RuleId::NoBand:      return "no_band";
        case RuleId::LockSkipped: return "lock_skipped";
        case RuleId::NoSource:    return "no_source";
        case RuleId::GuardEaten:  return "guard_eaten";
        case RuleId::Overload:    return "overload";
        case RuleId::NoTransport: return "no_transport";
        case RuleId::Panicked:    return "panicked";
        case RuleId::Count:       break;
    }

    return "";
}

bool isAllowedParameter (const juce::String& id) noexcept
{
    // The only parameters any rule may write (dry/wet/output/bypass/lookahead/seed
    // and anything that changes latency are deliberately absent).
    static const char* const allowed[] = { "fill", "minGap", "retrigGuard", "density", "maxFill", "fillType",
                                           "bandLowOn", "bandMidOn", "bandHighOn", "fillLock", "detSource",
                                           "protNext", "qualityMode" };

    for (auto* a : allowed)
        if (id == a)
            return true;

    return false;
}

//==============================================================================
bool ApvtsParamAccess::has (const juce::String& id) const       { return state.getParameter (id) != nullptr; }

float ApvtsParamAccess::getReal (const juce::String& id) const
{
    if (auto* p = state.getRawParameterValue (id))
        return p->load();

    return 0.0f;
}

void ApvtsParamAccess::getRange (const juce::String& id, float& lo, float& hi) const
{
    const auto r = state.getParameterRange (id);
    lo = r.start;
    hi = r.end;
}

float ApvtsParamAccess::getNormalised (const juce::String& id) const
{
    if (auto* p = state.getParameter (id))
        return p->getValue();

    return 0.0f;
}

void ApvtsParamAccess::setNormalised (const juce::String& id, float normalised)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        p->endChangeGesture();
    }
}

float ApvtsParamAccess::toNormalised (const juce::String& id, float real) const
{
    return state.getParameterRange (id).convertTo0to1 (state.getParameterRange (id).snapToLegalValue (real));
}

//==============================================================================
Evidence buildEvidence (const FillEvent* newestFirst, int count, std::uint32_t healthMask,
                        float overloadWarnSeconds, float panicSeconds)
{
    Evidence e;
    e.healthMask = healthMask;
    e.overloadWarnSeconds = overloadWarnSeconds;
    e.panicSeconds = panicSeconds;

    std::vector<float> shortAvail;
    const auto n = juce::jmin (count, Evidence::windowGaps);

    for (int i = 0; i < n; ++i)
    {
        const auto& ev = newestFirst[i];

        if (ev.type != FillEventType::Started && ev.type != FillEventType::Rejected)
            continue;

        ++e.numGaps;

        if (ev.type == FillEventType::Rejected && (int) ev.reason < numRejectReasons)
        {
            ++e.reasons[(size_t) ev.reason];

            if (ev.reason == R::TooShort)
                shortAvail.push_back (ev.availMs);
        }
    }

    if (! shortAvail.empty())
    {
        std::sort (shortAvail.begin(), shortAvail.end());
        e.medianTooShortAvailMs = shortAvail[shortAvail.size() / 2];
    }

    return e;
}

//==============================================================================
namespace
{
    int roundInt (float v) { return (int) std::lround (v); }

    juce::String fmtNumber (float v)   { return juce::String (roundInt (v)); }

    Change numeric (const char* id, const char* label, const char* unit, float from, float to)
    {
        Change c;
        c.paramId = id;
        c.label = label;
        c.unit = unit;
        c.from = from;
        c.to = to;
        return c;
    }

    Change named (const char* id, const char* label, float from, float to, const char* toName)
    {
        Change c;
        c.paramId = id;
        c.label = label;
        c.from = from;
        c.to = to;
        c.toName = toName;
        return c;
    }

    float clampToRange (const ParamAccess& pa, const juce::String& id, float v)
    {
        float lo = 0.0f, hi = 1.0f;
        pa.getRange (id, lo, hi);
        return juce::jlimit (lo, hi, v);
    }

    float floorTo5 (float v)   { return 5.0f * std::floor (v / 5.0f); }

    Proposal makeProposal (RuleId rule, float strength, std::vector<Change> changes, bool confirm = false)
    {
        Proposal p;
        p.rule = rule;
        p.strength = strength;
        p.changes = std::move (changes);
        p.needsConfirm = confirm;

        for (auto& c : p.changes)
        {
            if (! p.text.isEmpty())
                p.text << ", ";

            if (c.toName.isNotEmpty())
                p.text << c.label << " -> " << c.toName;
            else
                p.text << c.label << " " << fmtNumber (c.from) << " -> " << fmtNumber (c.to) << c.unit;
        }

        p.spoken = "Suggestion: change " + p.text + ". Press Apply to accept.";
        return p;
    }

    Proposal makeAction (RuleId rule, ActionKind a, const char* text)
    {
        Proposal p;
        p.rule = rule;
        p.strength = 1.0f;
        p.action = a;
        p.text = text;
        p.spoken = juce::String ("Suggestion: ") + text + ". Press Apply to do it.";
        return p;
    }

    bool health (const Evidence& e, HealthCode c)   { return (e.healthMask & health::bitOf (c)) != 0; }
}

std::vector<Proposal> evaluateRules (const Evidence& e, const ParamAccess& pa)
{
    std::vector<Proposal> out;
    auto count = [&e] (R r) { return e.reasons[(size_t) r]; };

    // R1 fill_off
    if (count (R::FillOff) >= 3 && pa.has ("fill") && pa.getReal ("fill") < 50.0f)
        out.push_back (makeProposal (RuleId::FillOff, (float) count (R::FillOff) / 3.0f,
                                     { numeric ("fill", "Fill", "%", pa.getReal ("fill"), 50.0f) }));

    // R2 too_short
    if (count (R::TooShort) >= 5 && pa.has ("minGap"))
    {
        const auto cur = pa.getReal ("minGap");
        const auto target = clampToRange (pa, "minGap", juce::jmax (20.0f, floorTo5 (e.medianTooShortAvailMs)));

        if (target < cur - 0.5f)
            out.push_back (makeProposal (RuleId::TooShort, (float) count (R::TooShort) / 5.0f,
                                         { numeric ("minGap", "Min Gap", " ms", cur, target) }));
    }

    // R3 too_soon
    if (count (R::TooSoon) >= 5 && pa.has ("retrigGuard"))
    {
        const auto cur = pa.getReal ("retrigGuard");
        const auto target = clampToRange (pa, "retrigGuard", std::round (cur * 0.7f));

        if (target < cur - 0.5f)
            out.push_back (makeProposal (RuleId::TooSoon, (float) count (R::TooSoon) / 5.0f,
                                         { numeric ("retrigGuard", "Retrig Guard", " ms", cur, target) }));
    }

    // R4 density_roll
    if (count (R::DensityRoll) >= 6 && pa.has ("density"))
    {
        const auto cur = pa.getReal ("density");
        const auto target = clampToRange (pa, "density", cur + 20.0f);

        if (target > cur + 0.5f)
            out.push_back (makeProposal (RuleId::DensityRoll, (float) count (R::DensityRoll) / 6.0f,
                                         { numeric ("density", "Density", "%", cur, target) }));
    }

    // R5 no_voice
    if (count (R::NoVoice) >= 5 && pa.has ("maxFill"))
    {
        const auto cur = pa.getReal ("maxFill");
        const auto target = clampToRange (pa, "maxFill", juce::jmax (20.0f, std::round (cur * 0.8f)));

        if (target < cur - 0.5f)
            out.push_back (makeProposal (RuleId::NoVoice, (float) count (R::NoVoice) / 5.0f,
                                         { numeric ("maxFill", "Max Fill", " ms", cur, target) }));
    }

    // R6 no_algo (only when a specific algorithm is forced)
    if (count (R::NoAlgoFits) >= 4 && pa.has ("fillType") && roundInt (pa.getReal ("fillType")) != 0)
        out.push_back (makeProposal (RuleId::NoAlgo, (float) count (R::NoAlgoFits) / 4.0f,
                                     { named ("fillType", "Fill Type", pa.getReal ("fillType"), 0.0f, "Auto") }));

    // R7 no_band: switch on the first disabled band
    if (count (R::NoBand) >= 4)
    {
        for (const char* id : { "bandLowOn", "bandMidOn", "bandHighOn" })
        {
            if (pa.has (id) && pa.getReal (id) < 0.5f)
            {
                const char* label = id[4] == 'L' ? "Low band" : id[4] == 'M' ? "Mid band" : "High band";
                out.push_back (makeProposal (RuleId::NoBand, (float) count (R::NoBand) / 4.0f,
                                             { named (id, label, 0.0f, 1.0f, "On") }));
                break;
            }
        }
    }

    // R8 lock_skipped (parameter arrives with C-5; inactive until then)
    if (count (R::LockSkipped) >= 3 && pa.has ("fillLock") && pa.getReal ("fillLock") > 0.5f)
        out.push_back (makeProposal (RuleId::LockSkipped, (float) count (R::LockSkipped) / 3.0f,
                                     { named ("fillLock", "Fill Lock", 1.0f, 0.0f, "Off") }));

    // R9 no_source: sidechain selected but absent or silent
    if (count (R::NoSource) >= 4 && pa.has ("detSource") && roundInt (pa.getReal ("detSource")) == 1
         && (health (e, HealthCode::NoSidechain) || health (e, HealthCode::SidechainSilent)))
        out.push_back (makeProposal (RuleId::NoSource, (float) count (R::NoSource) / 4.0f,
                                     { named ("detSource", "Detector Source", pa.getReal ("detSource"), 0.0f, "Main") }));

    // R10 guard_eaten: weakens protection => explicit confirmation
    if (count (R::GuardEaten) >= 5 && pa.has ("protNext"))
    {
        const auto cur = pa.getReal ("protNext");
        const auto target = clampToRange (pa, "protNext", std::round (cur * 0.8f));

        if (target < cur - 0.5f)
            out.push_back (makeProposal (RuleId::GuardEaten, (float) count (R::GuardEaten) / 5.0f,
                                         { numeric ("protNext", "Protect Next", "%", cur, target) }, true));
    }

    // R11 overload: needs the quality modes of F-D03 (inactive until that parameter exists)
    if (e.overloadWarnSeconds >= 5.0f && pa.has ("qualityMode") && roundInt (pa.getReal ("qualityMode")) == 0)
        out.push_back (makeProposal (RuleId::Overload, e.overloadWarnSeconds / 5.0f,
                                     { named ("qualityMode", "Quality", 0.0f, 1.0f, "Balanced") }));

    // R12 / R13 actions (not undoable)
    if (health (e, HealthCode::NoTransport))
        out.push_back (makeAction (RuleId::NoTransport, ActionKind::OpenTempoOptions,
                                   "Open Options: Tempo when host provides none"));

    if (health (e, HealthCode::PanicActive) && e.panicSeconds > 10.0f)
        out.push_back (makeAction (RuleId::Panicked, ActionKind::Recover, "Recover from Panic"));

    std::stable_sort (out.begin(), out.end(), [] (const Proposal& a, const Proposal& b)
    {
        if (a.strength != b.strength)
            return a.strength > b.strength;

        return (int) a.rule < (int) b.rule;
    });

    return out;
}

//==============================================================================
bool Controller::isOnCooldown (RuleId r, std::uint32_t nowMs) const noexcept
{
    return cooldownUntil[(size_t) r] != 0 && (std::int32_t) (cooldownUntil[(size_t) r] - nowMs) > 0;
}

void Controller::tick (std::uint32_t nowMs, const Evidence& evidence, ParamAccess& params,
                       bool enabled, std::uint32_t mutedRules)
{
    // The "Applied ... [Undo]" chip is independent of new evidence.
    if (chip.kind == Chip::Kind::Applied)
    {
        bool touched = false;

        for (const auto& a : applied)
            touched = touched || std::abs (params.getNormalised (a.id) - a.appliedNormalised) > 1.0e-6f;

        if (touched || (std::uint32_t) (nowMs - appliedAt) >= timing.appliedMs)
        {
            chip = {};
            applied.clear();
        }

        return;
    }

    if (! enabled)
    {
        chip = {};
        firstSeen.fill (0);
        return;
    }

    auto candidates = evaluateRules (evidence, params);

    // Drop muted rules and rules on cooldown; track how long each has been firing.
    std::array<bool, (size_t) numRules> firing {};
    std::vector<Proposal> eligible;

    for (auto& p : candidates)
    {
        const auto idx = (size_t) p.rule;

        if ((mutedRules & (1u << (unsigned) idx)) != 0 || isOnCooldown (p.rule, nowMs))
            continue;

        firing[idx] = true;

        if (firstSeen[idx] == 0)
            firstSeen[idx] = nowMs + 1;

        if ((std::uint32_t) (nowMs + 1 - firstSeen[idx]) >= timing.holdMs)
            eligible.push_back (p);
    }

    for (int i = 0; i < numRules; ++i)
        if (! firing[(size_t) i])
            firstSeen[(size_t) i] = 0;

    if (chip.kind == Chip::Kind::Suggestion)
    {
        const bool stillValid = std::any_of (eligible.begin(), eligible.end(),
                                             [this] (const Proposal& p) { return p.rule == chip.proposal.rule; });

        if ((std::uint32_t) (nowMs - shownAt) < timing.minShowMs)
            return;                                                       // shown for at least 6 s, even if the evidence fades

        if (! stillValid && eligible.empty())
        {
            chip = {};
            return;
        }

        // Rotate to the next eligible suggestion (or stay on the only one).
        if (eligible.size() > 1)
        {
            std::size_t cur = 0;

            for (std::size_t i = 0; i < eligible.size(); ++i)
                if (eligible[i].rule == chip.proposal.rule)
                    cur = i;

            const auto next = stillValid ? (cur + 1) % eligible.size() : 0u;
            chip.proposal = eligible[next];
            shownAt = nowMs;
        }
        else if (! stillValid)
        {
            chip.proposal = eligible[0];
            shownAt = nowMs;
        }
        else
        {
            chip.proposal = eligible[0];       // refresh numbers; same rule
        }

        return;
    }

    if (! eligible.empty())
    {
        chip.kind = Chip::Kind::Suggestion;
        chip.proposal = eligible[0];
        chip.canUndo = false;
        shownAt = nowMs;
    }
}

bool Controller::apply (std::uint32_t nowMs, ParamAccess& params, bool confirmed)
{
    if (chip.kind != Chip::Kind::Suggestion)
        return false;

    const auto proposal = chip.proposal;

    if (proposal.needsConfirm && ! confirmed)
        return false;

    cooldownUntil[(size_t) proposal.rule] = nowMs + timing.cooldownMs;
    firstSeen[(size_t) proposal.rule] = 0;

    if (proposal.action != ActionKind::None)
    {
        if (hooks.runAction != nullptr)
            hooks.runAction (proposal.action);

        chip = {};
        return true;
    }

    // Defence in depth: the table above is the whitelist, enforce it again here.
    for (const auto& c : proposal.changes)
        if (! isAllowedParameter (c.paramId) || ! params.has (c.paramId))
        {
            chip = {};
            return false;
        }

    if (hooks.pushUndoSnapshot != nullptr)
        hooks.pushUndoSnapshot();

    applied.clear();

    for (const auto& c : proposal.changes)
    {
        const auto previous = params.getNormalised (c.paramId);
        params.setNormalised (c.paramId, params.toNormalised (c.paramId, c.to));
        applied.push_back ({ c.paramId, previous, params.getNormalised (c.paramId) });
    }

    chip.kind = Chip::Kind::Applied;
    chip.proposal = proposal;
    chip.canUndo = true;
    appliedAt = nowMs;
    return true;
}

bool Controller::undo (ParamAccess& params)
{
    if (chip.kind != Chip::Kind::Applied)
        return false;

    bool touched = false;

    for (const auto& a : applied)
        touched = touched || std::abs (params.getNormalised (a.id) - a.appliedNormalised) > 1.0e-6f;

    if (! touched)
        for (const auto& a : applied)
            params.setNormalised (a.id, a.previousNormalised);

    chip = {};
    applied.clear();
    return ! touched;
}

void Controller::dismiss (std::uint32_t nowMs)
{
    if (chip.kind == Chip::Kind::Suggestion)
    {
        cooldownUntil[(size_t) chip.proposal.rule] = nowMs + timing.cooldownMs;
        firstSeen[(size_t) chip.proposal.rule] = 0;
    }

    chip = {};
}

} // namespace gapfill::fixit
