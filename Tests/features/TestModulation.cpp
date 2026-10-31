#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/modulation/Sources.h"
#include "features/modulation/ExtendedState.h"
#include "features/modulation/Targets.h"
#include "features/modulation/Runtime.h"
#include "features/common/PerformanceTargets.h"
#include "features/effective/Effective.h"
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

GF_TEST (Modulation, first_thirty_six_knob_targets_match_the_repaired_v2_field_arithmetic)
{
    gapfill::macros::initTargets();
    gapfill::modulation::initTargets();
    for (int target = 0; target < gapfill::macros::kNumTargets; ++target)
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto value = sample / 255.0f;
            gapfill::macros::MacroRuntime old;
            for (auto& count : old.cfg.count) count = 0;
            old.cfg.count[0] = 1;
            old.cfg.map[0][0].target = (std::uint8_t) target;
            gapfill::EngineSettings expected;
            expected.perf.tension.depth[0] = .8f;
            auto actual = expected;
            std::uint32_t ownership = 0;
            const float knobs[] { value, 0, 0, 0 };
            old.evaluateBlock (expected, knobs, ownership);
            expected.perf.advancedMacroOwnedMask = 0;
            TargetOutputs outputs;
            applyTarget (actual, target, value, captureTargetBase (actual), outputs);
            GF_EXPECT_MSG (std::memcmp (&actual, &expected, sizeof (actual)) == 0, gapfill::macros::targetInfo (target).id);
        }
}

GF_TEST (Modulation, new_physical_targets_cover_integer_skew_decay_gate_bands_and_virtual_send)
{
    gapfill::modulation::initTargets();
    gapfill::EngineSettings settings;
    TargetOutputs outputs;
    const auto base = captureTargetBase (settings);
    applyTarget (settings, 36, .5f, base, outputs);
    applyTarget (settings, 37, .5f, base, outputs);
    applyTarget (settings, 38, .5f, base, outputs);
    applyTarget (settings, 39, .5f, base, outputs);
    applyTarget (settings, 40, .5f, base, outputs);
    applyTarget (settings, 41, .5f, base, outputs);
    applyTarget (settings, 43, .25f, base, outputs);
    for (int target = 44; target <= 46; ++target) applyTarget (settings, target, .5f, base, outputs);
    GF_EXPECT_NEAR (settings.perf.mistakes01, .5f, 1e-6f);
    GF_EXPECT (settings.fx.rcrush.bits == 9 && settings.fx.rcrush.rate == 8);
    GF_EXPECT_NEAR (settings.fx.resonator.decaySeconds, .2f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.resonator.damp01, .5f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.ratchet.gate01, .55f, 1e-6f);
    GF_EXPECT_NEAR (outputs.fxSend01, .25f, 1e-6f);
    for (const auto amount : settings.bandAmt) GF_EXPECT_NEAR (amount, .5f, 1e-6f);
    GF_EXPECT (advancedOwnership (46) == (1u << 25));
    GF_EXPECT (advancedOwnership (47) == (1u << 26));
    applyTarget (settings, 47, .5f, base, outputs);
    GF_EXPECT_NEAR (settings.fx.tapeExtra.amount01, .5f, 1e-6f);
    GF_EXPECT (isContinuousTarget (26) && isContinuousTarget (42) && ! isContinuousTarget (25));
}

GF_TEST (Modulation, tape_mix_targets_only_the_effective_active_path)
{
    gapfill::EngineSettings settings;
    TargetOutputs outputs;
    applyTarget (settings, 42, .25f, captureTargetBase (settings), outputs);
    GF_EXPECT_NEAR (settings.fx.tape.mixOverride01, .25f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.tapeExtra.mix01, 1, 1e-6f);
    settings.fx.tape.mixOverride01 = -1;
    settings.fx.tapeExtra.amount01 = .5f;
    applyTarget (settings, 42, .75f, captureTargetBase (settings), outputs);
    GF_EXPECT_NEAR (settings.fx.tapeExtra.mix01, .75f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.tape.mixOverride01, -1, 1e-6f);
    GF_EXPECT (juce::String (targetParameterId (42, true)) == "fm20_mix");
    GF_EXPECT (juce::String (targetParameterId (42, false)) == "tdAmount");
}

GF_TEST (Modulation, runtime_resolves_ownership_before_the_tape_selector_and_mix)
{
    gapfill::modulation::initTargets();
    Runtime runtime;
    for (auto& count : runtime.cfg.count) count = 0;
    runtime.cfg.count[0] = 1;
    runtime.cfg.map[0][0] = { 42, 0, 0, 0, .25f, .25f, 0 };
    runtime.cfg.count[7] = 2;
    runtime.cfg.map[7][0] = { 47, 0, 0, 0, .5f, .5f, 0 };
    runtime.cfg.map[7][1] = { 42, 0, 0, 0, .75f, .75f, 0 };
    gapfill::EngineSettings settings;
    runtime.claimOwnership (settings);
    (void) runtime.evaluateDecision (settings, {});
    GF_EXPECT_NEAR (settings.fx.tapeExtra.amount01, .5f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.tapeExtra.mix01, .25f, 1e-6f);
    GF_EXPECT_NEAR (settings.fx.tape.mixOverride01, -1, 1e-6f);
    GF_EXPECT (settings.perf.advancedMacroOwnedMask == ((1u << 21) | (1u << 26)));
}

GF_TEST (Modulation, context_projection_preserves_derived_results_and_restores_nominal_depth_before_decision)
{
    gapfill::modulation::initTargets();
    Runtime runtime;
    for (auto& count : runtime.cfg.count) count = 0;
    runtime.cfg.count[0] = 4;
    for (int index = 0; index < 4; ++index) runtime.cfg.map[0][index] = { (std::uint8_t) (32 + index), 0, 0, 0, .5f, .5f, 0 };
    gapfill::EngineSettings settings;
    settings.perf.tension.depth[0] = .8f;
    const auto saved = runtime.projectContextTargets (settings, {});
    GF_EXPECT_NEAR (settings.perf.tension.depth[0], .4f, 1e-6f);
    GF_EXPECT_NEAR (settings.perf.energy.adapt01, .5f, 1e-6f);
    settings.perf.energy.level = 2;
    settings.perf.groove.snap.valid = true;
    Runtime::restoreContextBase (settings, saved);
    GF_EXPECT_NEAR (settings.perf.tension.depth[0], .8f, 1e-6f);
    GF_EXPECT (settings.perf.energy.level == 2 && settings.perf.groove.snap.valid);
    (void) runtime.evaluateDecision (settings, {});
    GF_EXPECT_NEAR (settings.perf.tension.depth[0], .4f, 1e-6f);
}

GF_TEST (Modulation, control_grid_uses_pinned_sources_and_the_same_cell_midpoint_across_host_cuts)
{
    gapfill::modulation::initTargets();
    Runtime runtime;
    for (auto& count : runtime.cfg.count) count = 0;
    runtime.cfg.count[0] = 1;
    runtime.cfg.map[0][0].target = 5;
    GF_EXPECT (! runtime.needsControlGrid());
    runtime.inputs.source[0] = MacroSource::Energy;
    GF_EXPECT (! runtime.needsControlGrid() && ! runtime.widthAt ({}).active);
    runtime.inputs.source[0] = MacroSource::LfoA;
    GF_EXPECT (runtime.needsControlGrid());
    for (int sample = -64; sample < 64; ++sample)
    {
        DecisionInputs at;
        at.transportValid = true; at.outputPos = sample; at.ppq = sample / 24000.0;
        const auto midpoint = Runtime::controlCellMidpoint (at, 24000);
        GF_EXPECT (midpoint.outputPos % 32 == 16 || midpoint.outputPos % 32 == -16);
        GF_EXPECT_NEAR (midpoint.ppq, midpoint.outputPos / 24000.0, 1e-12);
        GF_EXPECT (Runtime::samplesUntilControlBoundary (sample, 512) >= 1);
        GF_EXPECT (Runtime::samplesUntilControlBoundary (sample, 512) <= 32);
    }
}

GF_TEST (Modulation, pinned_scene_source_projection_reaches_every_new_parameter_kind)
{
    gapfill::Effective::ModulationInputs inputs;
    const auto project = [&] (const char* id, float value)
    {
        const auto index = gapfill::performanceTargets::indexForId (id);
        GF_EXPECT (index >= 0);
        gapfill::performanceTargets::applyModulation (inputs, index, value);
    };
    project ("fm23_macro5", 75);
    project ("fm23_src8", 3);
    project ("fm23_depth8", -25);
    project ("fm23_lfoBShape", 5);
    project ("fm23_lfoBDiv", 15);
    project ("fm23_lfoBHz", 8);
    GF_EXPECT_NEAR (inputs.macros.extendedInputs.knob01[4], .75f, 1e-6f);
    GF_EXPECT (inputs.macros.extendedInputs.source[7] == MacroSource::Energy);
    GF_EXPECT_NEAR (inputs.macros.extendedInputs.depth[7], -.25f, 1e-6f);
    GF_EXPECT (inputs.macros.extendedInputs.lfo[1].shape == LfoShape::RandomGlide);
    GF_EXPECT (inputs.macros.extendedInputs.lfo[1].division == 15);
    GF_EXPECT_NEAR (inputs.macros.extendedInputs.lfo[1].freeHz, 8, 1e-6f);
}

GF_TEST (Modulation, full_runtime_evaluation_is_bounded_and_has_no_audio_thread_allocation)
{
    gapfill::modulation::initTargets();
    Runtime runtime;
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        runtime.inputs.source[macro] = macro % 2 ? MacroSource::LfoA : MacroSource::LfoB;
        runtime.cfg.count[macro] = 8;
        for (int mapping = 0; mapping < 8; ++mapping)
            runtime.cfg.map[macro][mapping].target = (std::uint8_t) ((macro * 8 + mapping) % kTargetCount);
    }
    DecisionInputs at;
    at.transportValid = true;
    gftest::AllocGuard guard;
    for (int sample = 0; sample < 4096; sample += 32)
    {
        at.outputPos = sample; at.ppq = sample / 24000.0;
        gapfill::EngineSettings settings;
        const auto send = runtime.evaluateDecision (settings, at).fxSend01;
        GF_EXPECT (std::isfinite (settings.fill01) && settings.fill01 >= 0 && settings.fill01 <= 1);
        GF_EXPECT (send >= 0 && send <= 1);
    }
    GF_EXPECT (guard.count() == 0);
}
