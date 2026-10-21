#pragma once

// F-M16 Rhythmic ratchet (docs/dev/12 section 7, VoiceFx).
//
// Amount = probability that a fill is ratcheted (sub-RNG kSalt_Ratchet; 100 =
// always). A ratcheted voice records its first slice S (one musical division)
// into a preallocated ring and re-emits it R times at t_k = sum_{j<k} I_j with
// I_0 = S and I_{j+1} = max(8 ms, I_j * (1 - 0.5 * accel)). Each repeat is
// gated to ratGate % of its interval with 1.5 ms cosine edges. Repeats are
// clipped to the voice end (which already ends before the next protected hit)
// and none starts with fewer than 8 ms left. After the last repeat the voice
// is silent until it ends.

#include "EffectCard.h"

#include <vector>

namespace gapfill::fx
{

class RatchetCard final : public VoiceFx
{
public:
    static constexpr int kMaxRepeats = 16;
    static constexpr float kMinIntervalMs = 8.0f;
    static constexpr float kEdgeMs = 1.5f;
    static constexpr float kRingMs = 500.0f;

    /** The exact repeat schedule of one fill (pure function, unit-tested). */
    struct Plan
    {
        bool active = false;
        int  sliceLength = 0;                ///< S in samples
        int  count = 0;                      ///< repeats actually scheduled (<= kMaxRepeats)
        int  start[kMaxRepeats] {};          ///< t_k
        int  interval[kMaxRepeats] {};       ///< I_k
        int  playLength[kMaxRepeats] {};     ///< audible samples of repeat k (gate, clipped)
        bool tempoFallback = false;
    };

    static Plan makePlan (const RatchetSettings&, double samplesPerBeat, double sampleRate,
                          int voiceLength) noexcept;

    CardId id() const noexcept override { return CardId::Ratchet; }
    void prepare (double sampleRate, int maxBlock, int channels) override;
    void reset() override;
    bool enabled (const FxSettings& s) const noexcept override { return s.ratchet.amount01 > kOffThreshold; }

    bool startFill (const VoiceFxStart&) override;
    void processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept override;

    const Plan& currentPlan() const noexcept { return plan; }
    bool isFillActive() const noexcept { return plan.active; }

private:
    double sr = 48000.0;
    int ringSize = 0;
    std::vector<float> ringL, ringR;
    Plan plan;
    int pos = 0;            ///< samples since the fill started
    int cursor = 0;         ///< repeat index currently relevant
    int edge = 1;
};

} // namespace gapfill::fx
