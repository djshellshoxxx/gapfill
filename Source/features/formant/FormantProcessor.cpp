#include "FormantProcessor.h"
#include "Vowels.h"

#include <algorithm>
#include <cmath>

namespace gapfill::formant
{
namespace
{
    constexpr float kLn10Over20 = 0.11512925465f;
    float bounded (float x, float lo, float hi, float fallback) noexcept
    {
        return std::isfinite (x) ? std::clamp (x, lo, hi) : fallback;
    }
}

void FormantProcessor::prepare (double sampleRate)
{
    const int n = sizeForRate (sampleRate);
    stft.prepare (sampleRate, n);
    int order = 0;
    while ((1 << order) < n) ++order;
    cepstralFft = std::make_unique<juce::dsp::FFT> (order);
    cepInput.assign ((std::size_t) n, {});
    cepOutput.assign ((std::size_t) n, {});
    for (auto* values : { &envelopeLog, &targetLog, &channelEnvelope[0], &channelEnvelope[1] })
        values->assign ((std::size_t) stft.numBins(), 0.0f);
    warp.assign ((std::size_t) stft.numBins(), {});
    octave.assign ((std::size_t) stft.numBins(), 0.0f);
    for (auto& shape : vowelShapes) shape.assign ((std::size_t) stft.numBins(), 0.0f);
    vowelMeans.fill (0.0f);
    for (int k = 0; k < stft.numBins(); ++k)
    {
        const float hz = std::max ((float) k * (float) sampleRate / (float) n, 50.0f);
        octave[(std::size_t) k] = std::log2 (hz / 1000.0f);
        for (int v = 1; v <= 5; ++v)
        {
            float value = 0.05f;
            for (float centre : vowelFormants (v))
            {
                const float x = (hz - centre) / (0.5f * (60.0f + 0.1f * centre));
                value += 1.0f / (1.0f + x * x);
            }
            const float logValue = std::log (value);
            vowelShapes[(std::size_t) v][(std::size_t) k] = logValue;
            vowelMeans[(std::size_t) v] += logValue / (float) stft.numBins();
        }
    }
    for (auto& delay : dryDelay) delay.assign ((std::size_t) n, 0.0f);
    faults.store (0, std::memory_order_relaxed);
    claimed = false;
    reset();
}

void FormantProcessor::reset() noexcept
{
    stft.reset();
    for (auto& delay : dryDelay) std::fill (delay.begin(), delay.end(), 0.0f);
    wetPrevious[0] = wetPrevious[1] = 0.0f;
    dryIndex = 0;
    frameFault = false;
}

bool FormantProcessor::claim (const FormantSetup& setup) noexcept
{
    if (claimed || ! stft.isPrepared() || ! setup.enabled || ! std::isfinite (setup.amount01)
        || setup.amount01 <= 0.0f)
        return false;
    settings = setup;
    settings.amount01 = bounded (setup.amount01, 0.0f, 1.0f, 0.0f);
    settings.shiftSt = bounded (setup.shiftSt, -12.0f, 12.0f, 0.0f);
    settings.tiltDbPerOct = bounded (setup.tiltDbPerOct, -6.0f, 6.0f, 0.0f);
    settings.preserve01 = bounded (setup.preserve01, 0.0f, 1.0f, 1.0f);
    settings.vowel = std::clamp (setup.vowel, 0, 5);
    settings.detail = std::clamp (setup.detail, 0, 2);
    constexpr double detailMs[] { 0.5, 1.0, 2.0 };
    lifter = std::clamp ((int) std::lround (detailMs[settings.detail] * 0.001 * stft.getSampleRate()),
                         2, stft.size() / 2);
    imposeVowel = settings.vowel > 0 && settings.preserve01 < 1.0f;
    const float alpha = std::exp2 (settings.shiftSt / 12.0f);
    for (int k = 0; k < stft.numBins(); ++k)
    {
        const float source = std::clamp ((float) k / alpha, 0.0f, (float) (stft.numBins() - 1));
        auto& bin = warp[(std::size_t) k];
        bin.index = (int) source;
        bin.fraction = source - (float) bin.index;
        bin.tilt = std::clamp (kLn10Over20 * settings.tiltDbPerOct * octave[(std::size_t) k],
                              -24.0f * kLn10Over20, 24.0f * kLn10Over20);
    }
    reset();
    claimed = true;
    return true;
}

std::size_t FormantProcessor::preallocatedBytes() const noexcept
{
    return stft.preallocatedBytes() + sizeof (float) * (envelopeLog.size()
        + targetLog.size() + channelEnvelope[0].size() + channelEnvelope[1].size() + octave.size()
        + dryDelay[0].size() + dryDelay[1].size()) + warp.size() * sizeof (WarpBin)
        + (std::size_t) stft.numBins() * 6 * sizeof (float)
        + (cepInput.size() + cepOutput.size()) * sizeof (std::complex<float>);
}

void FormantProcessor::process (float& left, float& right) noexcept
{
    if (! claimed)
        return;
    if (! std::isfinite (left) || ! std::isfinite (right))
    {
        faults.fetch_add (1, std::memory_order_relaxed);
        reset();
        left = right = 0.0f;
        return;
    }
    const float dry[2] { dryDelay[0][(std::size_t) dryIndex], dryDelay[1][(std::size_t) dryIndex] };
    dryDelay[0][(std::size_t) dryIndex] = left;
    dryDelay[1][(std::size_t) dryIndex] = right;
    dryIndex = (dryIndex + 1) % stft.size();
    const float* inputs[] { &left, &right };
    float wet[2] {};
    float* outputs[] { &wet[0], &wet[1] };
    stft.process (inputs, outputs, 1, *this);
    if (frameFault)
    {
        reset();
        left = right = 0.0f;
        return;
    }
    // SP-0 reconstructs at N-1; this extra sample makes the FL20 contract exact N.
    left = (1.0f - settings.amount01) * dry[0] + settings.amount01 * wetPrevious[0];
    right = (1.0f - settings.amount01) * dry[1] + settings.amount01 * wetPrevious[1];
    wetPrevious[0] = wet[0];
    wetPrevious[1] = wet[1];
}

void FormantProcessor::estimateEnvelopes (const spectral::SpectralFrame& left,
                                               const spectral::SpectralFrame& right) noexcept
{
    const int n = left.fftSize, bins = left.numBins;
    std::fill (cepInput.begin(), cepInput.end(), std::complex<float> {});
    for (int k = 0; k < bins; ++k)
    {
        const std::complex<float> value { std::log (std::max (left.mag[k], 1.0e-9f)),
                                          std::log (std::max (right.mag[k], 1.0e-9f)) };
        cepInput[(std::size_t) k] = value;
        if (k > 0 && k < n / 2) cepInput[(std::size_t) (n - k)] = value;
    }
    // Both log spectra are real and even, so their transforms are real.
    // Packing them as real/imaginary streams computes both cepstra in one FFT.
    cepstralFft->perform (cepInput.data(), cepOutput.data(), false);
    std::fill (cepInput.begin(), cepInput.end(), std::complex<float> {});
    const int taperStart = lifter * 3 / 4;
    for (int q = 0; q < lifter; ++q)
    {
        const float taper = q < taperStart ? 1.0f : 0.5f * (1.0f
            + std::cos (spectral::kPi * (float) (q - taperStart) / (float) (lifter - taperStart)));
        const auto value = cepOutput[(std::size_t) q] * (taper / (float) n);
        cepInput[(std::size_t) q] = value;
        if (q > 0) cepInput[(std::size_t) (n - q)] = value;
    }
    cepstralFft->perform (cepInput.data(), cepOutput.data(), false);
    for (int k = 0; k < bins; ++k)
    {
        channelEnvelope[0][(std::size_t) k] = cepOutput[(std::size_t) k].real();
        channelEnvelope[1][(std::size_t) k] = cepOutput[(std::size_t) k].imag();
    }
}

void FormantProcessor::processFrames (spectral::SpectralFrame& left,
                                      spectral::SpectralFrame& right) noexcept
{
    if (settings.shiftSt != 0.0f || settings.tiltDbPerOct != 0.0f || imposeVowel)
        estimateEnvelopes (left, right);
    for (auto* frame : { &left, &right })
    {
        std::copy (channelEnvelope[(std::size_t) frame->channel].begin(),
                   channelEnvelope[(std::size_t) frame->channel].end(), envelopeLog.begin());
        processFrame (*frame);
    }
}

void FormantProcessor::processFrame (spectral::SpectralFrame& frame) noexcept
{
    const int bins = frame.numBins;
    double before = 0.0;
    float smallest = 1.0e30f, largest = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        if (! std::isfinite (frame.mag[k]))
        {
            std::fill (frame.mag, frame.mag + bins, 0.0f);
            faults.fetch_add (1, std::memory_order_relaxed);
            frameFault = true;
            return;
        }
        const double weight = k == 0 || k == bins - 1 ? 1.0 : 2.0;
        before += weight * (double) frame.mag[k] * frame.mag[k];
        smallest = std::min (smallest, frame.mag[k]);
        largest = std::max (largest, frame.mag[k]);
    }
    if (before < 1.0e-18 || (settings.shiftSt == 0.0f && settings.tiltDbPerOct == 0.0f && ! imposeVowel))
        return;
    // A spectrally flat impulse is left intact, avoiding symmetric pre-ringing
    // while keeping the onset and phase coherent. Sustained frames still warp.
    if (smallest > largest * 0.995f)
        return;
    float meanEnvelope = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        const auto& bin = warp[(std::size_t) k];
        const int index = bin.index;
        const float mix = bin.fraction;
        const float warped = envelopeLog[(std::size_t) index] * (1.0f - mix)
            + envelopeLog[(std::size_t) std::min (index + 1, bins - 1)] * mix;
        targetLog[(std::size_t) k] = warped + bin.tilt;
        if (imposeVowel)
        {
            meanEnvelope += envelopeLog[(std::size_t) k];
        }
    }
    const float offset = meanEnvelope / (float) bins - vowelMeans[(std::size_t) settings.vowel];
    double after = 0.0;
    for (int k = 0; k < bins; ++k)
    {
        float target = targetLog[(std::size_t) k];
        if (imposeVowel)
            target = settings.preserve01 * target + (1.0f - settings.preserve01)
                * (vowelShapes[(std::size_t) settings.vowel][(std::size_t) k] + offset);
        const float gain = std::exp (std::clamp (target - envelopeLog[(std::size_t) k],
                                               -30.0f * kLn10Over20, 30.0f * kLn10Over20));
        frame.mag[k] *= gain;
        const double weight = k == 0 || k == bins - 1 ? 1.0 : 2.0;
        after += weight * (double) frame.mag[k] * frame.mag[k];
    }
    const float normalise = (float) std::min (std::sqrt (before / std::max (after, 1.0e-30)), 3.981071706);
    for (int k = 0; k < bins; ++k) frame.mag[k] *= normalise;
}

} // namespace gapfill::formant
