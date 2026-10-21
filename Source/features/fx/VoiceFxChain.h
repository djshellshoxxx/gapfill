#pragma once

// The per-voice effect chain (docs/dev/12 FX-0). FillVoice owns exactly one.
//
// Fixed permanent order: ratchet -> tape destruction -> (formant, WP9) -> rate
// crush. Swell and Cloud are render modes inside FillVoice, ahead of this chain.
// A new voice card = new files + one explicit line in VoiceFxChain::build().
//
// The chain is only entered for fills in which at least one card is active;
// otherwise FillVoice takes its original direct path (bit-identical output).

#include "EffectCard.h"
#include "FxMeters.h"
#include "RatchetCard.h"
#include "RcrushCard.h"
#include "TapeDestrCard.h"

#include <array>

namespace gapfill::fx
{

class VoiceFxChain
{
public:
    static constexpr int kMaxCards = 6;

    VoiceFxChain();

    void prepare (double sampleRate, int maxBlock, int channels);
    void reset();

    /** Called when the voice starts a fill. Cards decide for themselves; the
        chain is active when at least one accepted. */
    void start (const VoiceFxStart&);

    bool isActive() const noexcept { return active; }

    /** Processes `n <= kMaxVoiceBlock` samples in place. `samplesLeft` is the
        number of voice samples remaining after the block's first sample. */
    void process (float* l, float* r, int n, int samplesLeft, int voiceLength) noexcept;

    /** Wind every card down (panic). */
    void fadeOut (int samples);

    void setMeters (FxMeters* m) noexcept { meters = m; }

    int numCards() const noexcept { return count; }
    VoiceFx* card (int i) noexcept { return cards[(size_t) i]; }

    RatchetCard& ratchetCard() noexcept { return ratchet; }
    TapeDestrCard& tapeCard() noexcept { return tape; }
    RcrushCard& rcrushCard() noexcept { return rcrush; }

private:
    /** The explicit, reviewable card list (permanent chain order). */
    void build();

    RatchetCard ratchet;
    TapeDestrCard tape;
    RcrushCard rcrush;

    std::array<VoiceFx*, kMaxCards> cards {};
    std::array<bool, kMaxCards> cardActive {};
    int count = 0;
    bool active = false;
    double sr = 48000.0;
    int endGuard = 64;
    std::uint32_t timingCounter = 0;
    FxMeters* meters = nullptr;
};

} // namespace gapfill::fx
