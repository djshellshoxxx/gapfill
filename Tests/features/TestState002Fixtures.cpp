// Compatibility expectations produced by the actual v0.0.2-beta1 processor.
// The manifest identifies immutable production sources and records the old
// reader's values. Its render hashes supplement the numeric legacy goldens.

#include "../support/TestRig.h"

#include <bit>
#include <iostream>

using namespace gapfill;
using namespace gftest;

namespace
{
    juce::File fixtureDir() { return testDataDir().getChildFile ("fixtures"); }

    juce::var manifest()
    {
        const auto file = fixtureDir().getChildFile ("state_0.0.2-beta1_manifest.json");
        GF_EXPECT_MSG (file.existsAsFile(), "Missing immutable 0.0.2-beta1 fixture manifest");
        GF_EXPECT (file.getSize() > 0 && file.getSize() <= 2 * 1024 * 1024);
        const auto data = juce::JSON::parse (file.loadFileAsString());
        GF_EXPECT (data.isObject() && data["validFixtures"].isArray());
        return data;
    }

    juce::MemoryBlock bytesFor (const juce::String& name)
    {
        GF_EXPECT (! name.containsChar ('/') && ! name.containsChar ('\\') && ! name.contains (".."));
        const auto file = fixtureDir().getChildFile (name);
        GF_EXPECT (file.getSize() > 0 && file.getSize() <= 1024 * 1024);
        juce::MemoryBlock out;
        GF_EXPECT_MSG (file.loadFileAsData (out), "Could not load " + name);
        return out;
    }

    juce::String compilerIdentity()
    {
       #if defined (__clang__)
        return "clang:" __clang_version__;
       #elif defined (_MSC_VER)
        return "msvc:" + juce::String (_MSC_VER);
       #else
        return "gcc:" __VERSION__;
       #endif
    }

    juce::String renderFingerprint (const juce::AudioBuffer<float>& audio)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                const auto bits = std::bit_cast<std::uint32_t> (audio.getSample (ch, sample));
                for (int byte = 0; byte < 4; ++byte)
                {
                    hash ^= (std::uint8_t) (bits >> (8 * byte));
                    hash *= 1099511628211ull;
                }
            }
        return juce::String::toHexString ((juce::int64) hash).paddedLeft ('0', 16);
    }
}

GF_TEST (StateCompat002, release_provenance_and_factory_fixture_coverage_are_explicit)
{
    const auto data = manifest();
    GF_EXPECT (data["releaseTag"].toString() == "v0.0.2-beta1");
    GF_EXPECT (data["productionCommit"].toString() == "e89aecd218253141cfcd35ad3d9ddfe76c8891e3");
    GF_EXPECT ((int) data["parameterCount"] == 216);
    GF_EXPECT ((int) data["factoryPresetCount"] == 41);
    GF_EXPECT (data["validFixtures"].getArray()->size() == 43);
    int presets = 0;
    for (const auto& row : *data["validFixtures"].getArray())
        if (row["file"].toString().contains ("_preset_"))
            ++presets;
    GF_EXPECT (presets == 41);
}

GF_TEST (StateCompat002, every_released_fixture_restores_the_old_readers_parameter_values)
{
    const auto data = manifest();
    for (const auto& row : *data["validFixtures"].getArray())
    {
        const auto name = row["file"].toString();
        const auto blob = bytesFor (name);
        GapFillAudioProcessor p;
        p.setStateInformation (blob.getData(), (int) blob.getSize());
        GF_EXPECT (row["parameters"].isArray() && row["parameters"].getArray()->size() == 216);
        for (const auto& value : *row["parameters"].getArray())
        {
            const auto id = value["id"].toString();
            const auto* actual = p.getValueTreeState().getRawParameterValue (id);
            GF_EXPECT_MSG (actual != nullptr, name + ": missing parameter " + id);
            const auto expected = (double) value["raw"];
            GF_EXPECT_MSG (std::abs ((double) actual->load() - expected) <= 1e-5 * juce::jmax (1.0, std::abs (expected)),
                           name + ": parameter differs from released reader: " + id);
        }
    }
}

GF_TEST (StateCompat002, released_sessions_resave_stably_and_render_finite_audio)
{
    const auto data = manifest();
    const auto input = drumLoop (48000.0, 150.0, 1);
    for (const auto& row : *data["validFixtures"].getArray())
    {
        const auto name = row["file"].toString();
        const auto blob = bytesFor (name);
        GapFillAudioProcessor first;
        first.setStateInformation (blob.getData(), (int) blob.getSize());
        juce::MemoryBlock saved;
        first.getStateInformation (saved);
        GF_EXPECT (saved.getSize() > 0);
        GapFillAudioProcessor second;
        second.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MemoryBlock again;
        second.getStateInformation (again);
        GF_EXPECT_MSG (saved == again, name + ": migrated state does not resave stably");
        const auto result = renderOffline (second, input, 512, 48000.0);
        GF_EXPECT_MSG (allFinite (result.out), name + ": non-finite restored render");
    }
}

GF_TEST (StateCompat002, released_render_identity_is_preserved_on_the_recorded_toolchain)
{
    const auto data = manifest();
    int checked = 0;
   #ifdef NDEBUG
    constexpr bool releaseBuild = true;
   #else
    constexpr bool releaseBuild = false;
   #endif
    for (const auto& row : *data["validFixtures"].getArray())
    {
        if (row["renderPlatform"].toString() != goldenPlatform()
            || row["renderCompiler"].toString() != compilerIdentity()
            || (bool) row["renderReleaseBuild"] != releaseBuild)
            continue;
        const auto name = row["file"].toString();
        const auto blob = bytesFor (name);
        GapFillAudioProcessor p;
        p.setStateInformation (blob.getData(), (int) blob.getSize());
        const auto result = renderGoldenScenarioA (p);
        GF_EXPECT_MSG (renderFingerprint (result.out) == row["renderFingerprint"].toString(),
                       name + ": restored audio differs from the real released reader");
        GF_EXPECT (result.fills == (int) row["renderFills"]);
        ++checked;
    }
    if (checked == 0)
        std::cout << "         [StateCompat002] release render fingerprint for this compiler/platform: UNVERIFIED\n";
}

GF_TEST (StateCompat002, corrupt_released_blobs_are_rejected_atomically)
{
    for (const char* stem : { "corrupt_truncated", "corrupt_bitflips" })
    {
        const auto blob = bytesFor ("state_0.0.2-beta1_" + juce::String (stem) + ".bin");
        GapFillAudioProcessor p;
        setParams (p, { { "fill", 37.0f }, { "wet", 41.0f } });
        p.setStateInformation (blob.getData(), (int) blob.getSize());
        GF_EXPECT_NEAR (p.getValueTreeState().getRawParameterValue ("fill")->load(), 37.0, 1e-5);
        GF_EXPECT_NEAR (p.getValueTreeState().getRawParameterValue ("wet")->load(), 41.0, 1e-5);
    }
}
