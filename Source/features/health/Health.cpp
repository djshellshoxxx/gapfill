#include "Health.h"

#include <cmath>

namespace gapfill::health
{

using C = HealthCode;

Timing HealthMonitor::timingFor (HealthCode c) noexcept
{
    switch (c)
    {
        case C::NoTransport:        return { 1.0f, 2.0f };
        case C::TransportJump:      return { 0.0f, 0.0f };   // latched for jumpLatchSeconds
        case C::NoSidechain:        return { 0.5f, 2.0f };
        case C::SidechainSilent:    return { 0.0f, 2.0f };   // the 2 s of silence are counted separately
        case C::FreeTimingFallback: return { 0.5f, 2.0f };
        case C::OverloadWarn:       return { 2.0f, 2.0f };
        case C::OverloadCritical:   return { 0.25f, 2.0f };
        case C::QueueOverflow:      return { 0.0f, 0.0f };   // latched for overflowLatchSeconds
        case C::BypassActive:       return { 0.0f, 2.0f };
        case C::PanicActive:        return { 0.0f, 0.0f };   // immediate both ways
        case C::Count:              break;
    }

    return { 0.0f, 0.0f };
}

void HealthMonitor::reset() noexcept
{
    slots = {};
    jumpLatch = overflowLatch = overrunLatch = 0.0f;
    scSilentFor = 0.0f;
    mask.store (0, std::memory_order_relaxed);
}

void HealthMonitor::step (HealthCode code, bool condition, float dt) noexcept
{
    auto& s = slots[(size_t) code];
    const auto t = timingFor (code);

    if (condition)
    {
        s.fall = 0.0f;

        if (! s.active)
        {
            s.rise += dt;

            if (s.rise >= t.set)
                s.active = true;
        }
    }
    else
    {
        s.rise = 0.0f;

        if (s.active)
        {
            s.fall += dt;

            if (s.fall >= t.clear)
            {
                s.active = false;
                s.fall = 0.0f;
            }
        }
    }
}

void HealthMonitor::update (const Observations& o) noexcept
{
    if (o.numSamples <= 0 || ! (o.sampleRate > 0.0))
        return;

    const auto dt = (float) ((double) o.numSamples / o.sampleRate);

    // Latches for the pulse-type codes.
    if (o.transportJumped)       jumpLatch = jumpLatchSeconds;
    if (o.droppedEvents > 0)     overflowLatch = overflowLatchSeconds;
    if (o.deadlineOverrun)       overrunLatch = overrunLatchSeconds;

    // Time the sidechain has been (almost) silent while the bus is in use.
    const bool scInUse = o.scRequested && o.scBusEnabled;
    const bool quiet = o.scPeak < std::pow (10.0f, silenceDb / 20.0f);
    scSilentFor = (scInUse && quiet) ? scSilentFor + dt : 0.0f;

    const auto cpu = std::isfinite (o.cpu01) ? o.cpu01 : 0.0f;

    step (C::NoTransport,        o.tempoFeatureOn && ! o.transportValid, dt);
    step (C::TransportJump,      jumpLatch > 0.0f, dt);
    step (C::NoSidechain,        o.scRequested && ! o.scBusEnabled, dt);
    step (C::SidechainSilent,    scInUse && scSilentFor >= 2.0f, dt);
    step (C::FreeTimingFallback, o.freeTimingFallback, dt);
    step (C::OverloadWarn,       cpu > warnCpu, dt);
    step (C::OverloadCritical,   cpu > criticalCpu || overrunLatch > 0.0f, dt);
    step (C::QueueOverflow,      overflowLatch > 0.0f, dt);
    step (C::BypassActive,       o.bypassed, dt);
    step (C::PanicActive,        o.panicActive, dt);

    jumpLatch     = juce::jmax (0.0f, jumpLatch - dt);
    overflowLatch = juce::jmax (0.0f, overflowLatch - dt);
    overrunLatch  = juce::jmax (0.0f, overrunLatch - dt);

    std::uint32_t m = 0;

    for (int i = 0; i < numHealthCodes; ++i)
        if (slots[(size_t) i].active)
            m |= 1u << (unsigned) i;

    mask.store (m, std::memory_order_relaxed);
}

//==============================================================================
Description describe (HealthCode code, const DescribeContext& ctx)
{
    const auto pct = juce::String ((int) std::lround (juce::jlimit (0.0f, 9.99f, ctx.cpu01) * 100.0f));
    const auto bpm = juce::String (juce::roundToInt (ctx.fallbackBpm));

    switch (code)
    {
        case C::NoTransport:
            return { "Transport unavailable - free timing at " + bpm + " BPM",
                     "The host is not providing tempo or play state, so synced features run on a fixed tempo.",
                     "Start the host transport, or set the fallback tempo in Options." };
        case C::TransportJump:
            return { "Transport jumped",
                     "The playhead jumped (loop, locate or seek) in the last 10 seconds; running fills were stopped.",
                     "Nothing to do - this is informational." };
        case C::NoSidechain:
            return { "No sidechain - using input",
                     "Detector Source is set to a sidechain, but no sidechain input is connected.",
                     "Route a track to GapFill's sidechain input, or set Detector Source to Main." };
        case C::SidechainSilent:
            return { "Sidechain is silent - using input",
                     "The sidechain input is connected but has carried no signal for 2 seconds.",
                     "Check the sending track is playing and its send level is up, or set Detector Source to Main." };
        case C::FreeTimingFallback:
            return { "Free timing at " + bpm + " BPM",
                     "No host tempo is available, so the fallback tempo is being used.",
                     "Start the host transport, or set the fallback tempo in Options." };
        case C::OverloadWarn:
            return { "CPU high (" + pct + "%) - try Quality: Balanced",
                     "GapFill is using more than 70% of the time available per audio block (estimated).",
                     "Lower Complexity, shorten Max Fill, or increase the host buffer size." };
        case C::OverloadCritical:
            return { "CPU critical (" + pct + "%) - audio may drop out",
                     "GapFill is close to, or past, the time available per audio block (estimated).",
                     "Lower Complexity or Lookahead, or increase the host buffer size." };
        case C::QueueOverflow:
            return { "Some events were dropped",
                     "The editor could not keep up and a few fill events were discarded; sound is unaffected.",
                     "Close other plug-in windows or reduce editor size." };
        case C::BypassActive:
            return { "Bypassed - dry signal only",
                     "GapFill is bypassed, so no fills are generated.",
                     "Switch Bypass off to hear fills." };
        case C::PanicActive:
            return { "Panic active - press Recover",
                     "GapFill's wet signal is held silent until you recover.",
                     "Press Recover." };
        case C::Count:
            break;
    }

    return {};
}

const std::array<HealthCode, (size_t) numHealthCodes>& priorityOrder() noexcept
{
    static constexpr std::array<HealthCode, (size_t) numHealthCodes> order =
    {
        C::PanicActive, C::OverloadCritical, C::NoSidechain, C::SidechainSilent,
        C::NoTransport, C::FreeTimingFallback, C::OverloadWarn, C::QueueOverflow,
        C::BypassActive, C::TransportJump
    };

    return order;
}

bool highestVisible (std::uint32_t activeMask, std::uint32_t mutedMask, HealthCode& out) noexcept
{
    for (auto c : priorityOrder())
    {
        const auto muted = isMutable (c) && (mutedMask & bitOf (c)) != 0;

        if ((activeMask & bitOf (c)) != 0 && ! muted)
        {
            out = c;
            return true;
        }
    }

    return false;
}

//==============================================================================
void HealthTracker::update (std::uint32_t activeMask, std::uint32_t nowMs) noexcept
{
    activeMask &= allCodesMask;
    const auto appeared = activeMask & ~current;

    for (int i = 0; i < numHealthCodes; ++i)
        if ((appeared & (1u << (unsigned) i)) != 0)
            onset[(size_t) i] = nowMs;

    current = activeMask;
}

int HealthTracker::list (Entry* out, int maxOut, std::uint32_t mutedMask) const noexcept
{
    int n = 0;

    for (auto c : priorityOrder())
    {
        if (n >= maxOut)
            break;

        if ((current & bitOf (c)) != 0)
            out[n++] = { c, onset[(size_t) c], isMutable (c) && (mutedMask & bitOf (c)) != 0 };
    }

    return n;
}

FooterKind selectFooter (const FooterCandidates& c) noexcept
{
    if (c.haveInfo)     return FooterKind::Info;
    if (c.haveHealth)   return FooterKind::Health;
    if (c.haveFixit)    return FooterKind::Fixit;
    if (c.haveHover)    return FooterKind::Hover;
    if (c.haveLastGap)  return FooterKind::LastGap;
    return FooterKind::Nothing;
}

} // namespace gapfill::health
