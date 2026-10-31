#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/modulation/Sources.h"
#include "features/modulation/ExtendedState.h"
#include "features/macros/MacroModel.h"
#include <cstring>

#include <cmath>
#include <limits>

using namespace gapfill::modulation;

GF_TEST (Modulation, source_depth_is_additive_bounded_and_knob_ignores_depth)
{
    GF_EXPECT_NEAR (macroValue (MacroSource::Knob, .2f, 1, 1, 1, 1), .2f, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::LfoA, .5f, -.25f, 1, 0, 0), .25f, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::LfoB, .9f, .5f, 0, 1, 0), 1, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::Energy, .5f, .5f, 0, 0, 1), 1, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::Energy, .5f, .5f, 0, 0, 0), 0, 1e-7f);
}

GF_TEST (Modulation, permanent_divisions_observe_host_meter_and_free_clock_uses_output_sample)
{
    GF_EXPECT_NEAR (divisionBeats (1, 4, 4), 1.0 / 3, 1e-12);
    GF_EXPECT_NEAR (divisionBeats (8, 7, 8), 3.5, 1e-12);
    GF_EXPECT_NEAR (divisionBeats (11, 7, 8), 28, 1e-12);
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    at.ppq = 1;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 1, 1e-6f);
    at.transportValid = false;
    at.outputPos = 24000;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 1, 1e-6f);
    at.outputPos = 0;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 0, 1e-6f);
}

GF_TEST (Modulation, random_access_lfos_are_seek_safe_independent_and_glide_is_continuous)
{
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    at.ppq = 2;
    lfo.shape = LfoShape::RandomStep;
    const auto original = lfoValue (lfo, at, 0);
    GF_EXPECT (original != lfoValue (lfo, at, 1));
    at.ppq = -1000;
    (void) lfoValue (lfo, at, 0);
    at.ppq = 2;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), original, 0);
    lfo.shape = LfoShape::RandomGlide;
    at.ppq = 4 - 1e-8;
    const auto before = lfoValue (lfo, at, 0);
    at.ppq = 4 + 1e-8;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), before, 1e-6f);
}

GF_TEST (Modulation, every_shape_stays_finite_and_bounded_at_hostile_positions)
{
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    for (int shape = 0; shape < 6; ++shape)
    {
        lfo.shape = (LfoShape) shape;
        for (int sample = -10000; sample < 10000; ++sample)
        {
            at.ppq = sample * .017;
            const auto value = lfoValue (lfo, at, 0);
            GF_EXPECT (std::isfinite (value) && value >= -1 && value <= 1);
        }
    }
    at.ppq = 1e300;
    GF_EXPECT (std::isfinite (lfoValue (lfo, at, 0)));
    at.ppq = std::numeric_limits<double>::quiet_NaN();
    at.sampleRate = std::numeric_limits<double>::infinity();
    lfo.freeHz = std::numeric_limits<float>::quiet_NaN();
    GF_EXPECT (std::isfinite (lfoValue (lfo, at, 0)));
}

GF_TEST (Modulation, source_evaluation_performs_no_audio_thread_allocation)
{
    DecisionInputs at;
    at.transportValid = true;
    LfoSettings lfo;
    lfo.shape = LfoShape::RandomGlide;
    float sum = 0;
    gftest::AllocGuard guard;
    for (int sample = 0; sample < 4096; ++sample)
    {
        at.ppq = sample / 24000.0;
        sum += macroValue (MacroSource::LfoA, .5f, .5f, lfoValue (lfo, at, 0), 0, .4f);
    }
    GF_EXPECT (std::isfinite (sum) && guard.count() == 0);
}

GF_TEST (Modulation, extended_codec_admits_all_sixty_four_records_and_preserves_exact_new_floats)
{
    auto config = extendedFromLegacy (gapfill::macros::defaultConfig());
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        std::memset (config.names[macro], '\0', sizeof (config.names[macro]));
        std::memset (config.names[macro], '\"', gapfill::macros::kNameBytes);
        config.count[macro] = gapfill::macros::kMaxMappings;
        for (int index = 0; index < gapfill::macros::kMaxMappings; ++index)
        {
            auto& mapping = config.map[macro][index];
            mapping.target = (std::uint8_t) ((macro * 8 + index) % kTargetCount);
            mapping.curve = 3; mapping.bipolar = 1;
            mapping.min01 = .123456789f; mapping.max01 = .987654321f; mapping.smoothMs = 499.75f;
        }
    }
    const auto tree = toExtendedTree (config);
    GF_EXPECT (tree.isValid() && tree.toXmlString().getNumBytesAsUTF8() < kMaxStateBytes);
    ExtendedConfig restored;
    GF_EXPECT (fromExtendedTree (tree, restored));
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        GF_EXPECT (restored.count[macro] == 8);
        GF_EXPECT (std::memcmp (restored.map[macro], config.map[macro], sizeof (config.map[macro])) == 0);
    }
    GF_EXPECT (gapfill::macros::MacroModel::legacyConfigToTree (restored.retainedLegacy)
        .isEquivalentTo (gapfill::macros::MacroModel::legacyConfigToTree (config.retainedLegacy)));
}

GF_TEST (Modulation, extended_codec_rejects_unsupported_versions_invalid_records_and_duplicate_indices_atomically)
{
    const auto config = extendedFromLegacy (gapfill::macros::defaultConfig());
    auto tree = toExtendedTree (config);
    auto restored = config;
    restored.count[7] = 1;
    tree.setProperty ("v", 4, nullptr);
    GF_EXPECT (! fromExtendedTree (tree, restored) && restored.count[7] == 1);
    tree = toExtendedTree (config);
    tree.getChild (1).setProperty ("d", "AA==", nullptr);
    GF_EXPECT (! fromExtendedTree (tree, restored) && restored.count[7] == 1);
    tree = toExtendedTree (config);
    tree.getChild (2).setProperty ("i", 0, nullptr);
    GF_EXPECT (! validateExtendedTree (tree));
    auto invalid = config;
    invalid.map[0][0].min01 = std::numeric_limits<float>::quiet_NaN();
    GF_EXPECT (! toExtendedTree (invalid).isValid());
}

GF_TEST (Modulation, explicit_extended_mode_preserves_the_complete_legacy_configuration_and_returns_to_it)
{
    gapfill::macros::MacroModel model;
    GF_EXPECT (! model.isExtendedEnabled());
    gapfill::macros::LfoSettings lfo;
    lfo.enabled = 1; lfo.shape = 4; lfo.rateHz = 7.25f; lfo.phaseDeg = 123;
    GF_EXPECT (model.setLfo (0, lfo));
    const auto legacy = model.toValueTree();
    GF_EXPECT (model.enableExtended() && ! model.enableExtended());
    gapfill::macros::Mapping mapping;
    mapping.target = 46;
    mapping.min01 = .123456789f;
    GF_EXPECT (model.addMapping (7, mapping) == gapfill::macros::MacroModel::AddResult::Ok);
    GF_EXPECT (model.rename (7, "Eight"));
    GF_EXPECT (! model.setLfo (0, {}));
    ConfigSnapshot published;
    GF_EXPECT (model.extendedPublisher().consume (published) && published.enabled == 1);
    GF_EXPECT (published.config.count[7] == 1);
    GF_EXPECT (gapfill::macros::MacroModel::legacyConfigToTree (published.config.retainedLegacy).isEquivalentTo (legacy));
    gapfill::macros::MacroModel reopened;
    reopened.restore (model.toValueTree());
    GF_EXPECT (reopened.isExtendedEnabled() && reopened.getExtended().count[7] == 1);
    GF_EXPECT_NEAR (reopened.getExtended().map[7][0].min01, mapping.min01, 0);
    GF_EXPECT (reopened.returnToLegacy() && ! reopened.returnToLegacy());
    GF_EXPECT (reopened.toValueTree().isEquivalentTo (legacy));
    GF_EXPECT (reopened.extendedPublisher().consume (published) && published.enabled == 0);
    GF_EXPECT (gapfill::macros::MacroModel::legacyConfigToTree (published.config.retainedLegacy).isEquivalentTo (legacy));
}

GF_TEST (Modulation, mapping_codec_uses_the_documented_little_endian_record_bytes)
{
    auto config = extendedFromLegacy (gapfill::macros::defaultConfig());
    for (auto& count : config.count) count = 0;
    config.count[0] = 1;
    config.map[0][0] = { 46, 2, 1, 0, .25f, .75f, 500 };
    const std::uint8_t expected[] { 46, 2, 1, 0, 0, 0, 128, 62, 0, 0, 64, 63, 0, 0, 250, 67 };
    const auto tree = toExtendedTree (config);
    GF_EXPECT (tree.isValid());
    GF_EXPECT (tree.getChild (1)["d"].toString() == juce::Base64::toBase64 (expected, sizeof (expected)));
    config.retainedLegacy.count[0] = 255;
    GF_EXPECT (! toExtendedTree (config).isValid());
}
