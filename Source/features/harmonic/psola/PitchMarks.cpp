#include "PitchMarks.h"
#include <algorithm>
#include <cmath>

namespace gapfill::harmonic::psola
{
void PitchMarks::prepare (double sampleRate)
{
    rate = std::isfinite (sampleRate) && sampleRate >= 8000 && sampleRate <= 768000 ? sampleRate : 48000;
    scratch.assign ((std::size_t) std::min (kMaxCaptureSamples, (int) std::ceil (rate * 0.32)), 0);
    // A prior larger sample rate must not retain storage beyond the ceiling.
    if (scratch.capacity() > (std::size_t) kMaxCaptureSamples)
        std::vector<float> (scratch).swap (scratch);
}

EpochResult PitchMarks::find (const Capture& capture, float f0) noexcept
{
    EpochResult result;
    if (capture.left == nullptr || capture.length <= 0 || capture.length > (int) scratch.size()
        || capture.channels < 1 || capture.channels > 2 || (capture.channels == 2 && capture.right == nullptr))
        return result;
    if (! std::isfinite (f0) || f0 < 50 || f0 > 1000)
    {
        result.failure = EpochFailure::InvalidPitch;
        return result;
    }
    const double period = rate / f0;
    const int margin = (int) std::ceil (period);
    if (capture.length < 2 * margin + (int) std::ceil (3 * period) + 1)
    {
        result.failure = EpochFailure::TooShort;
        return result;
    }
    // One RBJ Butterworth biquad, mono analysis only; captured stereo audio is
    // never overwritten and the renderer still reads L/R independently.
    const double omega = 6.283185307179586 * std::min (4.0 * f0, rate * 0.45) / rate;
    const double c = std::cos (omega), alpha = std::sin (omega) / 1.4142135623730951, a0 = 1 + alpha;
    const float b0 = (float) ((1 - c) * 0.5 / a0), b1 = (float) ((1 - c) / a0), b2 = b0;
    const float a1 = (float) (-2 * c / a0), a2 = (float) ((1 - alpha) / a0);
    float z1 = 0, z2 = 0;
    for (int i = 0; i < capture.length; ++i)
    {
        const float l = capture.left[i], r = capture.channels == 2 ? capture.right[i] : l;
        if (! std::isfinite (l) || ! std::isfinite (r)) { result.failure = EpochFailure::NonFinite; return result; }
        const float x = 0.5f * l + 0.5f * r;
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (! std::isfinite (y)) { result.failure = EpochFailure::NonFinite; return result; }
        scratch[(std::size_t) i] = std::abs (y);
    }
    const int newest = capture.length - 1 - margin;
    const int from = std::max (margin, capture.length - (int) std::ceil (2 * period));
    const auto peak = [&] (int lo, int hi, double expected)
    {
        int best = lo;
        for (int i = lo + 1; i <= hi; ++i)
        {
            const float a = scratch[(std::size_t) i], b = scratch[(std::size_t) best];
            if (a > b || (a == b && std::abs (i - expected) < std::abs (best - expected))) best = i;
        }
        return best;
    };
    int at = peak (from, newest, newest);
    if (scratch[(std::size_t) at] < 1e-8f) { result.failure = EpochFailure::TooShort; return result; }
    while (result.epochs.count < kEpochs)
    {
        result.epochs.positions[(std::size_t) result.epochs.count++] = at;
        const double expected = at - period;
        const int lo = std::max (margin, (int) std::ceil (expected - 0.25 * period));
        const int hi = std::min (at - 1, (int) std::floor (expected + 0.25 * period));
        if (lo > hi || expected < margin) break;
        at = peak (lo, hi, expected);
    }
    if (result.epochs.count < 4) { result.failure = EpochFailure::TooShort; return result; }
    std::reverse (result.epochs.positions.begin(), result.epochs.positions.begin() + result.epochs.count);
    double sum = 0;
    for (int i = 1; i < result.epochs.count; ++i)
        sum += result.epochs.positions[(std::size_t) i] - result.epochs.positions[(std::size_t) (i - 1)];
    const double mean = sum / (result.epochs.count - 1);
    double squared = 0;
    for (int i = 1; i < result.epochs.count; ++i)
    {
        const double delta = result.epochs.positions[(std::size_t) i] - result.epochs.positions[(std::size_t) (i - 1)] - mean;
        squared += delta * delta;
    }
    result.epochs.spacingCv = (float) (std::sqrt (squared / (result.epochs.count - 1)) / mean);
    if (result.epochs.spacingCv > 0.15f) { result.failure = EpochFailure::Irregular; return result; }
    result.failure = EpochFailure::None;
    return result;
}
} // namespace gapfill::harmonic::psola
