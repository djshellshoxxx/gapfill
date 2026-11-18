#include "../support/TestRig.h"
#include "features/state/SavedStateCache.h"
#include <array>
#include <atomic>
#include <thread>

using namespace gapfill;
using namespace gftest;

GF_TEST (SavedStateCache, failure_preserves_caller_or_recovers_last_complete_blob)
{
    state::SavedStateCache cache;
    juce::MemoryBlock empty;
    GF_EXPECT (cache.retainOnFailure (empty) == state::SavedStateCache::Fallback::Unavailable);
    const juce::MemoryBlock valid ("complete", 8);
    cache.remember (valid);
    cache.remember ({});
    GF_EXPECT (cache.retainOnFailure (empty) == state::SavedStateCache::Fallback::CachedRestored);
    GF_EXPECT (empty == valid);
    juce::MemoryBlock caller ("caller", 6);
    GF_EXPECT (cache.retainOnFailure (caller) == state::SavedStateCache::Fallback::CallerPreserved);
    GF_EXPECT (caller == juce::MemoryBlock ("caller", 6));
}

GF_TEST (SavedStateCache, concurrent_cache_readers_never_observe_a_partial_blob)
{
    state::SavedStateCache cache;
    std::array<unsigned char, 4096> bytes {};
    cache.remember (juce::MemoryBlock (bytes.data(), bytes.size()));
    std::atomic<bool> intact { true };
    std::array<std::thread, 3> readers;
    for (auto& reader : readers)
        reader = std::thread ([&]
        {
            for (int i = 0; i < 2000; ++i)
            {
                juce::MemoryBlock saved;
                const auto result = cache.retainOnFailure (saved);
                if (result != state::SavedStateCache::Fallback::CachedRestored || saved.getSize() != bytes.size())
                {
                    intact.store (false);
                    continue;
                }
                const auto* data = static_cast<const unsigned char*> (saved.getData());
                for (std::size_t n = 1; n < saved.getSize(); ++n)
                    if (data[n] != data[0])
                        intact.store (false);
            }
        });
    for (int i = 0; i < 2000; ++i)
    {
        bytes.fill (static_cast<unsigned char> (i));
        cache.remember (juce::MemoryBlock (bytes.data(), bytes.size()));
    }
    for (auto& reader : readers)
        reader.join();
    GF_EXPECT (intact.load());
}

GF_TEST (SavedStateCache, invalid_feature_save_recovers_even_into_an_empty_host_buffer)
{
    auto processor = std::make_unique<GapFillAudioProcessor>();
    setParams (*processor, { { "fill", 73.0f } });
    juce::MemoryBlock valid;
    processor->getStateInformation (valid);
    GF_EXPECT (valid.getSize() > 0);
    auto& bank = processor->getLiveControl().scenes().getBank();
    scenes::Scene scene;
    scene.name = "Valid";
    GF_EXPECT (bank.store (0, scene));
    bank.at (0).params.push_back ({ 65535, 0.5f }); // rejected atomically by the real scene serializer
    juce::MemoryBlock freshHostBuffer;
    processor->getStateInformation (freshHostBuffer);
    GF_EXPECT (freshHostBuffer == valid);
    juce::MemoryBlock caller ("prior host bytes", 16);
    processor->getStateInformation (caller);
    GF_EXPECT (caller == juce::MemoryBlock ("prior host bytes", 16));
    auto restored = std::make_unique<GapFillAudioProcessor>();
    restored->setStateInformation (freshHostBuffer.getData(), (int) freshHostBuffer.getSize());
    GF_EXPECT_NEAR (restored->getValueTreeState().getRawParameterValue ("fill")->load(), 73.0f, 0.001f);
}

GF_TEST (SavedStateCache, load_before_first_host_save_retains_the_loaded_project)
{
    auto original = std::make_unique<GapFillAudioProcessor>();
    setParams (*original, { { "fill", 73.0f } });
    juce::MemoryBlock project;
    original->getStateInformation (project);
    auto loaded = std::make_unique<GapFillAudioProcessor>();
    loaded->setStateInformation (project.getData(), (int) project.getSize());
    auto& bank = loaded->getLiveControl().scenes().getBank();
    scenes::Scene valid;
    valid.name = "Valid";
    GF_EXPECT (bank.store (0, valid));
    bank.at (0).params.push_back ({ 65535, 0.5f });
    juce::MemoryBlock firstSave;
    loaded->getStateInformation (firstSave);
    auto restored = std::make_unique<GapFillAudioProcessor>();
    restored->setStateInformation (firstSave.getData(), (int) firstSave.getSize());
    GF_EXPECT_NEAR (restored->getValueTreeState().getRawParameterValue ("fill")->load(), 73.0f, 0.001f);
    GF_EXPECT (restored->getLiveControl().scenes().getBank().size() == 0);
}
