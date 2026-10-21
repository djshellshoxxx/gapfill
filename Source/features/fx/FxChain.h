#pragma once

// The bus-side effect chain (docs/dev/12 FX-0, F-L14). PluginProcessor owns one
// and makes exactly three calls per block:
//
//     auto io = fxChain.beginBlock (settings.fx, numSamples);   // before engine.process
//     engine.process (..., &io);
//     fxChain.processBus (wetBuffer, 0, numSamples, settings.fx, io, blockStartPos);
//
// processBus runs BEFORE the wet post-processing (Low/Centre Protect, Width).
// Fixed permanent order (WP9 adds the cards): resonator -> fsdelay -> dub ->
// convo -> spfreeze. With no bus card enabled, processBus returns immediately:
// the chain is the identity (bit-exact) and publishes neither duck nor a second bus.
//
// F-L14: when a band's effects are switched off AND a BusFx is enabled, voices
// of that band render into a second, preallocated stereo bus (`plain`) that the
// chain sums after the BusFx ran; otherwise a single bus is used as before.

#include "EffectCard.h"
#include "EngineBlockIO.h"
#include "FxMeters.h"
#include "bus/BusCards.h"
#include "../../dsp/EngineEvents.h"

#include <array>
#include <vector>

namespace gapfill::fx
{

class FxChain
{
public:
    static constexpr int kMaxBusCards = 10;

    FxChain();

    void prepare (double sampleRate, int maxBlock, int channels);
    void reset();
    void fadeOut (int samples);

    /** True when at least one bus card needs to run for these settings
        (enabled, or still ringing out). */
    bool anyBusActive (const FxSettings&) const noexcept;

    /** F-L14: two wet buses are needed only when some band bypasses effects
        and a bus card is active. */
    bool needsSplit (const FxSettings&) const noexcept;

    /** Prepares the second bus / duck buffer for this block as needed. */
    EngineBlockIO beginBlock (const FxSettings&, int numSamples) noexcept;

    void processBus (juce::AudioBuffer<float>& wet, int start, int n, const FxSettings&,
                     const EngineBlockIO&, std::int64_t blockStartPos) noexcept;

    int maxLatencySamples() const noexcept;
    int numBusCards() const noexcept { return count; }
    BusFx* busCard (int i) noexcept { return cards[(size_t) i]; }

    /** Test / WP9 hook: appends a card to the explicit list (bounded). */
    bool addBusCard (BusFx*) noexcept;

    /** The WP9 bus cards (control access for the editor and tests). */
    bus::BusCards& cardsRef() noexcept { return busCards; }
    const bus::BusCards& cardsRef() const noexcept { return busCards; }

    /** X-1 commands 24 / 25 (SpectralCapture / SpectralRelease): same requests as the
        Effects section buttons (atomic counters, audio-thread safe). True when handled. */
    bool handleCommand (const Command& c) noexcept
    {
        if (c.type == CommandType::SpectralCapture) { busCards.spfreeze.requestCapture(); return true; }
        if (c.type == CommandType::SpectralRelease) { busCards.spfreeze.requestRelease(); return true; }
        return false;
    }

    FxMeters& getMeters() noexcept { return meters; }
    const FxMeters& getMeters() const noexcept { return meters; }

    std::size_t preallocatedBytes() const noexcept;

private:
    /** The explicit, reviewable card list (permanent chain order). */
    void build();

    bus::BusCards busCards;
    std::array<BusFx*, kMaxBusCards> cards {};
    int count = 0;
    double sr = 48000.0;
    int blockCapacity = 0;
    std::vector<float> duckBuf;
    juce::AudioBuffer<float> plain;
    FxMeters meters;
    std::uint32_t timingCounter = 0;
};

} // namespace gapfill::fx
