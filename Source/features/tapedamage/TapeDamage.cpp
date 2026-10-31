#include "TapeDamage.h"

#include <algorithm>
#include <cmath>

namespace gapfill::tapedamage
{
namespace
{
    constexpr double kTwoPi = 6.2831853071795864769;
    constexpr std::uint64_t kChoiceSalt = 0x5441504543484f49ull;
    constexpr std::uint64_t kDspSalt = 0x5441504544535031ull;
    constexpr float kWowBudgetSeconds = 0.0024f;
    constexpr float kFlutterBudgetSeconds = 0.00065f;
    constexpr float kDriftBudgetSeconds = 0.00035f;
    constexpr float kPlusThreeDb = 1.4125375f;
    constexpr int kTanhIntervals = 2048;
    // Shared immutable Padé curve: interpolation error is below1e-6. Building
    // it at compile time avoids either division or lazy initialisation in audio.
    constexpr auto kTanh = []
    {
        std::array<float, kTanhIntervals + 1> table {};
        for (int i = 0; i <= kTanhIntervals; ++i)
        {
            const float x = (float) i * (5.0f / kTanhIntervals), x2 = x*x;
            table[(std::size_t) i] = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
                / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
        }
        return table;
    }();

    int nextInterval (Rng& random, float rate, double sr) noexcept
    {
        if (rate <= 0.0f) return 0x3fffffff;
        const auto interval = -std::log (1.0 - (double) random.nextFloat()) * sr / rate;
        return interval >= 0x3fffffff ? 0x3fffffff : std::max (1, (int) std::lround (interval));
    }
}

void TapeDamage::prepare (double sampleRate)
{
    sr = std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0 ? sampleRate : 48000.0;
    baseDelay = (int) std::ceil (sr * 0.004);
    int size = 1;
    while (size < (int) std::ceil (sr * 0.060) + 8) size <<= 1;
    mask = size - 1;
    line.assign ((std::size_t) size, {});
    minimumDelay = (float) (sr * 0.0005);
    maximumDelay = (float) (sr * 0.0075);
    flutterLimit = (float) (sr * kFlutterBudgetSeconds);
    driftLimit = (float) (sr * kDriftBudgetSeconds);
    emphasisPole = (float) std::exp (-kTwoPi * 3000.0 / sr);
    dcPole = (float) std::exp (-kTwoPi * 10.0 / sr);
    controlStep = (float) (1000.0 / sr);
    driftPole = (float) std::exp (-1.0 / (0.1 * sr));
    noiseLowPole = (float) std::exp (-kTwoPi * 14.0 / 1000.0);
    noiseHighPole = (float) std::exp (-kTwoPi * 6.0 / 1000.0);
    const double a = noiseLowPole, b = noiseHighPole;
    const double c = b * (1.0 - a), p = c * (1.0 - a) / (b - a), q = c - p;
    const double variance = p*p/(1-a*a) + q*q/(1-b*b) + 2*p*q/(1-a*b);
    const double covariance = p*p*a/(1-a*a) + q*q*b/(1-b*b) + p*q*(a+b)/(1-a*b);
    noiseNorm = (float) (1.0 / std::sqrt (((2.0 * variance + covariance) / 3.0) / 3.0));
    reset();
}

void TapeDamage::reset() noexcept
{
    active = shortSkipped = renderSelected = false;
    meta = {};
    elapsed = plannedSamples = write = 0;
    eventSize = nextEvent = 0;
    dropoutIndex = -1;
    noiseLp = noiseHp = noiseLpPrevious = 0.0f;
    flutterPrevious = flutterNext = flutterDelay = 0.0f;
    driftTarget = driftSpeed = driftDelay = controlPhase = 0.0f;
    preL = preR = deL = deR = 0.0f;
    dcInputL = dcInputR = dcOutputL = dcOutputR = 0.0f;
    spliceFrom = spliceTo = 0.0f;
    spliceStart = spliceEnd = 0;
    lastWow = lastFlutter = lastDrift = 0.0f;
    lastReadDelay = (float) baseDelay;
    std::fill (line.begin(), line.end(), Stereo {});
}

bool TapeDamage::begin (const fx::TapeExtraSettings& requested, std::uint64_t voiceSeed, int outputSamples,
                        int minimumSourceSamples, const TapeMeta& replay, bool replayRecipe, float liveSend01) noexcept
{
    active = shortSkipped = renderSelected = false;
    eventSize = nextEvent = 0;
    plannedSamples = outputSamples;
    elapsed = 0;
    meta = replayRecipe && isValidTapeMeta (replay) ? replay : TapeMeta {};
    const float send = std::isfinite (liveSend01) ? std::clamp (liveSend01, 0.0f, 1.0f) : 0.0f;
    // A live carrier gate cannot rewrite a saved decision. Natural ineligible
    // voices omit it. This precedes even latching, RNG construction and reset.
    if (send <= 0.0f) return false;
    if (! replayRecipe) meta = latchTapeSettings (requested, hashCombine (voiceSeed, kDspSalt));
    if (meta.version == 0 || (replayRecipe && meta.chosen == 0)) return false;
    settings = settingsFromTapeMeta (meta);
    settings.mix01 *= send;
    if (mask == 0 || outputSamples < (int) std::ceil (sr * 0.040)
        || outputSamples < baseDelay || std::max (0, minimumSourceSamples) > outputSamples - baseDelay)
    {
        meta.chosen = 0;
        shortSkipped = true;
        return false;
    }
    if (settings.mix01 <= 0.0f)
    {
        if (! replayRecipe) meta.chosen = 0;
        return false;
    }
    if (! replayRecipe && settings.amount01 < 1.0f)
    {
        Rng choice;
        choice.seed (hashCombine (voiceSeed, kChoiceSalt));
        if (! choice.chance (settings.amount01)) { meta.chosen = 0; return false; }
    }
    const auto chosen = meta;
    reset();
    meta = chosen;
    meta.chosen = 1;
    settings = settingsFromTapeMeta (meta);
    settings.mix01 *= send;
    plannedSamples = outputSamples;

    Rng wow;
    wow.seed (hashCombine (meta.subseed, 1));
    const auto ratio = std::exp2 ((double) settings.wow01 * 40.0 / 1200.0) - 1.0;
    const auto minimumHz = std::max (0.5, ratio / (kTwoPi * kWowBudgetSeconds));
    wowHz = (float) (minimumHz + (2.0 - minimumHz) * wow.nextFloat());
    wowAmplitude = (float) (ratio * sr / (kTwoPi * wowHz));
    const auto phaseLimit = wowAmplitude > 0.0f
        ? std::clamp ((float) (sr * kWowBudgetSeconds) / wowAmplitude - 1.0f, 0.0f, 1.0f) : 1.0f;
    wowInitial = wow.nextBipolar() * phaseLimit;
    wowSin = wowInitial;
    wowCos = std::sqrt (std::max (0.0f, 1.0f - wowSin * wowSin)) * (wow.nextFloat() < 0.5f ? -1.0f : 1.0f);
    wowSinStep = (float) std::sin (kTwoPi * wowHz / sr);
    wowCosStep = (float) std::cos (kTwoPi * wowHz / sr);
    flutterVelocity = (float) (std::log (2.0) * 15.0 / 1200.0) * settings.flutter01;
    flutterRng.seed (hashCombine (meta.subseed, 2));
    driftRng.seed (hashCombine (meta.subseed, 3));
    if (settings.flutter01 > 0.0f)
        for (int i = 0; i < 256; ++i) updateControl(); // fixed warm-up, outside the sample loop
    flutterPrevious = flutterNext;
    driftTarget = driftSpeed = 0.0f;
    driftRng.seed (hashCombine (meta.subseed, 3)); // flutter warm-up cannot advance drift's sequence
    drive = 1.0f + 5.0f * settings.sat01;
    inverseDrive = 1.0f / drive;
    createEvents();
    renderSelected = true;
    active = true;
    return true;
}

void TapeDamage::updateControl() noexcept
{
    flutterPrevious = flutterNext;
    if (settings.flutter01 > 0.0f)
    {
        noiseLp = noiseLowPole * noiseLp + (1.0f - noiseLowPole) * flutterRng.nextBipolar();
        noiseHp = noiseHighPole * (noiseHp + noiseLp - noiseLpPrevious);
        noiseLpPrevious = noiseLp;
        flutterNext = noiseHp * noiseNorm;
    }
    if (settings.instab01 > 0.0f)
        driftTarget = std::clamp (driftTarget + 0.05f * driftRng.nextBipolar(), -1.0f, 1.0f);
}

void TapeDamage::createEvents() noexcept
{
    Rng drops, splices;
    drops.seed (hashCombine (meta.subseed, 4));
    splices.seed (hashCombine (meta.subseed, 5));
    const float dropRate = settings.dropDensity01 * 10.0f, spliceRate = settings.splice01 * 4.0f;
    int nextDrop = nextInterval (drops, dropRate, sr), nextSplice = nextInterval (splices, spliceRate, sr);
    int previousDrop = -1, previousSplice = -1;
    const int audible = std::max (0, plannedSamples - baseDelay);
    const int minimumRamp = std::max (1, (int) std::ceil (sr * 0.001));
    for (int attempt = 0; attempt < 128 && eventSize < kMaxEvents; ++attempt)
    {
        const bool drop = nextDrop <= nextSplice;
        const int when = drop ? nextDrop : nextSplice;
        if (when >= audible) break;
        Event event;
        event.start = when;
        event.type = drop ? EventType::Dropout : EventType::Splice;
        if (drop)
        {
            const auto durationMs = std::clamp (-std::log (1.0 - (double) drops.nextFloat()) * 10.0, 2.0, 30.0);
            event.end = when + std::max (2 * minimumRamp, (int) std::lround (durationMs * sr * 0.001));
            event.ramp = minimumRamp;
            event.value = 1.0f - settings.dropDepth01;
            nextDrop += nextInterval (drops, dropRate, sr);
        }
        else
        {
            event.end = when + minimumRamp;
            event.ramp = minimumRamp;
            event.value = (float) (sr * 0.001) * (1.0f + 2.5f * splices.nextFloat())
                        * (splices.nextFloat() < 0.5f ? -1.0f : 1.0f);
            nextSplice += nextInterval (splices, spliceRate, sr);
        }
        int& previous = drop ? previousDrop : previousSplice;
        if (previous >= 0 && event.start <= events[(std::size_t) previous].end)
        {
            auto& merged = events[(std::size_t) previous];
            merged.end = std::max (merged.end, event.end);
            merged.value = drop ? std::min (merged.value, event.value) : event.value;
        }
        else
        {
            events[(std::size_t) eventSize] = event;
            previous = eventSize++;
        }
    }
}

forcedinline TapeDamage::Stereo TapeDamage::readTap (float delay) const noexcept
{
    const int whole = (int) delay;
    const float t = 1.0f - (delay - (float) whole);
    const auto centre = write - whole - 1;
    const float c0 = -t * (t - 1.0f) * (t - 2.0f) * (1.0f / 6.0f);
    const float c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) * 0.5f;
    const float c2 = -(t + 1.0f) * t * (t - 2.0f) * 0.5f;
    const float c3 = (t + 1.0f) * t * (t - 1.0f) * (1.0f / 6.0f);
    const auto i0 = (std::size_t) ((centre - 1) & mask), i1 = (std::size_t) (centre & mask);
    const auto i2 = (std::size_t) ((centre + 1) & mask), i3 = (std::size_t) ((centre + 2) & mask);
    return { line[i0].l*c0 + line[i1].l*c1 + line[i2].l*c2 + line[i3].l*c3,
             line[i0].r*c0 + line[i1].r*c1 + line[i2].r*c2 + line[i3].r*c3 };
}

float TapeDamage::boundedTanh (float value, float saturationDrive) noexcept
{
    // The caller applies latched1/drive for level compensation.
    const float position = std::min (std::abs (value * saturationDrive), 5.0f) * (kTanhIntervals / 5.0f);
    const int index = std::min ((int) position, kTanhIntervals - 1);
    const float a = kTanh[(std::size_t) index], b = kTanh[(std::size_t) index + 1];
    return std::copysign (a + (b - a) * (position - (float) index), value);
}

void TapeDamage::process (float& l, float& r) noexcept
{
    if (! active) return;
    const float inL = sanitise (l, 3.98f), inR = sanitise (r, 3.98f);
    line[(std::size_t) write] = { inL, inR };
    const int age = elapsed - baseDelay;
    Stereo wet;
    if (age >= 0)
    {
        while (nextEvent < eventSize && events[(std::size_t) nextEvent].start <= age)
        {
            const auto& event = events[(std::size_t) nextEvent];
            if (event.type == EventType::Dropout) dropoutIndex = nextEvent;
            else { spliceFrom = spliceTo; spliceTo = event.value; spliceStart = event.start; spliceEnd = event.end; }
            ++nextEvent;
        }
        lastWow = wowAmplitude * (wowSin - wowInitial);
        lastFlutter = flutterDelay;
        lastDrift = driftDelay;
        const float base = (float) baseDelay + lastWow + lastFlutter + lastDrift;
        const auto delay = [&] (float offset)
        {
            return std::clamp (base + offset, minimumDelay, maximumDelay);
        };
        lastReadDelay = delay (spliceTo);
        wet = readTap (lastReadDelay);
        if (age < spliceEnd)
        {
            const auto old = readTap (delay (spliceFrom));
            const auto phase = (float) (age - spliceStart) / (float) std::max (1, spliceEnd - spliceStart);
            const float a = std::cos (phase * (float) (kTwoPi * 0.25)), b = std::sin (phase * (float) (kTwoPi * 0.25));
            const auto correction = std::min (1.0f, kPlusThreeDb / (a + b));
            wet = { (old.l*a + wet.l*b)*correction, (old.r*a + wet.r*b)*correction };
        }
        if (settings.sat01 > 0.0f)
        {
            const float a = wet.l - emphasisPole * preL, b = wet.r - emphasisPole * preR;
            preL = wet.l; preR = wet.r;
            deL = boundedTanh (a, drive) * inverseDrive + emphasisPole * deL;
            deR = boundedTanh (b, drive) * inverseDrive + emphasisPole * deR;
            wet = { deL, deR };
        }
        if (dropoutIndex >= 0)
        {
            const auto& drop = events[(std::size_t) dropoutIndex];
            if (age < drop.end)
            {
                const auto distance = std::min (age - drop.start, drop.end - age);
                const auto phase = std::clamp ((float) distance / (float) drop.ramp, 0.0f, 1.0f);
                const auto shape = 0.5f - 0.5f * std::cos ((float) (kTwoPi * 0.5) * phase);
                const auto gain = 1.0f + (drop.value - 1.0f) * shape;
                wet.l *= gain; wet.r *= gain;
            }
        }

        const float nextSin = wowSin * wowCosStep + wowCos * wowSinStep;
        wowCos = wowCos * wowCosStep - wowSin * wowSinStep;
        wowSin = nextSin;
        controlPhase += controlStep;
        if (controlPhase >= 1.0f)
        {
            controlPhase -= 1.0f;
            updateControl();
            const auto inverse = 1.0f / std::sqrt (wowSin*wowSin + wowCos*wowCos);
            wowSin *= inverse; wowCos *= inverse;
        }
        const auto velocity = (flutterPrevious + (flutterNext - flutterPrevious) * controlPhase) * flutterVelocity;
        flutterDelay = std::clamp (flutterDelay - velocity, -flutterLimit, flutterLimit);
        driftSpeed = driftPole * driftSpeed + (1.0f - driftPole) * driftTarget;
        driftDelay = std::clamp (driftDelay - driftSpeed * 0.005f * settings.instab01, -driftLimit, driftLimit);
    }
    const auto dryIndex = (std::size_t) ((write - baseDelay) & mask);
    const float outL = line[dryIndex].l + (wet.l - line[dryIndex].l) * settings.mix01;
    const float outR = line[dryIndex].r + (wet.r - line[dryIndex].r) * settings.mix01;
    const auto highL = outL - dcInputL + dcPole * dcOutputL;
    const auto highR = outR - dcInputR + dcPole * dcOutputR;
    dcInputL = outL; dcInputR = outR; dcOutputL = highL; dcOutputR = highR;
    l = sanitise (highL, 3.98f); r = sanitise (highR, 3.98f);
    write = (write + 1) & mask;
    if (++elapsed >= plannedSamples) active = false;
}
} // namespace gapfill::tapedamage
