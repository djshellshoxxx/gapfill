#include "FxUiState.h"

#include "../../PluginProcessor.h"

namespace gapfill::fx
{

namespace
{
    constexpr const char* kKeys[kNumCards] =
    {
        "swell", "cloud", "ratchet", "tapedestr", "formant", "rcrush",
        "resonator", "fsdelay", "dub", "convo", "spfreeze"
    };

    int cardIndexFromKey (const juce::String& key) noexcept
    {
        for (int i = 0; i < kNumCards; ++i)
            if (key == kKeys[i])
                return i;

        return -1;
    }
}

const char* cardKey (CardId id) noexcept
{
    const auto i = (int) id;
    return i >= 0 && i < kNumCards ? kKeys[i] : "";
}

void FxUiState::reset() noexcept
{
    for (int i = 0; i < kNumCards; ++i)
    {
        folded[i].store (true, std::memory_order_relaxed);
        rememberedAmount[i].store (0.0f, std::memory_order_relaxed);
    }
}

bool FxUiState::isDefault() const noexcept
{
    for (int i = 0; i < kNumCards; ++i)
        if (! folded[i].load (std::memory_order_relaxed) || rememberedAmount[i].load (std::memory_order_relaxed) != 0.0f)
            return false;

    return true;
}

//==============================================================================
bool validateFxTree (const juce::ValueTree& tree)
{
    if (! tree.hasType ("Fx"))
        return false;

    juce::StringArray seen;

    for (const auto& card : tree)
    {
        if (! card.hasType ("Card"))
            return false;

        const auto key = card["id"].toString();

        if (cardIndexFromKey (key) < 0 || seen.contains (key))
            return false;

        seen.add (key);

        const auto amount = (double) card.getProperty ("amount", 0.0);

        if (! (amount >= 0.0 && amount <= 100.0))
            return false;
    }

    return true;
}

void applyFxTree (GapFillAudioProcessor& proc, const juce::ValueTree& tree)
{
    auto& ui = proc.getFxUiState();
    ui.reset();
    proc.getFxChain().cardsRef().convo.setAutoCapture (false);          // defaults: IR auto off,
    proc.getFxChain().cardsRef().spfreeze.setAutoCapture (true);        // freeze auto on

    for (const auto& card : tree)
    {
        const auto i = cardIndexFromKey (card["id"].toString());

        if (i < 0)
            continue;

        ui.folded[i].store ((bool) card.getProperty ("folded", true), std::memory_order_relaxed);
        ui.rememberedAmount[i].store ((float) (double) card.getProperty ("amount", 0.0), std::memory_order_relaxed);

        // WP9: capture preferences live with the cards, persisted here (no audio)
        if (i == (int) CardId::Convo && card.hasProperty ("auto"))
            proc.getFxChain().cardsRef().convo.setAutoCapture ((bool) card.getProperty ("auto"));

        if (i == (int) CardId::SpFreeze && card.hasProperty ("auto"))
            proc.getFxChain().cardsRef().spfreeze.setAutoCapture ((bool) card.getProperty ("auto"));
    }
}

juce::ValueTree serialiseFxTree (const GapFillAudioProcessor& proc)
{
    const auto& ui = proc.getFxUiState();

    const auto& cards = const_cast<GapFillAudioProcessor&> (proc).getFxChain().cardsRef();   // read-only use
    const bool autoNonDefault = cards.convo.getAutoCapture() || ! cards.spfreeze.getAutoCapture();

    if (ui.isDefault() && ! autoNonDefault)
        return {};                                  // write nothing: states without effects stay byte-identical

    juce::ValueTree tree ("Fx");

    for (int i = 0; i < kNumCards; ++i)
    {
        const bool folded = ui.folded[i].load (std::memory_order_relaxed);
        const auto amount = ui.rememberedAmount[i].load (std::memory_order_relaxed);

        const bool autoCard = (i == (int) CardId::Convo && cards.convo.getAutoCapture())
                           || (i == (int) CardId::SpFreeze && ! cards.spfreeze.getAutoCapture());

        if (folded && amount == 0.0f && ! autoCard)
            continue;

        juce::ValueTree card ("Card");

        if (i == (int) CardId::Convo)
            card.setProperty ("auto", cards.convo.getAutoCapture(), nullptr);

        if (i == (int) CardId::SpFreeze)
            card.setProperty ("auto", cards.spfreeze.getAutoCapture(), nullptr);

        card.setProperty ("id", kKeys[i], nullptr);
        card.setProperty ("folded", folded, nullptr);
        card.setProperty ("amount", (double) juce::jlimit (0.0f, 100.0f, amount), nullptr);
        tree.appendChild (card, nullptr);
    }

    return tree;
}

void resetFxTree (GapFillAudioProcessor& proc)
{
    proc.getFxUiState().reset();
    proc.getFxChain().cardsRef().convo.setAutoCapture (false);
    proc.getFxChain().cardsRef().spfreeze.setAutoCapture (true);
}

} // namespace gapfill::fx
