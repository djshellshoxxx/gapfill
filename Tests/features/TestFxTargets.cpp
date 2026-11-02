// Complete scene FX targets use the same real-unit conversion as direct
// parameter gathering, without changing the released macro/battle lookup.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "features/common/EngineTargets.h"
#include "features/fx/FxHooks.h"
#include "features/scenes/SceneModel.h"
#include "features/scenes/SceneOverlay.h"

#include <vector>
#include <cmath>

using namespace gapfill;
using namespace gftest;

namespace
{
    std::vector<float> values (const fx::FxSettings& f)
    {
        return { f.swell.amount01, (float) f.swell.curve, f.swell.filter01, (float) f.swell.target,
                 f.cloud.amount01, f.cloud.densityHz, f.cloud.sizeMs, f.cloud.spanMs, f.cloud.jitter01,
                 f.cloud.pitchCents, f.cloud.spread01, (float) f.cloud.maxGrains,
                 f.ratchet.amount01, (float) f.ratchet.division, (float) f.ratchet.repeats, f.ratchet.accel, f.ratchet.gate01,
                 f.rcrush.amount01, (float) f.rcrush.bits, (float) f.rcrush.rate, (float) f.rcrush.hold, f.rcrush.tone,
                 f.tape.amount01, f.tape.wow01, f.tape.flutter01, f.tape.sat01, f.tape.dropout01, f.tape.splice01,
                 (float) f.scrub.on, f.scrub.pos01, f.scrub.widthMs, f.scrub.windowMs,
                 (float) f.bandFx.low, (float) f.bandFx.mid, (float) f.bandFx.high,
                 f.resonator.amount01, (float) f.resonator.note, (float) f.resonator.mode, f.resonator.decay01,
                 f.resonator.damp01, f.resonator.spread01,
                 f.fsdelay.amount01, f.fsdelay.shiftHz, (float) f.fsdelay.time, f.fsdelay.feedback01, f.fsdelay.damp01, f.fsdelay.stereo01,
                 f.dub.amount01, (float) f.dub.div, f.dub.feedback01, f.dub.lowHz, f.dub.highHz, f.dub.move01, f.dub.sat01, (float) f.dub.ping,
                 f.formant.amount01, f.formant.shiftSt, f.formant.tilt01, (float) f.formant.vowel,
                 f.spfreeze.amount01, f.spfreeze.lengthMs, f.spfreeze.decay01, f.spfreeze.smear01, f.spfreeze.morph01,
                 f.spfreeze.pitchSt, f.spfreeze.width01, f.convo.amount01, f.convo.sizeMs, f.convo.damp01,
                 (float) f.lite, f.tailCapScale };
    }
}

GF_TEST (FxTargets, all_scene_effect_parameters_have_audio_targets)
{
    for (const auto& id : scenes::idsForScope (scenes::ScopeFx))
        GF_EXPECT_MSG ((bool) targets::sceneRefForId (id.toRawUTF8()), id);
    // An unrelated new scene lookup must not silently activate advanced
    // mappings in old saved macro/battle configurations.
    GF_EXPECT (! targets::refForId ("swellAmount"));
    GF_EXPECT (! targets::refForId ("fmtAmount"));
}

GF_TEST (FxTargets, scene_effect_units_match_direct_parameter_gathering_at_min_mid_and_max)
{
    GapFillAudioProcessor p;
    ParamCache cache;
    cache.connect (p.getValueTreeState());
    for (const auto& id : scenes::idsForScope (scenes::ScopeFx))
    {
        auto* parameter = p.getValueTreeState().getParameter (id);
        GF_EXPECT (parameter != nullptr);
        const auto original = parameter->getValue();
        for (float norm : { 0.0f, 0.47f, 1.0f })
        {
            EngineSettings actual, expected;
            fx::gatherFx (actual, cache);
            parameter->setValueNotifyingHost (norm);
            fx::gatherFx (expected, cache);
            targets::apply (actual, targets::sceneRefForId (id.toRawUTF8()), parameter->convertFrom0to1 (parameter->getValue()));
            const auto a = values (actual.fx), b = values (expected.fx);
            GF_EXPECT (a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++i)
                GF_EXPECT_MSG (std::abs (a[i] - b[i]) <= 1e-6f, id + ": mismatched effect field " + juce::String ((int) i));
            parameter->setValueNotifyingHost (original);
        }
    }
}

GF_TEST (FxTargets, voice_formant_scene_fields_use_the_registered_units)
{
    EngineSettings s;
    const auto apply = [&s] (const char* id, float value) { targets::apply (s, targets::sceneRefForId (id), value); };
    apply ("fl20_amount", 75.0f);
    apply ("fl20_shift", -7.0f);
    apply ("fl20_tilt", 4.0f);
    apply ("fl20_vowel", 3.0f);
    apply ("fl20_preserve", 62.0f);
    apply ("fl20_detail", 2.0f);
    GF_EXPECT (s.voiceFormant.enabled);
    GF_EXPECT_NEAR (s.voiceFormant.amount01, 0.75, 1e-6);
    GF_EXPECT_NEAR (s.voiceFormant.shiftSt, -7.0, 1e-6);
    GF_EXPECT_NEAR (s.voiceFormant.tiltDbPerOct, 4.0, 1e-6);
    GF_EXPECT (s.voiceFormant.vowel == 3 && s.voiceFormant.detail == 2);
    GF_EXPECT_NEAR (s.voiceFormant.preserve01, 0.62, 1e-6);
    apply ("fl20_amount", 0.0f);
    GF_EXPECT (! s.voiceFormant.enabled);
}

GF_TEST (FxTargets, cached_scene_effect_application_performs_no_allocation)
{
    std::vector<targets::Ref> refs;
    for (const auto& id : scenes::idsForScope (scenes::ScopeFx))
        refs.push_back (targets::sceneRefForId (id.toRawUTF8()));
    EngineSettings s;
    AllocGuard guard;
    for (const auto ref : refs)
        targets::apply (s, ref, 0.0f);
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (FxTargets, legacy_scene_dispatch_is_preserved_while_new_scenes_overlay_effects)
{
    scenes::initTables();
    scenes::initOverlayTargets();
    for (int version : { 1, 2 })
    {
        scenes::Overlay overlay;
        overlay.id = 1;
        overlay.scopeVersion = (std::uint8_t) version;
        overlay.n = 1;
        overlay.idx[0] = (std::uint16_t) scenes::paramIndexOf ("fmtAmount");
        overlay.real[0] = 75.0f;
        TripleBuffer<scenes::Overlay> pub;
        pub.publish (overlay);
        scenes::SceneEngine engine;
        scenes::TransportSnap transport;
        engine.beginChunk (pub, transport);
        EngineSettings settings;
        engine.view().applyTo (settings);
        GF_EXPECT_NEAR (settings.fx.formant.amount01, version == 1 ? 0.0 : 0.75, 1e-6);
    }
}
