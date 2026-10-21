#pragma once

// N-EASY Fix-it Suggestions (docs/dev/12 N-EASY).
//
// Message thread only. Reads the E-3 outcome log and the N-USE-b health mask,
// proposes ONE concrete, safe change at a time, applies it with one undo
// snapshot and can take it back bit-identically. This module has no audio-thread
// code and never includes the processor's process path: it cannot influence a
// fill decision except through the parameter values the user accepted.

#include "../../dsp/EngineEvents.h"
#include "../health/Health.h"

#include <juce_core/juce_core.h>

#include <array>
#include <functional>
#include <vector>

namespace juce { class AudioProcessorValueTreeState; }

namespace gapfill::fixit
{

/** Permanent rule ids (R1..R13 of the spec table). Never renumber. */
enum class RuleId : std::uint8_t
{
    FillOff = 0, TooShort, TooSoon, DensityRoll, NoVoice, NoAlgo, NoBand,
    LockSkipped, NoSource, GuardEaten, Overload, NoTransport, Panicked,
    Count
};

inline constexpr int numRules = (int) RuleId::Count;

/** Stable textual id (global settings key), e.g. "fill_off". */
const char* ruleName (RuleId) noexcept;

enum class ActionKind : std::uint8_t { None, OpenTempoOptions, Recover };

/** Parameters the fix-it module is allowed to change. Everything else (dry, wet,
    output, bypass, lookahead, seed, latency ...) is forbidden. */
bool isAllowedParameter (const juce::String& id) noexcept;

//==============================================================================
/** Message-thread view of the parameters. Real units (ms, %, choice index). */
class ParamAccess
{
public:
    virtual ~ParamAccess() = default;

    virtual bool has (const juce::String& id) const = 0;
    virtual float getReal (const juce::String& id) const = 0;
    virtual void getRange (const juce::String& id, float& minValue, float& maxValue) const = 0;

    /** Normalised value as the host sees it (exact; used for bit-identical undo). */
    virtual float getNormalised (const juce::String& id) const = 0;

    /** One begin/set/end gesture. */
    virtual void setNormalised (const juce::String& id, float normalised) = 0;
    virtual float toNormalised (const juce::String& id, float real) const = 0;
};

class ApvtsParamAccess : public ParamAccess
{
public:
    explicit ApvtsParamAccess (juce::AudioProcessorValueTreeState& s) : state (s) {}

    bool has (const juce::String& id) const override;
    float getReal (const juce::String& id) const override;
    void getRange (const juce::String& id, float& minValue, float& maxValue) const override;
    float getNormalised (const juce::String& id) const override;
    void setNormalised (const juce::String& id, float normalised) override;
    float toNormalised (const juce::String& id, float real) const override;

private:
    juce::AudioProcessorValueTreeState& state;
};

//==============================================================================
/** Everything the rules look at. Plain data so tests can write any log. */
struct Evidence
{
    static constexpr int windowGaps = 8;

    int numGaps = 0;                                         ///< evaluated gaps in the window (<= 8)
    std::array<int, (size_t) numRejectReasons> reasons {};   ///< "k of 8" per reason
    float medianTooShortAvailMs = 0.0f;
    std::uint32_t healthMask = 0;
    float overloadWarnSeconds = 0.0f;                        ///< how long OverloadWarn has been active
    float panicSeconds = 0.0f;                               ///< how long PanicActive has been active
};

/** `newestFirst[0]` is the newest outcome; only the first 8 are used. */
Evidence buildEvidence (const FillEvent* newestFirst, int count, std::uint32_t healthMask,
                        float overloadWarnSeconds = 0.0f, float panicSeconds = 0.0f);

struct Change
{
    juce::String paramId;
    juce::String label;      ///< "Min Gap"
    juce::String unit;       ///< " ms", "%" or "" (discrete)
    juce::String toName;     ///< for choice / bool changes: "Auto", "On", "Off", "Balanced"
    float from = 0.0f;       ///< real units
    float to = 0.0f;         ///< real units
};

struct Proposal
{
    RuleId rule = RuleId::FillOff;
    float strength = 0.0f;               ///< evidence / threshold (>= 1 when triggered)
    std::vector<Change> changes;         ///< empty for actions
    ActionKind action = ActionKind::None;
    bool needsConfirm = false;           ///< R10 weakens protection
    juce::String text;                   ///< "Min Gap 120 -> 85 ms" or the action label
    juce::String spoken;                 ///< accessibility description
};

/** All rules that currently fire, strongest first (ties: lower rule id first).
    Deterministic: the same evidence and parameter values give the same list. */
std::vector<Proposal> evaluateRules (const Evidence&, const ParamAccess&);

//==============================================================================
/** What the footer chip should show. */
struct Chip
{
    enum class Kind { None, Suggestion, Applied } kind = Kind::None;
    Proposal proposal;
    bool canUndo = false;
};

class Controller
{
public:
    struct Timing
    {
        std::uint32_t holdMs = 4000;        ///< evidence must persist before a suggestion shows
        std::uint32_t minShowMs = 6000;     ///< shown at least this long, then rotates
        std::uint32_t cooldownMs = 300000;  ///< after Apply / dismiss, per rule (5 minutes)
        std::uint32_t appliedMs = 10000;    ///< the "Applied ... [Undo]" chip
    };

    struct Hooks
    {
        std::function<void()> pushUndoSnapshot;          ///< the existing whole-sound Undo
        std::function<void (ActionKind)> runAction;
    };

    Controller() = default;
    explicit Controller (Timing t) : timing (t) {}

    void setHooks (Hooks h)                  { hooks = std::move (h); }

    /** 2 Hz from the processor/editor timer. `enabled` = Options "Show suggestions". */
    void tick (std::uint32_t nowMs, const Evidence&, ParamAccess&, bool enabled, std::uint32_t mutedRules);

    const Chip& getChip() const noexcept     { return chip; }

    /** Apply the visible suggestion. Returns false when nothing was applied
        (no suggestion, or an unconfirmed R10). */
    bool apply (std::uint32_t nowMs, ParamAccess&, bool confirmed);

    /** Undo the applied change: restores the exact previous values unless a
        later edit touched those parameters (then the chip just disappears). */
    bool undo (ParamAccess&);

    /** The [x] button: hides the suggestion and starts the rule's cooldown. */
    void dismiss (std::uint32_t nowMs);

    bool isOnCooldown (RuleId, std::uint32_t nowMs) const noexcept;

private:
    struct Prev { juce::String id; float previousNormalised, appliedNormalised; };

    Timing timing;
    Hooks hooks;
    Chip chip;

    std::array<std::uint32_t, (size_t) numRules> firstSeen {};     ///< 0 = not currently firing
    std::array<std::uint32_t, (size_t) numRules> cooldownUntil {};
    std::uint32_t shownAt = 0, appliedAt = 0;
    int rotation = 0;
    std::vector<Prev> applied;
};

} // namespace gapfill::fixit
