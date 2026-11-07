#pragma once

// F-G01 Glitch rack: the per-sample modules.
//
//   Stutter   (B)  tempo/free slice repeats with probability, reverse, variation,
//                  older-slice pick, gate and decay
//   Stab      (I)  triggered slice repeats, fixed count or infinite until released
//   Reverser  (H)  segment reverse (each segment plays the previous one backwards)
//   Gator     (D)  microgator: rhythmic gate with pattern, depth, attack/release
//   Sweep     (F)  HP/LP/BP state-variable filter swept by an LFO
//   Phaser    (G)  multi-stage first-order all-pass phaser with feedback
//   Flip      (E)  polarity inversion, pan inversion/channel swap, rhythmic flips
//
// All of them: allocation-free after prepare(), no locks, no I/O, bounded work per
// sample, finite output for finite input. Filters use the topology-preserving
// (trapezoidal) form, which stays stable under fast cutoff modulation.

#include "GlitchClock.h"
#include "GlitchTypes.h"
#include "SliceRepeater.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gapfill::glitch
{

inline constexpr double kPi = 3.14159265358979323846;

inline int msToSamples (double ms, double sr) noexcept { return std::max (1, (int) std::lround (ms * 0.001 * sr)); }

inline float flushTiny (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

/** Linear 0..1 ramp that engages or bypasses one module without a click. */
struct Engage
{
    float value = 0.0f;
    float step = 1.0f / 480.0f;

    void prepare (double sr) noexcept { step = (float) (1.0 / std::max (1.0, 0.010 * sr)); }
    void reset() noexcept { value = 0.0f; }
    float next (bool on) noexcept
    {
        value = on ? std::min (1.0f, value + step) : std::max (0.0f, value - step);
        return value;
    }
    bool idle (bool on) const noexcept { return ! on && value <= 0.0f; }
};

/** LFO value 0..1 for a phase (cycles); `cycle` indexes the random steps. */
inline double lfoValue (LfoShape shape, double phase, int seed, std::uint32_t salt) noexcept
{
    const auto cycle = (std::int64_t) std::floor (phase);
    const auto f = phase - (double) cycle;

    switch (shape)
    {
        case LfoShape::Sine:       return 0.5 - 0.5 * std::cos (2.0 * kPi * f);
        case LfoShape::Triangle:   return f < 0.5 ? 2.0 * f : 2.0 - 2.0 * f;
        case LfoShape::RampUp:     return f;
        case LfoShape::RampDown:   return 1.0 - f;
        case LfoShape::Square:     return f < 0.5 ? 1.0 : 0.0;
        case LfoShape::RandomStep: return hash01 (seed, salt, cycle);
        case LfoShape::Count:      break;
    }

    return 0.0;
}

//==============================================================================
class Stutter
{
public:
    void prepare (double sr, int maxSlice, int historyCapacity)
    {
        sampleRate = sr;
        repeater.prepare (maxSlice);
        history.prepare (historyCapacity);
        reset();
    }

    void reset() noexcept
    {
        repeater.reset();
        history.reset();
        active = false;
        lastStep = lastSub = 0;
        primed = false;
    }

    bool isRepeating() const noexcept { return repeater.isRepeating(); }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        history.push (l, r);
        repeater.setFadeSamples (msToSamples (s.stutSmoothMs, sampleRate));

        if (s.stutMode == ClockMode::Off)
        {
            if (repeater.isActive())
                repeater.release (msToSamples (s.stutSmoothMs, sampleRate));

            primed = false;
            repeater.process (l, r, l, r);
            return;
        }

        const bool tempo = s.stutMode == ClockMode::Tempo;
        const auto unitSamples = tempo ? divBeats (s.stutDiv, t.tsNum, t.tsDen) * t.samplesPerBeat
                                       : std::max (1.0, s.stutMs * 0.001 * sampleRate);
        const auto position = tempo ? c.beat / divBeats (s.stutDiv, t.tsNum, t.tsDen)
                                    : (double) c.freeSamples / unitSamples;
        const Grid grid (s.swing01, s.timeVar01, s.seed, 0x5701u);
        const auto g = grid.locate (position);
        const auto repeats = std::max (1, s.stutRepeats);
        const auto window = floorDiv (g.step, repeats);
        const auto unit = (int) (g.step - window * repeats);

        // Slice variation: some windows repeat a half or quarter unit, still on the base grid.
        const auto vh = hash01 (s.seed, 0x5702u, window);
        const int subdiv = vh < s.stutVar01 ? (vh < s.stutVar01 * 0.35 ? 4 : 2) : 1;
        const auto sub = (int) std::min<double> (subdiv - 1, std::floor (g.phase * subdiv));
        const auto relocated = c.relocated && primed;

        if (! primed || g.step != lastStep || sub != lastSub || relocated)
        {
            const bool newStep = ! primed || g.step != lastStep;
            const auto sliceLen = std::max (16, (int) std::lround (unitSamples / subdiv));
            const auto gate = std::max (1, (int) std::lround (s.stutGate01 * unitSamples * g.length / subdiv));

            if (relocated || (newStep && unit == 0) || ! primed)
            {
                const bool stutter = ! relocated && hash01 (s.seed, 0x5703u, window) < s.stutProb01
                                     && (unit == 0 || ! primed);
                active = false;

                if (stutter && primed)
                {
                    const auto pickHash = hash01 (s.seed, 0x5704u, window);
                    int back = 0;

                    if (pickHash < s.stutPick01)
                        back = 1 + (int) std::floor (hash01 (s.seed, 0x5705u, window) * kMaxLookbackUnits);

                    if (back > 0 && repeater.beginFromHistory (history, back * sliceLen, sliceLen,
                                                               reverseFor (s, window, 0, 0), 1.0f, gate))
                        active = true;
                    else
                    {
                        repeater.beginLiveCapture (sliceLen);
                        active = true;
                    }
                }
                else if (repeater.isActive())
                {
                    repeater.release (msToSamples (s.stutSmoothMs, sampleRate));
                }
            }
            else if (active)
            {
                const auto index = unit * subdiv + sub;
                const auto gain = (float) std::pow (1.0 - (double) s.stutDecay01, (double) index);
                repeater.startRepeat (reverseFor (s, window, unit, sub), gain, gate);
            }

            lastStep = g.step;
            lastSub = sub;
            primed = true;
        }

        repeater.process (l, r, l, r);
    }

private:
    static std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    static bool reverseFor (const Settings& s, std::int64_t window, int unit, int sub) noexcept
    {
        return s.stutReverse01 > 0.0f && hash01 (s.seed, 0x5706u, window * 64 + unit * 4 + sub) < s.stutReverse01;
    }

    double sampleRate = 48000.0;
    SliceRepeater repeater;
    HistoryRing history;
    bool active = false, primed = false;
    std::int64_t lastStep = 0;
    int lastSub = 0;
};

//==============================================================================
/** STAB macro: a press captures one unit of the live signal and repeats it.
    Finite: exactly `stabRepeats` repeats, then back to live (a one-shot; holding
    longer changes nothing). Infinite: repeats until the trigger is released or
    Infinite is switched off. A new press restarts the capture. Unit length is
    latched at the press, so tempo changes and transport stops do not disturb a
    running stab. */
class Stab
{
public:
    void prepare (double sr, int maxSlice)
    {
        sampleRate = sr;
        repeater.prepare (maxSlice);
        reset();
    }

    void reset() noexcept
    {
        repeater.reset();
        running = false;
        held = false;
        elapsed = 0;
        unit = 0;
        unitSamples = 0;
    }

    bool isRunning() const noexcept { return running; }
    int  repeatIndex() const noexcept { return unit; }

    void process (const Settings& s, const Timing& t, float& l, float& r) noexcept
    {
        const bool pressed = s.stab;
        repeater.setFadeSamples (msToSamples (3.0, sampleRate));

        if (pressed && ! held)
        {
            unitSamples = std::clamp ((int) std::lround (divBeats (s.stabDiv, t.tsNum, t.tsDen) * t.samplesPerBeat), 16,
                                      (int) (kMaxSliceSeconds * sampleRate));
            repeater.beginLiveCapture (unitSamples);
            running = true;
            elapsed = -1;      // the press sample is sample 0 of the captured unit
            unit = 0;
        }

        held = pressed;

        if (running)
        {
            const bool releaseNow = s.stabInfinite ? ! pressed : false;

            if (releaseNow)
            {
                stop (s);
            }
            else if (++elapsed >= unitSamples)
            {
                elapsed = 0;
                ++unit;

                if (! s.stabInfinite && unit > std::max (1, s.stabRepeats))
                {
                    stop (s);
                }
                else
                {
                    // pow() underflows to 0 instead of a denormal thanks to the clamp in startRepeat.
                    const auto gain = (float) std::pow (1.0 - (double) s.stabDecay01, (double) std::min (unit - 1, 4096));
                    const auto gate = std::max (1, (int) std::lround (s.stabGate01 * unitSamples));
                    repeater.startRepeat (false, gain, gate);
                }
            }
        }

        repeater.process (l, r, l, r);
    }

private:
    void stop (const Settings& s) noexcept
    {
        repeater.release (msToSamples (s.stabReleaseMs, sampleRate));
        running = false;
        elapsed = 0;
    }

    double sampleRate = 48000.0;
    SliceRepeater repeater;
    bool running = false, held = false;
    int elapsed = 0, unit = 0, unitSamples = 0;
};

//==============================================================================
/** Each segment plays the previous segment backwards. The reversed audio is
    inherently one segment late; no host latency is reported because the effect
    is a creative time shift, like a reverse delay. Edges get a fade of revFadeMs. */
class Reverser
{
public:
    void prepare (double sr, int maxSegment)
    {
        sampleRate = sr;
        ring.prepare (2 * std::max (16, maxSegment) + 4);
        maxSeg = std::max (16, maxSegment);
        reset();
    }

    void reset() noexcept
    {
        ring.reset();
        primed = false;
        prevLen = 0;
        elapsed = 0;
        reversing = false;
        wet = 0.0f;
    }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        ring.push (l, r);

        if (s.revMode == ClockMode::Off)
        {
            primed = false;
            reversing = false;
        }

        const bool tempo = s.revMode == ClockMode::Tempo;
        const auto segSamples = tempo ? divBeats (s.revDiv, t.tsNum, t.tsDen) * t.samplesPerBeat
                                      : std::max (1.0, s.revMs * 0.001 * sampleRate);
        const auto position = tempo ? c.beat / divBeats (s.revDiv, t.tsNum, t.tsDen) : (double) c.freeSamples / segSamples;
        const auto step = (std::int64_t) std::floor (position);

        if (s.revMode != ClockMode::Off && (! primed || step != lastStep || (c.relocated && primed)))
        {
            const bool contiguous = primed && step == lastStep + 1 && ! c.relocated;
            prevLen = contiguous ? std::min (elapsed, maxSeg) : 0;
            reversing = contiguous && prevLen >= 16 && hash01 (s.seed, 0x4E01u, step) < s.revProb01;
            elapsed = 0;
            lastStep = step;
            primed = true;
        }

        const auto fade = std::max (1, msToSamples (s.revFadeMs, sampleRate));
        const auto wetStep = 1.0f / (float) fade;
        const auto target = reversing ? s.revMix01 : 0.0f;
        wet = wet < target ? std::min (target, wet + wetStep) : std::max (target, wet - wetStep);

        if (wet > 0.0f)
        {
            float rl = 0.0f, rr = 0.0f;

            if (reversing && elapsed < prevLen)
            {
                // Sample `elapsed` of this segment = sample (prevLen - 1 - elapsed) of the previous one,
                // which was pushed 2 * elapsed + 1 samples ago (the current sample is distance 1).
                ring.read (2 * elapsed + 2, rl, rr);
                const auto edge = std::min (elapsed, prevLen - 1 - elapsed);
                const auto env = edge >= fade ? 1.0f : (float) edge / (float) fade;
                rl *= env;
                rr *= env;
            }

            l = wet >= 1.0f ? rl : l + (rl - l) * wet;
            r = wet >= 1.0f ? rr : r + (rr - r) * wet;
        }

        ++elapsed;
    }

private:
    double sampleRate = 48000.0;
    HistoryRing ring;
    int maxSeg = 16, prevLen = 0, elapsed = 0;
    std::int64_t lastStep = 0;
    bool primed = false, reversing = false;
    float wet = 0.0f;
};

//==============================================================================
class Gator
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept { gainL = gainR = 1.0f; }

    static bool patternStep (GatePattern p, std::int64_t step, int seed) noexcept
    {
        static constexpr std::uint16_t masks[] = {
            0xFFFF,   // Straight
            0xAAAA,   // Offbeat: steps 1, 3, 5 ...
            0x9249,   // Dotted: every third step
            0x4949,   // Tresillo 3-3-2
            0x6D6D,   // Euclidean 5 in 8 (x.xx.xx.), twice
            0xDDDD,   // Gallop (x.xx)
        };
        const auto i = (int) (((step % 16) + 16) % 16);

        if (p == GatePattern::Random)
            return hash01 (seed, 0x6A01u, step) < 0.6;

        const auto idx = std::clamp ((int) p, 0, 5);
        return ((masks[idx] >> i) & 1u) != 0;
    }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        const bool tempo = s.gateMode == ClockMode::Tempo;
        const auto position = tempo ? c.beat / divBeats (s.gateDiv, t.tsNum, t.tsDen)
                                    : (double) c.freeSamples * (double) std::max (0.01f, s.gateHz) / sampleRate;
        const Grid grid (s.swing01, s.timeVar01, s.seed, 0x6A02u);
        const auto open = [&] (double p)
        {
            const auto g = grid.locate (p);
            auto on = patternStep (s.gatePattern, g.step, s.seed);

            if (s.gateVar01 > 0.0f && hash01 (s.seed, 0x6A03u, g.step) < s.gateVar01)
                on = ! on;

            return on && g.phase < s.gateLen01;
        };

        const auto closed = 1.0f - std::clamp (s.gateDepth01, 0.0f, 1.0f);
        const auto targetL = open (position) ? 1.0f : closed;
        const auto targetR = s.gateStereo01 > 0.0f ? (open (position + 0.5 * s.gateStereo01) ? 1.0f : closed) : targetL;
        const auto att = coeff (s.gateAttackMs), rel = coeff (s.gateReleaseMs);
        gainL = flushTiny (targetL + (gainL - targetL) * (targetL > gainL ? att : rel));
        gainR = flushTiny (targetR + (gainR - targetR) * (targetR > gainR ? att : rel));
        l *= gainL;
        r *= gainR;
    }

    float getGainLeft() const noexcept { return gainL; }

private:
    float coeff (float ms) const noexcept
    {
        return (float) std::exp (-1.0 / std::max (1.0, (double) std::max (0.05f, ms) * 0.001 * sampleRate));
    }

    double sampleRate = 48000.0;
    float gainL = 1.0f, gainR = 1.0f;
};

//==============================================================================
/** Topology-preserving state-variable filter (A. Simper, "Linear trapezoidal
    integrated SVF", 2013; the published equations, implemented independently). */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void set (double sr, double hz, double q) noexcept
    {
        const auto g = std::tan (kPi * std::clamp (hz, 10.0, 0.45 * sr) / sr);
        k = (float) (1.0 / std::max (0.5, q));
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) g * a1;
        a3 = (float) g * a2;
    }

    float process (float v0, FilterMode mode) noexcept
    {
        const auto v3 = v0 - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushTiny (2.0f * v1 - ic1);
        ic2 = flushTiny (2.0f * v2 - ic2);

        switch (mode)
        {
            case FilterMode::LowPass:  return v2;
            case FilterMode::HighPass: return v0 - k * v1 - v2;
            case FilterMode::BandPass: return k * v1;      // unity gain at the centre
            case FilterMode::Off:      break;
        }

        return v0;
    }
};

/** Free-running LFO phase (cycles) that does not depend on block boundaries. */
struct FreePhase
{
    double phase = 0.0;
    void reset() noexcept { phase = 0.0; }
    double advance (double hz, double sr) noexcept
    {
        const auto p = phase;
        phase += hz / sr;
        phase -= std::floor (phase);
        return p;
    }
};

inline constexpr int kCoeffInterval = 16;   ///< coefficient update period, aligned to the free sample counter

class Sweep
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : filters)
            f.reset();

        free.reset();
        smoothL = smoothR = -1.0;
    }

    double getCutoffLeft() const noexcept { return cutoffL; }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        const auto freePhase = free.advance (std::max (0.001f, s.filtHz), sampleRate);

        if (c.freeSamples % kCoeffInterval == 0 || smoothL < 0.0)
        {
            const auto phase = s.filtSync ? c.beat / divBeats (s.filtDiv, t.tsNum, t.tsDen) : freePhase;
            const auto offset = s.filtStereoDeg / 360.0;
            cutoffL = cutoffFor (s, lfoFor (s, phase, smoothL));
            cutoffR = cutoffFor (s, lfoFor (s, phase + offset, smoothR));
            const auto q = 0.5 + (double) s.filtRes01 * (double) s.filtRes01 * 11.5;
            filters[0].set (sampleRate, cutoffL, q);
            filters[1].set (sampleRate, cutoffR, q);
        }

        l = filters[0].process (l, s.filtMode);
        r = filters[1].process (r, s.filtMode);
    }

private:
    double lfoFor (const Settings& s, double phase, double& smooth) const noexcept
    {
        auto v = lfoValue (s.filtShape, phase, s.seed, 0x7F01u);

        // Square and random steps are glided over ~5 ms so the cutoff never jumps.
        if (s.filtShape == LfoShape::Square || s.filtShape == LfoShape::RandomStep)
        {
            const auto a = std::exp (-(double) kCoeffInterval / (0.005 * sampleRate));
            smooth = smooth < 0.0 ? v : v + (smooth - v) * a;
            return smooth;
        }

        smooth = v;
        return v;
    }

    static double cutoffFor (const Settings& s, double lfo01) noexcept
    {
        auto lo = std::clamp ((double) std::min (s.filtMinHz, s.filtMaxHz), 20.0, 20000.0);
        auto hi = std::clamp ((double) std::max (s.filtMinHz, s.filtMaxHz), 20.0, 20000.0);
        return lo * std::pow (hi / lo, std::clamp ((double) s.filtDepth01, 0.0, 1.0) * lfo01);
    }

    double sampleRate = 48000.0;
    std::array<Svf, 2> filters;
    FreePhase free;
    double smoothL = -1.0, smoothR = -1.0;
    double cutoffL = 1000.0, cutoffR = 1000.0;
};

//==============================================================================
class Phaser
{
public:
    static constexpr int kMaxStages = 12;

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : state)
            ch.fill (0.0f);

        feedbackL = feedbackR = 0.0f;
        free.reset();
        primed = false;
    }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        const auto freePhase = free.advance (std::max (0.001f, s.phHz), sampleRate);

        if (c.freeSamples % kCoeffInterval == 0 || ! primed)
        {
            const auto phase = s.phSync ? c.beat / divBeats (s.phDiv, t.tsNum, t.tsDen) : freePhase;
            gL = coefficient (s, lfoValue (LfoShape::Sine, phase, s.seed, 0x8E01u));
            gR = coefficient (s, lfoValue (LfoShape::Sine, phase + s.phStereoDeg / 360.0, s.seed, 0x8E01u));
            primed = true;
        }

        const auto stages = std::clamp (s.phStages, 1, kMaxStages);
        const auto fb = std::clamp (s.phFeedback, -0.95f, 0.95f);
        const auto m = std::clamp (s.phMix01, 0.0f, 1.0f);
        const auto apL = chain (state[0], stages, gL, l + fb * feedbackL);
        const auto apR = chain (state[1], stages, gR, r + fb * feedbackR);
        // tanh keeps the feedback path bounded whatever the input level.
        feedbackL = flushTiny (std::tanh (apL));
        feedbackR = flushTiny (std::tanh (apR));
        l = l * (1.0f - 0.5f * m) + apL * 0.5f * m;
        r = r * (1.0f - 0.5f * m) + apR * 0.5f * m;
    }

private:
    float coefficient (const Settings& s, double lfo01) const noexcept
    {
        const auto octaves = (lfo01 * 2.0 - 1.0) * 2.0 * std::clamp ((double) s.phDepth01, 0.0, 1.0);
        const auto hz = std::clamp ((double) s.phCentreHz * std::pow (2.0, octaves), 20.0, 0.45 * sampleRate);
        const auto g = std::tan (kPi * hz / sampleRate);
        return (float) (g / (1.0 + g));
    }

    static float chain (std::array<float, kMaxStages>& st, int stages, float G, float x) noexcept
    {
        for (int i = 0; i < stages; ++i)
        {
            // TPT one-pole: lp = v + s, all-pass = 2 lp - x.
            const auto v = (x - st[(std::size_t) i]) * G;
            const auto lp = v + st[(std::size_t) i];
            st[(std::size_t) i] = flushTiny (lp + v);
            x = 2.0f * lp - x;
        }

        return x;
    }

    double sampleRate = 48000.0;
    std::array<std::array<float, kMaxStages>, 2> state {};
    float feedbackL = 0.0f, feedbackR = 0.0f, gL = 0.5f, gR = 0.5f;
    FreePhase free;
    bool primed = false;
};

//==============================================================================
/** 2x2 output matrix: [l', r'] = [[ll, lr], [rl, rr]] . [l, r]. Polarity
    inversion negates a row, pan inversion crossfades towards the swapped matrix
    (100 % = channels swapped, the stereo image mirrored). Coefficients glide
    over 5 ms so switching never clicks. Note: inverting ONE channel cancels in a
    mono fold-down; inverting both, or swapping, is mono-safe. */
class Flip
{
public:
    void prepare (double sr) noexcept
    {
        glide = 1.0f / (float) std::max (1.0, 0.005 * sr);
        reset();
    }

    void reset() noexcept { cur = { 1.0f, 0.0f, 0.0f, 1.0f }; }

    static std::array<float, 4> targetFor (const Settings& s, bool rhythmicActive) noexcept
    {
        const auto a = std::clamp (s.panInvert01, 0.0f, 1.0f);
        std::array<float, 4> m { 1.0f - a, a, a, 1.0f - a };

        if (rhythmicActive)
        {
            switch (s.flipMode)
            {
                case FlipMode::Swap:        m = { m[2], m[3], m[0], m[1] }; break;
                case FlipMode::InvertLeft:  m[0] = -m[0]; m[1] = -m[1]; break;
                case FlipMode::InvertRight: m[2] = -m[2]; m[3] = -m[3]; break;
                case FlipMode::InvertBoth:  for (auto& v : m) v = -v; break;
                case FlipMode::Off:         break;
            }
        }

        if (s.polLeft)  { m[0] = -m[0]; m[1] = -m[1]; }
        if (s.polRight) { m[2] = -m[2]; m[3] = -m[3]; }
        return m;
    }

    void process (const Settings& s, const Timing& t, const ClockSample& c, float& l, float& r) noexcept
    {
        bool rhythmic = false;

        if (s.flipMode != FlipMode::Off)
        {
            const Grid grid (s.swing01, s.timeVar01, s.seed, 0x9F01u);
            const auto step = grid.locate (c.beat / divBeats (s.flipDiv, t.tsNum, t.tsDen)).step;
            rhythmic = hash01 (s.seed, 0x9F02u, step) < s.flipProb01;
        }

        const auto target = targetFor (s, rhythmic);

        for (std::size_t i = 0; i < 4; ++i)
            cur[i] = cur[i] < target[i] ? std::min (target[i], cur[i] + 2.0f * glide)
                                        : std::max (target[i], cur[i] - 2.0f * glide);

        const auto nl = cur[0] * l + cur[1] * r;
        const auto nr = cur[2] * l + cur[3] * r;
        l = nl;
        r = nr;
    }

    const std::array<float, 4>& matrix() const noexcept { return cur; }

private:
    std::array<float, 4> cur { 1.0f, 0.0f, 0.0f, 1.0f };
    float glide = 1.0f / 240.0f;
};

} // namespace gapfill::glitch
