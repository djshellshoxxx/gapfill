// N-EASY Fix-it Suggestions (docs/dev/12 N-EASY).

#include "../support/TestRig.h"

#include "features/fixit/Fixit.h"
#include "features/health/Health.h"

#include <map>
#include <set>

using namespace gapfill;
using namespace gapfill::fixit;
using health::HealthCode;
using health::bitOf;
using namespace gftest;

namespace
{
    /** In-memory parameters with explicit ranges (real units == normalised * range). */
    class FakeParams : public ParamAccess
    {
    public:
        struct P { float value, lo, hi; };
        std::map<juce::String, P> p;
        std::vector<juce::String> written;

        FakeParams()
        {
            p["fill"] = { 35, 0, 100 };           p["minGap"] = { 120, 10, 500 };
            p["retrigGuard"] = { 30, 5, 250 };    p["density"] = { 65, 0, 100 };
            p["maxFill"] = { 400, 20, 2000 };     p["fillType"] = { 3, 0, 15 };
            p["bandLowOn"] = { 1, 0, 1 };         p["bandMidOn"] = { 0, 0, 1 };
            p["bandHighOn"] = { 0, 0, 1 };        p["detSource"] = { 1, 0, 2 };
            p["protNext"] = { 70, 0, 100 };       p["fl01_lock"] = { 1, 0, 1 };
            p["qualityMode"] = { 0, 0, 3 };       p["dry"] = { 1, 0, 1 };
        }

        bool has (const juce::String& id) const override                  { return p.count (id) > 0; }
        float getReal (const juce::String& id) const override             { return p.at (id).value; }
        void getRange (const juce::String& id, float& lo, float& hi) const override { lo = p.at (id).lo; hi = p.at (id).hi; }
        float getNormalised (const juce::String& id) const override       { return toNormalised (id, p.at (id).value); }
        float toNormalised (const juce::String& id, float r) const override
        {
            const auto& q = p.at (id);
            return (juce::jlimit (q.lo, q.hi, r) - q.lo) / (q.hi - q.lo);
        }
        void setNormalised (const juce::String& id, float n) override
        {
            auto& q = p.at (id);
            q.value = q.lo + n * (q.hi - q.lo);
            written.push_back (id);
        }
    };

    Evidence evidenceWith (std::initializer_list<std::pair<RejectReason, int>> reasons, std::uint32_t mask = 0)
    {
        std::vector<FillEvent> log;

        for (const auto& r : reasons)
            for (int i = 0; i < r.second; ++i)
            {
                FillEvent e;
                e.type = FillEventType::Rejected;
                e.reason = r.first;
                e.availMs = 40.0f + (float) i;     // distinct so the median is meaningful
                e.needMs = 120.0f;
                log.push_back (e);
            }

        return buildEvidence (log.data(), (int) log.size(), mask);
    }

    const Proposal* find (const std::vector<Proposal>& v, RuleId r)
    {
        for (const auto& p : v)
            if (p.rule == r)
                return &p;

        return nullptr;
    }
}

GF_TEST (Fixit, one_synthetic_log_per_rule_gives_the_exact_proposal)
{
    FakeParams pa;

    {   // R1
        const auto v = evaluateRules (evidenceWith ({ { RejectReason::FillOff, 3 } }), pa);
        const auto* p = find (v, RuleId::FillOff);
        GF_EXPECT (p != nullptr && p->changes.size() == 1);
        GF_EXPECT (p->changes[0].paramId == "fill" && p->changes[0].to == 50.0f && p->text == "Fill 35 -> 50%");
    }
    {   // R2: median of avail = 40..44 -> 42 -> floor5 = 40
        const auto v = evaluateRules (evidenceWith ({ { RejectReason::TooShort, 5 } }), pa);
        const auto* p = find (v, RuleId::TooShort);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "minGap" && p->changes[0].to == 40.0f);
        GF_EXPECT (p->text == "Min Gap 120 -> 40 ms");
    }
    {   // R3: 70 % of 30 = 21
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::TooSoon, 5 } }), pa);
        const auto* p = find (v_p, RuleId::TooSoon);
        GF_EXPECT (p != nullptr && p->changes[0].to == 21.0f && p->text == "Retrig Guard 30 -> 21 ms");
    }
    {   // R4
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::DensityRoll, 6 } }), pa);
        const auto* p = find (v_p, RuleId::DensityRoll);
        GF_EXPECT (p != nullptr && p->changes[0].to == 85.0f);
    }
    {   // R5: 80 % of 400
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::NoVoice, 5 } }), pa);
        const auto* p = find (v_p, RuleId::NoVoice);
        GF_EXPECT (p != nullptr && p->changes[0].to == 320.0f);
    }
    {   // R6: only while a specific algorithm is forced
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::NoAlgoFits, 4 } }), pa);
        const auto* p = find (v_p, RuleId::NoAlgo);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "fillType" && p->changes[0].to == 0.0f && p->text == "Fill Type -> Auto");
        pa.p["fillType"].value = 0;
        GF_EXPECT (find (evaluateRules (evidenceWith ({ { RejectReason::NoAlgoFits, 4 } }), pa), RuleId::NoAlgo) == nullptr);
        pa.p["fillType"].value = 3;
    }
    {   // R7: the first disabled band
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::NoBand, 4 } }), pa);
        const auto* p = find (v_p, RuleId::NoBand);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "bandMidOn" && p->text == "Mid band -> On");
    }
    {   // R8
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::LockSkipped, 3 } }), pa);
        const auto* p = find (v_p, RuleId::LockSkipped);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "fl01_lock" && p->changes[0].to == 0.0f);
    }
    {   // R9: needs the sidechain health codes too
        const auto without = evaluateRules (evidenceWith ({ { RejectReason::NoSource, 4 } }), pa);
        GF_EXPECT (find (without, RuleId::NoSource) == nullptr);
        const auto with = evaluateRules (evidenceWith ({ { RejectReason::NoSource, 4 } }, bitOf (HealthCode::NoSidechain)), pa);
        const auto* p = find (with, RuleId::NoSource);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "detSource" && p->text == "Detector Source -> Main");
    }
    {   // R10: protection-weakening needs a confirm click
        const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::GuardEaten, 5 } }), pa);
        const auto* p = find (v_p, RuleId::GuardEaten);
        GF_EXPECT (p != nullptr && p->changes[0].to == 56.0f && p->needsConfirm);
    }
    {   // R11
        auto ev = evidenceWith ({});
        ev.overloadWarnSeconds = 5.0f;
        const auto v_p = evaluateRules (ev, pa);
        const auto* p = find (v_p, RuleId::Overload);
        GF_EXPECT (p != nullptr && p->changes[0].paramId == "qualityMode" && p->changes[0].to == 1.0f && p->text == "Quality -> Balanced");
        ev.overloadWarnSeconds = 4.9f;
        GF_EXPECT (find (evaluateRules (ev, pa), RuleId::Overload) == nullptr);
    }
    {   // R12 / R13 are actions, not parameter changes
        const auto v_p12 = evaluateRules (evidenceWith ({}, bitOf (HealthCode::NoTransport)), pa);
        const auto* p12 = find (v_p12, RuleId::NoTransport);
        GF_EXPECT (p12 != nullptr && p12->action == ActionKind::OpenTempoOptions && p12->changes.empty());

        auto ev = evidenceWith ({}, bitOf (HealthCode::PanicActive));
        ev.panicSeconds = 10.5f;
        const auto v_p13 = evaluateRules (ev, pa);
        const auto* p13 = find (v_p13, RuleId::Panicked);
        GF_EXPECT (p13 != nullptr && p13->action == ActionKind::Recover);
        ev.panicSeconds = 9.0f;
        GF_EXPECT (find (evaluateRules (ev, pa), RuleId::Panicked) == nullptr);
    }
}

GF_TEST (Fixit, below_threshold_no_suggestion)
{
    FakeParams pa;
    const std::pair<RejectReason, int> justUnder[] =
    {
        { RejectReason::FillOff, 2 }, { RejectReason::TooShort, 4 }, { RejectReason::TooSoon, 4 },
        { RejectReason::DensityRoll, 5 }, { RejectReason::NoVoice, 4 }, { RejectReason::NoAlgoFits, 3 },
        { RejectReason::NoBand, 3 }, { RejectReason::LockSkipped, 2 }, { RejectReason::GuardEaten, 4 }
    };

    for (const auto& u : justUnder)
        GF_EXPECT (evaluateRules (evidenceWith ({ u }, bitOf (HealthCode::NoSidechain)), pa).empty());

    GF_EXPECT (evaluateRules (evidenceWith ({}), pa).empty());
}

GF_TEST (Fixit, evidence_window_is_the_last_eight_gaps)
{
    std::vector<FillEvent> log;

    for (int i = 0; i < 12; ++i)
    {
        FillEvent e;
        e.type = i < 4 ? FillEventType::Started : FillEventType::Rejected;     // newest first: 4 filled, then 8 skipped
        e.reason = RejectReason::DensityRoll;
        log.push_back (e);
    }

    const auto ev = buildEvidence (log.data(), (int) log.size(), 0);
    GF_EXPECT (ev.numGaps == 8);
    GF_EXPECT (ev.reasons[(size_t) RejectReason::DensityRoll] == 4);       // only 4 of the newest 8
}

GF_TEST (Fixit, clamped_to_param_range_and_never_raises_in_the_wrong_direction)
{
    FakeParams pa;
    pa.p["density"].value = 95;                       // +20 would exceed 100
    const auto v_p = evaluateRules (evidenceWith ({ { RejectReason::DensityRoll, 8 } }), pa);
        const auto* p = find (v_p, RuleId::DensityRoll);
    // Still a legal improvement, clamped to the range end.
    GF_EXPECT (p != nullptr && p->changes[0].to == 100.0f);

    pa.p["density"].value = 100;
    GF_EXPECT (find (evaluateRules (evidenceWith ({ { RejectReason::DensityRoll, 8 } }), pa), RuleId::DensityRoll) == nullptr);

    pa.p["minGap"].value = 30;                        // proposing 40 would raise it
    GF_EXPECT (find (evaluateRules (evidenceWith ({ { RejectReason::TooShort, 5 } }), pa), RuleId::TooShort) == nullptr);

    pa.p["maxFill"].value = 20;                       // already at the floor
    GF_EXPECT (find (evaluateRules (evidenceWith ({ { RejectReason::NoVoice, 6 } }), pa), RuleId::NoVoice) == nullptr);
}

GF_TEST (Fixit, deterministic_for_same_log)
{
    FakeParams pa;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 }, { RejectReason::TooSoon, 5 } });
    const auto a = evaluateRules (ev, pa);
    const auto b = evaluateRules (ev, pa);
    GF_EXPECT (a.size() == b.size() && a.size() == 2);

    for (size_t i = 0; i < a.size(); ++i)
        GF_EXPECT (a[i].rule == b[i].rule && a[i].text == b[i].text);

    // Strongest evidence first: TooSoon has 5/5 = 1.0, FillOff 3/3 = 1.0 -> tie -> lower rule id.
    GF_EXPECT (a[0].rule == RuleId::FillOff);
}

GF_TEST (Fixit, never_touches_forbidden_ids)
{
    // Every parameter any rule can write is on the whitelist; the forbidden ones never are.
    for (const char* id : { "dry", "wet", "output", "bypass", "lookahead", "seed", "capture", "audition" })
        GF_EXPECT (! isAllowedParameter (id));

    FakeParams pa;
    pa.p["bypass"] = { 0, 0, 1 };
    pa.p["lookahead"] = { 40, 0, 250 };
    pa.p["seed"] = { 7, 0, 9999 };

    std::set<juce::String> touched;

    // Every rule at once: the evidence window holds eight gaps, so build the counts directly.
    Evidence ev;
    ev.numGaps = 8;

    for (auto r : { RejectReason::FillOff, RejectReason::TooShort, RejectReason::TooSoon, RejectReason::DensityRoll,
                    RejectReason::NoVoice, RejectReason::NoAlgoFits, RejectReason::NoBand, RejectReason::LockSkipped,
                    RejectReason::NoSource, RejectReason::GuardEaten })
        ev.reasons[(size_t) r] = 8;

    ev.medianTooShortAvailMs = 45.0f;
    ev.healthMask = health::allCodesMask;
    ev.overloadWarnSeconds = 30.0f;
    ev.panicSeconds = 30.0f;

    for (const auto& p : evaluateRules (ev, pa))
        for (const auto& c : p.changes)
        {
            GF_EXPECT_MSG (isAllowedParameter (c.paramId), c.paramId);
            touched.insert (c.paramId);
        }

    GF_EXPECT (touched.size() >= 10);
    GF_EXPECT (touched.count ("dry") == 0 && touched.count ("bypass") == 0 && touched.count ("lookahead") == 0 && touched.count ("seed") == 0);
}

GF_TEST (Fixit, evidence_hold_time)
{
    FakeParams pa;
    Controller c;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 } });

    c.tick (10000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);
    c.tick (13900, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);          // 3.9 s of evidence: not yet
    c.tick (14000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::Suggestion);    // >= 4 s

    // Evidence that disappears before the hold time starts the clock again.
    Controller d;
    d.tick (1000, ev, pa, true, 0);
    d.tick (3000, evidenceWith ({}), pa, true, 0);
    d.tick (4000, ev, pa, true, 0);
    d.tick (7900, ev, pa, true, 0);
    GF_EXPECT (d.getChip().kind == Chip::Kind::None);
    d.tick (8000, ev, pa, true, 0);
    GF_EXPECT (d.getChip().kind == Chip::Kind::Suggestion);
}

GF_TEST (Fixit, suggestion_is_shown_at_least_six_seconds_then_leaves_when_evidence_goes)
{
    FakeParams pa;
    Controller c;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::Suggestion);

    c.tick (9000, evidenceWith ({}), pa, true, 0);             // evidence gone but only shown for 5 s
    GF_EXPECT (c.getChip().kind == Chip::Kind::Suggestion);    // a suggestion never flashes by
    c.tick (10100, evidenceWith ({}), pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);
}

GF_TEST (Fixit, two_suggestions_rotate_after_the_minimum_show_time)
{
    FakeParams pa;
    Controller c;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 }, { RejectReason::TooSoon, 5 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    const auto first = c.getChip().proposal.rule;
    c.tick (8000, ev, pa, true, 0);                            // 4 s after showing: still the first
    GF_EXPECT (c.getChip().proposal.rule == first);
    c.tick (10100, ev, pa, true, 0);                           // > 6 s: the other one
    GF_EXPECT (c.getChip().proposal.rule != first);
    c.tick (16200, ev, pa, true, 0);
    GF_EXPECT (c.getChip().proposal.rule == first);
}

GF_TEST (Fixit, cooldown_and_dismiss)
{
    FakeParams pa;
    Controller c;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::Suggestion);

    c.dismiss (5000);
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);
    GF_EXPECT (c.isOnCooldown (RuleId::FillOff, 5000 + 299999));
    GF_EXPECT (! c.isOnCooldown (RuleId::FillOff, 5000 + 300000));

    for (std::uint32_t t = 6000; t < 300000; t += 2000)
    {
        c.tick (t, ev, pa, true, 0);
        GF_EXPECT (c.getChip().kind == Chip::Kind::None);      // 5 minutes of silence for that rule
    }

    c.tick (306000, ev, pa, true, 0);
    c.tick (310100, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::Suggestion);
}

GF_TEST (Fixit, dont_suggest_this_and_disabled_option_hide_everything)
{
    FakeParams pa;
    const auto ev = evidenceWith ({ { RejectReason::FillOff, 3 } });

    Controller muted;
    const auto mutedBit = 1u << (unsigned) RuleId::FillOff;
    muted.tick (0, ev, pa, true, mutedBit);
    muted.tick (9000, ev, pa, true, mutedBit);
    GF_EXPECT (muted.getChip().kind == Chip::Kind::None);

    Controller off;
    off.tick (0, ev, pa, false, 0);
    off.tick (9000, ev, pa, false, 0);
    GF_EXPECT (off.getChip().kind == Chip::Kind::None);

    // Switching the option off removes a visible suggestion immediately.
    Controller live;
    live.tick (0, ev, pa, true, 0);
    live.tick (4000, ev, pa, true, 0);
    GF_EXPECT (live.getChip().kind == Chip::Kind::Suggestion);
    live.tick (4500, ev, pa, false, 0);
    GF_EXPECT (live.getChip().kind == Chip::Kind::None);
}

GF_TEST (Fixit, apply_pushes_undo_and_sets_values)
{
    FakeParams pa;
    Controller c;
    int undoPushes = 0;
    Controller::Hooks hooks;
    hooks.pushUndoSnapshot = [&undoPushes] { ++undoPushes; };
    c.setHooks (hooks);

    const auto ev = evidenceWith ({ { RejectReason::TooSoon, 5 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.apply (5000, pa, false));

    GF_EXPECT (undoPushes == 1);
    GF_EXPECT (pa.p["retrigGuard"].value == 21.0f);
    GF_EXPECT (pa.written.size() == 1 && pa.written[0] == "retrigGuard");
    GF_EXPECT (c.getChip().kind == Chip::Kind::Applied && c.getChip().canUndo);

    // "Applied" stays for 10 s, then disappears; the rule is on cooldown.
    c.tick (14000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::Applied);
    c.tick (15001, ev, pa, true, 0);
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);
    GF_EXPECT (c.isOnCooldown (RuleId::TooSoon, 20000));
}

GF_TEST (Fixit, undo_restores_bit_identical)
{
    FakeParams pa;
    pa.p["retrigGuard"].value = 33.3f;
    const auto before = pa.p["retrigGuard"].value;
    const auto beforeNorm = pa.getNormalised ("retrigGuard");

    Controller c;
    const auto ev = evidenceWith ({ { RejectReason::TooSoon, 5 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.apply (4100, pa, false));
    GF_EXPECT (pa.p["retrigGuard"].value != before);

    GF_EXPECT (c.undo (pa));
    GF_EXPECT (pa.getNormalised ("retrigGuard") == beforeNorm);        // exact same bits
    GF_EXPECT (c.getChip().kind == Chip::Kind::None);

    // A later user edit of the same parameter: the chip disappears and nothing is restored.
    Controller d;
    d.tick (0, ev, pa, true, 0);
    d.tick (4000, ev, pa, true, 0);
    GF_EXPECT (d.apply (4100, pa, false));
    pa.p["retrigGuard"].value = 99.0f;
    d.tick (5000, ev, pa, true, 0);
    GF_EXPECT (d.getChip().kind == Chip::Kind::None);
    GF_EXPECT (! d.undo (pa) && pa.p["retrigGuard"].value == 99.0f);
}

GF_TEST (Fixit, r10_requires_confirm)
{
    FakeParams pa;
    Controller c;
    int undoPushes = 0;
    Controller::Hooks hooks;
    hooks.pushUndoSnapshot = [&undoPushes] { ++undoPushes; };
    c.setHooks (hooks);

    const auto ev = evidenceWith ({ { RejectReason::GuardEaten, 6 } });
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.getChip().proposal.needsConfirm);

    GF_EXPECT (! c.apply (4100, pa, false));                   // an accidental click changes nothing
    GF_EXPECT (pa.p["protNext"].value == 70.0f && undoPushes == 0 && c.getChip().kind == Chip::Kind::Suggestion);
    GF_EXPECT (c.apply (4200, pa, true));
    GF_EXPECT (pa.p["protNext"].value == 56.0f && undoPushes == 1);
}

GF_TEST (Fixit, actions_run_the_hook_and_are_not_undoable)
{
    FakeParams pa;
    Controller c;
    std::vector<ActionKind> ran;
    int undoPushes = 0;
    Controller::Hooks hooks;
    hooks.pushUndoSnapshot = [&undoPushes] { ++undoPushes; };
    hooks.runAction = [&ran] (ActionKind a) { ran.push_back (a); };
    c.setHooks (hooks);

    const auto ev = evidenceWith ({}, bitOf (HealthCode::NoTransport));
    c.tick (0, ev, pa, true, 0);
    c.tick (4000, ev, pa, true, 0);
    GF_EXPECT (c.apply (4100, pa, false));
    GF_EXPECT (ran.size() == 1 && ran[0] == ActionKind::OpenTempoOptions);
    GF_EXPECT (undoPushes == 0 && c.getChip().kind == Chip::Kind::None);
}

GF_TEST (Fixit, priority_vs_health_and_other_status)
{
    using F = health::FooterKind;
    // Health outranks a suggestion, which outranks the last gap outcome.
    GF_EXPECT (health::selectFooter ({ true, true, true }) == F::Health);
    GF_EXPECT (health::selectFooter ({ false, true, true }) == F::Fixit);
    GF_EXPECT (health::selectFooter ({ false, false, true }) == F::LastGap);
}

GF_TEST (Fixit, apvts_adapter_applies_and_undoes_on_the_real_parameters)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "retrigGuard", 40.0f }, { "density", 65.0f } });
    ApvtsParamAccess access (proc.getValueTreeState());

    GF_EXPECT (access.has ("retrigGuard") && ! access.has ("nonexistent"));
    GF_EXPECT_NEAR (access.getReal ("retrigGuard"), 40.0f, 1.0e-4);

    float lo = 0, hi = 0;
    access.getRange ("minGap", lo, hi);
    GF_EXPECT (lo == 10.0f && hi == 500.0f);

    const auto rawBefore = proc.getValueTreeState().getRawParameterValue ("retrigGuard")->load();

    Controller c;
    int pushes = 0;
    Controller::Hooks hooks;
    hooks.pushUndoSnapshot = [&] { ++pushes; proc.pushUndoSnapshot(); };
    c.setHooks (hooks);

    const auto ev = evidenceWith ({ { RejectReason::TooSoon, 5 } });
    c.tick (0, ev, access, true, 0);
    c.tick (4000, ev, access, true, 0);
    GF_EXPECT (c.apply (4100, access, false));
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("retrigGuard")->load(), 28.0f, 0.51);
    GF_EXPECT (pushes == 1 && proc.canUndo());

    GF_EXPECT (c.undo (access));
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("retrigGuard")->load() == rawBefore);
}

GF_TEST (Fixit, processor_runs_fixit_on_its_timer_from_the_real_outcome_log)
{
    GapFillAudioProcessor proc;
    proc.getVisualisationFifo().setEnabled (true);
    setParams (proc, { { "fill", 0.0f } });
    renderGoldenScenarioA (proc);
    proc.drainFillEvents();

    // The log holds FillOff outcomes; 14 timer ticks with a manual clock are not possible
    // from outside, so drive the same code path the timer uses.
    auto& state = proc.getExplainState();
    const auto& log = proc.getGapOutcomeLog();
    FillEvent newest[8];
    const auto n = juce::jmin (log.size(), 8);

    for (int i = 0; i < n; ++i)
        newest[i] = log.get (i);

    const auto ev = buildEvidence (newest, n, 0);
    ApvtsParamAccess access (proc.getValueTreeState());
    state.fixitController.tick (100, ev, access, true, 0);
    state.fixitController.tick (4200, ev, access, true, 0);
    GF_EXPECT (state.fixitController.getChip().kind == Chip::Kind::Suggestion);
    GF_EXPECT (state.fixitController.getChip().proposal.rule == RuleId::FillOff);
    GF_EXPECT (state.applyFixit (false));
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("fill")->load(), 50.0f, 0.01);
    GF_EXPECT (state.undoFixit());
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("fill")->load() == 0.0f);
}
